# NixBench

NixBench is an experimental desktop environment for NetBSD, built in C with
SDL3. Its interaction model and visual direction take inspiration from Amiga
Workbench and AROS while remaining an original project.

The goal is a lightweight, coherent desktop that eventually runs directly on
the NetBSD console without requiring X.org, while supporting both applications
designed specifically for NixBench and traditional Unix GUI applications. The
project is in its bootstrap stage. The runnable prototype opens an SDL3 desktop
screen under a host window system and draws NixBench-managed windows inside it.

## Goals

- Make NetBSD a first-class development and runtime platform.
- Run standalone by owning the display and input devices directly.
- Provide a responsive desktop shell with a Workbench-inspired workflow.
- Treat volumes, files, applications, and launchers as clear desktop objects.
- Run native NixBench applications as independent processes and windows.
- Preserve a compatibility path for conventional X11 applications.
- Keep the core small, understandable, and built from portable components where
  doing so does not compromise the NetBSD experience.

## Architecture

### Initial development architecture

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

### Unix application compatibility direction

NixBench is growing an embedded Wayland compositor rather than embedding a
general-purpose widget toolkit. This lets applications keep using mature
toolkits while NixBench owns window management and composition:

- GTK and SDL applications can eventually use their existing Wayland backends
  and appear as NixBench-managed windows.
- A small NixBench shell extension can carry desktop-specific integration such
  as an application's global menu model without replacing its widget toolkit.
- Xaw and other X11-only applications remain candidates for a later, optional
  Xwayland compatibility service.
- Moving the outer display from SDL/Xorg to NetBSD KMS changes the physical
  output backend, not the client protocol or application toolkit.

The current implementation is an early protocol slice. It advertises
`wl_compositor`, `wl_shm`, one logical `wl_output`, a pointer-and-keyboard
`wl_seat`, and stable `xdg_wm_base`; accepts ARGB/XRGB shared-memory buffers;
and maps an `xdg_toplevel` into an internal NixBench window. The logical output
tracks the SDL renderer size and mapped surfaces receive output membership
events. SDL pointer motion and buttons are routed to client content with
surface-coordinate scaling and an implicit button grab.

Keyboard focus follows the active NixBench window. The compositor publishes an
XKB keymap, repeat settings, held-key state, and modifiers, and converts SDL's
portable physical scancodes through that keymap rather than exposing
host-specific raw codes. The compositor copies committed buffers before
releasing them, so clients and the shell do not share mutable rendering state.

A standalone `nixbench-wayland-demo` client renders through `wl_shm`, loads
the published XKB keymap, and exercises pointer and keyboard interaction
with a beveled toggle control. The socket-pair protocol test covers
configure/acknowledge, mapping, pixels, frame completion, output state and
membership, XKB keymap delivery, pointer and keyboard events, grabs, focus
cleanup, close, and unmap without requiring a running display server.

X.org is a transitional development platform, not the final runtime
architecture. It lets the project validate the shell, interaction model, and
internal compositor before taking responsibility for the entire display stack.

### Standalone target architecture

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
console state on normal teardown. An opt-in, duration-bounded supervisor
harness has completed the first two-second framebuffer presentation and
verified restoration on a ThinkPad X220. An explicit desktop-preview frame
source now renders the real menu bar, clock, and managed-window chrome into the
same canonical CPU frame without initializing SDL video. A separate,
explicit `--interactive-preview` research mode temporarily owns the fixed
`/dev/wskbd` and `/dev/wsmouse` mux aliases, adds a software cursor, routes the
left button to menus and window dragging, and accepts Escape as an orderly
early exit. A new explicit `--runtime-preview` replaces that lightweight scene
with the same shared desktop runtime used by the hosted SDL frontend, including
the real NixInfo application and application-owned global menus. It keeps
Wayland publication disabled and still uses only the software canvas,
`wsdisplay`, and wscons devices. The adapter and harness are not a crash-safe
login session: a production wscons input/session layer and privileged recovery
watchdog are still required before this can become a standalone desktop
session. See
[the standalone backend architecture](docs/standalone-backend.md) for the
staged safety and implementation boundaries.

