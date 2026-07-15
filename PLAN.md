# NixBench Initial Plan

This roadmap covers the path from an empty repository, through a NetBSD/Xorg
development prototype, to a standalone desktop that owns the NetBSD console
display and input devices. Milestones are ordered by dependency rather than
assigned calendar dates. A milestone is complete only when its exit criteria
are met.

## Guiding constraints

- Use C11, CMake, and SDL3 for the hosted-window prototype.
- Develop and validate on NetBSD first.
- Keep native applications in separate processes.
- Use established Wayland client protocols as the native surface compatibility
  baseline, adding narrowly scoped NixBench protocols only for shell features
  such as global application menus.
- Make direct DRM/KMS or framebuffer operation without X.org the final runtime.
- Keep the shell, desktop model, compositor policy, and application lifecycle
  independent of the display backend.
- Preserve an optional route for unmodified X11 applications without making
  X.org a prerequisite for the standalone desktop.
- Build an original interface informed by Workbench-style ideas without copying
  proprietary artwork, names, or branding.
- Avoid declaring stable APIs until at least one real consumer demonstrates the
  required behavior.

## Milestone 1: Bootstrap the shell

Establish a small buildable program and a repeatable development workflow.

Deliverables:

- A CMake project with explicit SDL3 dependency discovery.
- A documented source layout and build/run instructions for NetBSD.
- A minimal executable that initializes SDL3, opens a desktop window, processes
  input and lifecycle events, and shuts down cleanly.
- Platform interfaces that prevent shell state and rendering policy from
  depending directly on X11 types.
- Basic logging and error reporting suitable for diagnosing startup failures.
- Automated build and smoke checks where the available CI environment permits
  them.

Exit criteria:

- A clean checkout configures and builds using the documented commands.
- The shell opens and exits cleanly on a supported NetBSD/Xorg system.
- Missing dependencies and initialization failures produce useful diagnostics.

## Milestone 2: Establish the desktop shell

Build the core interaction and presentation layer before managing other
applications.

Deliverables:

- An original visual language with a Workbench-inspired desktop, menu bar, and
  pointer interactions.
- A small internal rendering and layout layer for shell-owned interfaces.
- Keyboard and mouse focus rules for desktop objects and menus.
- A minimal configuration mechanism for display and interaction preferences.
- Tests for non-graphical layout, state, and configuration behavior.

Exit criteria:

- Users can navigate the desktop and menus using both mouse and keyboard.
- Shell state remains consistent across resize, redraw, and display changes.
- Invalid or absent configuration falls back to documented defaults.

## Milestone 3: Build the internal window compositor

Build NixBench's own window model and compositor inside the single SDL host
window. Xorg remains responsible only for the outer development window; all
desktop behavior must remain independent of X11.

Deliverables:

- An internal representation for windows, surfaces, stacking order, focus, and
  lifecycle state.
- Composition of multiple shell-owned test surfaces into the SDL output.
- Window placement, focus, raise/lower, move, resize, minimize, restore, and
  close behavior with original decorations.
- Logical desktop coordinates and damage tracking that do not depend on the host
  window's size, pixel density, or video backend.
- Consistent mouse and keyboard routing, including grabs used during menus and
  move/resize operations.
- Tests for geometry, stacking, focus transitions, clipping, and invalid client
  state.

Exit criteria:

- At least two test windows can be focused, stacked, moved, resized, minimized,
  restored, and closed within the NixBench host window.
- Resizing or scaling the outer SDL window preserves internal geometry and
  produces a correct redraw.
- No compositor or shell module includes X11-specific types or headers.

## Milestone 4: Add desktop objects and preferences

Create the minimum useful Workbench-style desktop model.

Deliverables:

- Representations for mounted volumes, directories, files, applications, and
  launchers.
- Icon selection, opening, movement, arrangement, and contextual actions.
- Application launching with explicit argument and working-directory handling.
- Persistent desktop layout and user preferences stored under an appropriate
  per-user configuration location.
- Refresh behavior for filesystem and mount-state changes.

Exit criteria:

- Users can browse and launch items from the desktop without a terminal.
- Layout and preferences survive a clean restart.
- Missing media, broken launchers, and inaccessible paths fail without losing
  desktop state.

