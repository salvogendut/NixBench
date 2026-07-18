# Privilege-separated standalone session

[Back to the main README](../README.md)

## Development launcher

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
that copy. The ordinary-user core and applications remain in the build tree
and are never executed until after the credential drop. The script changes no
display state until the operator types `START-NIXBENCH`. The session has no
automatic deadline: exit from the desktop menu, press Escape while no Wayland
client owns keyboard focus, or use the printed supervisor `SIGTERM` command
from the retained second SSH session.

The desktop starts empty. Use the global **Applications** menu to start
NixClock, Sakura Terminal, or Midori Web Browser instances. NixClock is
resolved from the build tree or installed prefix; the GTK entries currently use
`/usr/pkg/bin/sakura` and `/usr/pkg/bin/midori`. To export GTK application
menus into the bar as well, start the session with
`NIXBENCH_GTK_MENU_BRIDGE=1`.

The shell's **NixBench > Take Screenshot** command shows a centered
`SCREENSHOT IN 5` through `SCREENSHOT IN 1` panel, then captures the next
complete desktop frame after the countdown has disappeared. A short saved or
failed panel confirms the result without appearing in the PNG. Successful
captures are mode-`0600` files named
`~/nixbench-USER-PID-YYYYMMDD-HHMMSS.png`; a numeric suffix prevents an
existing same-second file from being replaced.

The ordinary-user core creates `~/.nixbenchrc` with mode `0600` the first time
`nixbench-session` runs. The shell's **NixBench > Settings...** command opens
the same panel as **Applications > Edit Application Pins...**. Settings are
live and saved atomically. The current panel provides:

- two palette color pickers for the desktop backdrop;
- an optional vertical, horizontal, or diagonal gradient from color 1 to
  color 2;
- persistent pins for NixClock, Sakura, and Midori; and
- minimize- and maximize-gadget visibility, with left or right window-gadget
  placement.

The default layout groups minimize, maximize, and close on the right, in that
order. The close gadget is therefore the rightmost control. Existing
`windows.controls=split` files are accepted as the new right layout; selecting
Left in Settings groups all visible gadgets on the left instead.

The version-2 file also reserves `desktop.wallpaper`, `desktop.theme`, and
`windows.theme` keys for future wallpaper and skinning work. Version-1 files
used `windows.minimize` as an inactive placeholder; that old value is ignored
and the new minimize gadget defaults to visible until the user saves a
version-2 preference. Minimizing a window hides it from the desktop without
changing its geometry and creates a compact, titled button in the global bar.
Clicking that button restores, raises, and focuses the window. Unknown keys
are ignored so newer configurations remain readable by older builds. A
malformed known value is reported and the session uses safe defaults without
giving the file to the privileged helper.

## Installation and local launch

For a package-style NetBSD installation, configure the standard pkgsrc prefix,
build, test, and install as root:

```sh
cmake -S . -B build-installed \
  -DCMAKE_INSTALL_PREFIX=/usr/pkg \
  -DNIXBENCH_WAYLAND=ON \
  -DNIXBENCH_BUILD_APPLICATIONS=ON \
  -DNIXBENCH_BUILD_WSDISPLAY_SESSION=ON
cmake --build build-installed
ctest --test-dir build-installed --output-on-failure
sudo cmake --install build-installed
```

This installs the user-facing `nixbench-session` command in `/usr/pkg/bin`,
the private session helper and core in `/usr/pkg/libexec/nixbench`, and the GTK
bridge in `/usr/pkg/lib/gtk-3.0/modules`. Run the installed session with:

```sh
nixbench-session
```

The installed launcher normally requires SSH so a recovery channel remains
available while it owns the console. To start it directly from a physical
NetBSD console, use the explicit local-launch mode:

```sh
nixbench-session --local
```

