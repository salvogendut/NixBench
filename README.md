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
focus cleanup, close, and unmap without requiring a running display server.

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
console state on normal teardown. An opt-in supervisor harness, bounded by
default with an explicit interactive run-until-exit mode, has completed the
first two-second framebuffer presentation and
verified restoration on a ThinkPad X220. An explicit desktop-preview frame
source now renders the real menu bar, clock, and managed-window chrome into the
same canonical CPU frame without initializing SDL video. A separate,
explicit `--interactive-preview` research mode temporarily owns the fixed
`/dev/wskbd` and `/dev/wsmouse` mux aliases, adds a software cursor, routes the
left button to menus and window dragging, and queries the active wscons keymap
for the bounded shell controls Escape, F10, the four arrows, Return, and keypad
Enter. F10 and those navigation keys drive the global menu path; Escape also
provides an orderly early exit. Repeated downs are marked as repeats and
`ALL_KEYS_UP` synthesizes every held control release. A new explicit
`--runtime-preview` replaces that lightweight scene
with the same shared desktop runtime used by the hosted SDL frontend, including
the real NixInfo application and application-owned global menus. It keeps
Wayland publication disabled and still uses only the software canvas,
`wsdisplay`, and wscons devices. That research harness is not a crash-safe
login session because its desktop runtime and device worker both remain root.
See
[the standalone backend architecture](docs/standalone-backend.md) for the
staged safety and implementation boundaries.
The root-helper versus ordinary-user-core decision is detailed in the
[standalone privilege-boundary assessment](docs/privilege-boundary.md).

The separate, opt-in `nixbench-wsdisplay-session` milestone now implements that
process split without changing the old harness. A root recovery supervisor and
root device worker retain `wsdisplay`, wscons, VT, presentation, heartbeat, and
restoration authority. The trusted child irreversibly changes to the invoking
sudo account before it executes `nixbench-session-core`, which creates the
desktop, publishes a private Wayland display, and launches NixClock. The core
receives only a bounded anonymous protocol endpoint; NixClock does not receive
that endpoint, and neither process receives a framebuffer, wscons, recovery,
or VT descriptor. This new path has device-free coverage but has not yet
completed the broader failure-injection matrix. Its physical console takeover,
normal exit, VT 1 -> 2 -> 1 cycle, and supervised SIGTERM recovery gate have
passed, but it remains an explicitly acknowledged development milestone rather
than a supported login session.

The older `nixbench-wsdisplay-smoke` research harness deliberately remains
available for framebuffer and input experiments. It neither publishes Wayland
nor launches external applications, and its runtime continues to execute as
root. Do not confuse it with the privilege-separated session.

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

