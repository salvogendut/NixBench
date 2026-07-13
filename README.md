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

NixBench now has a minimal C11/SDL3 shell and a small internal window manager.
Two overlapping demonstration windows are drawn entirely by NixBench. They can
be independently focused, raised, moved, resized, and closed, and remain inside
the desktop. The manager uses stable window identifiers and keeps geometry,
stacking, focus, hit testing, and pointer routing independent of SDL and X11.
SDL is confined to input adaptation and rendering. These are shell-owned test
windows rather than application surfaces, and NixBench does not yet operate
directly on the NetBSD console.

The initial chrome uses an original palette and geometry while exploring a
classic beveled Workbench/AROS-inspired vocabulary. AROS was studied as a design
reference, but no AROS source, constants, artwork, or other assets were copied.
This boundary preserves NixBench's BSD 2-Clause licensing.

See [PLAN.md](PLAN.md) for milestones, deliverables, and exit criteria.

## Tested configurations

- **NetBSD 10.1 (GENERIC), amd64** with SDL3 3.4.2 from pkgsrc: the CMake
  build and hosted desktop window were confirmed working on July 13, 2026.

This is a manual target-system validation; automated NetBSD testing remains
future work.

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

Windowed operation is the development default. Click an internal window to
focus it and bring it to the front, drag it by its title bar, and use its
top-left gadget to close it. Resize it using the gadget at the right end of its
bottom decorator rail. Clicking the desktop clears the active window. Pass
`--fullscreen` only for a hosted full-display preview. Press Escape or close
the outer host window to exit NixBench. Use `--help` to list all current options.

The CMake configuration deliberately uses the system SDL3 package instead of
downloading dependencies during the build. Direct X11 dependencies are not
needed for the hosted-window phase.

## Contributing

The interfaces and source layout have not been established yet. Early
contributions should begin with discussion of the relevant roadmap milestone
and should preserve the NetBSD-first, lightweight design goals.

Project artwork, names, and interface elements must be original or distributed
under compatible terms. Workbench and AROS are inspirations, not sources of
assets or branding.

## License

NixBench is distributed under the [BSD 2-Clause License](LICENSE).