The supervisor and restoration protocol remain active, but a separate SSH
login is still strongly recommended because the launching terminal is hidden
during console takeover. The privileged device worker intercepts the physical
Ctrl+Alt+F1 through F12 bindings and requests the corresponding configured,
one-based VT through NetBSD's native `VT_ACTIVATE` lifecycle. The existing
release/acquire state machine closes input, restores emulation mode, and
acknowledges the switch before leaving the NixBench VT. Ctrl+Alt+Backspace is
also intercepted there: it closes raw input, terminates the ordinary-user
session with bounded TERM/KILL escalation, and returns through the same
verified console-restoration path, so it works while a client has focus or the
core is unresponsive.

Plain F1 through F12 keystrokes are not desktop-global shortcuts: NixBench
delivers them to the focused Wayland or Xwayland application. F10 opens the
global menu from the keyboard only while the desktop or an internal NixBench
window owns keyboard focus. This keeps application bindings such as 1984's
F4-through-F12 controls usable without weakening the privileged VT chords.

This follows pkgsrc's standard `${LOCALBASE}` layout; `/usr/pkg` is the
default prefix. The source-tree script remains the build-and-test development
harness.

## Compatibility probes

Set one absolute executable path to open an application at startup for a
compatibility probe. Arguments are not supported yet:

```sh
NIXBENCH_APPLICATION=/usr/pkg/bin/midori ./tools/run-wsdisplay-session.sh
```

Midori's fresh Speed Dial contains no tiles until browsing history exists, so
its page area can look empty even when WebKit is rendering correctly. Load a
fixed, offline HTML page to verify browser content without typing or network
access:

```sh
NIXBENCH_APPLICATION="$PWD/tools/run-midori-content-probe.sh" \
  ./tools/run-wsdisplay-session.sh
```

The NetBSD/pkgsrc-specific wrapper selects WebKitGTK 2.36's software
compositing path and finally executes `/usr/pkg/bin/midori` with a built-in
`data:` URL. The page visibly identifies itself as the NixBench WebKit probe.

If Midori maps its Speed Dial window and later crashes, capture the
physical-session-only fault without enabling core dumps:

```sh
NIXBENCH_APPLICATION="$PWD/tools/run-midori-gdb.sh" \
  ./tools/run-wsdisplay-session.sh
```