## Milestone 5: Define the native application model

Define the integration needed for real NixBench-native applications while
preserving process isolation and remaining usable under both hosted and
standalone display backends.

Deliverables:

- At least one small native application used to drive integration requirements.
- A versioned, minimal application metadata format if desktop discovery needs
  one.
- A prototype local surface and input protocol that lets an application submit
  content to the compositor without owning the physical display.
- An embedded Wayland server with protocol tests, beginning with `wl_shm` and
  stable `xdg-shell`, then adding output, seat/input, popup, subsurface, data
  transfer, and resize behavior as concrete clients require them.
- A documented boundary between standard Wayland protocols and any
  NixBench-specific shell protocol. Global application menus are expected to
  use a small opt-in extension or toolkit bridge rather than a new widget
  toolkit.
- Compatibility trials with existing GTK and SDL Wayland clients before any
  application integration interface is declared public.
- Shared-memory software surfaces first, with accelerated buffer sharing
  deferred until measurements justify it.
- Lifecycle, timeout, validation, and compatibility rules for every introduced
  protocol surface.

Exit criteria:

- The example application installs, launches, and participates in the desktop
  through documented interfaces.
- Malformed messages and terminated applications cannot destabilize the shell.
- Public integration surfaces have protocol tests and versioning rules.

Current checkpoint: NixClock is the first real out-of-process native
application. Under the hosted desktop it maps a release-aware `wl_shm`
analog-clock surface through stable `xdg-shell` and publishes its focused
global menu through the private, versioned
`nixbench-application-menu-v1` extension. The application-named **NixClock**
menu contains **Quit**; **Settings** contains the checkable **Show seconds**
command, which toggles a differently colored seconds hand. Transactional menu
publication, checked and enabled state, command delivery, surface lifetime,
clock geometry, redraw timing, and command-line behavior have dedicated tests.
The earlier `nixbench-wayland-demo` remains an input/protocol probe.

Hosted NixBench remains the physically validated NixClock development path.
The root `wsdisplay` research harness still does not publish a Wayland socket
or launch external applications: its compositor and desktop run in the
privileged worker. A separate opt-in `nixbench-wsdisplay-session` milestone now
implements the audited root supervisor/device-helper and ordinary-user core
split, publishes a private Wayland display, and launches NixClock after the
credential drop. Its device-free integration path and first physical
console-takeover/normal-exit, VT 1 -> 2 -> 1, and supervised SIGTERM recovery
trials are complete. Further failure injection and repeated-session validation
remain pending. Desktop-managed installation, popup/subsurface/data protocols,
toolkit trials, and broader application-menu bridges also remain outstanding.

## Milestone 6: Package and validate the hosted prototype

Make the desktop reproducible and practical to evaluate on NetBSD.

Deliverables:

- NetBSD installation and hosted-window startup instructions.
- Packaging metadata appropriate for evaluation and eventual pkgsrc work.
- End-to-end tests for program startup, application management, preference
  persistence, clean exit, and recovery after component failure.
- Performance and resource measurements on representative hardware or virtual
  machines.
- A documented troubleshooting and debug-log collection workflow.

Exit criteria:

- A new user can install, start, exercise, and cleanly leave NixBench from a
  normal Xorg desktop using the documentation.
- The release checklist passes on the declared NetBSD and Xorg versions.
- Known limitations and compatibility results are published with the prototype.

## Milestone 7: Run standalone on the NetBSD console

Replace X.org as the host with direct control of display and input while keeping
the same shell and native application behavior.

Deliverables:

- A tested NetBSD display path using DRM/KMS where available and a documented
  decision on framebuffer fallback support.
- NetBSD console support in SDL3 or a narrowly scoped platform layer, with
  upstream contributions preferred over a permanent private SDL fork.
- Display discovery, modesetting, page presentation, cursor handling, and
  recovery from device or mode changes.
- Keyboard and pointer input through NetBSD console facilities, with correct
  keymaps and virtual-terminal transition behavior.
- Connection of the existing compositor to the physical output without changing
  its client-surface, focus, stacking, or damage semantics.
- Session startup, device-permission, shutdown, and crash-recovery behavior that
  restores a usable console.
- A standalone build/run mode with no X11 libraries or running X server required.

