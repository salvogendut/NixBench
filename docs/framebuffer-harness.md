# Framebuffer research harness

[Back to the main README](../README.md)

## Running the harness

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
open. On a stable single-keyboard PC-XT mux reporting exact `KB_US` plus the
required sentinels, the same provider also emits the complete standard
physical XKB set needed for standalone GTK text entries and shortcuts. USB,
multiple, nested, variant, and unknown keyboard configurations retain only the
bounded controls. Absolute-only pointer devices are not translated by this
first research provider.

## Input behavior and diagnostics

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

## Presentation performance

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

## Takeover and recovery

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
missing timing samples, and a missing post-acquire frame. The complete release/
acquire cycle and Escape exit have passed on the X220; the new full PC-XT text
profile has deterministic reducer and XKB tests. Control+L and the first text
key reached Midori on hardware; the focused type/edit/Return retry now awaits
physical validation of the new popup path.

Interactive input is acquired only by the framebuffer worker. Both mux
descriptors are closed and held button/keyboard state is discarded before an
acknowledged VT release and again during every cleanup path. The display
adapter itself remains output-only. While `/dev/wskbd` is owned by the worker,
normal console text translation and the usual keyboard VT-switch shortcuts are
unavailable. NixBench's own guarded PC-XT/exact-`KB_US` physical mapping remains
available to its shell and focused Wayland clients; other
configurations retain only the control fallback. Keep SSH recovery open while
this hardware-specific profile remains experimental.

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
research path rather than a layout-reconciled, hotplug, multi-device,
production seat or session implementation. The second acknowledgement
recognizes that supervisor
failure still needs manual recovery. CTest exercises parsers, reducers,
rendering, help, and refusal with synthetic input only; automated tests never
open wscons devices or perform a console takeover.