[sdl-kmsbsd]: https://wiki.libsdl.org/SDL3/README-kmsbsd

The Wayland integration boundary and eventual X11 compatibility mechanism are
not public contracts yet. The roadmap requires focused prototypes before either
choice is stabilized.

## Project status

NixBench now has a minimal C11/SDL3 shell, an internal window manager, and its
first reference application. NixInfo displays a system snapshot in one or more
NixBench-managed windows. On NetBSD it reports kernel identity, CPU model and
count, physical memory, uptime, load averages, and root-volume capacity using
libc and native `sysctl` interfaces. Its Project, View, and Window menus are
published by the application and shown in the global top bar alongside a live
local-time clock. Clicking the desktop restores the shell's own menu set.

NixInfo is deliberately a separate, SDL-free application controller, although
it still runs in the shell process during this prototype phase. A small
in-process application host assigns application IDs, tracks window ownership,
deep-copies and validates double-buffered menu snapshots, delivers lifecycle
and command events, and applies deferred application requests. Menu commands
retain the exact focused-window context, and Quit NixInfo closes only NixInfo.
Application-specific drawing uses a clipped content-rendering seam that can
now be supplied by the experimental Wayland shared-memory surface path.

The separate `nixbench-wayland-demo` executable is the first out-of-process
protocol consumer. It creates an `xdg_toplevel`, manages release-aware
shared-memory buffers and frame callbacks, and reacts to compositor-delivered
pointer and keyboard events.

The desktop runtime now talks to a backend-neutral host contract rather than
driving its SDL window directly. The shell renders through an SDL software
canvas into XRGB8888 CPU memory; the hosted SDL adapter uploads that frame to
its window, while a deterministic headless adapter and framebuffer conversion
tests exercise the same lifecycle and pixel contract without a display server.
This keeps the shell, focus, and Wayland policy independent of the eventual
`wsdisplay` and DRM/KMS output implementations.

Neither the in-process event/request contract nor the Wayland integration is a
stable public API yet. The Wayland slice currently has one scale-1 logical
output and no touch capability, pointer-axis scrolling, client cursor
rendering, buffer scale/transform or input-region handling, popups,
subsurfaces, clipboard, accelerated buffers, resize negotiation, process
supervision, or application-supplied global menus. General GTK/SDL
applications are therefore not expected to be usable yet. NixBench also does
not yet offer a supported direct-console runtime.

The initial chrome uses an original palette and geometry while exploring a
classic beveled Workbench/AROS-inspired vocabulary. AROS was studied as a design
reference, but no AROS source, constants, artwork, or other assets were copied.
This boundary preserves NixBench's BSD 2-Clause licensing.

See [PLAN.md](PLAN.md) for milestones, deliverables, and exit criteria.

## Tested configurations

- **NetBSD 10.1 (GENERIC), amd64** with SDL3 3.4.2, Wayland, libxkbcommon,
  and wayland-protocols from pkgsrc: default dependency discovery, generated
  xdg-shell bindings, native compilation of both SDL and experimental
  `wsdisplay` hosts, and all 24 then-current tests were confirmed working on
  July 14, 2026. An earlier X11-hosted run also published the nested Wayland
  socket and displayed the demo client's first rendered frame.

  The capability probe on the current QEMU guest reports SDL's Wayland, X11,
  offscreen, and dummy drivers but no KMSDRM driver. The guest has four static
  `/dev/dri/card*` character-device nodes, but actual libdrm opens return
  `ENODEV`: its QEMU VGA device has no configured DRM driver. Those nodes are
  therefore not usable KMS devices, regardless of their ownership or mode
  bits. The PCIVGA `wsdisplay` console also does not provide framebuffer
  information through the interface required by the software adapter. No
  console mode change, framebuffer mapping, DRM buffer allocation, modeset, or
  page flip was attempted.