NixClock is the first real out-of-process application. It creates an
`xdg_toplevel`, manages release-aware shared-memory buffers and frame callbacks,
and draws a scalable analog clock with continuous hour and minute hands. Its
redraw schedule follows local wall-clock boundaries instead of accumulating
timer drift. The focused window publishes an application-named **NixClock**
menu with **Quit** and a **Settings** menu whose checkable **Show seconds** item
toggles a differently colored third hand. The
`nixbench-application-menu-v1` extension sends activated commands back to the
owning process and switches the global bar to that surface's committed menu.
The separate `nixbench-wayland-demo` remains the smaller pointer and keyboard
protocol probe.

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
supervision, desktop-managed application launching, or general toolkit bridge
for application menus. NixClock exercises the first private application-menu
protocol, but general GTK/SDL applications are therefore not expected to be
usable yet. NixBench also does not yet offer a supported direct-console
runtime.

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
  native build, and all 32 tests were confirmed working on July 15, 2026. The
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
  slower and less fluid than the hosted desktop. Subsequent profiling and
  source-shadow damage suppression reduced the measured userspace-read-to-
  framebuffer-copy-complete average to 5 ms on this machine. Native wscons
  timestamp-bucket grouping is now physically validated on the X220: all 1738
  relative events used native timestamps, forming 1035 buckets with 703 same-
  timestamp events and no fallback or clock-source reset. The user reported
  that the result was all good. The subsequent readiness-driven blocking-wait
  trial also felt good: all 1232 waits woke for wscons input, with no timeout,
  interruption or clock regression; that normal input-driven trial observed no
  lifecycle activity. Input read through
  framebuffer-copy completion averaged 6 ms and reached 24 ms at most, and the
  supervisor restored and independently verified the console. The successful
  interaction trials do not turn this research harness into a production
  input/session path.

  On 2026-07-14, the first guided `--runtime-preview` trial also completed on
  the X220. The physical console displayed the shared desktop runtime and real
  NixInfo application through `wsdisplay`/wscons without X11, Wayland, or SDL
  video. The user confirmed the scene worked, and the guided postflight
  completed with the console restored.

  On 2026-07-15, the no-deadline `--vt-cycle` runtime trial completed a
  physical switch from VT 1 to VT 2 and back. Release/acquire requests and
  completions and wscons input suspend/resume each balanced at 1/1 with no
  timing regression. Release acknowledgement took 145 ms, the console stayed
  away for 11383 ms, and acquire acknowledgement took 13 ms. Escape then
  produced the orderly exit and its input-associated frame completed in 5 ms.
  The supervisor verified the saved console state, the independent postflight
  matched emulation mode, automatic VT handling, video-on state, and screen 0,
  and the active one-based VT was again 1.

  A subsequent adaptive-pointer trial kept 888 of 1709 relative events at
  100% gain, put 626 at 101..149%, 186 at 150..199%, and 9 at 200..249%; it
  never reached 250%. Userspace input read through framebuffer-copy completion
  still averaged 5 ms. The overall feel was good, but the user reported that
  small movements fluttered instead of holding a straight course. A repeat
  trial after the carry-hygiene revision looked and felt good. It cleared 45
  precision-boundary and 20 direction-boundary carries, reported zero non-edge
  suppressions and zero raw-zero packets across 1855 relative events, and
  retained the 5 ms input-to-copy average. Console restoration again passed.

This is a manual target-system validation; automated NetBSD testing remains
future work.

## Build and run

Required development dependencies:

- A C11 compiler
- CMake 3.16 or newer and a supported build tool
- SDL 3.2.0 or newer, including its development files
- A video backend supported by SDL3; Xorg is the initial NetBSD host

The compositor path additionally uses the Wayland server library and scanner,
libxkbcommon, and the stable `xdg-shell.xml` from `wayland-protocols`. NixClock
and the separate demo also require the Wayland client development library.
Configuration defaults to `-DNIXBENCH_WAYLAND=AUTO`: it enables the feature
when all server components are found and otherwise builds the SDL-only desktop.
Use `-DNIXBENCH_WAYLAND=ON` to require it, or `OFF` to omit it explicitly.
`NIXBENCH_BUILD_EXAMPLES` defaults to `ON`; use
`-DNIXBENCH_BUILD_EXAMPLES=OFF` to omit experimental clients. NetBSD's pkgsrc
`wayland`, `libxkbcommon`, and `wayland-protocols` packages provide these
components; no `pkg-config` executable is required by this build.
`NIXBENCH_BUILD_APPLICATIONS` also defaults to `ON`; set it to `OFF` to omit
NixClock. Application targets are built only when the Wayland client and
compositor dependencies are available.

Detailed NetBSD DRM/KMS inventory is an optional libdrm feature controlled by
`-DNIXBENCH_LIBDRM=AUTO`, the default. `AUTO` enables it when `xf86drm.h`,
`xf86drmMode.h`, `libdrm/drm.h`, and the libdrm library are found, and otherwise
keeps the path-only probe available. `ON` makes any missing libdrm component a
configuration error; `OFF` omits all libdrm calls explicitly. Discovery checks
normal CMake search locations and NetBSD's base-system `/usr/X11R7/include`,
`/usr/X11R7/include/libdrm`, and `/usr/X11R7/lib` layout directly, so it does
not require `pkg-config`.

The privilege-separated NetBSD session is also excluded by default. Enabling
`-DNIXBENCH_BUILD_WSDISPLAY_SESSION=ON` requires Wayland server support,
Wayland client development files, and `NIXBENCH_BUILD_APPLICATIONS=ON`. It
builds:

- `nixbench-wsdisplay-session`, the root recovery supervisor and device-helper
  launcher;
