# NixBench Initial Plan

This roadmap covers the path from an empty repository to a usable NetBSD/Xorg
desktop prototype. Milestones are ordered by dependency rather than assigned
calendar dates. A milestone is complete only when its exit criteria are met.

## Guiding constraints

- Use C11, CMake, SDL3, and XCB.
- Develop and validate on NetBSD first.
- Keep native applications in separate processes.
- Support unmodified X11 applications through window management, not embedding.
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

## Milestone 3: Manage X11 applications

Turn NixBench into an X11 window manager while retaining SDL3 for shell
rendering and input.

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

## Milestone 5: Integrate native applications

Define only the integration needed for real NixBench-native applications while
preserving process isolation.

Deliverables:

- At least one small native application used to drive integration requirements.
- A versioned, minimal application metadata format if desktop discovery needs
  one.
- Unix-domain-socket IPC only for demonstrated needs such as desktop services,
  notifications, or application requests.
- Lifecycle, timeout, validation, and compatibility rules for every introduced
  protocol surface.

Exit criteria:

- The example application installs, launches, and participates in the desktop
  through documented interfaces.
- Malformed messages and terminated applications cannot destabilize the shell.
- Public integration surfaces have protocol tests and versioning rules.

## Milestone 6: Package and validate the prototype

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

## Deferred work

The initial prototype will not include:

- A custom display server or compositor.
- Wayland support.
- First-class support for operating systems other than NetBSD.
- A comprehensive general-purpose widget toolkit.
- Stable binary compatibility guarantees.
- Broad IPC or service APIs without concrete consumers.

These areas may be reconsidered after the NetBSD/Xorg prototype demonstrates
the core desktop and application model.
