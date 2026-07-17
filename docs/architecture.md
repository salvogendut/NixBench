# Architecture

[Back to the main README](../README.md)

## Initial development architecture

The first usable prototype runs as a normal window inside a NetBSD/Xorg
desktop:

- **C11** is the implementation language.
- **CMake** drives configuration and builds.
- **SDL3** supplies the software drawing API; an SDL host adapter creates the
  development window and normalizes presentation and input events.
- **NixBench** draws its entire desktop and internal window model inside that
  one SDL window. Shell drawing first lands in a canonical CPU framebuffer,
  which is then handed to the selected physical-output adapter.
- **Xorg** is reached through SDL3's existing X11 video backend; NixBench does
  not initially depend on Xlib, XCB, or X11 window-manager ownership.

The hosted window is a development container for the future standalone
desktop, not a set of host-system windows. Desktop objects, menus, NixBench
windows, focus, stacking, and client surfaces are all implemented internally.
This keeps their behavior independent of X11 and allows the same compositor to
be retained when the physical output moves to KMS. Traditional X11 applications
remain outside NixBench during this phase.

## Unix application compatibility direction

NixBench is growing an embedded Wayland compositor rather than embedding a
general-purpose widget toolkit. This lets applications keep using mature
toolkits while NixBench owns window management and composition:

- GTK and SDL applications can eventually use their existing Wayland backends
  and appear as NixBench-managed windows.
- A small NixBench shell extension can carry desktop-specific integration such
  as an application's global menu model without replacing its widget toolkit.
  The first GTK slice bridges `GtkApplication` menubars and `app-menu` models,
  plus classic GTK3 `GtkMenuBar` widget trees and detached application popup
  menus such as Sakura's.
- Xaw and other X11-only applications use an optional rootless Xwayland
  compatibility service. NixBench owns the unprivileged XWM connection and
  adopts each X11 top-level's `WL_SURFACE_ID` as an independent native frame;
  the older rootful probe remains available as a diagnostic checkpoint.
- Moving the outer display from SDL/Xorg to NetBSD KMS changes the physical
  output backend, not the client protocol or application toolkit.

The current implementation is an early protocol slice. It advertises
`wl_compositor`, `wl_shm`, one logical `wl_output`, a pointer-and-keyboard
`wl_seat`, and stable `xdg_wm_base`; accepts ARGB/XRGB shared-memory buffers;
and maps an `xdg_toplevel` into an internal NixBench window. A first
`xdg_positioner`/`xdg_popup` slice configures shared-memory popups relative to
their parent, accepts GTK's valid pre-map popup grab, and CPU-composites popup
pixels into the parent snapshot. Parent unmap and teardown recursively dismiss
dependent popups so their lifetime cannot outlive the surface relationship.
The logical output tracks the SDL renderer size and mapped surfaces receive
output membership events. SDL pointer motion and buttons are routed to client
content with surface-coordinate scaling and an implicit button grab.

Keyboard focus follows the active NixBench window. The compositor publishes an
XKB keymap, repeat settings, held-key state, and modifiers, and converts SDL's
portable physical scancodes through that keymap rather than exposing
host-specific raw codes. The compositor copies committed buffers before
releasing them, so clients and the shell do not share mutable rendering state.

NixClock is the first real out-of-process NixBench application. It uses
`wl_shm` and stable `xdg-shell` for its resizable analog-clock surface, plus a
small versioned NixBench extension to publish the global menus associated with
its toplevel. The focused application contributes **NixClock**, containing
**Quit**, and **Settings**, containing the checkable **Show seconds** command.
The seconds hand is hidden by default and appears in a distinct color when the
setting is enabled. The earlier separate `nixbench-wayland-demo` remains a
focused input and protocol probe.

Socket-pair protocol tests cover configure/acknowledge, mapping, pixels, frame
completion, output state and membership, application-menu transactions and
command delivery, XKB keymap delivery, pointer and keyboard events, grabs,
focus cleanup, popup configure/map/composition/teardown, close, and unmap
without requiring a running display server.

X.org is a transitional development platform, not the final runtime
architecture. It lets the project validate the shell, interaction model, and
internal compositor before taking responsibility for the entire display stack.

## Standalone target architecture

The final target starts from a NetBSD console without an X server beneath it:

- NixBench owns display output through DRM/KMS where supported, with a practical
  framebuffer path considered for hardware without KMS.
- Console keyboard and pointer input are obtained through the appropriate
  NetBSD/SDL3 backend, including wscons where applicable.