- `nixbench-session-core`, its internal ordinary-user desktop process; and
- `nixclock`, the initial native application launched on the core's private
  Wayland display.

`nixbench-wsdisplay-smoke` is the older, explicitly opt-in root hardware
harness and is not built by default. Enable it separately with
`-DNIXBENCH_BUILD_WSDISPLAY_SMOKE=ON`. Neither option makes `wsdisplay` a
supported production runtime.

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

### Opt-in privilege-separated wsdisplay session

The guided entry point for the new standalone milestone is NetBSD-only and must
be started over SSH while the physical console is visible:

```sh
./tools/run-wsdisplay-session.sh
```

The script requires passwordless `sudo` for recovery, configures
`NIXBENCH_WAYLAND=ON`, `NIXBENCH_BUILD_APPLICATIONS=ON`, and
`NIXBENCH_BUILD_WSDISPLAY_SESSION=ON`, builds, runs the non-destructive tests,
stages the device launcher as the root-owned, non-writable
`/var/run/nixbench-wsdisplay-session`, and performs a query-only preflight with
that copy. The ordinary-user core and NixClock remain in the build tree and are
never executed until after the credential drop. The script changes no display
state until the operator types `START-NIXBENCH`. The session has no automatic
deadline: exit from the desktop menu, press Escape while no Wayland client owns
keyboard focus, or use the printed supervisor `SIGTERM` command from the
retained second SSH session.

For the explicit supervisor-termination recovery gate, run:

```sh
NIXBENCH_EXPECT_SUPERVISOR_TERM=1 ./tools/run-wsdisplay-session.sh
```

That mode omits the VT-switch instructions and requires the operator to send
the exact printed `sudo kill -TERM` command from the second SSH session. It
reports success only when SIGTERM drove the shutdown without an independent
supervision fault, the worker and ordinary-user session are gone, the console
was independently restored, the recovery record is absent, and the original
VT is active again. A normal desktop exit or any nonzero gated-launch result is
reported as a failed trial.

Before any privileged device is opened, the launcher reserves standard file
descriptors 0, 1, and 2 so a closed standard stream cannot accidentally become
an inherited device capability. A root-owned mode-0600 flock at
`/var/run/nixbench-wsdisplay-session.lock` serializes launch, preflight, and
recovery and remains held by both the supervisor and live device worker. The
launcher captures the console and exclusively creates the root-owned recovery
record at `/var/run/nixbench-wsdisplay-session.state`, so unresolved recovery
also prevents another launch. The root supervisor remains unmapped, tracks the
worker and core lifetimes, applies bounded TERM/KILL escalation, and restores
and independently verifies the saved console state before removing the record.
The device worker enforces the core handshake and heartbeat.

The device worker alone opens `wsdisplay` and wscons, owns `VT_PROCESS`, maps
the framebuffer, and converts canonical frames. The core receives a private
anonymous socket endpoint after an irreversible `setgid()`/`setuid()`
transition to the sudo account. It creates a session-owned runtime directory,
publishes a private Wayland socket, and launches NixClock with the matching
`XDG_RUNTIME_DIR` and `WAYLAND_DISPLAY`. NixClock participates in the normal
global-menu path but receives neither the helper protocol descriptor nor any
console capability.

Keep a second SSH session open throughout the first hardware trials. If the
launcher leaves the recovery record, first verify that no
`nixbench-wsdisplay-session` helper remains, then run:

```sh
sudo /var/run/nixbench-wsdisplay-session --recover
```

`sudo /var/run/nixbench-wsdisplay-session --preflight` is query-only. The new
targets build natively on the NetBSD test host and all 46 device-free tests
pass there, including the ordinary-user core integration and failed-client
launch path. `ldd` reports only NetBSD libc for the staged root launcher; SDL3,
Wayland, and their client-side dependencies are confined to the ordinary-user
side. A staged-launcher preflight also confirmed screen 0 in emulation mode,
automatic VT handling, and video on without changing display state.

