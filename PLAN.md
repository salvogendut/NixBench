# NixBench Initial Plan

This roadmap covers the path from an empty repository, through a NetBSD/Xorg
development prototype, to a standalone desktop that owns the NetBSD console
display and input devices. Milestones are ordered by dependency rather than
assigned calendar dates. A milestone is complete only when its exit criteria
are met.

## Guiding constraints

- Use C11, CMake, and SDL3; use XCB only in the hosted X11 backend.
- Develop and validate on NetBSD first.
- Keep native applications in separate processes.
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

- A CMake project with explicit SDL3 and XCB dependency discovery.
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

## Milestone 3: Build the hosted X11 backend

Turn the development prototype into an X11 window manager while retaining SDL3
for shell rendering and input. This backend is a bootstrap, debugging, and
fallback environment rather than the final architecture.

Deliverables:

- XCB connection setup and acquisition of window-manager ownership.
- Discovery and management of existing and newly created X11 client windows.
- Window placement, focus, raise/lower, move, resize, minimize, restore, and
  close-request behavior.
- Original window decorations and consistent keyboard shortcuts.
- Handling for common ICCCM and EWMH expectations needed by target applications.
- Safe detection of another active window manager and recovery from disappearing
  or unresponsive clients.

Exit criteria:

- A representative set of SDL3 and conventional X11 applications can be
  launched, focused, moved, resized, minimized, restored, and closed.
- Starting NixBench under an existing window manager fails safely with a clear
  diagnostic.
- Client crashes do not terminate or wedge the desktop.

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
- A documented evaluation of using established Wayland client/compositor
  protocols versus a NixBench-specific Unix-domain-socket protocol; select one
  before declaring the application interface public.
- Shared-memory software surfaces first, with accelerated buffer sharing
  deferred until measurements justify it.
- Lifecycle, timeout, validation, and compatibility rules for every introduced
  protocol surface.

Exit criteria:

- The example application installs, launches, and participates in the desktop
  through documented interfaces.
- Malformed messages and terminated applications cannot destabilize the shell.
- Public integration surfaces have protocol tests and versioning rules.

## Milestone 6: Package and validate the hosted prototype

Make the desktop reproducible and practical to evaluate on NetBSD.

Deliverables:

- NetBSD installation and X session startup instructions.
- Packaging metadata appropriate for evaluation and eventual pkgsrc work.
- End-to-end tests for session startup, application management, preference
  persistence, clean logout, and recovery after component failure.
- Performance and resource measurements on representative hardware or virtual
  machines.
- A documented troubleshooting and debug-log collection workflow.

Exit criteria:

- A new user can install, start, exercise, and cleanly leave a NixBench session
  using the documentation.
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
- A compositor that combines shell and independent client surfaces, performs
  damage tracking, and enforces focus and stacking policy.
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
- The same shell behavior tests pass under the X11-hosted and standalone
  backends.

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