That NetBSD/pkgsrc-specific diagnostic is itself the selected ordinary-user
application. It sets WebKitGTK 2.36's
`WEBKIT_DISABLE_COMPOSITING_MODE=1` compatibility switch, then finally
`exec`s `/usr/bin/gdb`. GDB starts `/usr/pkg/bin/midori` and, if a fault
remains, prints the crashing thread, bounded stacks for the other threads, and
loaded shared libraries to the retained SSH session. This single run therefore
tests the software-compositing workaround and captures its failure case. It
does not change the privilege boundary, process group, core-dump limit, or
console recovery path. The A/B switch follows the same workaround recorded for
this legacy renderer in [WebKit bug 238513](https://bugs.webkit.org/show_bug.cgi?id=238513).

The guided script checks that path as the ordinary user before invoking
`sudo`. Privileged code only validates and forwards bounded path text; the
credential-dropped core performs the actual `exec` without a shell. This is
not an application sandbox: the selected program retains the invoking user's
home-directory and group access. Use Midori only with blank or otherwise
trusted content during this first probe. A main window may expose the next
compatibility boundary rather than be fully usable: outside-click dismissal
and positioner constraint adjustment, pointer-axis scrolling, subsurfaces,
drag-and-drop, binary clipboard MIME types, and accelerated buffers remain
incomplete.

Set `NIXBENCH_TRACE_WAYLAND=1` when invoking `./tools/run-wsdisplay-session.sh`
if you need a client-side protocol trace for the first failing interaction.
Set `NIXBENCH_GTK_MENU_BRIDGE=1` when running the GTK probes if you want them
to load the optional NixBench GTK menu bridge. The probe scripts look for the
module in the local build tree at `build/gtk-modules/` and add it to
`GTK3_MODULES`. A requested bridge that was not built is reported as an error
instead of silently starting the application without menu integration. Normal
menu discovery and publication are silent; set
`NIXBENCH_GTK_MENU_BRIDGE_DEBUG=1` on an individual GTK process when bridge
diagnostics are needed. Actual bridge failures remain visible without debug
mode.
When rerunning tests after one of those probes, `./tools/run-clean-env.sh`
clears the `NIXBENCH_*` launch variables first.

## Failure-injection gates

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

The next two recovery gates inject a fault only after the supervisor has
validated the ordinary-user core:

```sh
NIXBENCH_EXPECT_CORE_FAILURE=crash ./tools/run-wsdisplay-session.sh
NIXBENCH_EXPECT_CORE_FAILURE=hang ./tools/run-wsdisplay-session.sh
```

Wait for the desktop and the launcher's `Required core ... trigger armed`
message, then run its exact printed `sudo -n /bin/kill -USR1 ...` command from
the second SSH session. Signal only the supervisor. The crash gate asks the
device worker to deliver `SIGKILL` to the validated core. The hang gate asks it
to deliver `SIGSTOP`, then requires the bounded heartbeat watchdog to detect
the stopped core; do not send `SIGCONT` yourself. Either gate succeeds only for
the requested failure, with no unrelated supervision error, after the complete
core/application process group and runtime sentinel have been reaped, console
restoration has been verified, and the recovery record has been removed.

## Security and recovery

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
the framebuffer, and converts canonical frames. Two sibling children receive
separate private anonymous socket endpoints after irreversible
`setgid()`/`setuid()` transitions to the sudo account. The runtime sentinel
creates and retains descriptor-based ownership of the private runtime
directory. The core publishes its Wayland socket there and starts empty, or
launches the selected initial application, with the matching
`XDG_RUNTIME_DIR` and `WAYLAND_DISPLAY`. It explicitly fixes
`EGL_PLATFORM=wayland` so NetBSD libEGL does not select its X11 default in the
standalone session. When rootless Xwayland is enabled, the root supervisor
also provisions the standard root-owned `/tmp/.X11-unix` socket directory with
mode `01777` before takeover; unsafe pre-existing paths are rejected rather
than repaired. The rootless X window manager publishes and handles the EWMH
fullscreen state used by SDL and other X11 toolkits. An active fullscreen
client receives the complete viewport without NixBench decorations or menu
bar, and leaving fullscreen restores its prior normal or maximized frame. The
standalone core paces frame callbacks and presentation to the output refresh
interval; wsdisplay uses a 17 ms fallback because dumb
framebuffer output does not report a refresh rate. Client redraws arriving
inside one interval are coalesced, clipped to their affected NixBench windows,
and transported as validated packed damage rectangles. The privileged helper
retains the last complete frame and copies only those changed pixels to the
framebuffer. Once the core and application process group are gone, the
worker asks the ordinary-user sentinel to remove the bounded, user-owned
runtime subtree and its directory. The descriptor-relative traversal never
follows symbolic links, never crosses to another filesystem, and rejects
unexpected ownership or excessive nesting. This accommodates application
runtime paths such as GTK's `dbus-1/services` and `at-spi` directories without
weakening the privilege boundary. Privileged code never performs a filesystem
operation on the reported user-owned path. NixClock participates in the normal
global-menu path. No selected application receives the helper protocol
descriptor or any console capability.

Keep a second SSH session open throughout the first hardware trials. If the
launcher leaves the recovery record, first verify that no
`nixbench-wsdisplay-session` helper remains, then run:

```sh
sudo /var/run/nixbench-wsdisplay-session --recover
```

`sudo /var/run/nixbench-wsdisplay-session --preflight` is query-only. The new
targets build natively on the NetBSD test host and all 48 device-free tests
pass there, including the ordinary-user core integration and failed-client
launch path. `ldd` reports only NetBSD libc for the staged root launcher; SDL3,
Wayland, and their client-side dependencies are confined to the ordinary-user
side. A staged-launcher preflight also confirmed screen 0 in emulation mode,
automatic VT handling, and video on without changing display state.

## Physical validation history

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
private display alive until the tracked application has exited.

The physical core-crash gate subsequently passed. After the validated core was
ready, the exact armed supervisor `SIGUSR1` command caused the worker to inject
`SIGKILL`; the required crash was observed, the core/application group and
runtime sentinel were cleaned up, and the reported runtime directory was
independently confirmed absent. Restoration and postflight found screen 0 in
emulation mode, automatic VT handling, video on, and one-based VT 1; the
recovery record was absent and no NixBench process survived. NixClock's Wayland
read error is expected when its compositor is deliberately killed.

The physical core-hang gate now passes as well. The armed trigger stopped the
validated core with `SIGSTOP`; the heartbeat watchdog detected the resulting
stall, contained the resumed core/application group, verified sentinel cleanup,
restored the console, and removed the recovery record. Independent checks again
found the reported runtime path and recovery record absent, no surviving
NixBench process, and one-based VT 1 with the complete saved console state. The
`Could not request orderly session shutdown` diagnostic is expected after the
watchdog has invalidated the hung core's in-band helper session; verified forced
containment is the required path. Malformed protocol, worker or supervisor hard
failure, and repeated sessions remain later gates. The guided command therefore
remains an opt-in development test rather than a login-session installation
procedure.

## GTK3 compatibility history

The first physical GTK3 compatibility probe selected the installed Midori 9.0
binary. The ordinary-user process connected to the private Wayland display,
but no window mapped. GTK reported a null `GdkSeat` because GTK 3.24 waits for
both `wl_seat` and `wl_data_device_manager` before constructing a seat.
NixBench now advertises a minimal version-1 data-device discovery skeleton and
sends the required empty selection event before keyboard focus; clipboard and
drag-and-drop transfer remain unimplemented.

The repeat probe cleared that GTK failure but exposed a client `SIGSEGV` before
the first window. A device-free replay kept the installed `gtk3-demo` alive,
then GDB located Midori's crash below WebKitGTK in NetBSD libEGL's default X11
DRI2 initialization path. With no X server, that path reached
`xcb_connection_has_error()` without a valid connection. Explicitly selecting
`EGL_PLATFORM=wayland` avoids that initial crash. A client protocol trace then
showed Midori configuring an `xdg_toplevel`, attaching shared-memory buffers,
gaining keyboard focus, and submitting subsequent frames. The standalone core
now sets that fixed platform for every client after the credential drop.

The next physical run confirmed that the real Speed Dial window is visible,
but the Midori UI process later encountered a second `SIGSEGV`. An equivalent
device-free run remained alive for 20 seconds despite the same EGL, Cairo,
accessibility, and D-Bus warnings. The ordinary-user GDB launcher then remained
stable on the physical console and Midori exited normally through its close
gadget, proving that the software-compositing run avoids the second fault.

An SDL offscreen capture with a fixed `data:` page subsequently showed the
complete Midori chrome and large rendered HTML text. The apparently blank
Speed Dial was therefore its valid empty-history state, not missing NixBench
surface pixels. The dedicated content-probe wrapper above makes that result
repeatable on the physical console. Standalone clients also receive
`XDG_CURRENT_DESKTOP=NixBench`, `XDG_SESSION_DESKTOP=NixBench`,
`XDG_SESSION_TYPE=wayland`, `GDK_BACKEND=wayland`, and `LANG=C.UTF-8` alongside
the private display and EGL selection. A supervised per-session D-Bus remains
future work; the current accessibility and GApplication bus warnings are
non-fatal.

The first physical address-bar input trial then proved that Control+L reaches
GTK and focuses Midori's URL entry. Typing the first character (`d`) caused
GTK 3.24's `GtkEntryCompletion` to request and map its one-character
completion dropdown as an `xdg_popup`. NixBench's former fatal "popup not
implemented" protocol response disconnected the client, after which Midori
terminated with `SIGSEGV`; this was a compositor protocol gap, not a failed
letter mapping. NixBench now implements the basic positioner/popup lifecycle,
accepts GTK's valid pre-map popup grab, CPU-composites the popup, and safely
dismisses descendants when their parent disappears. The NetBSD device-free
suite passes all 50 tests with this slice. A physical Control+L, type/edit,
and Return retry remains the acceptance gate.