Exit criteria:

- NixBench starts from a text console with `DISPLAY` unset and presents the
  desktop on supported NetBSD hardware.
- At least two independent native client processes can draw, receive input, be
  moved and resized, and terminate without destabilizing the compositor.
- Console switching, clean logout, and abnormal termination return display and
  input devices to a usable state.
- The same shell behavior tests pass under the hosted-window and standalone
  backends.

Current checkpoint: `nixbench-backend-probe` remains the mandatory
non-destructive capability inventory before any console takeover. Actual
presentation is isolated in the separately built, explicitly acknowledged
`nixbench-wsdisplay-smoke` harness. The probe lists the video drivers compiled
into SDL and, on NetBSD, queries `wsdisplay` framebuffer metadata plus wscons
device accessibility. With optional libdrm support, it opens each primary DRM
node and inventories the live driver,
dumb-buffer capabilities, KMS resources, CRTCs, cached connectors and modes,
and legacy-visible planes. Cached connector queries avoid forcing a display
reprobe, and the command does not modeset, map memory, allocate buffers, flip a
page, consume input, or explicitly request DRM master.

An idle primary-node open can grant DRM master implicitly. The probe detects
and drops that state before the inventory, and aborts the card if either safety
operation fails. Because current NetBSD releases restrict the drop operation
to privileged processes, an unprivileged probe of an otherwise idle live
primary node aborts safely and a complete idle-console inventory requires a
controlled privileged diagnostic session. The direct-KMS candidate
classification requires a
read-write open, a completed master-safety check with any implicit grant
dropped, a live driver version, KMS resources with at least one CRTC and
encoder, dumb-buffer support, and a connected output with at least one cached
mode. This is only a preflight for later takeover and presentation tests. In
particular, static device nodes do not qualify by themselves: the current QEMU
NetBSD guest has four `/dev/dri/card*` nodes whose actual opens fail with
`ENODEV` because no DRM driver is configured for its display device.

The first real-hardware preflight and bounded software-framebuffer presentation
now pass on a NetBSD 11.0_RC6 ThinkPad X220. Its Intel i915 device exposes a
connected 1366x768 LVDS panel, two CRTCs, eight encoders, dumb buffers, and two
legacy-visible planes. Its `intelfb` console reports a supported 1366x768
XRGB8888-compatible framebuffer layout. The supervised harness presented for
2000 ms through `/dev/ttyE0`, then independently restored and verified the
original console state. Direct KMS has not taken over the console yet.

Detailed DRM inventory is controlled by `NIXBENCH_LIBDRM=AUTO|ON|OFF`. `AUTO`
degrades to path-only inspection when libdrm is absent, `ON` fails configuration
when any required header or library is missing, and `OFF` deliberately omits
the feature. Direct discovery includes NetBSD's `/usr/X11R7` base-system layout
and does not depend on a `pkg-config` executable. The probe is the mandatory
preflight for the narrow host-output abstraction and software `wsdisplay`
experiment described below; actual presentation still requires a usable DRM
driver or a console driver that exposes a supported RGB framebuffer.

The SDL-free host contract now describes logical and pixel output geometry,
normalized input, pointer capture, monotonic timing, software frame submission,
asynchronous presentation completion, and suspend/resume transitions. Its
deterministic headless implementation exercises the contract without a display
server and provides the test seam for the current SDL and NetBSD adapters.
The framebuffer conversion layer validates channel masks, stride, and buffer
arithmetic before converting canonical software frames into native-endian RGB
16-, 24-, or 32-bit layouts while preserving device row padding.
The hosted SDL adapter now implements the same host contract with normalized
events, a persistent presentation texture, and explicit completion events. A
separate SDL software canvas produces the canonical CPU frame that will let the
runtime feed hosted SDL and future `wsdisplay` output without changing shell
renderers. The existing desktop main loop now runs through this host contract,
including output changes, focus/input normalization, frame completion, and
console suspend/resume events.