- **NetBSD 11.0_RC6 (GENERIC), amd64 on a Lenovo ThinkPad X220** with Intel
  Sandy Bridge graphics and SDL3 3.4.2 from pkgsrc: a clean-machine setup,
  explicit `NIXBENCH_LIBDRM=ON` and `NIXBENCH_WAYLAND=ON` configuration, full
  native build, and all 28 tests were confirmed working on July 14, 2026. The
  kernel attaches `i915drmkms` and provides an `intelfb` console.

  The privileged query-only probe reports a supported 1366x768, 32-bit RGB
  `wsdisplay` layout and a live i915 DRM/KMS device. The internal LVDS panel is
  connected at 1366x768 at 60 Hz; dumb buffers, two CRTCs, eight encoders, and
  two legacy-visible planes are available. Both the software framebuffer and
  direct KMS paths pass preflight.

  The opt-in harness selected native screen index 0 (`/dev/ttyE0`), mapped the
  software framebuffer, presented its diagnostic frame for 2000 ms, and exited
  normally. Its parent verified restoration of emulation mode, video-on state,
  automatic VT handling, and the original active screen. An independent SSH
  watcher saw the root-only recovery record only while the processes were
  alive; it disappeared on successful restoration, and a fresh preflight and
  backend probe reported the original state. A later 5000 ms run selected
  `--desktop-preview` and completed through the same supervised,
  output-only software-framebuffer path. The parent again
  verified restoration, the guided postflight and a separate SSH preflight
  found screen 0 in emulation mode with automatic VT handling and video on,
  and the recovery record and harness processes were absent. Manual recovery
  was not needed for either run. Those recorded trials predate the explicit
  interactive mode: no DRM buffer allocation, modeset, page flip, or input
  read was attempted.

  A subsequent bounded `--interactive-preview` trial also completed on the
  X220. The software cursor responded, and the global menus and managed window
  could be operated with the physical pointer. Motion was usable but felt
  slower and less fluid than the hosted desktop. Pointer acceleration, input
  scheduling, and the cost of full software-rendered framebuffer updates are
  therefore explicit follow-up profiling and tuning work; the successful
  interaction trial does not turn this research harness into a production
  input/session path.

This is a manual target-system validation; automated NetBSD testing remains
future work.

## Build and run

Required development dependencies:

- A C11 compiler
- CMake 3.16 or newer and a supported build tool
- SDL 3.2.0 or newer, including its development files
- A video backend supported by SDL3; Xorg is the initial NetBSD host

The compositor path additionally uses the Wayland server library and scanner,
libxkbcommon, and the stable `xdg-shell.xml` from `wayland-protocols`. The
standalone demo also requires the Wayland client development library.
Configuration defaults to `-DNIXBENCH_WAYLAND=AUTO`: it enables the feature
when all server components are found and otherwise builds the SDL-only desktop.
Use `-DNIXBENCH_WAYLAND=ON` to require it, or `OFF` to omit it explicitly.
`NIXBENCH_BUILD_EXAMPLES` defaults to `ON`; use
`-DNIXBENCH_BUILD_EXAMPLES=OFF` to omit experimental clients. NetBSD's pkgsrc
`wayland`, `libxkbcommon`, and `wayland-protocols` packages provide these
components; no `pkg-config` executable is required by this build.

Detailed NetBSD DRM/KMS inventory is an optional libdrm feature controlled by
`-DNIXBENCH_LIBDRM=AUTO`, the default. `AUTO` enables it when `xf86drm.h`,
`xf86drmMode.h`, `libdrm/drm.h`, and the libdrm library are found, and otherwise
keeps the path-only probe available. `ON` makes any missing libdrm component a
configuration error; `OFF` omits all libdrm calls explicitly. Discovery checks
normal CMake search locations and NetBSD's base-system `/usr/X11R7/include`,
`/usr/X11R7/include/libdrm`, and `/usr/X11R7/lib` layout directly, so it does
not require `pkg-config`.

`nixbench-wsdisplay-smoke` is an explicitly opt-in hardware harness and is not
built by default. Enable it with `-DNIXBENCH_BUILD_WSDISPLAY_SMOKE=ON`. This
does not make `wsdisplay` a supported desktop runtime.

SDL3 is available from NetBSD pkgsrc as `devel/SDL3`. Configure, build, and test
with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Inspect the backends available on the current machine with:

