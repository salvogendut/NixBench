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

Current checkpoint: an independent `wl_shm`/`xdg-shell` demo maps into the
hosted desktop, joins its logical `wl_output`, and receives pointer and XKB
keyboard input through `wl_seat`. It remains an integration probe rather than a
completed native application: desktop-managed launching and installation,
popup/subsurface/data protocols, toolkit trials, and global-menu integration
are still outstanding.

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
startup path. Its unsupported-platform stub is covered by normal tests, and
the NetBSD branch compiles against the official NetBSD 10.1 amd64 headers under
strict warnings. It is intentionally not a selectable desktop runtime yet:
broader hardware validation, wscons input, failure-injection tests, and a
separate privileged watchdog that can recover after a compositor crash are the
next safety gates. The detailed design and source references are in
[`docs/standalone-backend.md`](docs/standalone-backend.md).

The harness is excluded by default behind
`NIXBENCH_BUILD_WSDISPLAY_SMOKE=ON`. Its query-only `--preflight-only` action
does not alter display state. A run requires both
`--acknowledge-console-takeover` and
`--acknowledge-no-crash-watchdog`, is limited to 250..5000 ms, and writes a
root-only recovery record at `/var/run/nixbench-wsdisplay-smoke.state` before
forking. The framebuffer worker maps and presents; an unmapped parent enforces
the deadline, reaps the worker, and independently restores and verifies the
saved console state. `--recover` provides a separate-session restoration path
if the record remains. Automated tests cover support code and refusal gates,
but CTest never performs a takeover. The first 2000 ms X220 presentation run
completed with automatic restoration and an independent SSH watch; wscons
input, failure injection, privilege separation, broader hardware coverage, and
a production crash watchdog remain later gates.

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
