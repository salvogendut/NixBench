# Project status and validation

[Back to the main README](../README.md)

NixBench now has a minimal C11/SDL3 shell, an internal window manager, and its
first reference application. NixInfo displays a system snapshot in one or more
NixBench-managed windows. On NetBSD it reports kernel identity, CPU model and
count, physical memory, uptime, load averages, and root-volume capacity using
libc and native `sysctl` interfaces. A recessed rolling-load panel at the
bottom samples `kern.cp_time` and `vm.uvmexp2` once per second while a NixInfo
system window is open, retaining 120 seconds of cyan CPU and amber memory
history without polling while NixInfo is closed. Its Project, View, and Window
menus are published by the application and shown in the global top bar
alongside a live local-time clock. Clicking the desktop restores the shell's
own menu set.

NixInfo is deliberately a separate, SDL-free application controller, although
it still runs in the shell process during this prototype phase. A small
in-process application host assigns application IDs, tracks window ownership,
deep-copies and validates double-buffered menu snapshots, delivers lifecycle
and command events, and applies deferred application requests. Menu commands
retain the exact focused-window context, and Quit NixInfo closes only NixInfo.
Application-specific drawing uses a clipped content-rendering seam that can
now be supplied by the experimental Wayland shared-memory surface path.

The standalone session appends an **Applications** menu to the currently
focused application's menus, so the launcher remains reachable even when a
client covers the desktop. It contains pinned entries for NixClock and the
pkgsrc Sakura and Midori executables, plus **Edit Application Pins...**. Pin
changes are applied immediately and persist in the invoking user's
`~/.nixbenchrc`. Every launched process inherits the private Wayland display
and is tracked for orderly session shutdown; a missing executable is reported
without ending the desktop.

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
rendering, buffer scale/transform or input-region handling, subsurfaces,
clipboard, accelerated buffers, resize negotiation,
desktop-managed application launching, or a full toolkit bridge for
application menus. Its popup support is deliberately narrow: full pointer
routing into popups, outside-click dismissal policy, and positioner constraint
adjustment are not implemented yet. NixClock exercises the first private
application-menu protocol. Existing GTK/SDL Wayland clients are still only
partially supported; the standalone initial-application selector permits
diagnostic compatibility probes that are expected to expose the remaining
gaps.
NixBench also does not yet offer a supported production direct-console login
session.

The initial chrome uses an original palette and geometry while exploring a
classic beveled Workbench/AROS-inspired vocabulary. AROS was studied as a design
reference, but no AROS source, constants, artwork, or other assets were copied.
This boundary preserves NixBench's BSD 2-Clause licensing.

See [PLAN.md](../PLAN.md) for milestones, deliverables, and exit criteria.

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