The first physical privilege-separated trial then launched the desktop and
NixClock through the private Wayland display as UID 1000. Normal desktop exit
completed, the supervisor independently verified restoration, removed the
recovery record, and postflight again found screen 0 in emulation mode with
automatic VT handling, video on, and one-based VT 1 active. A subsequent
privilege-separated trial switched from VT 1 to VT 2 and back. Release/acquire
completions balanced at 1/1, the ordinary-user desktop and NixClock returned
after acquisition, and normal exit again cleared the recovery record.
Independent postflight verified screen 0, emulation mode, automatic VT
handling, video on, and active VT 1. The supervised SIGTERM gate also passed:
after receiving the operator signal, the root supervisor delivered SIGTERM to
the ordinary-user core. The core completed its in-band orderly shutdown,
worker/core cleanup succeeded, restoration was verified, and the recovery
record was removed. Independent postflight again found screen 0 in emulation
mode with automatic VT handling, video on, and one-based VT 1 active, and the
guided harness reported success. That successful trial exposed a non-fatal
NixClock Wayland EOF diagnostic; a follow-up cleanup-order fix now keeps the
private display alive until the tracked application has exited. Core crash or
hang, malformed protocol, worker or supervisor hard failure, and repeated
sessions remain hardware gates. The guided command therefore remains an opt-in
development test rather than a login-session installation procedure.

The opt-in `wsdisplay` presentation harness must run as root. Start with its
query-only preflight:

```sh
sudo ./build/nixbench-wsdisplay-smoke --preflight-only
```

On a NetBSD test machine, the convenience runner performs configuration,
building, tests, preflight, the supervised presentation, and postflight checks
in one guided command. Run it over SSH while watching the physical console:

```sh
./tools/run-wsdisplay-smoke.sh [--vt-cycle] [duration-ms]
```

Normal trials default to 3000 ms. `--vt-cycle` with no duration now runs until
Escape so the operator can reach the physical console without racing a
deadline; supplying an explicit duration keeps the 250..30000 ms bounded mode.
Bounded runs retain the derived outer timeout. The no-deadline form requires
typing `TAKEOVER-UNTIL-EXIT`, omits the outer timeout, and leaves the parent
supervisor and recovery record active throughout. It explicitly selects
`--runtime-preview`: the shared desktop runtime, real NixInfo application,
application-owned global menu bar, clock,
managed-window chrome, and software cursor are rendered into an SDL software
surface without initializing SDL video or opening X11 or Wayland. The worker
temporarily opens only the fixed `/dev/wskbd` and `/dev/wsmouse` mux aliases.
Relative pointer motion and the left button exercise menus and window dragging.
The active-map F10, arrow, Return, keypad-Enter, and Escape bindings exercise
the keyboard menu path; Escape requests an orderly early exit when no menu is
open. Absolute-only pointer devices are not translated by this first research
provider, and these limited controls are not general text or modifier input.

The guided hardware run configures `RelWithDebInfo` by default so its pointer
measurements are not dominated by unoptimized per-pixel diagnostic code. Set
`NIXBENCH_BUILD_TYPE=Debug` explicitly when the takeover is intended for
debugger work instead of responsiveness measurement.

Raw wscons relative motion retains a flat identity profile as its library and
command-line default. `--wscons-pointer-profile flat|adaptive` selects the
profile. Flat mode may be tuned from 25 through 400% with
`--wscons-pointer-sensitivity-percent`; signed per-axis fixed-point carry keeps
fractional gain symmetric and drift-free. An explicit sensitivity cannot be
combined with `--wscons-pointer-profile adaptive`. Neither profile is ever
applied to hosted SDL input. The guided X220 runner selects adaptive mode.

On NetBSD, the adaptive profile validates each native wscons event's full
realtime `timespec` and places X and Y events with matching seconds and
nanoseconds in one timestamp bucket. NetBSD stamps these events with
`getnanotime(9)`, whose expected precision is one kernel clock tick despite the
nanosecond-shaped field, so a bucket is not necessarily one physical report.
A bucket uses the preceding completed bucket's velocity, smoothed with a
one-quarter EWMA, so all motion in it receives the same gain. Invalid native
timestamps fall back to the monotonic userspace-read clock. A native/fallback
source switch, timestamp regression, at least 100 ms idle, pointer-edge clamp,
profile/configuration change, or input lifecycle transition safely resets the
adaptive history. Gain is 100% through 400 raw counts per second, interpolates
linearly to 150% at 750, 200% at 1500, and 250% at 2500, then remains at 250%.
When filtered motion returns to identity gain, the reducer clears both
fractional carries rather than applying an old accelerated fraction to later
precision motion. It also clears an axis carry when that axis reverses sign,
preventing a residual from the previous direction from delaying a correction.
The carry correction and native timestamp-bucket grouping are physically
validated on the X220. In the timestamp trial, all 1738 relative events used
native timestamps, with no fallback, clock-source reset, or timestamp
regression.

