# NetBSD standalone backend architecture

This note describes the staged route from the hosted NixBench prototype to a
desktop that owns a NetBSD console without X.org. The shell, window model, and
embedded Wayland server remain unprivileged. Display and session mechanisms
are replaceable platform adapters, not part of application or shell policy.

> **Current prototype limits:** the supported development path is still the SDL
> hosted window. The older `nixbench-wsdisplay-smoke` command remains an
> all-root hardware-research harness. A separate, opt-in
> `nixbench-wsdisplay-session` milestone now splits root console ownership from
> an ordinary-user desktop and private Wayland server, with NixClock as the
> default client or one operator-selected initial application. Its
> device-free integration tests, physical takeover/normal-exit trial, and
> VT 1 -> 2 -> 1 cycle and supervised SIGTERM recovery gate pass, but it has
> now also passed both physical core-failure gates. The remaining failure-
> injection matrix is pending, as are complete seat/input support, acceleration,
> and broad hardware coverage. It is not a production login session or a
> default crash-safe console owner.

## Process and backend boundaries

```text
hosted development:
  native/Wayland clients -> ordinary-user core -> canonical frame -> SDL host

opt-in standalone milestone:
  root recovery supervisor
    `-- root device worker -> wsdisplay, wscons, and VT ownership
          |-- private fixed protocol --> ordinary-user core <- Wayland clients
          `-- READY/CLEAN protocol ----> ordinary-user runtime sentinel

later output adapter:
  root device worker -> DRM/KMS modesetting and page flips
```