An experimental NetBSD `wsdisplay` adapter implements the first output-only
standalone slice. It validates and maps RGB DUMBFB memory, converts canonical
frames, uses process-controlled VT release/acquire with a self-pipe signal
handoff, and attempts full state restoration on every normal and partial
startup path. The bounded harness can now feed either its diagnostic pattern
or an explicit `--desktop-preview` frame from the real shell renderers. That
preview remains output-only. A separate explicit `--interactive-preview` mode
composes a small wscons research provider with the same output adapter. It owns
the fixed `/dev/wskbd` and `/dev/wsmouse` mux aliases only for the bounded
worker lifetime, draws a software cursor, routes the left button to menus and
window dragging, and discovers Escape, F10, arrows, Return, and keypad Enter
from the active wscons map. Those limited controls drive the existing global
menu keyboard path and let Escape request an early clean exit. Repeated downs
are flagged and `ALL_KEYS_UP` queues deterministic releases for every held
binding. Both preview modes use SDL only for an in-memory software surface;
neither initializes SDL video nor opens X11 or Wayland. An additional explicit
`--runtime-preview`
mode now connects the same host and input provider to the shared desktop
runtime used by the SDL frontend, including the real NixInfo application and
application-owned global menus; Wayland publication remains disabled. Its
unsupported-platform stub is covered by normal tests, and the NetBSD branch
compiles against the official NetBSD 10.1 amd64 headers under strict warnings.
Raw wscons motion retains a flat 100% library/default profile, with opt-in
25..400% fixed-point sensitivity. The harness exposes
`--wscons-pointer-profile flat|adaptive`; adaptive rejects an explicit fixed
sensitivity, and the guided X220 runner selects it. On NetBSD it validates the
native wscons event `timespec`, groups X/Y events with exactly matching seconds
and nanoseconds into one timestamp bucket, applies the preceding bucket's
velocity with a one-quarter EWMA, and ramps gain from 100% through 400
counts/s to 150% at 750, 200% at 1500, and 250% at 2500 counts/s. An invalid
native timestamp falls back to the monotonic userspace-read clock. NetBSD's
`getnanotime(9)` stamping is normally precise to one kernel clock tick, so a
bucket is not asserted to be one physical report. History
resets after 100 ms idle, timestamp regression, a native/fallback source switch,
edge clamp, configuration change, or lifecycle transition. Returning to
identity gain clears both fractional
carries, and an axis sign reversal clears that axis carry, so a previous
accelerated fraction cannot cause a delayed precision correction. Diagnostics
expose the selected profile, adaptive gain buckets, peak capped filtered
velocity/gain, idle/timestamp/edge and carry reset counts, non-edge suppression,
zero-valued relative packets, native/fallback event and motion-group counts,
clock-source resets, raw/logical motion, and input-to-copy measurements. Host
and latency timestamps remain the post-read `CLOCK_MONOTONIC` value; native
realtime is private to acceleration. A realtime step can reset history or
briefly distort acceleration, but cannot alter latency measurements. Hosted
SDL coordinates are never scaled.
Keyboard diagnostics add the current binding count and raw, emitted, repeat,
ignored, `ALL_KEYS_UP`, and synthesized-release events. VT diagnostics add
release/acquire requests and completions, input suspend/resume counts, timing
regressions, and acknowledge/suspended timing distributions.
The first measured runtime averaged 176 ms per input-associated frame. A
canonical 32-bit fast path and optimized hardware build reduced this to 36 ms,
with 2 ms in rendering and 34 ms in full mapped-framebuffer presentation. The
`wsdisplay` host now retains a 32-bit source shadow and converts only each
changed row's first-through-last changed-pixel span. It invalidates on every
map/unmap and falls back to full conversion if the optional shadow cannot be
allocated. Physical X220 validation of this damage-suppressed path averaged
5 ms from userspace input read through framebuffer-copy completion.
This root-only harness remains a supervised hardware-validation mode rather
than a desktop session. Broader hardware validation and complete wscons
keymap/seat/hotplug support are still required. Privilege separation and a
separate recovery supervisor now exist in the distinct opt-in session path
described below, but that path still needs its core crash/hang, malformed-
protocol, worker/supervisor hard-failure, and repeated-session acceptance
trials.
The active-map controls are shell navigation only; general text, modifiers,
client keymap reconciliation, and multi-device seat handling remain in that
broader input gate.
The detailed design and source references are in
[`docs/standalone-backend.md`](docs/standalone-backend.md).

