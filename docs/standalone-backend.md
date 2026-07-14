# NetBSD standalone backend architecture

This note describes the staged route from the hosted NixBench prototype to a
desktop that owns a NetBSD console without X.org. The shell, window model, and
embedded Wayland server remain unprivileged. Display and session mechanisms
are replaceable platform adapters, not part of application or shell policy.

> **Current prototype limits:** the supported development path is still the SDL
> hosted window. The `wsdisplay` adapter is an experimental, output-only
> bring-up path: it has no wscons input provider, no production privileged
> helper/watchdog, no acceleration, and no broad hardware coverage. It must not
> yet be treated as a production login session or crash-safe console owner.

## Process and backend boundaries

```text
native/Wayland clients
          |
unprivileged NixBench core (shell, policy, composition)
          |
nb_software_canvas_finish() -> struct nb_host_frame
          |
          +-- SDL host (nested X11/Wayland development window)
          +-- wsdisplay host (first standalone software bring-up)
          `-- DRM/KMS host (later modesetting and page flips)

privileged session helper/watchdog
          `-- only device acquisition, VT control, and emergency restore
```

The core must not run as root. A future trusted launcher starts a small helper
which opens only the selected console/display devices, records their original
state, and then starts or hands narrowly scoped descriptors to the
unprivileged core. Its local protocol must expose fixed operations such as
acquire, release, query, and restore; it must not accept arbitrary paths,
ioctls, or memory ranges from the core. The helper retains enough authority to
restore the console if the core exits, crashes, or stops responding. This
follows the useful separation in [Arcan's privileged process][arcan-suid], but
NixBench does not depend on Arcan or adopt its protocol.

Applications never receive console or DRM descriptors. They remain separate
processes which submit surfaces through the compositor protocol.

## Stable frame seam

Physical output consumes the backend-neutral contract in `src/host.h`:

- `struct nb_host_output` separates logical desktop size from pixel size.
- `struct nb_host_frame` carries a complete native-endian CPU frame in
  `NB_HOST_PIXEL_FORMAT_XRGB8888` or
  `NB_HOST_PIXEL_FORMAT_ARGB8888_PREMULTIPLIED`.
- `nb_host_present()` accepts strictly increasing, nonzero serials and reports
  completion through `NB_HOST_EVENT_FRAME_COMPLETE`.
- console transitions use `NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED`,
  `NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED`, and the corresponding
  `nb_host_complete_console_*()` calls.

SDL currently draws the shell into the canonical CPU buffer exposed by
`nb_software_canvas_finish()`. `nb_host_sdl_create()` uploads that same frame to
a hosted SDL window. The deterministic headless host and framebuffer-format
tests exercise this seam without a display server. Standalone backends must not
reach back into shell rendering, focus, stacking, or Wayland surface policy.

## Capability-probe boundary

`nixbench-backend-probe` is a preflight diagnostic, not a display takeover
test. It always reports SDL video drivers and NetBSD device-path metadata. When
configured with optional libdrm support, it also attempts a read-write open of
each DRM primary node, falls back to read-only for diagnosis, and inventories:

- the live DRM driver name and version;
- dumb-buffer, preferred-depth, shadow-preference, and asynchronous-page-flip
  capabilities;
- KMS resource counts and size limits, CRTC query and active-state results;
- connector state and the modes already cached by the kernel; and
- planes visible through the legacy client capability set.

Connector inspection deliberately uses the cached-current query rather than
requesting a fresh connector probe. The diagnostic does not enable atomic or
universal-plane client capabilities, so its plane count is intentionally only
the legacy-visible set. An asynchronous-page-flip capability result is not
treated as evidence that ordinary page presentation works.

Opening an idle DRM primary node can grant DRM master as a side effect even
though the process never explicitly requests it. The probe checks immediately
after opening, drops an implicitly granted master before collecting the
inventory, and abandons that card if it cannot establish the master state or
drop it. It never explicitly requests master, changes a mode, creates or maps a
buffer, changes connector or plane state, or submits a page flip.
Current NetBSD releases restrict the drop-master operation to privileged
processes. As a result, an unprivileged probe of an otherwise idle, working
primary node aborts safely before inventory collection; a complete idle-console
inventory currently requires a controlled privileged diagnostic session.

The direct-KMS candidate result is deliberately conservative. It requires a
read-write open, a completed master-safety check with any implicit grant
dropped, a live driver version, readable KMS resources with at least one CRTC
and encoder, dumb-buffer support, and a connected connector with at least one
cached mode. It does not require an active CRTC or legacy-visible plane, and it
does not prove that a future master acquisition, modeset, buffer creation, or
page flip will succeed. Likewise, a `/dev/dri/card*` entry and permissive access
bits do not establish that a DRM driver is attached. The current QEMU NetBSD
guest contains four such static nodes, but opens return `ENODEV` because its
display device has no configured DRM driver.

The first real-hardware preflight passes on a NetBSD 11.0_RC6 ThinkPad X220.
The privileged query-only run sees a 1366x768 at 60 Hz internal LVDS panel, a
live i915 driver, two CRTCs, eight encoders, dumb-buffer support, and two
legacy-visible planes. The same machine's `intelfb` console reports a
supported 1366x768, 32-bit RGB framebuffer with a 5504-byte stride. This
establishes both DRM/KMS and software `wsdisplay` as candidates. The bounded
software path has since mapped and presented successfully for 2000 ms, then
restored and verified the saved console state. DRM/KMS takeover, page flips,
input, and VT-switch testing remain unvalidated.

The CMake setting `NIXBENCH_LIBDRM=AUTO|ON|OFF` controls this detailed layer.
`AUTO`, the default, enables it when all headers and the library are found and
otherwise retains path-only inspection. `ON` makes a missing libdrm component a
configuration error, while `OFF` suppresses libdrm discovery and calls. In
addition to normal CMake locations, discovery handles NetBSD's base-system
headers and library under `/usr/X11R7` directly; `pkg-config` is not required.

## Staged implementation

### 1. Software `wsdisplay` bring-up

Run `nixbench-backend-probe` first; its query-only boundary is described above.
The experimental `nb_host_wsdisplay_create()` then:

1. requires NetBSD, one active console screen, an initial emulation mode, and a
   true-colour RGB framebuffer it can validate;
2. saves display mode, video state, and VT state before making any change;
3. obtains `WSDISPLAYIO_GET_FBINFO`, switches to
   `WSDISPLAYIO_MODE_DUMBFB`, validates the offset, size, stride, and channel
   masks, and maps from device offset zero through only the required,
   page-rounded `fbi_fboffset + framebuffer bytes` range;
4. converts the canonical frame into the reported native 16-, 24-, or 32-bit
   layout while preserving row padding; and
5. restores the saved state on every normal and partial-initialization exit.

This path deliberately provides no modesetting, page flipping, hardware
cursor, or acceleration. It is a correctness and recovery checkpoint, and a
possible fallback for machines which expose a suitable framebuffer. The
NetBSD [`wsdisplay(4)` manual][wsdisplay] is the authoritative ioctl contract;
[Hands-on graphics without X11][graphics-article] is a useful worked example,
not an API specification.

### 2. Bounded `wsdisplay` smoke harness

`nixbench-wsdisplay-smoke` is a privileged hardware-validation tool, not a
desktop session. Its executable is excluded from normal builds; configure with
`-DNIXBENCH_BUILD_WSDISPLAY_SMOKE=ON` to build it. Run
`sudo ./build/nixbench-wsdisplay-smoke --preflight-only` first. Preflight uses
the `ttyEstat` status node to select and snapshot the active console but does
not change display state. NetBSD's native active-screen index is zero-based,
while its USL-compatible `VT_*` ioctl numbers are one-based; the harness
translates and checks that boundary explicitly.

A presentation run requires both `--acknowledge-console-takeover` and
`--acknowledge-no-crash-watchdog`. `--duration-ms` accepts only 250..30000 ms
and defaults to 3000 ms. Before forking, the root parent persists the original
display mode, video state, VT mode, and active screen in the root-only
`/var/run/nixbench-wsdisplay-smoke.state`. The child alone creates the
`wsdisplay` host, maps the framebuffer, and presents the selected image. The
diagnostic pattern remains the default. An explicit `--desktop-preview` mode
uses the real NixBench shell and chrome renderers to draw a global menu bar,
clock, desktop, and managed window into an SDL software surface. It does not
initialize SDL video, create an SDL window, start X11 or Wayland, or read input;
the same output-only `wsdisplay` host submits the resulting XRGB frame. The
unmapped parent applies a hard deadline, terminates and reaps an unresponsive
child, then independently restores and verifies every saved console property.
Job-control stop signals also request supervised shutdown rather than pausing
the parent indefinitely. The state file is removed only after restoration is
verified.

`tools/run-wsdisplay-smoke.sh` configures, builds, tests, performs preflight,
explicitly selects the desktop preview, and verifies postflight state. It still
requires an SSH session, passwordless recovery access, a typed `TAKEOVER`, both
harness acknowledgements, and the outer timeout. That outer deadline is the
requested duration rounded up to seconds plus a ten-second restoration margin.

If the supervisor itself fails, a second SSH session can run
`sudo ./build/nixbench-wsdisplay-smoke --recover` against the persisted record.
This manual fallback is why the command explicitly acknowledges the absence of
a production crash watchdog. The harness still runs both parent and worker as
root and must not be confused with the future least-privilege session helper.

CTest registers only support-unit and help checks for the executable. It never
supplies the acknowledgements and never performs a console takeover. The first
2000 ms ThinkPad X220 diagnostic run completed normally: the supervisor
verified the saved emulation, video, VT, and active-screen state; an independent
SSH watcher saw the recovery record clear with no manual recovery needed; and
the post-run probe matched the baseline. A later 5000 ms
`--desktop-preview` run completed through the same software-framebuffer path.
The supervisor again verified restoration; the guided postflight and a
separate SSH preflight found the original console state, no recovery record,
and no harness process. Manual recovery was not needed.

### 3. Production supervised standalone sessions

Move device ownership and final restoration from the compositor process into
the helper/watchdog. Use a private, inherited local channel with peer and
message validation. The helper monitors both child exit and bounded
heartbeats, performs an idempotent restore, and drops every privilege not
needed for console/session recovery. Closing the core or killing it with a
fatal signal must return the display to text mode without relying on core
cleanup handlers.

### 4. DRM/KMS, then GBM/EGL

The preflight inventory now discovers the queryable live driver, connectors,
CRTCs, legacy-visible planes, and cached modes without taking over the display.
The next step is a separate host which selects among that inventory, acquires
the session's DRM authority, performs modesets, and completes frames from
page-flip events. Start with CPU composition into dumb or GBM-backed buffers so
the existing frame contract survives unchanged. Add EGL/GPU composition only
after modesetting and recovery are reliable; keep a software path for diagnosis
and unsupported hardware.

Availability must be detected, not assumed. SDL currently documents KMSDRM on
NetBSD as unsupported, while its wscons support is input-only
([SDL KMSBSD status][sdl-kmsbsd]). NixBench should therefore use the NetBSD
libdrm/GBM/EGL interfaces directly or contribute a verified fix upstream,
rather than silently depending on SDL's KMSDRM driver.

### 5. Input as a separate seam

The current `nb_host_event` API normalizes SDL pointer and XKB physical-key
events. For standalone operation, wscons acquisition and translation should be
a separate input provider which feeds the same normalized event stream; the
display adapter must not open `/dev/wskbd*` or `/dev/wsmouse*`. This separation
allows independent keymap, hotplug, repeat, pointer, and seat testing and later
lets the aggregate host facade compose display, session, and input providers.
Consult [`wskbd(4)`][wskbd], [`wsmouse(4)`][wsmouse], and the versioned event
definitions in [`wsconsio.h`][wsconsio] before stabilizing that API.

## VT lifecycle and crash recovery

On kernels built with `WSDISPLAY_COMPAT_USL`, the session owner uses
`VT_PROCESS` release/acquire signalling. Signal handlers do only async-signal-
safe notification (a `sig_atomic_t` flag and nonblocking self-pipe write); all
ioctls, allocation, logging, and state changes happen in the event loop.
The experimental adapter also turns `SIGINT`, `SIGTERM`, `SIGHUP`, and
`SIGQUIT` into a normal host quit event so the event loop can restore the
console. This depends on the caller continuing to poll and is not a substitute
for the future external watchdog. Its process-global signal handling also makes
this first adapter single-threaded-only from creation through destruction.

On release request, NixBench stops accepting frames, cancels focus, held input,
and pointer capture, then acknowledges through
`nb_host_complete_console_release()`. On acquire, it acknowledges through
`nb_host_complete_console_acquire()`, re-queries output state, remaps or
recreates invalid resources, and performs a full redraw before resuming.
Lifecycle requests must not be lost behind a full ordinary event queue.

The watchdog records enough original state to restore emulation mode, video
state, and automatic VT handling. Restoration is ordered, best-effort, and
idempotent so it is safe after partial setup. The implementation should use the
NetBSD [`wsdisplay` USL compatibility source][wsdisplay-usl] as the reference
for `VT_SETMODE`/`VT_RELDISP` semantics rather than assuming Linux behaviour.

## Safety and testing gates

No stage becomes the default standalone path until its preceding gate passes:

- unit tests cover host state transitions, serial/completion rules, queue
  saturation, stale-input purging, pixel masks, strides, offsets, and integer
  overflow;
- an ioctl/mmap test double injects failure after every acquisition step and
  verifies exactly-once cleanup and restoration;
- hosted SDL and headless tests render the same shell behavior through the
  common frame contract;
- NetBSD integration tests cover unsupported/text-only displays, VT switching,
  resize or mode change, clean exit, `SIGTERM`, core crash, compositor hang,
  helper failure, and repeated start/stop cycles;
- hardware trials include a VM and more than one physical DRM driver, with a
  serial console or SSH recovery path available during development; and
- a privilege/protocol audit confirms that the core and all clients are
  unprivileged and the helper cannot be used as a general device-opening or
  ioctl service.

The production gate is simple: no tested exit, crash, hang, VT switch, or
partial-startup case may leave the console unusable. DRM/KMS is preferred for
the final desktop, but the backend seam and recovery rules apply equally to
`wsdisplay`.

## References

- [NetBSD 10.1 `wsdisplay(4)`][wsdisplay]
- [NetBSD 10 `wsconsio.h` definitions][wsconsio]
- [NetBSD 10 USL VT compatibility implementation][wsdisplay-usl]
- [SDL KMSDRM/wscons status on BSD][sdl-kmsbsd]
- [Arcan repository: privileged-process split and watchdog][arcan-suid]
- [Hands-on graphics without X11][graphics-article]
- [DRM/KMS documentation][drm-kms], [Mesa's GBM API][gbm], and the
  [Khronos EGL registry][egl]

[arcan-suid]: https://codeberg.org/letoram/arcan/src/branch/master/README.md#suid-notes
[drm-kms]: https://docs.kernel.org/gpu/drm-kms.html
[egl]: https://registry.khronos.org/EGL/
[gbm]: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gbm/main/gbm.h
[graphics-article]: https://blogsystem5.substack.com/p/netbsd-graphics-wo-x11
[sdl-kmsbsd]: https://wiki.libsdl.org/SDL3/README-kmsbsd
[wsconsio]: https://github.com/NetBSD/src/blob/netbsd-10/sys/dev/wscons/wsconsio.h
[wsdisplay]: https://man.netbsd.org/NetBSD-10.1/wsdisplay.4
[wsdisplay-usl]: https://github.com/NetBSD/src/blob/netbsd-10/sys/dev/wscons/wsdisplay_compat_usl.c
[wskbd]: https://man.netbsd.org/NetBSD-10.1/wskbd.4
[wsmouse]: https://man.netbsd.org/NetBSD-10.1/wsmouse.4
