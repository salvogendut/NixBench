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
- **SDL3** creates the host window and provides rendering and input.
- **NixBench** draws its entire desktop and internal window model inside that
  one SDL window.
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
`wl_compositor`, `wl_shm`, a pointer-only `wl_seat`, and stable `xdg_wm_base`;
accepts ARGB/XRGB shared-memory buffers; and maps an `xdg_toplevel` into an
internal NixBench window. SDL pointer motion and buttons are routed to client
content with surface-coordinate scaling and an implicit button grab. The
compositor copies committed buffers before releasing them, so clients and the
shell do not share mutable rendering state.

A standalone `nixbench-wayland-demo` client renders through `wl_shm` and
exercises pointer interaction with a beveled toggle control. The socket-pair
protocol test covers configure/acknowledge, mapping, pixels, frame completion,
seat discovery, scaled pointer events, grabs, focus cleanup, close, and unmap
without requiring a running display server.

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

SDL3's [KMSDRM backend](https://wiki.libsdl.org/SDL3/README-kmsbsd) does not
currently support NetBSD console output. Reaching the standalone target
therefore includes enabling that backend on NetBSD or providing the necessary
NetBSD platform integration, preferably in a form that can be maintained
upstream.

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
pointer events.

Neither the in-process event/request contract nor the Wayland integration is a
stable public API yet. The Wayland slice currently provides pointer input only:
it does not advertise outputs or keyboard/touch capabilities and has no
pointer-axis scrolling, client cursor rendering, buffer scale/transform or
input-region handling, popups, subsurfaces, clipboard, accelerated buffers,
resize negotiation, process supervision, or application-supplied global menus.
General GTK/SDL applications are therefore not expected to be usable yet.
NixBench also does not yet operate directly on the NetBSD console.

The initial chrome uses an original palette and geometry while exploring a
classic beveled Workbench/AROS-inspired vocabulary. AROS was studied as a design
reference, but no AROS source, constants, artwork, or other assets were copied.
This boundary preserves NixBench's BSD 2-Clause licensing.

See [PLAN.md](PLAN.md) for milestones, deliverables, and exit criteria.

## Tested configurations

- **NetBSD 10.1 (GENERIC), amd64** with SDL3 3.4.2, Wayland, and
  wayland-protocols from pkgsrc: default dependency discovery, generated
  xdg-shell bindings, all 15 tests, and an X11-hosted startup with a published
  nested Wayland socket and the demo client's first rendered frame were
  confirmed working on July 13, 2026.

This is a manual target-system validation; automated NetBSD testing remains
future work.

## Build and run

Required development dependencies:

- A C11 compiler
- CMake 3.16 or newer and a supported build tool
- SDL 3.2.0 or newer, including its development files
- A video backend supported by SDL3; Xorg is the initial NetBSD host

The compositor path additionally uses the Wayland server library and scanner
plus the stable `xdg-shell.xml` from `wayland-protocols`. The standalone demo
also requires the Wayland client development library. Configuration defaults
to `-DNIXBENCH_WAYLAND=AUTO`: it enables the feature when all server components
are found and otherwise builds the SDL-only desktop. Use
`-DNIXBENCH_WAYLAND=ON` to require it, or `OFF` to omit it explicitly.
`NIXBENCH_BUILD_EXAMPLES` defaults to `ON`; use
`-DNIXBENCH_BUILD_EXAMPLES=OFF` to omit experimental clients. NetBSD's pkgsrc
`wayland` and `wayland-protocols` packages provide these components; no
`pkg-config` executable is required by this build.

SDL3 is available from NetBSD pkgsrc as `devel/SDL3`. Configure, build, and test
with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

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
it. Escape exits NixBench when no menu is open. The right end of the bar shows
local time. Clicking the desktop clears the active window. Pass `--fullscreen`
only for a hosted full-display preview. Close the outer host window to exit
NixBench. Use `--help` to list all current options.

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

In another terminal, use the display name logged by NixBench (normally
`wayland-0`) to start the standalone client:

```sh
XDG_RUNTIME_DIR="$HOME/.nixbench-runtime" \
WAYLAND_DISPLAY=wayland-0 \
./build/nixbench-wayland-demo
```

Clicking the centered control toggles its color and indicator. The active
client window also supplies the temporary Application > Close Application menu
in NixBench's global bar. Pass `--exit-after-first-frame` to the demo for a
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
