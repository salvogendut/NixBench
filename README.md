# NixBench

NixBench is an experimental desktop environment for NetBSD, built in C with
SDL3. Its interaction model and visual direction take inspiration from Amiga
Workbench and AROS while remaining an original project.

The goal is a lightweight, coherent desktop that eventually runs directly on
the NetBSD console without requiring X.org, while supporting both applications
designed specifically for NixBench and traditional Unix GUI applications. The
project is in its bootstrap stage. The first runnable program opens an empty
SDL3 desktop screen under a host window system.

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

The first usable prototype will run on NetBSD with Xorg:

- **C11** is the implementation language.
- **CMake** drives configuration and builds.
- **SDL3** provides rendering, input, and the foundation of the desktop shell.
- **XCB** provides the X11 window-manager operations that SDL3 does not expose.
- **NixBench** owns window-management policy and manages ordinary X11 clients.

SDL3 is not itself a display server or a general-purpose window manager. In the
initial architecture, SDL3 renders the shell and NixBench-owned interfaces,
while XCB communicates with Xorg to discover, place, decorate, and control
application windows. SDL applications and traditional Unix applications remain
normal, isolated processes rather than plugins loaded into the desktop.

X.org is a transitional development platform, not the final runtime
architecture. It lets the project validate the shell, interaction model, and
application management before taking responsibility for the entire display
stack.

### Standalone target architecture

The final target starts from a NetBSD console without an X server beneath it:

- NixBench owns display output through DRM/KMS where supported, with a practical
  framebuffer path considered for hardware without KMS.
- Console keyboard and pointer input are obtained through the appropriate
  NetBSD/SDL3 backend, including wscons where applicable.
- A NixBench compositor combines the shell and independent application surfaces
  into the physical display output.
- Native applications remain separate processes and submit surfaces through a
  versioned local client protocol rather than opening the console device
  themselves.
- X11 support becomes an optional compatibility service layered on NixBench; it
  is not required to run the desktop or native applications.

SDL3's [KMSDRM backend](https://wiki.libsdl.org/SDL3/README-kmsbsd) does not
currently support NetBSD console output. Reaching the standalone target
therefore includes enabling that backend on NetBSD or providing the necessary
NetBSD platform integration, preferably in a form that can be maintained
upstream.

The native surface protocol and the eventual X11 compatibility mechanism are
not public contracts yet. The roadmap requires focused prototypes before either
choice is stabilized.

## Project status

NixBench now has a minimal C11/SDL3 shell that opens a resizable desktop screen,
clears it to the initial NixBench background color, handles window events, and
shuts down cleanly. It does not yet manage application windows or operate
directly on the NetBSD console.

See [PLAN.md](PLAN.md) for milestones, deliverables, and exit criteria.

## Build and run

Required development dependencies:

- A C11 compiler
- CMake 3.16 or newer and a supported build tool
- SDL 3.2.0 or newer, including its development files
- A video backend supported by SDL3; Xorg is the initial NetBSD host

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

Pass `--fullscreen` to occupy the current display. Press Escape or close the
window to exit. Use `--help` to list all current options.

The CMake configuration deliberately uses the system SDL3 package instead of
downloading dependencies during the build. XCB will be added when work begins
on the hosted X11 window-manager backend.

## Contributing

The interfaces and source layout have not been established yet. Early
contributions should begin with discussion of the relevant roadmap milestone
and should preserve the NetBSD-first, lightweight design goals.

Project artwork, names, and interface elements must be original or distributed
under compatible terms. Workbench and AROS are inspirations, not sources of
assets or branding.

## License

NixBench is distributed under the [BSD 2-Clause License](LICENSE).
