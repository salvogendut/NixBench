# Standalone privilege-boundary assessment

This note records the security boundary required before NixBench launches
external applications in a standalone NetBSD session. It applies to the
software `wsdisplay` backend first. The same boundary can later contain other
display backends, but direct KMS is not a dependency for this work.

## Assessment result

The current `nixbench-wsdisplay-smoke` harness has a recovery supervisor, but
it has no privilege separation. The command requires effective UID 0 before
preflight, forks without changing credentials, and runs the desktop runtime,
rendering, NixInfo, framebuffer host, and wscons input provider as root. That
is acceptable only for the explicitly acknowledged hardware-research harness.
It is not acceptable for a desktop that starts external applications.

The separately built `nixbench-wsdisplay-session` milestone now implements the
first opt-in version of the required boundary. Its root recovery supervisor
captures and persists the original console state, watches a root device worker,
and performs final restoration independently. The device worker owns the fixed
`wsdisplay` and wscons devices, VT lifecycle, frame presentation, and core
heartbeat. `nixbench-session-core` runs as the invoking ordinary user, publishes
a private Wayland display, and launches NixClock. A separately dropped sibling
of the same internal executable is the ordinary-user runtime sentinel: it owns
the private runtime directory. On the verified worker path it removes that
directory only after the complete core/application process group is contained;
controller loss also triggers its unprivileged cleanup fallback. Device-free
tests exercise the split, heartbeat timeout, sentinel cleanup, and exact
crash/hang gate policy.
Physical takeover, normal exit, VT 1 -> 2 -> 1, and supervised-SIGTERM recovery
have completed, and both physical core-failure gates now pass. The remaining
failure-injection and repeated-session matrix is still pending, so this is not
a production login session.

The implemented boundary is:

```text
root recovery supervisor
  exclusive recovery record, worker/core lifetime, final restore/verify
  `-- root device worker
        wsdisplay, wscons, VT lifecycle, presentation, heartbeat
        |-- private fixed-message channel --> ordinary-user NixBench core
        |     shell, compositor, private Wayland service, applications
        |     canonical software frames
        `-- private READY/CLEAN channel ----> ordinary-user runtime sentinel
              runtime-directory ownership and bounded cleanup
```

The root side owns device authority and recovery: the supervisor retains the
recovery record and final-restore duty, while the worker owns live device and
protocol handling. The core owns desktop and application policy. External
applications are children or clients of the ordinary-user side and never
inherit privileged descriptors or credentials.
A root-owned mode-0600 flock at the fixed session-lock path serializes launch,
preflight, and recovery. Both the supervisor and live device worker retain a
reference, so killing the supervisor alone cannot admit a competing recovery
or launch. The separate root-only recovery record is created exclusively; its
continued presence also prevents another launch. It is removed only after the
worker and core are gone and restoration has been verified.

## Measured NetBSD device boundary

On the NetBSD 11.0_RC6 X220 used for physical validation, `/dev/ttyEstat`,
`/dev/ttyE0`, `/dev/wskbd`, and `/dev/wsmouse` are character devices owned by
`root` with mode `0600`. The invoking user therefore cannot open them after a
credential drop. The helper must open and retain these fixed devices; changing
their global permissions is not part of the design.

The NetBSD 10 `wsdisplay` implementation performs TTY authorization while
opening a screen. Its mode-switch and framebuffer-mapping paths then operate on
the selected screen descriptor. The USL compatibility layer associates
`VT_PROCESS` with the calling process and routes release/acquire acknowledgments
through that owner. Passing the screen descriptor to the core would therefore
pass a broad ioctl and mapping capability, not merely a frame sink. Passing
wscons descriptors would likewise prevent the helper from reliably revoking
input ownership during a VT release.

Consequently the core receives no wsdisplay, wscons, recovery-state, DRM, or
signal-pipe descriptor and no framebuffer mapping.

## Authority inventory

The privileged supervisor and device worker are collectively allowed to:

- select the active screen from the fixed `ttyEstat` status node;
- open the corresponding fixed `ttyE` screen and fixed wscons mux devices;
- snapshot display mode, video state, VT mode, and active screen;
- create, validate, and remove the root-only recovery record;
- install and service `VT_PROCESS`, including release/acquire acknowledgment;
- map the validated framebuffer, convert canonical frames, and present them;
- close input and unmap output before acknowledging a VT release;
- reopen and revalidate devices after acquisition;
- monitor the core, enforce heartbeat and termination grace periods, and
  restore the saved console state after exit or failure.

Neither root process is allowed to accept a device path, ioctl number, VT
number, memory address, mapping range, command, executable path, or environment
assignment from the core.

The ordinary-user core is responsible for:

