# NixBench

NixBench is an experimental desktop environment for NetBSD, built in C with
SDL3. Its interaction model and visual direction take inspiration from Amiga
Workbench and AROS while remaining an original project.

The goal is a lightweight, coherent desktop that can support both applications
designed specifically for NixBench and traditional Unix GUI applications. The
project is at the planning stage: there is no runnable implementation yet.

## Goals

- Make NetBSD a first-class development and runtime platform.
- Provide a responsive desktop shell with a Workbench-inspired workflow.
- Treat volumes, files, applications, and launchers as clear desktop objects.
- Run native NixBench applications as independent processes and windows.
- Manage conventional X11 applications without requiring them to be modified.
- Keep the core small, understandable, and built from portable components where
  doing so does not compromise the NetBSD experience.

## Initial architecture

The first usable version will run on NetBSD with Xorg:

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

A native integration API and local IPC may be introduced once concrete
shell/application interactions require them. They are intentionally not public
contracts yet.

## Project status

NixBench currently consists only of its project definition and initial
roadmap. The next milestone will introduce the CMake and C source skeleton and
open the first SDL3 desktop window.

See [PLAN.md](PLAN.md) for milestones, deliverables, and exit criteria.

## Expected development dependencies

The bootstrap milestone is expected to require:

- A C11 compiler
- CMake and a supported build tool
- SDL3 development files
- XCB development files
- Xorg for integration testing

Exact package names and supported versions will be documented when the first
build is added.

## Contributing

The interfaces and source layout have not been established yet. Early
contributions should begin with discussion of the relevant roadmap milestone
and should preserve the NetBSD-first, lightweight design goals.

Project artwork, names, and interface elements must be original or distributed
under compatible terms. Workbench and AROS are inspirations, not sources of
assets or branding.

## License

NixBench is distributed under the [BSD 2-Clause License](LICENSE).