Host event timestamps and all input-to-frame timing remain the
`CLOCK_MONOTONIC` time captured after `read`. Native realtime is used only
inside the acceleration estimator. A realtime clock step can safely reset its
history or briefly distort acceleration, but cannot alter host dispatch or
latency measurements.

The interactive worker no longer wakes on a fixed 10 ms input slice. It blocks
in `poll(2)` on the wsdisplay lifecycle self-pipe and active keyboard/mouse
descriptors until lifecycle activity, input readiness, or the remaining trial
deadline. Run-until-exit mode uses the same readiness-driven wait without
treating wait expiry as a presentation deadline. Lifecycle events are checked
before blocking and after every wake,
timeout, or interruption, and take priority when lifecycle and input become
ready together. Each loop phase drains at most 128 host events and 64 input
events, with host events rechecked around input handling and presentation so an
input flood cannot starve VT release or termination. Device-free tests cover
the wait rules, and the X220 hardware trial physically validated the normal
input-driven path: 1232 calls produced 1232 input-ready wakes and no timeout,
interruption, signal-pipe wake, simultaneous readiness, or queued host event.
The interaction felt good, average input-read-to-copy completion was 6 ms, and
console restoration passed.

`--wscons-input-stats` identifies the active profile and reports raw and
logical distance, unit deltas, suppression/clamping, adaptive gain buckets,
peak filtered velocity (capped at 2500 counts/s) and gain, idle/timestamp/edge
reset counts, precision and direction carry resets, non-edge suppression,
zero-valued relative packets, native/fallback timestamp usage, motion-group
and same-timestamp event counts, clock-source resets, event gaps, and userspace-
read-to-framebuffer-copy-complete timing. That timing excludes
device/kernel queueing, scanout, and physical-display latency. Its input-frame
pipeline also separates time waiting to render, SDL software rendering,
synchronous host presentation, the framebuffer-copy-complete timestamp, and
event delivery. Blocking-wait calls, input and signal-pipe readiness,
simultaneous readiness, host events, timeouts, and interruptions are reported
alongside that pipeline. Keyboard diagnostics report the current discovered
binding count plus raw, emitted, repeated, ignored, `ALL_KEYS_UP`, and
synthesized-release events. VT diagnostics report release/acquire requests and
completions, input suspend/resume counts, timing regressions, and min/average/
max release-acknowledge, suspended-away, and acquire-acknowledge durations.
Those durations measure userspace observation and handling, not exact kernel
switch or display-scanout latency.

The first measured X220 runtime averaged 176 ms from input read through the
framebuffer copy. A canonical 32-bit conversion fast path reduced that to
36 ms: about 2 ms of rendering and 34 ms in the remaining full mapped-
framebuffer write. The `wsdisplay` host now keeps a tightly packed source
shadow in ordinary RAM. Its first frame and every frame after VT reacquisition
remain full refreshes; steady frames compare visible rows and convert only the
span between each row's first and last changed pixel. No framebuffer readback
is used. Damage covering more than half the frame and shadow-allocation failure
safely retain full-frame presentation. The later guided X220 validation of
this damage-suppressed path averaged 5 ms from userspace input read through
framebuffer-copy completion.

