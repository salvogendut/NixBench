#include "session_watchdog.h"

#include <stddef.h>

static uint64_t add_milliseconds(uint64_t start, uint64_t duration)
{
    return duration > UINT64_MAX - start ? UINT64_MAX : start + duration;
}

static uint64_t take_token(struct nb_session_watchdog *watchdog)
{
    uint64_t token = watchdog->next_token;

    if (token == 0) {
        token = 1;
    }
    watchdog->next_token = token == UINT64_MAX ? UINT64_C(1) : token + 1;
    return token;
}

void nb_session_watchdog_init(struct nb_session_watchdog *watchdog,
                              uint64_t now)
{
    if (watchdog == NULL) {
        return;
    }
    watchdog->deadline = add_milliseconds(
        now,
        NB_SESSION_WATCHDOG_STARTUP_GRACE_MS);
    watchdog->next_token = 1;
    watchdog->outstanding_token = 0;
    watchdog->phase = NB_SESSION_WATCHDOG_PHASE_STARTUP;
}

bool nb_session_watchdog_note_ready(struct nb_session_watchdog *watchdog,
                                    uint64_t now)
{
    if (watchdog == NULL) {
        return false;
    }
    if (watchdog->phase == NB_SESSION_WATCHDOG_PHASE_READY ||
        watchdog->phase == NB_SESSION_WATCHDOG_PHASE_AWAITING_PONG) {
        return true;
    }
    if (watchdog->phase != NB_SESSION_WATCHDOG_PHASE_STARTUP) {
        return false;
    }
    watchdog->deadline = add_milliseconds(
        now,
        NB_SESSION_WATCHDOG_PING_INTERVAL_MS);
    watchdog->outstanding_token = 0;
    watchdog->phase = NB_SESSION_WATCHDOG_PHASE_READY;
    return true;
}

bool nb_session_watchdog_note_pong(struct nb_session_watchdog *watchdog,
                                   uint64_t token,
                                   uint64_t now)
{
    if (watchdog == NULL || token == 0 ||
        watchdog->phase != NB_SESSION_WATCHDOG_PHASE_AWAITING_PONG ||
        token != watchdog->outstanding_token) {
        return false;
    }
    watchdog->deadline = add_milliseconds(
        now,
        NB_SESSION_WATCHDOG_PING_INTERVAL_MS);
    watchdog->outstanding_token = 0;
    watchdog->phase = NB_SESSION_WATCHDOG_PHASE_READY;
    return true;
}

enum nb_session_watchdog_action nb_session_watchdog_advance(
    struct nb_session_watchdog *watchdog,
    uint64_t now,
    uint64_t *ping_token)
{
    uint64_t token;

    if (ping_token == NULL) {
        return NB_SESSION_WATCHDOG_ACTION_WAIT;
    }
    *ping_token = 0;
    if (watchdog == NULL) {
        return NB_SESSION_WATCHDOG_ACTION_WAIT;
    }

    switch (watchdog->phase) {
    case NB_SESSION_WATCHDOG_PHASE_STARTUP:
        if (now < watchdog->deadline) {
            return NB_SESSION_WATCHDOG_ACTION_WAIT;
        }
        watchdog->phase = NB_SESSION_WATCHDOG_PHASE_STARTUP_EXPIRED;
        return NB_SESSION_WATCHDOG_ACTION_STARTUP_EXPIRED;
    case NB_SESSION_WATCHDOG_PHASE_READY:
        if (now < watchdog->deadline) {
            return NB_SESSION_WATCHDOG_ACTION_WAIT;
        }
        token = take_token(watchdog);
        watchdog->outstanding_token = token;
        watchdog->deadline = add_milliseconds(
            now,
            NB_SESSION_WATCHDOG_PONG_TIMEOUT_MS);
        watchdog->phase = NB_SESSION_WATCHDOG_PHASE_AWAITING_PONG;
        *ping_token = token;
        return NB_SESSION_WATCHDOG_ACTION_SEND_PING;
    case NB_SESSION_WATCHDOG_PHASE_AWAITING_PONG:
        if (now < watchdog->deadline) {
            return NB_SESSION_WATCHDOG_ACTION_WAIT;
        }
        watchdog->phase = NB_SESSION_WATCHDOG_PHASE_HEARTBEAT_EXPIRED;
        return NB_SESSION_WATCHDOG_ACTION_HEARTBEAT_EXPIRED;
    case NB_SESSION_WATCHDOG_PHASE_STARTUP_EXPIRED:
        return NB_SESSION_WATCHDOG_ACTION_STARTUP_EXPIRED;
    case NB_SESSION_WATCHDOG_PHASE_HEARTBEAT_EXPIRED:
        return NB_SESSION_WATCHDOG_ACTION_HEARTBEAT_EXPIRED;
    default:
        return NB_SESSION_WATCHDOG_ACTION_WAIT;
    }
}

uint32_t nb_session_watchdog_wait_timeout(
    const struct nb_session_watchdog *watchdog,
    uint64_t now)
{
    uint64_t remaining;

    if (watchdog == NULL || now >= watchdog->deadline) {
        return 0;
    }
    remaining = watchdog->deadline - now;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}