- A NixBench compositor combines the shell and independent application surfaces
  into the physical display output.
- Native applications remain separate processes and submit surfaces through
  standard Wayland protocols plus narrowly scoped, versioned NixBench shell
  extensions rather than opening the console device themselves.
- X11 support becomes an optional compatibility service layered on NixBench; it
  is not required to run the desktop or native applications.

SDL3 contains an experimental KMSDRM path that can use wscons input on NetBSD,
but it is only compiled when libdrm, GBM, and EGL support are all available,
and [SDL currently describes NetBSD KMSDRM as unsupported][sdl-kmsbsd].
Its presence in an SDL package and its behavior on supported NetBSD DRM
hardware must therefore be detected rather than assumed. A software
`wsdisplay` framebuffer adapter now exists as an output-only, experimental
bring-up path. It validates the reported RGB layout, maps only the required
framebuffer range, converts the canonical CPU frame, participates in
process-controlled virtual-terminal release/acquire, and restores the saved
console state on normal teardown. An opt-in supervisor harness, bounded by
default with an explicit interactive run-until-exit mode, has completed the
first two-second framebuffer presentation and
verified restoration on a ThinkPad X220. An explicit desktop-preview frame
source now renders the real menu bar, clock, and managed-window chrome into the
same canonical CPU frame without initializing SDL video. A separate,
explicit `--interactive-preview` research mode temporarily owns the fixed
`/dev/wskbd` and `/dev/wsmouse` mux aliases, adds a software cursor, routes the
left button to menus and window dragging, and queries the active wscons keymap
and keyboard type. A stable mux containing exactly one direct PC-XT keyboard
that reports exact `KB_US` encoding and required physical sentinels uses a
bounded physical XKB profile for letters, digits, punctuation, modifiers,
navigation, function keys, and the keypad. USB, mixed/multiple, variant,
hot-plugged, and unknown configurations retain the proven Escape, F10, arrow,
Return, and keypad-Enter fallback. F10 and those navigation keys drive the
global menu path; Escape also provides an orderly early exit.
Repeated downs are marked as repeats and `ALL_KEYS_UP` synthesizes every held
mapped-key release. A new explicit
`--runtime-preview` replaces that lightweight scene
with the same shared desktop runtime used by the hosted SDL frontend, including
the real NixInfo application and application-owned global menus. It keeps
Wayland publication disabled and still uses only the software canvas,
`wsdisplay`, and wscons devices. That research harness is not a crash-safe
login session because its desktop runtime and device worker both remain root.
See
[the standalone backend architecture](standalone-backend.md) for the
staged safety and implementation boundaries.
The root-helper versus ordinary-user-core decision is detailed in the
[standalone privilege-boundary assessment](privilege-boundary.md).

The separate, opt-in `nixbench-wsdisplay-session` milestone now implements that
process split without changing the old harness. A root recovery supervisor and
root device worker retain `wsdisplay`, wscons, VT, presentation, heartbeat, and
restoration authority. Trusted children irreversibly change to the invoking
sudo account before executing `nixbench-session-core`. An ordinary-user
runtime sentinel owns cleanup of the private runtime directory; its sibling
core creates the desktop, publishes the Wayland display there, and launches
no application by default, or one operator-selected initial application. Its
ordinary-user process manager services the global **Applications** menu,
which can start NixClock, Sakura Terminal, and Midori Web Browser
processes without involving the privileged helper. The core
receives only a bounded anonymous protocol endpoint; the selected application
does not receive that endpoint, and neither ordinary-user process receives a
framebuffer, wscons, recovery, or VT descriptor. The heartbeat, runtime
cleanup, and deterministic core-crash/core-hang gates have device-free
coverage. Console takeover, normal exit, VT 1 -> 2 -> 1, supervised SIGTERM
recovery, and both core-failure gates have passed on hardware. This remains an
explicitly acknowledged development milestone rather than a supported login
session while the harder failure matrix remains.

The older `nixbench-wsdisplay-smoke` research harness deliberately remains
available for framebuffer and input experiments. It neither publishes Wayland
nor launches external applications, and its runtime continues to execute as
root. Do not confuse it with the privilege-separated session.

[sdl-kmsbsd]: https://wiki.libsdl.org/SDL3/README-kmsbsd

The Wayland integration boundary and eventual X11 compatibility mechanism are
not public contracts yet. The roadmap requires focused prototypes before either
choice is stabilized.