Preflight reads `/dev/ttyEstat` to select the active zero-based screen node;
it does not change display state. A bounded presentation changes that console
to framebuffer mode and accepts only durations from 250 through 30000
milliseconds (default 3000). `--until-exit` is mutually exclusive with an
explicit duration and is accepted only for interactive/runtime content with an
Escape path. Direct harness runs draw the diagnostic pattern
by default. `--desktop-preview` selects the same shell scene while remaining
output-only, and `--interactive-preview` explicitly adds the experimental
wscons input path. `--runtime-preview` instead connects that wscons provider and
the software framebuffer host to the shared desktop runtime used by the hosted
frontend. It refuses to run unless both risk acknowledgements are present. Keep
a second SSH session available. Bounded direct runs should retain an outer
timeout during hardware bring-up:

```sh
sudo -n /usr/bin/timeout -s SIGTERM -k 15s 10s \
  ./build/nixbench-wsdisplay-smoke \
  --acknowledge-console-takeover \
  --acknowledge-no-crash-watchdog \
  --runtime-preview \
  --wscons-pointer-profile adaptive \
  --wscons-input-stats \
  --duration-ms 3000
```

The opt-in guided VT checkpoint requires a balanced release/reacquire and
input suspend/resume pair, clean lifecycle timing, and a post-acquire frame:

```sh
./tools/run-wsdisplay-smoke.sh --vt-cycle
```

With no numeric argument this guided form supplies `--until-exit` and has no
automatic presentation deadline. Press Escape with no menu open after testing
to exit successfully. An explicit numeric argument opts back into bounded
mode. The harness prints its supervisor PID and an exact second-SSH SIGTERM
command for cancellation; cancellation restores the console but intentionally
returns failure status.

Before takeover, the runner captures the originating one-based VT. It chooses
VT 2 as the idle away console unless VT 2 is the origin, in which case it
chooses VT 1, and prints the exact switch-away and return commands for the
retained second SSH session. Set `NIXBENCH_VT_AWAY` to another configured,
idle text console before starting if that default is unsuitable. Pause until
the away console is visible before running the printed return command.

This supplies the harness `--require-vt-cycle` option, which is accepted only
for an interactive/runtime preview and fails unless the worker closes input,
acknowledges release, reacquires and reconfigures, reopens input, and then
completes a full redraw. It also rejects lifecycle timestamp regressions,
missing timing samples, and a missing post-acquire frame. The keyboard
menu-navigation bindings still require focused physical validation; the
complete release/acquire cycle and Escape exit have already passed on the X220.

Interactive input is acquired only by the framebuffer worker. Both mux
descriptors are closed and held button/keyboard state is discarded before an
acknowledged VT release and again during every cleanup path. The display
adapter itself remains output-only. While `/dev/wskbd` is owned by the worker,
normal console text translation and the usual keyboard VT-switch shortcuts are
unavailable. NixBench's limited active-map shell controls remain available,
but general text/modifier translation does not; this is another reason to keep
SSH recovery open.

Before forking the framebuffer worker, the parent records the console state in
the root-only `/var/run/nixbench-wsdisplay-smoke.state`. The parent stays
unmapped, enforces the deadline for bounded runs or waits for explicit exit,
reaps the worker, and independently restores
and verifies display mode, video state, VT mode, and active screen. It removes
the recovery record only after successful verification and confirmed worker
reap. If restoration is not verified, the worker cannot be reaped, or the
supervisor itself dies, recover from another session with:

```sh
sudo ./build/nixbench-wsdisplay-smoke --recover
```

This all-root supervisor is a development safeguard, not the separate
privilege-separated session described above or a production helper/watchdog.
The worker still runs privileged, and its fixed-mux input is a short-lived
research path rather than a general text/modifier keymap, hotplug, multi-device,
production seat, or session implementation. The second acknowledgement
recognizes that supervisor
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
`wayland-0`) to start NixClock:

```sh
XDG_RUNTIME_DIR="$HOME/.nixbench-runtime" \
WAYLAND_DISPLAY=wayland-0 \
./build/nixclock
```

Focus the clock to place its **NixClock** and **Settings** menus in the global
bar. Choose **Settings > Show seconds** to toggle the colored seconds hand; the
item's check mark follows the setting. Choose **NixClock > Quit** to terminate
only the clock application. Pass `--show-seconds` to enable that hand at
startup. NixClock currently connects only to NixBench because its required
global-menu extension is deliberately still private and experimental.

The earlier separate protocol probe can be started in the same way:

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