The harness is excluded by default behind
`NIXBENCH_BUILD_WSDISPLAY_SMOKE=ON`. Its query-only `--preflight-only` action
does not alter display state. A run requires both
`--acknowledge-console-takeover` and
`--acknowledge-no-crash-watchdog`, uses a 250..30000 ms bounded lifetime by
default, and writes a root-only recovery record at
`/var/run/nixbench-wsdisplay-smoke.state` before
forking. The diagnostic pattern remains the default; `--desktop-preview`
selects the bounded output-only shell scene, while `--interactive-preview`
adds the fixed wscons muxes explicitly. `--runtime-preview` uses those muxes
with the shared desktop runtime, and the guided hardware script now selects
that mode. Its opt-in `--vt-cycle` form supplies `--require-vt-cycle`; without a
numeric duration it uses the explicit interactive `--until-exit` mode and fails
unless a balanced release/acquire plus input suspend/resume completes with
clean lifecycle timing and a post-acquire frame. The runner captures the
originating one-based VT, chooses a distinct
away VT (overridable with `NIXBENCH_VT_AWAY`), and prints the exact second-SSH
switch-away and return commands.
Bounded runs retain their typed confirmation and derived outer timeout.
Run-until-exit requires `TAKEOVER-UNTIL-EXIT`, keeps the supervisor and recovery
record active, and exits successfully only through the interactive Escape
path. Input descriptors are closed and held state is cancelled before every
acknowledged VT release and
cleanup. The framebuffer worker maps and presents; an unmapped parent enforces
bounded deadlines or waits for explicit exit, uses bounded termination/reap
grace, and independently restores and verifies the saved console state.
`--recover` provides a separate-session
restoration path if the record remains. Automated tests cover support code,
input reduction, shell interaction, and refusal gates using device-free data;
CTest never opens wscons or performs a takeover. The first 2000 ms X220
diagnostic presentation completed with automatic restoration and an
independent SSH watch; production wscons input/session integration, failure
injection, broader hardware coverage, and a production login-session review
remain later gates. The privilege-separated session is a different executable
and does not change the root-only nature of this historical harness.
A subsequent 5000 ms `--desktop-preview` run completed through the same X220
software-framebuffer path. The supervisor verified restoration, and separate
postflight checks found the original console state with no recovery record or
harness process remaining; manual recovery was not needed.
A later bounded `--interactive-preview` trial displayed the software cursor
and allowed the physical pointer to operate the global menus and managed
window. Motion was functional but noticeably slower and less fluid than the
hosted path. Subsequent pipeline profiling isolated the dominant cost to the
full-frame mapped-framebuffer write. Canonical conversion is now fast and the
backend's source-shadow damage suppression has been physically validated at a
5 ms average. A subsequent adaptive-pointer trace distributed 1709 relative
events as 888 at 100% gain, 626 at 101..149%, 186 at 150..199%, 9 at
200..249%, and none at 250%, while input-to-copy latency remained 5 ms on
average. The overall feel was good, but low-speed movement was reported to
flutter rather than track straight. A repeat after the revised identity
threshold and carry clearing looked and felt good: 45 precision carries and
20 direction carries were cleared, no non-edge suppression or raw-zero event
was observed across 1855 relative events, and input-to-copy latency remained
5 ms on average. That carry fix is physically validated. Native timestamp-
bucket grouping is also physically validated: all 1738 relative events used
native timestamps, forming 1035 buckets with 703 same-timestamp events and no
fallback, clock-source reset, or timestamp regression; the user reported that
the result was all good. A readiness-driven blocking wait now covers the
wsdisplay self-pipe and both wscons descriptors, gives lifecycle events
simultaneous-readiness priority, and retains bounded 128-event host and
64-event input phases. Its wait counters are exposed, portable tests pass, and
the X220 trial physically validated the normal input-driven path. All 1232
waits woke for input, with no timeout, interruption, signal-pipe wake,
simultaneous readiness, queued host event, or clock regression. Input-read-to-
copy completion averaged 6 ms with a 24 ms maximum, console restoration
passed, and the user reported that the interaction felt good.
A first guided `--runtime-preview` X220 trial completed on 2026-07-14. The
physical console displayed the shared runtime and real NixInfo application
through `wsdisplay` and wscons without X11, Wayland publication, or SDL video;
the user confirmed it worked, and the guided postflight restored the console.
The no-deadline required VT-cycle path was physically validated on 2026-07-15:
VT release/acquire and input suspend/resume each balanced at 1/1, a
post-acquire frame completed, Escape exited cleanly, and both supervisor and
postflight verification restored screen 0 in emulation mode with automatic VT
handling, video on, and VT 1 active. The remaining active-map menu-navigation
bindings retain device-free coverage but still await a focused physical trial.

