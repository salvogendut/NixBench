#ifndef NIXBENCH_SESSION_WATCHDOG_H
#define NIXBENCH_SESSION_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

enum {
    NB_SESSION_WATCHDOG_STARTUP_GRACE_MS = 10000,
    NB_SESSION_WATCHDOG_PING_INTERVAL_MS = 1000,
    NB_SESSION_WATCHDOG_PONG_TIMEOUT_MS = 5000
};

enum nb_session_watchdog_action {
    NB_SESSION_WATCHDOG_ACTION_WAIT,
    NB_SESSION_WATCHDOG_ACTION_SEND_PING,
    NB_SESSION_WATCHDOG_ACTION_STARTUP_EXPIRED,
    NB_SESSION_WATCHDOG_ACTION_HEARTBEAT_EXPIRED
};

enum nb_session_watchdog_phase {
    NB_SESSION_WATCHDOG_PHASE_STARTUP,
    NB_SESSION_WATCHDOG_PHASE_READY,
    NB_SESSION_WATCHDOG_PHASE_AWAITING_PONG,
    NB_SESSION_WATCHDOG_PHASE_STARTUP_EXPIRED,
    NB_SESSION_WATCHDOG_PHASE_HEARTBEAT_EXPIRED
};

/*
 * All times are caller-supplied monotonic milliseconds. Deadlines saturate at
 * UINT64_MAX. READY and PONG observations should be applied before advance()
 * when both are observed at the same timestamp as a deadline.
 */
struct nb_session_watchdog {
    uint64_t deadline;
    uint64_t next_token;
    uint64_t outstanding_token;
    enum nb_session_watchdog_phase phase;
};

void nb_session_watchdog_init(struct nb_session_watchdog *watchdog,
                              uint64_t now);

/* Idempotent after the first READY observation. */
bool nb_session_watchdog_note_ready(struct nb_session_watchdog *watchdog,
                                    uint64_t now);

/* Only the token from the currently outstanding PING is accepted. */
bool nb_session_watchdog_note_pong(struct nb_session_watchdog *watchdog,
                                   uint64_t token,
                                   uint64_t now);

/* ping_token is set to zero unless SEND_PING is returned. */
enum nb_session_watchdog_action nb_session_watchdog_advance(
    struct nb_session_watchdog *watchdog,
    uint64_t now,
    uint64_t *ping_token);

/* Milliseconds until advance() can change state, saturated to UINT32_MAX. */
uint32_t nb_session_watchdog_wait_timeout(
    const struct nb_session_watchdog *watchdog,
    uint64_t now);

#endif