```sh
./build/nixbench-backend-probe
```

The probe lists SDL video drivers and, on NetBSD, checks the default
`wsdisplay`, wscons input, and DRM device paths. It opens `wsdisplay` read-only
only long enough to query its type, current mode, and framebuffer layout. When
libdrm support was built, it also tries each DRM primary node read-write and
then read-only, and reports the live driver version, dumb-buffer capabilities,
KMS resources, CRTC state, cached connectors and modes, and legacy-visible
planes. It uses the cached connector query so the diagnostic does not request a
fresh display probe, and it does not enable additional DRM client capabilities.

The command never explicitly requests DRM master, changes a display mode, maps
device memory, allocates a DRM buffer, issues a page flip, or consumes input
events. Opening an idle primary DRM node may nevertheless grant DRM master
implicitly. The probe detects that state and drops it before collecting the
driver and KMS inventory; it aborts all further queries for that card if it
cannot establish the master state or drop an implicitly granted master.
On current NetBSD releases, dropping DRM master is privileged, so an
unprivileged probe of an otherwise idle, working primary node will abort safely
at this point. Use a controlled privileged diagnostic session when a full
idle-console inventory is needed; the probe never treats the failed partial
result as usable.

A card is reported as a direct KMS candidate only when the detailed query is
available, the card opens read-write, the master-safety check succeeds, any
implicit grant is dropped, a live driver version and KMS resources can be read,
at least one CRTC and encoder exist, dumb buffers are supported, and at least
one connected connector has a cached mode. This is a conservative preflight,
not proof that modesetting or ordinary page flips will work. Alternate device
paths can be supplied with `--wsdisplay`, `--keyboard`, `--mouse`, and
`--drm-directory`; use `--help` for details.

The opt-in `wsdisplay` presentation harness must run as root. Start with its
query-only preflight:

```sh
sudo ./build/nixbench-wsdisplay-smoke --preflight-only
```

On a NetBSD test machine, the convenience runner performs configuration,
building, tests, preflight, the bounded presentation, and postflight checks in
one guided command. Run it over SSH while watching the physical console:

```sh
./tools/run-wsdisplay-smoke.sh
```

It defaults to 3000 ms; an alternate safe duration can be passed as its sole
argument, up to `./tools/run-wsdisplay-smoke.sh 30000`. Thirty seconds is the
hard harness maximum. The script derives its outer timeout from that duration
and still requires typing `TAKEOVER` before it supplies the harness
acknowledgements. It explicitly selects `--runtime-preview`: the shared desktop
runtime, real NixInfo application, application-owned global menu bar, clock,
managed-window chrome, and software cursor are rendered into an SDL software
surface without initializing SDL video or opening X11 or Wayland. The worker
temporarily opens only the fixed `/dev/wskbd` and `/dev/wsmouse` mux aliases.
Relative pointer motion and the left button exercise menus and window dragging;
Escape requests an orderly early exit. Absolute-only pointer devices are not
translated by this first research provider.

Preflight reads `/dev/ttyEstat` to select the active zero-based screen node;
it does not change display state. A presentation run changes that console to
framebuffer mode briefly and accepts only durations from 250 through 30000
milliseconds (default 3000). Direct harness runs draw the diagnostic pattern
by default. `--desktop-preview` selects the same shell scene while remaining
output-only, and `--interactive-preview` explicitly adds the bounded wscons
input experiment. `--runtime-preview` instead connects that wscons provider and
the software framebuffer host to the shared desktop runtime used by the hosted
frontend. It refuses to run unless both risk acknowledgements are present. Keep
a second SSH session available and retain an outer timeout during hardware
bring-up:

```sh
sudo -n /usr/bin/timeout -s SIGTERM -k 15s 10s \
  ./build/nixbench-wsdisplay-smoke \
  --acknowledge-console-takeover \
  --acknowledge-no-crash-watchdog \
  --runtime-preview \
  --duration-ms 3000
```