The first real external application, NixClock, now exercises both the hosted
application/global-menu path and the device-free integration path of the new
opt-in privilege-separated session. `nixbench-wsdisplay-session` reserves
standard descriptors before privileged opens, holds a root-owned flock across
both the supervisor and live device worker, creates the recovery record
exclusively, and supervises that worker. The worker retains fixed `wsdisplay`,
wscons, VT, presentation, and heartbeat authority. Its anonymous bounded
protocol connects to `nixbench-session-core`, which is executed only after the
trusted child irreversibly changes to the invoking sudo user. The core publishes
a private Wayland socket in a session-owned runtime directory and launches
NixClock. The core and application inherit no console or recovery descriptor.

`tools/run-wsdisplay-session.sh` configures the opt-in targets, builds and tests,
stages only the privileged launcher as a root-owned `/var/run` executable,
performs query-only preflight, requires `START-NIXBENCH`, and then runs without
an automatic deadline under the root recovery supervisor. A second SSH session
and the printed supervisor cancellation/manual `--recover` commands remain
mandatory. The exact opt-in configuration now builds on NetBSD and passes all
46 device-free tests; the staged root launcher links only NetBSD libc, and its
query-only preflight preserved the expected console state. The first physical
session also launched NixClock on the private Wayland display, exited normally,
cleared the recovery record, and restored screen 0, emulation mode, automatic
VT handling, video on, and one-based VT 1. A subsequent privilege-separated
trial completed VT 1 -> 2 -> 1 with release/acquire completions balanced at
1/1; the ordinary-user desktop and NixClock returned after acquisition, normal
exit cleared the recovery record, and independent postflight verified the same
saved console state with VT 1 active. The supervised SIGTERM gate subsequently
passed on the same hardware: after receiving the operator signal, the root
supervisor delivered SIGTERM to the ordinary-user core. The core completed its
in-band orderly shutdown, worker/core cleanup passed, console restoration was
verified, and the recovery record was cleared. Independent postflight again
found screen 0 in emulation mode with automatic VT handling, video on, and
one-based VT 1 active, and the harness reported success. The trial's non-fatal
NixClock Wayland EOF diagnostic prompted a cleanup-order fix that keeps the
private display alive until the tracked application exits. Its next gates are
core crash or hang, malformed protocol, worker or supervisor hard failure, and
repeated-session validation on the X220.
Direct KMS remains a Milestone 7 deliverable but is not on that immediate
critical path.

## Milestone 8: Add standalone X11 compatibility

Allow legacy X11 applications to participate in a standalone NixBench session
without placing an X server underneath the desktop.

Deliverables:

- A prototype and documented selection of the compatibility approach, including
  evaluation of the Wayland protocol plus Xwayland where NetBSD support is
  sufficient.
- An optional compatibility service whose dependencies are absent from a
  native-only installation.
- Translation of legacy windows, input, clipboard, and lifecycle behavior into
  NixBench compositor concepts.
- Clear diagnostics and isolation when the compatibility service is unavailable
  or crashes.

Exit criteria:

- A representative set of unmodified X11 applications can be launched and
  managed inside a standalone NixBench session.
- Stopping or crashing the compatibility service leaves native applications and
  the desktop operational.
- NixBench can still be built and run in standalone native-only mode without
  X11 dependencies.

## Deferred work

The initial prototype will not include:

- First-class support for operating systems other than NetBSD.
- A comprehensive general-purpose widget toolkit.
- Stable binary compatibility guarantees.
- Accelerated cross-process buffer sharing before the shared-memory path is
  correct and measured.
- Broad IPC or desktop-service APIs without concrete consumers.

These areas may be reconsidered after the hosted prototype and standalone
display path demonstrate the core desktop and application model.