- the desktop, window, menu, focus, composition, and application models;
- the embedded Wayland service and all native or compatibility clients;
- the SDL software canvas and canonical XRGB/ARGB frame production;
- user configuration, files, locale, and application environment; and
- responding to bounded helper liveness checks.

Signal handling, polling, clocks, rendering, pointer policy, and application
logic do not inherently require root.

## Private protocol constraints

The first protocol is local, inherited, versioned, and unavailable by
filesystem name. Every message has a fixed header, a known type, an exact
bounded payload size, and state-dependent validity. Unknown, malformed,
oversized, duplicated, or out-of-order messages terminate the core and start
restoration.

The security boundary is the private anonymous socketpair plus the trusted
fork child performing and verifying an irreversible credential drop before
`execve()`. `CORE_HELLO` repeats the expected child PID and real, effective, and
saved IDs as a consistency check against programming or launch mistakes. Those
fields are supplied by the core and are therefore not OS-authenticated peer
credentials; they must not be described as independent proof of the drop or as
authorization for broader helper operations.

The helper can send only:

- validated output descriptions and normalized input events;
- suspend, resume, frame-complete, liveness, and fatal-error notifications.

The core can send only:

- a bounded canonical frame with a strictly increasing serial;
- a liveness response or an orderly shutdown request.

The helper must parse incrementally without blocking VT lifecycle handling.
The correctness-first transport may copy frame bytes. A shared-memory transport
is deferred until its ownership, sealing or truncation behavior, and helper
`SIGBUS` failure mode are understood on supported NetBSD releases.

## Lifecycle and recovery rules

A VT release never waits for the core. The helper stops presentation, closes
input, discards held and queued input, unmaps output, restores the console-facing
mode as required, and acknowledges release. It then notifies the core if the
channel is healthy. Acquisition remaps and revalidates output, acknowledges,
reopens input, sends any output change, and requires a full replacement frame.

EOF, malformed protocol, core exit, fatal signal, or heartbeat expiry all use
the same idempotent restoration path. TERM and KILL escalation remains bounded.
The recovery record is removed only after the core is reaped and every saved
console property is independently verified.

The root recovery supervisor and root device worker remain trusted failure
domains. The supervisor stays outside the desktop process tree, monitors the
worker, and performs final restoration even when the core or worker fails.
During development, a second SSH session and the root-only recovery record
remain mandatory in case the supervisor itself fails. A later production-login
review can decide whether that recovery parent must become smaller still.

## Credential-drop requirements

The helper obtains the target account only from trusted launcher context and
cross-checks the strictly parsed sudo UID, GID, and user name with the system
account database. `getlogin()` is a session label and is not an identity
source. Before executing core code, the child starts a new session, resets its
environment and process state, and establishes the target supplementary groups
while still privileged. It then calls `setgid()` followed by `setuid()`.
NetBSD applies those calls to the real, effective, and saved IDs for a
privileged caller. The child verifies the resulting identity and that UID 0
cannot be reacquired before calling `execve()`.

Before opening the recovery record, console devices, or internal pipes, the
root launcher reserves descriptors 0, 1, and 2 with valid standard streams.
That prevents an inherited closed standard descriptor from being reused by a
privileged device or recovery-state open. The child also resets signal
dispositions and masks, umask, working directory, and resource limits. It
closes every descriptor except those standard streams and its single protocol
endpoint. Device, recovery, and helper self-pipe descriptors are close-on-exec
and never appear in application processes. A `chroot` is not used as a
substitute for this boundary because the desktop must execute normal user
applications and access the user's home and system libraries.

The launcher stays single-threaded through `fork()` and `execve()`. The
anonymous socketpair makes the channel private, while the child's verified,
irreversible credential drop establishes the authority boundary. The
`CORE_HELLO` PID and identity fields detect inconsistent launch state; because
the core supplies them, they do not authenticate the peer or prove the drop.

## Implementation and acceptance order

The opt-in milestone implements the first structural slice as three installed
binaries and five process roles:

- `nixbench-wsdisplay-session` is the root launcher and recovery supervisor;
- its root device worker owns the fixed console devices and bounded protocol;
- one `nixbench-session-core` process runs as the ordinary-user runtime
  sentinel;
- its sibling `nixbench-session-core` publishes the private Wayland display;
  and
- the core launches the ordinary-user `nixclock` client.

Device-free tests exercise recovery-record and supervisor policy, protocol
state, credential selection, standard-descriptor reservation, descriptor
isolation, and user-session startup. The actual launch path performs and
verifies the irreversible credential drop before `execve()`. The guided
`tools/run-wsdisplay-session.sh` command builds and tests this opt-in path,
performs query-only preflight, and requires explicit takeover confirmation. It
has no automatic presentation deadline, so the operator must keep a second SSH
session available to terminate the printed supervisor PID or run:

