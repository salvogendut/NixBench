# NixBench

NixBench is an experimental desktop environment for NetBSD, written in C and
built around SDL3, an internal window manager, and an embedded Wayland
compositor. Its interaction model and visual direction draw inspiration from
Amiga Workbench and AROS while remaining an original project.

The target is a lightweight desktop that runs directly on the NetBSD console
without X.org underneath it. NixBench already supports its own Wayland clients,
GTK3 applications through their Wayland backend, and traditional X11
applications through rootless Xwayland.

## Screenshots

![NixBench running NixInfo, Sakura, NixClock, and xclock](docs/images/nixbench-desktop.png)

*NixBench running directly on NetBSD with native NixInfo, a GTK3/Wayland
Sakura terminal, native Wayland NixClock, and X11 xclock through rootless
Xwayland.*

![The 1984 Amstrad CPC emulator running on NixBench](docs/images/nixbench-1984.png)

*The SDL3-based 1984 Amstrad CPC emulator running through Wayland on NixBench,
alongside NixInfo and a Sakura terminal.*

## What works today

- Hosted development inside an SDL3 window and standalone operation through
  NetBSD `wsdisplay` and wscons.
- A Workbench-style global menu bar supplied by the focused application.
- Movable, resizable, minimizable, and maximizable managed windows.
- Native NixBench applications including NixInfo and NixClock.
- GTK3/Wayland applications such as Sakura and Midori, with an optional bridge
  that publishes their menus in the NixBench bar.
- Rootless Xwayland compatibility for traditional X11 applications,
  including keyboard focus, EWMH fullscreen requests, and bounded direct or
  `INCR` text clipboard interoperability with Wayland applications.
- Persistent application pins, window-control preferences, and configurable
  solid or gradient backdrops in `~/.nixbenchrc`.
- Delayed PNG screenshots and live CPU/memory history in NixInfo.
- Privilege separation between the ordinary-user desktop and the small
  root-owned console/device helper.

NixBench is still experimental. Its protocol extensions and internal APIs are
not stable, and the standalone path is not yet presented as a production login
session.

## Goals

- Make NetBSD a first-class development and runtime platform.
- Run standalone by owning display and input devices directly.
- Provide a responsive, coherent Workbench-inspired desktop.
- Run native applications as independent processes and windows.
- Make existing Unix GUI software useful through Wayland and X11 compatibility.
- Keep the core small, understandable, and portable where that does not
  compromise the NetBSD experience.

## Quick start

For a normal source build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/nixbench
```

An installed NetBSD standalone session is started from a physical console with:

```sh
nixbench-session --local
```

The standalone session owns the console, so read the recovery and installation
instructions before the first hardware run.

## Documentation

- [Architecture](docs/architecture.md) — hosted development, application
  compatibility, and the standalone target.
- [Project status and hardware validation](docs/project-status.md) — current
  implementation details and recorded NetBSD test results.
- [Building and hosted development](docs/building.md) — dependencies, CMake
  options, backend probing, and running the desktop in a development window.
- [ARM64 Xwayland build notes](docs/arm64-xwayland.md) — binary dependency
  shortcuts, thermal-safe pkgsrc builds, and the current NetBSD 10.1 protocol
  header/buildlink blocker.
- [Privilege-separated standalone session](docs/standalone-session.md) —
  installation, configuration, application probes, recovery gates, and
  physical validation.
- [Framebuffer research harness](docs/framebuffer-harness.md) — the older
  root-only `wsdisplay`/wscons experiment and its safety procedures.
- [Standalone backend design](docs/standalone-backend.md) — staged output and
  input backend architecture.
- [Privilege-boundary assessment](docs/privilege-boundary.md) — device
  authority, recovery, and process-separation design.
- [Development plan](PLAN.md) — milestones, deliverables, and exit criteria.

## Contributing

The interfaces and source layout have not stabilized. Early contributions
should begin with discussion of the relevant roadmap milestone and preserve
the NetBSD-first, lightweight design goals.

Project artwork, names, and interface elements must be original or distributed
under compatible terms. Workbench and AROS are inspirations, not sources of
assets or branding.

## License

NixBench is distributed under the [BSD 2-Clause License](LICENSE).