The core must not run as root. In the opt-in implementation, the trusted root
launcher records the original console state and retains a recovery supervisor.
A root device worker opens only fixed console and wscons paths, performs VT and
presentation operations, and exchanges fixed, bounded messages with the core.
It does not accept arbitrary paths, ioctls, memory ranges, executable paths, or
environment assignments from the core. The child performs and verifies an
irreversible drop to the invoking account before executing
`nixbench-session-core`; neither it nor its Wayland clients receive console,
recovery, or helper descriptors. The supervisor retains enough authority to
restore the console if the core or device worker exits, crashes, or stops
responding. This follows the useful separation in
[Arcan's privileged process][arcan-suid], but NixBench does not depend on Arcan
or adopt its protocol.

The audited authority inventory, protocol restrictions, credential-drop rules,
and hardware acceptance gates are recorded in
[`privilege-boundary.md`](privilege-boundary.md).

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
and modesetting remain unvalidated. Physical pointer/runtime input, the Escape
exit binding, and a complete VT release/reacquire cycle are validated; the
remaining active-map menu-navigation bindings await a focused guided trial.

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

### 2. Supervised `wsdisplay` smoke harness

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
and defaults to 3000 ms. The mutually exclusive `--until-exit` mode is accepted
only for interactive/runtime content, retains Escape as an orderly exit, and
removes the presentation deadline without removing supervision. Before
forking, the root parent persists the original
display mode, video state, VT mode, and active screen in the root-only
`/var/run/nixbench-wsdisplay-smoke.state`. The child alone creates the
`wsdisplay` host, maps the framebuffer, and presents the selected image. The
diagnostic pattern remains the default. An explicit `--desktop-preview` mode
uses the real NixBench shell and chrome renderers to draw a global menu bar,
clock, desktop, and managed window into an SDL software surface. It does not
initialize SDL video, create an SDL window, start X11 or Wayland, or read input;
the same output-only `wsdisplay` host submits the resulting XRGB frame.

`--interactive-preview` is a separate, explicit research mode. It renders the
same shell scene plus a software cursor and temporarily owns NetBSD's fixed
`/dev/wskbd` and `/dev/wsmouse` mux aliases. Pointer motion and the left button
exercise menus and managed-window dragging. On every keyboard open or reopen,
the provider snapshots the mux inventory, keyboard type, and active wscons
map. A stable mux containing exactly one direct PC-XT device whose exact
`KB_US` encoding matches required sentinels receives a bounded physical XKB profile
covering ordinary text, modifiers, navigation, function keys, and the keypad.
The inventory is checked again after the keyboard queries. USB, mixed,
multiple, nested, variant, changed, and unknown configurations retain the
discovered Escape, F10, arrow, Return, and keypad-Enter fallback. Those
controls use the normalized XKB names already consumed
by the global menu path; Escape requests an orderly early exit when no menu is
open. Repeated downs are flagged, and `ALL_KEYS_UP` queues releases for every
held mapped key. The provider closes both device descriptors and clears held
input before every acknowledged VT release and on all cleanup paths. It does
not turn SDL into the display or input backend: rendering remains an in-memory SDL software
surface, `wsdisplay` remains the sole display owner, and no X11, Wayland, or SDL
video subsystem is started. The display adapter itself stays output-only.

`--runtime-preview` is the next explicit research mode. It connects that same
software framebuffer host and wscons provider to the shared desktop runtime
used by the hosted SDL frontend. The scene therefore contains the real NixInfo
application and its application-owned global menus rather than the lightweight
preview model. Wayland service publication remains disabled for this step, so
the mode does not require X11, Wayland, or SDL video.

Pointer gain remains a raw-wscons concern rather than a desktop-runtime or SDL
policy. The library and harness default to the flat identity profile.
`--wscons-pointer-profile flat|adaptive` selects the raw-input profile. Flat
mode retains `--wscons-pointer-sensitivity-percent` for explicit 25..400%
gain, with signed per-axis fixed-point carry to keep fractional scaling
symmetric and drift-free. The harness rejects adaptive mode combined with an
explicit sensitivity option. The guided X220 trial selects adaptive mode.
Neither choice changes hosted SDL input.

On NetBSD, the adaptive reducer validates the full realtime `timespec`
attached to each native wscons event. X and Y motion with matching seconds and
nanoseconds forms one timestamp bucket. `wsevent_inject()` stamps events with
`getnanotime(9)`, which is normally precise to one kernel clock tick despite
the nanosecond-shaped field; a bucket therefore does not assert a device packet
or physical-report boundary. Each bucket is scaled from the preceding completed
bucket's raw velocity, filtered with a one-quarter EWMA, so its motion receives
one gain. An invalid native timestamp falls back to the monotonic userspace-read
clock. Adaptive history is discarded after at least 100 ms idle, timestamp
regression, a native/fallback clock-source switch, pointer-edge
clamp, profile or sensitivity configuration change, and input open/close or
another lifecycle transition. A resumed stream therefore begins at identity
gain instead of inheriting stale timing. The piecewise-linear curve is 100% at
and below 400 counts/s, reaches 150% at 750 counts/s, 200% at 1500 counts/s,
and 250% at 2500 counts/s, where it saturates.

Adaptive scaling also treats fractional carry as motion-history state. When
filtered velocity returns to identity gain, both axis carries are cleared;
when one axis reverses sign, that axis carry is cleared before scaling the new
delta. These transitions prevent an accelerated residual from producing a
delayed correction during slow or reversed movement. Physical X220 validation
of the revised identity threshold and carry rules looked and felt good.

Native realtime is used only by the acceleration estimator. The normalized
host event's millisecond timestamp remains the `CLOCK_MONOTONIC` value captured
after its userspace `read`, as do the input/frame latency associations. Native
realtime values therefore never leak into host dispatch or mix with framebuffer
completion timing. A realtime clock step safely resets history or briefly
distorts only acceleration, never the latency measurements. Native timestamp-
bucket grouping is physically validated on the X220: all 1738 relative events
used native timestamps, forming 1035 buckets with 703 same-timestamp events and
no fallback, clock-source reset, or timestamp regression.

`--wscons-input-stats` identifies the active profile and prints raw/logical
distance and event-shape counters, adaptive gain buckets, peak filtered
velocity (capped at 2500 counts/s) and gain, idle/timestamp/edge reset counts,
precision and direction carry resets, non-edge suppressed events, zero-valued
relative packets, native/fallback timestamp events, motion groups,
same-timestamp events, clock-source resets, and userspace-read-to-framebuffer-
copy-complete timing. Host events are stamped at read time with `CLOCK_MONOTONIC`,
matching the wsdisplay completion clock. The metric excludes time already
spent in the device/kernel queue and does not measure scanout or glass latency.
An input-frame pipeline breakdown separates the wait to render, SDL software
rendering, the synchronous present call and copy-complete timestamp, and
completion-event delivery. Keyboard diagnostics add the current discovered
binding count and raw, emitted, repeat, ignored, `ALL_KEYS_UP`, and synthesized-
release events. VT diagnostics add release/acquire request and completion
counts, input suspend/resume counts, timing regressions, and min/average/max
release-acknowledge, suspended-away, and acquire-acknowledge durations.
These durations measure when userspace observes and handles lifecycle events;
they are not exact kernel-switch or display-scanout latency.

The interactive worker no longer wakes on a fixed 10 ms input slice. It blocks
in `poll(2)` on the wsdisplay lifecycle self-pipe and active keyboard/mouse
descriptors until lifecycle activity, input readiness, or the remaining trial
deadline. Run-until-exit repeats the same readiness wait without treating its
expiry as a presentation deadline. Lifecycle events are checked before
blocking and after every wake, timeout, or interruption, and take priority when
lifecycle and input become ready together. Each loop phase drains at most 128
host events and 64 input events, with host events rechecked around input
handling and presentation so an input flood cannot starve VT release or
termination. The input descriptors are
borrowed only for one wait call, so suspension cannot leave stale descriptors
registered in the host. `--wscons-input-stats` reports wait calls, input and
signal-pipe readiness, simultaneous readiness, host events, timeouts, and
interruptions. Device-free tests cover the wait rules. In the physical X220
trial, all 1232 calls woke for wscons input; signal-pipe readiness,
simultaneous readiness, queued host events, timeouts, interruptions, and clock
regressions remained zero. Input-read-to-framebuffer-copy completion averaged
6 ms with a 24 ms maximum, interaction felt good, and console restoration
passed.

For bounded runs, the unmapped parent applies a hard deadline of at most 30000
ms plus startup grace. In run-until-exit mode it instead watches the worker and
termination signals without a presentation deadline. Both modes use bounded
TERM and KILL grace periods, attempt restoration even if the child cannot be
reaped promptly, and independently verify every saved console property.
`SIGPIPE` is supervised so a lost output channel cannot bypass teardown.
Job-control stop signals also request shutdown rather than pausing the parent.
The state file is removed only after restoration is verified and worker reap
is confirmed.

`tools/run-wsdisplay-smoke.sh` configures, builds, tests, performs preflight,
explicitly selects `--runtime-preview`, and verifies postflight state. It
still requires an SSH session, passwordless recovery access, a typed
confirmation, both harness acknowledgements, and a second SSH recovery path.
Bounded runs retain an outer deadline equal to the requested duration rounded
up to seconds plus a ten-second restoration margin. A direct
`--desktop-preview` invocation remains the compatible output-only shell
preview, while `--interactive-preview` retains the previous lightweight
interactive scene as a fallback.

Its optional `--vt-cycle` form supplies the direct harness
`--require-vt-cycle` option. With no numeric argument it also supplies
`--until-exit`, omits the outer timeout, and requires the explicit
`TAKEOVER-UNTIL-EXIT` confirmation. A numeric argument retains bounded mode.
Before takeover, it captures the originating one-based VT, chooses a distinct
away VT (VT 2 unless that is the origin), and prints the exact commands for the
retained second SSH session.
`NIXBENCH_VT_AWAY` can select another configured, idle text console. The
operator pauses until the away console is visible before running the printed
return command. Required-cycle mode is valid only with an interactive/runtime
preview and fails unless release/acquire and input suspend/resume counts form a
complete balanced cycle, lifecycle timing has no regressions or missing
samples, and a post-acquire frame completes. The implementation and
device-free tests pass. The X220 physically validated the VT cycle and Escape
exit on 2026-07-15; the remaining menu-navigation keys still need a focused
physical trial.

The hardware runner selects `RelWithDebInfo` unless
`NIXBENCH_BUILD_TYPE` overrides it. Debug builds remain useful for correctness
work, but their unoptimized full-frame software conversion is not a meaningful
pointer-fluidity baseline.

The X220 timing checkpoint first measured 175..179 ms per input-associated
frame. Its already compatible 32-bit layout was still traversing the generic
per-channel converter in an unoptimized build. A canonical RGB8888 fast path
and `RelWithDebInfo` hardware runner reduced the next trial to 7..60 ms,
averaging 36 ms. The pipeline split attributed about 2 ms to SDL software
rendering and 34 ms to the synchronous full mapped-framebuffer presentation;
input-to-render and completion delivery both averaged zero at millisecond
resolution. Source-shadow damage suppression was then validated on the same
hardware: the latest guided runtime trial averaged 5 ms from userspace input
read through framebuffer-copy completion.

The next adaptive-pointer trace retained that 5 ms average. Of 1709 relative
events, 888 used 100% gain, 626 used 101..149%, 186 used 150..199%, 9 used
200..249%, and none reached 250%. The user judged the overall feel good but
reported flutter during small movements instead of a straight course. That
observation motivated the 400-count/s identity region and carry-clearing
checkpoint above. The repeat trial processed 1855 relative events, clearing
45 precision-boundary and 20 direction-boundary carries. It reported no
non-edge suppression and no raw-zero packet, kept the 5 ms input-to-copy
average, and looked and felt good to the user. Presentation peaked at 20 ms,
and the supervisor again verified complete console restoration.

To avoid rewriting roughly 4.0 MiB for every cursor update, the `wsdisplay`
host now owns a tightly packed copy of the last accepted 32-bit source
frame. After complete upfront validation, unchanged rows cause no device write
and a changed row converts only its first-through-last changed-pixel span. The
shadow ignores stride padding and source bits irrelevant to the destination.
It is never populated from framebuffer memory. Initial presentation, source or
destination format changes, destination allocation changes, and every map or
unmap invalidate it and force a full refresh. If the optional allocation is
unavailable, the existing full-frame path remains functional. A planned update
whose row spans cover more than half the pixels also uses one full conversion,
avoiding hundreds of small conversions for dense animation.

If the supervisor itself fails, a second SSH session can run
`sudo ./build/nixbench-wsdisplay-smoke --recover` against the persisted record.
This manual fallback is why the command explicitly acknowledges the absence of
a production crash watchdog. The harness still runs both parent and worker as
root and must not be confused with the separately built privilege-separated
session milestone described below.

CTest registers only device-free parser, reducer, rendering, interaction, and
help checks for the executable. It never opens `/dev/wskbd` or `/dev/wsmouse`,
supplies the acknowledgements, or performs a console takeover. The first
2000 ms ThinkPad X220 diagnostic run completed normally: the supervisor
verified the saved emulation, video, VT, and active-screen state; an independent
SSH watcher saw the recovery record clear with no manual recovery needed; and
the post-run probe matched the baseline. A later 5000 ms
`--desktop-preview` run completed through the same software-framebuffer path.
The supervisor again verified restoration; the guided postflight and a
separate SSH preflight found the original console state, no recovery record,
and no harness process. Manual recovery was not needed.

A later bounded `--interactive-preview` X220 trial rendered the software
cursor and allowed the physical pointer to operate the global menus and
managed window. Motion was functional but felt slower and less fluid than the
hosted path. The later pipeline measurements isolated the initial delay to
full-frame conversion and mapped-framebuffer writes; the optimized conversion
felt substantially better. Damage-suppressed mapped writes subsequently
averaged 5 ms in physical validation. A later adaptive trial retained the
5 ms average and felt good overall, but exposed low-speed flutter. The new
identity threshold and precision/direction carry resets eliminated non-edge
suppression in the repeat trace, and the user reported that it looked and felt
good, physically validating the carry fix. A subsequent native-timestamp trial
was also reported all good: all 1738 relative events used native timestamps,
forming 1035 buckets with 703 same-timestamp events and no fallback or clock-
source reset. Input-to-framebuffer-copy completion averaged 8 ms with a 22 ms
maximum, and console restoration passed. The subsequent readiness-driven wait
was then physically validated with lifecycle priority and bounded input
batches intact. All 1232 waits were input-ready, with no timeout,
interruption or clock regression; the input-driven trial observed no lifecycle
activity. Input-to-copy completion
averaged 6 ms with a 24 ms maximum, the interaction felt good, and console
restoration passed again.

On 2026-07-14, the first guided `--runtime-preview` X220 trial completed as
well. The physical console displayed the shared runtime and real NixInfo
application through `wsdisplay` and wscons with X11, Wayland publication, and
SDL video absent. The user confirmed the scene worked, and the guided
postflight restored and rechecked the console successfully.

On 2026-07-15, the no-deadline required-cycle trial switched the X220 from VT
1 to VT 2 and back. Release/acquire and input suspend/resume counts each
balanced at 1/1 without timing regressions; release acknowledgement took 145
ms, the suspended interval was 11383 ms, and acquire acknowledgement took 13
ms. The post-acquire frame completed, Escape exited cleanly, and its
input-associated frame completed in 5 ms. Supervisor and independent
postflight checks restored screen 0 in emulation mode with automatic VT
handling and video on, with one-based VT 1 active and no recovery record left.

### 3. Opt-in privilege-separated standalone session

Configure with `-DNIXBENCH_BUILD_WSDISPLAY_SESSION=ON` together with the
Wayland server, Wayland client, and application options. This installs three
executables whose session uses five process roles:

- `nixbench-wsdisplay-session`, the root launcher and recovery supervisor;
- a root device worker forked by that launcher, which alone owns `wsdisplay`,
  wscons input, VT lifecycle, frame presentation, and the core heartbeat;
- an ordinary-user `nixbench-session-core` runtime sentinel;
- its ordinary-user `nixbench-session-core` sibling, which publishes the
  private Wayland display; and
- the unprivileged selected initial client launched by that core (`nixclock`
  by default).

Before any privileged state or device open, the launcher ensures descriptors
0, 1, and 2 are occupied by valid standard streams. Privileged descriptors
cannot therefore leak into an inherited closed standard slot. A root-owned
mode-0600 flock at `/var/run/nixbench-wsdisplay-session.lock` serializes launch,
preflight, and recovery; both the supervisor and live device worker retain it.
The root-only recovery record at
`/var/run/nixbench-wsdisplay-session.state` is created exclusively, and its
continued presence independently prevents another launch. It is removed only
after the worker and core are gone and restoration has been verified.

The worker and core communicate through a private anonymous socketpair using
versioned, fixed-size, state-dependent messages. The actual security boundary
is that private channel plus the trusted child performing and verifying an
irreversible credential drop before `execve()`. The PID and real, effective,
and saved IDs repeated in `CORE_HELLO` are consistency checks supplied by the
core; they are not OS-authenticated peer credentials, independent proof of the
drop, or authorization for any broader operation.

The device worker monitors the core's bounded heartbeat. The supervisor tracks
the worker and core lifetimes, escalates TERM to KILL when needed, and performs
idempotent restoration without relying on core cleanup handlers. The guided
entry point is:

```sh
./tools/run-wsdisplay-session.sh
```

NixClock is the default initial client. An existing application can instead
be selected for a startup compatibility probe with one absolute executable
path; application arguments are not supported yet:

```sh
NIXBENCH_APPLICATION=/usr/pkg/bin/midori ./tools/run-wsdisplay-session.sh
```

An empty-history Midori Speed Dial has no shortcut tiles and can look like an
unpainted gray page. Use the fixed offline content wrapper to verify WebKit
page pixels without application arguments, typing, or a network:

```sh
NIXBENCH_APPLICATION="$PWD/tools/run-midori-content-probe.sh" \
  ./tools/run-wsdisplay-session.sh
```

It selects WebKitGTK 2.36's software-compositing compatibility path and
executes the installed Midori with a built-in `data:` page that visibly names
the NixBench WebKit probe.

For a post-map Midori crash that occurs only on the physical standalone host,
the same selector can launch a fixed, ordinary-user debugger wrapper:

```sh
NIXBENCH_APPLICATION="$PWD/tools/run-midori-gdb.sh" \
  ./tools/run-wsdisplay-session.sh
```

The wrapper is specific to the installed NetBSD/pkgsrc paths. It sets the
WebKitGTK 2.36 `WEBKIT_DISABLE_COMPOSITING_MODE=1` A/B switch, executes GDB
only after the normal credential drop, follows the direct Midori UI process,
and prints its crash stacks and loaded libraries to the SSH session if the
software path still fails. It does not enable core dumps, create a new process
group, or change privileged supervision and console restoration. The switch
is the upstream workaround recorded for this renderer in
[WebKit bug 238513](https://bugs.webkit.org/show_bug.cgi?id=238513).

The guided script validates the selected file before `sudo`, while still
running as the ordinary user. The privileged launcher performs only bounded
lexical validation and forwards the path; it does not resolve, open, or execute
the selected file. The core executes it without a shell only after its verified
credential drop. The application receives `XDG_RUNTIME_DIR` and
`WAYLAND_DISPLAY`; the core also sets `EGL_PLATFORM=wayland` explicitly so
NetBSD libEGL cannot choose its X11 default in this X-free session. These
values and the fixed standalone identity `XDG_CURRENT_DESKTOP=NixBench`,
`XDG_SESSION_DESKTOP=NixBench`, `XDG_SESSION_TYPE=wayland`,
`GDK_BACKEND=wayland`, and `LANG=C.UTF-8` are established after the sanitized
credential drop rather than inherited from the privileged launcher. The
application receives no console, recovery, or helper descriptor. This is
privilege separation, not application sandboxing: the client retains the
invoking user's home-directory and group access.

The initial Midori run is diagnostic and should use only blank or trusted
content. A toplevel may appear before the browser is fully usable. Missing
full popup pointer routing, outside-click dismissal policy, positioner
constraint adjustment, pointer-axis scrolling, subsurfaces, clipboard/data
transfer beyond the discovery skeleton, accelerated buffer sharing, and a
generic GTK global-menu bridge are the expected first compatibility
boundaries.

Set `NIXBENCH_TRACE_WAYLAND=1` on the Midori probe wrapper when you need a
client-side protocol trace for a failing interaction.

It configures and builds the opt-in targets, runs device-free tests, stages the
privileged launcher as the root-owned, non-writable
`/var/run/nixbench-wsdisplay-session`, performs a query-only preflight, and
requires explicit takeover confirmation. The user-owned core path is passed
separately and is executed only after the drop; the script never sudo-executes
the build-tree launcher. The session has no automatic presentation deadline.
Keep a second SSH connection open so the printed supervisor PID can be
terminated if necessary; if orderly recovery does not complete, use:

```sh
sudo /var/run/nixbench-wsdisplay-session --recover
```

The device-free fault gates are selected independently:

```sh
NIXBENCH_EXPECT_CORE_FAILURE=crash ./tools/run-wsdisplay-session.sh
NIXBENCH_EXPECT_CORE_FAILURE=hang ./tools/run-wsdisplay-session.sh
```

Once the validated core is ready, the supervisor atomically arms and prints an
exact `SIGUSR1` command. That signal asks the worker to deliver either `SIGKILL`
or `SIGSTOP` to the known core PID. The crash gate requires the expected signal
death; the hang gate requires the heartbeat timeout. Both require bounded
process-group containment, successful ordinary-user sentinel cleanup, verified
console restoration, and recovery-record removal, with no independent failure.
Privileged code never removes filesystem entries through the sentinel-reported
runtime path.

The process split and device-free integration coverage are implemented. The
exact configuration builds natively on NetBSD and passes all 48 device-free
tests. The staged root launcher links only NetBSD libc, has verified root
ownership, and completed query-only preflight without changing the saved
console state. The first physical takeover launched the desktop and NixClock
through the ordinary-user private Wayland display, then completed normal exit,
verified restoration, cleared the recovery record, and returned to one-based
VT 1. A subsequent privilege-separated trial completed VT 1 -> 2 -> 1 with
release/acquire completions balanced at 1/1. The ordinary-user desktop and
NixClock returned after acquisition, normal exit cleared the recovery record,
and independent postflight verified screen 0 in emulation mode with automatic
VT handling, video on, and VT 1 active. The physical supervised-SIGTERM gate
also passed. After receiving the operator signal, the root supervisor delivered
SIGTERM to the ordinary-user core. The core completed its in-band orderly
shutdown, worker/core cleanup passed, restoration was verified, and the
recovery record was cleared. Independent postflight again verified screen 0 in
emulation mode with automatic VT handling, video on, and one-based VT 1 active,
and the guided harness reported success. The trial's non-fatal NixClock Wayland
EOF diagnostic prompted a cleanup-order fix that keeps the private display
alive until the tracked application exits. The deterministic physical
core-crash gate now passes: its armed supervisor trigger injected `SIGKILL`
only into the validated core, observed the required death, contained the
application group, verified ordinary-user sentinel cleanup, restored the saved
console state, and removed the recovery record. Independent checks found no
reported runtime directory, recovery record, or surviving NixBench process and
found one-based VT 1 active. NixClock's Wayland read failure is expected for
this deliberate compositor crash. The physical core-hang gate also passes. The
worker stopped the validated core, the heartbeat watchdog detected the stall,
and forced process-group containment, sentinel cleanup, console restoration,
and record removal completed. Independent checks again found no runtime path,
recovery record, or surviving NixBench process and found one-based VT 1 active
with the saved console state. Failure of an orderly in-band shutdown request is
expected after the watchdog has invalidated the hung helper session. Malformed
protocol, worker or supervisor hard failure, and repeated-session recovery
remain later hardware gates.

The first physical existing-toolkit probe selected Midori 9.0. Its
ordinary-user process reached the private Wayland display, but GTK emitted
null-`GdkSeat` diagnostics and no toplevel mapped before the client closed its
connection. GTK 3.24 defers creating a seat until the registry contains both
`wl_seat` and `wl_data_device_manager`; NixBench exposed only the former. A
minimal version-1 data-device discovery skeleton now provides inert source and
device resources and sends an empty selection before keyboard focus. It does
not claim clipboard or drag-and-drop transfer behavior.

The repeat physical probe passed that seat gate but Midori then terminated
with `SIGSEGV` before mapping a window. The same crash reproduced against the
hosted compositor with SDL's device-free offscreen backend, while the installed
`gtk3-demo` remained alive with the same sanitized application environment.
GDB placed the fault below WebKitGTK in NetBSD libEGL's default X11 DRI2 path:
`eglInitialize()` reached `xcb_connection_has_error()` without an X
connection. Selecting `EGL_PLATFORM=wayland` prevents that X11 fallback.
Midori then remained alive and a Wayland client trace recorded a configured
`xdg_toplevel`, repeated shared-memory buffer commits, keyboard focus, and
frame callbacks. WebKit reports that EGL initialization itself is unavailable
with the base-system library and takes a software path. The next physical run
did display the Speed Dial window, but the tracked Midori UI process later
terminated with a second `SIGSEGV`. A matching device-free run survived for 20
seconds while emitting the same non-fatal warnings. The fixed ordinary-user
GDB wrapper then remained stable on the physical console and Midori exited
normally through its close gadget, validating forced software compositing as
the compatibility path.

A later device-free capture loaded a fixed `data:` page and showed both the
complete browser chrome and large WebKit-rendered HTML text. Midori's original
gray Speed Dial was its normal empty-history page, not a missing surface or
composition failure. The dedicated content wrapper above makes the same check
available on the physical console. Missing user-session D-Bus services still
produce non-fatal accessibility and GApplication diagnostics; one supervised
ordinary-user D-Bus session for the complete desktop remains future work.

The first physical keyboard trial subsequently delivered Control+L and focused
Midori's URL entry. GTK 3.24 then created its `GtkEntryCompletion` dropdown as
an `xdg_popup` as soon as the first character (`d`) was typed. The compositor's
former fatal "popup not implemented" response disconnected the client, and
Midori terminated with `SIGSEGV` while tearing down; the typed letter itself
had traversed the wscons and Wayland keyboard paths correctly. The embedded
server now implements a basic `xdg_positioner`/`xdg_popup` lifecycle: it sends
the initial configure sequence, accepts GTK's valid pre-map popup grab, copies
and CPU-composites shared-memory popup pixels into the parent snapshot, and
recursively dismisses descendants when a parent unmaps or is destroyed. The
NetBSD device-free suite passes 50/50 tests with that implementation. Full
pointer routing into popups, outside-click dismissal policy, and positioner
constraint adjustment remain incomplete. A physical Control+L, type/edit, and
Return retry is still pending.

The older all-root smoke harness remains useful only for bounded research and
does not become an application launcher.

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
events. The interactive smoke mode now exercises the intended separation with
a narrow provider that translates the fixed `/dev/wskbd` and `/dev/wsmouse`
mux streams into normalized events. The `wsdisplay` adapter never opens those
devices. The research provider covers a software pointer driven by relative
motion and the left button. Its first full keyboard profile explicitly maps
validated PC-XT raw positions 0 through 255 to physical XKB names; this
includes ordinary text, left/right modifiers, editing/navigation keys,
function keys, and the keypad. Activation requires one unchanged direct
keyboard in the mux, exact `KB_US` encoding, and required sentinels before and
after probing. USB, mixed/multiple, nested, variant, changed, and unknown
configurations keep the earlier active-map control fallback. Arbitrary maps
installed with `WSKBDIO_SETMAP` are not imported and may not be detected. It
propagates repeated downs and synthesizes every held-key
release on `ALL_KEYS_UP`; absolute-device calibration is deliberately
deferred. Layout reconciliation beyond exact `KB_US`, hotplug, heterogeneous mux
devices, LED synchronization, and production seat policy remain incomplete.
This separation permits device-free
reducer tests and later lets the aggregate host facade compose production
display, session, and input providers.
Consult [`wskbd(4)`][wskbd], [`wsmouse(4)`][wsmouse], and the versioned event
definitions in [`wsconsio.h`][wsconsio] before stabilizing that API.

## VT lifecycle and crash recovery

On kernels built with `WSDISPLAY_COMPAT_USL`, the session owner uses
`VT_PROCESS` release/acquire signalling. Signal handlers do only async-signal-
safe notification (a `sig_atomic_t` flag and nonblocking self-pipe write); all
ioctls, allocation, logging, and state changes happen in the event loop.
The experimental adapter also turns `SIGINT`, `SIGTERM`, `SIGHUP`, and
`SIGQUIT` into a normal host quit event so the event loop can restore the
console. This depends on the caller continuing to service the event loop and
does not make the old all-root smoke harness equivalent to the separate
recovery supervisor. Its process-global signal handling also makes this first
adapter single-threaded-only from creation through destruction.

On release request, NixBench stops accepting frames, cancels focus, held input,
and pointer capture, then acknowledges through
`nb_host_complete_console_release()`. On acquire, it acknowledges through
`nb_host_complete_console_acquire()`, re-queries output state, remaps or
recreates invalid resources, reopens wscons input, and re-queries the active
control bindings. The next event-loop presentation submits the required full
redraw. Lifecycle requests must not be lost behind a full ordinary event queue.
Required-cycle mode records acknowledgement and suspended-away timing and
enforces balanced release/acquire and input suspend/resume counts, complete
non-regressing timing samples, and a post-acquire frame.

The research supervisor and the new recovery supervisor record enough original
state to restore emulation mode, video state, and automatic VT handling.
Restoration is ordered, best-effort, and idempotent so it is safe after partial
setup. The implementation uses the NetBSD
[`wsdisplay` USL compatibility source][wsdisplay-usl] as the reference for
`VT_SETMODE`/`VT_RELDISP` semantics rather than assuming Linux behaviour.

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