```sh
sudo /var/run/nixbench-wsdisplay-session --recover
```

`NIXBENCH_EXPECT_SUPERVISOR_TERM=1` selects the forced-supervisor recovery
gate. In that mode the launcher requires SIGTERM to initiate shutdown and
returns success only after independent supervisor faults are excluded, worker
and core liveness are cleared, console restoration is verified, and the
recovery record is removed. The guided script additionally verifies record
absence and return to the original active VT.

`NIXBENCH_EXPECT_CORE_FAILURE=crash` and `hang` select deterministic core-fault
gates. After the supervisor has received the validated core PID, it prints and
atomically arms an exact `SIGUSR1` trigger command. The supervisor sends only a
fixed crash/hang command to the root worker; the worker signals only that known
positive PID. Crash requires the observed `SIGKILL` death. Hang requires a
stopped core followed by expiry of its outstanding heartbeat. An early trigger,
the wrong failure, a second signal, a protocol/device error, incomplete process-
group or sentinel cleanup, failed restoration, or a retained recovery record
all fail the gate.

The exact opt-in targets build natively on the NetBSD test host and all 48
device-free tests pass. Dynamic-link inspection reports only NetBSD libc for
the root launcher, while SDL3 and Wayland remain on the ordinary-user side.
The root-owned staged launcher and mode-0600 lock were verified, and query-only
preflight preserved screen 0 in emulation mode with automatic VT handling and
video on.

The first physical takeover launched the ordinary-user desktop and NixClock on
the private Wayland display and completed a normal exit. The root supervisor
verified restoration and removed the recovery record; independent postflight
found screen 0 in emulation mode, automatic VT handling, video on, and
one-based VT 1 active. A subsequent privilege-separated trial switched from VT
1 to VT 2 and back with release/acquire completions balanced at 1/1. The
ordinary-user desktop and NixClock returned after acquisition, clean normal
exit cleared the recovery record, and independent restoration verification
again found screen 0 in emulation mode, automatic VT handling, video on, and
active VT 1. A subsequent physical supervisor-SIGTERM trial completed its
required orderly path: after receiving the operator signal, the root supervisor
delivered SIGTERM to the ordinary-user core. The core completed its in-band
orderly shutdown, worker/core cleanup passed, restoration was verified, and the
recovery record was cleared. Independent postflight again found screen 0 in
emulation mode with automatic VT handling, video on, and active one-based VT 1,
and the guided harness reported success. The trial's non-fatal NixClock Wayland
EOF diagnostic prompted a cleanup-order fix that keeps the private display
alive until the tracked application exits. The runtime is now created and
removed by a separately credential-dropped sentinel, so the root worker only
coordinates a fixed READY/CLEAN/CLEANED exchange and never performs filesystem
operations on the user-owned path. Deterministic crash and hang gates are
implemented, and the NetBSD core-crash trial now passes. The armed supervisor
trigger caused the worker to deliver `SIGKILL` to the validated core; the
required failure, process-group containment, sentinel cleanup, restoration,
and recovery-record removal all completed. Independent checks found the
reported runtime directory and recovery record absent, no surviving NixBench
process, and active one-based VT 1 with the saved console state. NixClock's
Wayland read failure is expected when the compositor is deliberately killed.
The physical core-hang trial also passes. Its armed trigger delivered `SIGSTOP`
to the validated core, the heartbeat watchdog detected the stall, and forced
process-group containment, sentinel cleanup, restoration, and record removal
completed. Independent checks again found no runtime directory, recovery
record, or surviving NixBench process and found the saved console state with
one-based VT 1 active. Failure of the resumed core's orderly-shutdown request is
expected because the watchdog has already invalidated its helper session.
Remaining later hardware cases are malformed protocol, worker or supervisor
hard failure, and repeated sessions.

Every case must return to the saved screen in emulation mode with video on and
automatic VT handling, leave no worker or recovery record, and require no
manual recovery. Until those gates pass, the new session remains an opt-in
milestone rather than a production login session. The older
`nixbench-wsdisplay-smoke` executable remains a separate all-root research
harness and must not launch external applications.

## Primary NetBSD references

- [`wsdisplay(4)`](https://man.netbsd.org/wsdisplay.4)
- [`setuid(2)` and `setgid(2)`](https://man.netbsd.org/setuid.2)
- [`setgroups(2)`](https://man.netbsd.org/setgroups.2)
- [`initgroups(3)`](https://man.netbsd.org/initgroups.3)
- [NetBSD 10 `wsdisplay` implementation](https://github.com/NetBSD/src/blob/netbsd-10/sys/dev/wscons/wsdisplay.c)
- [NetBSD 10 USL VT compatibility implementation](https://github.com/NetBSD/src/blob/netbsd-10/sys/dev/wscons/wsdisplay_compat_usl.c)
