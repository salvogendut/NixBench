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

The production boundary is therefore:

```text
ordinary-user NixBench core
  shell, compositor, Wayland service, applications, user files
  canonical software frames
                    |
       private fixed-message channel
                    |
root device helper/watchdog
  wsdisplay, wscons, VT lifecycle, presentation, recovery record
```

The helper owns device authority and recovery. The core owns desktop and
application policy. External applications are children or clients of the
ordinary-user side and never inherit the helper's descriptors or credentials.

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

The privileged helper is allowed to:

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

It is not allowed to accept a device path, ioctl number, VT number, memory
address, mapping range, command, executable path, or environment assignment
from the core.

The ordinary-user core is responsible for:

- the desktop, window, menu, focus, composition, and application models;
- the embedded Wayland service and all native or compatibility clients;
- the SDL software canvas and canonical XRGB/ARGB frame production;
- user configuration, files, locale, and application environment; and
- responding to bounded helper liveness checks.

Signal handling, polling, clocks, rendering, pointer policy, and application
logic do not inherently require root.

## Private protocol constraints

The first protocol should be local, inherited, versioned, and unavailable by
filesystem name. Every message has a fixed header, a known type, an exact
bounded payload size, and state-dependent validity. Unknown, malformed,
oversized, duplicated, or out-of-order messages terminate the core and start
restoration.

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

The helper itself remains a trusted failure domain. During development, a
second SSH session and the root-only recovery record remain mandatory in case
the helper fails. A later production-login review can decide whether the
helper needs an even smaller recovery parent.

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

The child also resets signal dispositions and masks, umask, working directory,
and resource limits. It closes every descriptor except standard streams and
its single protocol endpoint. Device, recovery, and helper self-pipe
descriptors are close-on-exec and never appear in application processes. A
`chroot` is not used as a substitute for this boundary because the desktop
must execute normal user applications and access the user's home and system
libraries.

The launcher stays single-threaded through `fork()` and `execve()`. The initial
IPC design must validate the expected child PID and dropped credentials during
its handshake; a pre-fork anonymous socket alone is not proof that the peer has
dropped authority.

## Implementation and acceptance order

1. Extract console capture, restoration, and verification behind private,
   high-level operations and inject failure after every acquisition step.
2. Separately test recovery-record validation and supervisor reap/escalation
   policy. Do not expose fake operations through a root command-line or
   environment switch.
3. Implement the fixed helper/core protocol and move the existing wsdisplay
   host and wscons provider to the helper side.
4. Run the desktop core as the invoking ordinary user and verify descriptor and
   group state before enabling application launch.
5. On NetBSD hardware, validate normal Escape exit, VT 1 -> 2 -> 1, `SIGTERM`,
   `SIGKILL`, a stopped/hung core, malformed protocol, and repeated sessions.

Every case must return to the saved screen in emulation mode with video on and
automatic VT handling, leave no worker or recovery record, and require no
manual recovery. Until those gates pass, the existing root harness remains an
opt-in research tool and must not launch external applications.

## Primary NetBSD references

- [`wsdisplay(4)`](https://man.netbsd.org/wsdisplay.4)
- [`setuid(2)` and `setgid(2)`](https://man.netbsd.org/setuid.2)
- [`setgroups(2)`](https://man.netbsd.org/setgroups.2)
- [`initgroups(3)`](https://man.netbsd.org/initgroups.3)
- [NetBSD 10 `wsdisplay` implementation](https://github.com/NetBSD/src/blob/netbsd-10/sys/dev/wscons/wsdisplay.c)
- [NetBSD 10 USL VT compatibility implementation](https://github.com/NetBSD/src/blob/netbsd-10/sys/dev/wscons/wsdisplay_compat_usl.c)