Interactive input is acquired only by the framebuffer worker. Both mux
descriptors are closed and held button/keyboard state is discarded before an
acknowledged VT release and again during every cleanup path. The display
adapter itself remains output-only. While `/dev/wskbd` is owned by the worker,
normal console key translation, including the usual keyboard VT-switch
shortcuts, is unavailable; this is another reason to keep SSH recovery open.

Before forking the framebuffer worker, the parent records the console state in
the root-only `/var/run/nixbench-wsdisplay-smoke.state`. The parent stays
unmapped, enforces the deadline, reaps the worker, and independently restores
and verifies display mode, video state, VT mode, and active screen. It removes
the recovery record only after successful verification. If restoration is not
verified, or the supervisor itself dies, recover from another session with:

```sh
sudo ./build/nixbench-wsdisplay-smoke --recover
```

This supervisor is a development safeguard, not the production privileged
helper/watchdog. The worker still runs privileged, and its fixed-mux input is a
short-lived research path rather than production seat, keymap, hotplug, repeat,
or session support. The second acknowledgement recognizes that supervisor
failure still needs manual recovery. CTest exercises parsers, reducers,
rendering, help, and refusal with synthetic input only; automated tests never
open wscons devices or perform a console takeover.

Open the desktop in a development window:

```sh
./build/nixbench
```

Windowed operation is the development default. NixInfo opens at startup. Use
Project > New Window to create another application window, Refresh to update
the focused window's snapshot, View to show or hide its full kernel version,
and Window to close that window or its siblings. About NixInfo is a singleton,
and Quit NixInfo exits the application without exiting the desktop. The
NixBench desktop menu can start it again.

Click an internal window to focus it and bring it to the front, drag it by its
title bar, and use its top-left gadget to close it. Resize it using the gadget
at the right end of its bottom decorator rail. The global top bar switches
between the focused application's menus and the NixBench desktop menu. Click a
menu and then an item, or press and drag directly to an item. F10 opens the
keyboard menu path; use the arrow keys, Enter, and Escape to navigate or dismiss
it. Escape exits NixBench when no menu is open and no Wayland client owns
keyboard focus. The right end of the bar shows local time. Clicking the desktop
clears the active window. Pass `--fullscreen` only for a hosted full-display
preview. Close the outer host window to exit NixBench. Use `--help` to list all
current options.

The CMake configuration deliberately uses the system SDL3 package instead of
downloading dependencies during the build. Direct X11 dependencies are not
needed for the hosted-window phase.

When Wayland support is enabled, NixBench publishes a nested display beneath
`XDG_RUNTIME_DIR` and logs its `WAYLAND_DISPLAY` name. A development shell that
does not set a runtime directory can prepare one before starting NixBench:

```sh
install -d -m 700 "$HOME/.nixbench-runtime"
XDG_RUNTIME_DIR="$HOME/.nixbench-runtime" ./build/nixbench
```

The initial keymap follows libxkbcommon's `XKB_DEFAULT_*` environment. For
example, start NixBench with `XKB_DEFAULT_LAYOUT=it` to publish an Italian
layout to clients. NixBench does not yet import the active host Xorg layout
automatically; a desktop keyboard setting is planned.

In another terminal, use the display name logged by NixBench (normally
`wayland-0`) to start the standalone client:

```sh
XDG_RUNTIME_DIR="$HOME/.nixbench-runtime" \
WAYLAND_DISPLAY=wayland-0 \
./build/nixbench-wayland-demo
```

Clicking the centered control, or pressing Space or Enter while it is focused,
toggles its color and indicator. Escape closes the demo. The active client
window also supplies the temporary Application > Close Application menu in
NixBench's global bar. Pass `--exit-after-first-frame` to the demo for a
noninteractive rendering smoke run. Toolkit compatibility still requires the
additional protocol layers listed above.

## Contributing

The interfaces and source layout have not been stabilized yet. Early
contributions should begin with discussion of the relevant roadmap milestone
and should preserve the NetBSD-first, lightweight design goals.

Project artwork, names, and interface elements must be original or distributed
under compatible terms. Workbench and AROS are inspirations, not sources of
assets or branding.

## License

NixBench is distributed under the [BSD 2-Clause License](LICENSE).
