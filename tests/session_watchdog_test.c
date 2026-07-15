#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "session_watchdog.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static enum nb_session_watchdog_action advance(
    struct nb_session_watchdog *watchdog,
    uint64_t now,
    uint64_t *token)
{
    *token = UINT64_MAX;
    return nb_session_watchdog_advance(watchdog, now, token);
}

static void test_production_timings(void)
{
    CHECK(NB_SESSION_WATCHDOG_STARTUP_GRACE_MS == 10000);
    CHECK(NB_SESSION_WATCHDOG_PING_INTERVAL_MS == 1000);
    CHECK(NB_SESSION_WATCHDOG_PONG_TIMEOUT_MS == 5000);
}

static void test_startup_deadline(void)
{
    struct nb_session_watchdog watchdog;
    uint64_t token;

    nb_session_watchdog_init(&watchdog, 100);
    CHECK(advance(&watchdog, 100, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(token == 0);
    CHECK(advance(&watchdog, 10099, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(advance(&watchdog, 10100, &token) ==
          NB_SESSION_WATCHDOG_ACTION_STARTUP_EXPIRED);
    CHECK(token == 0);
    CHECK(advance(&watchdog, 20000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_STARTUP_EXPIRED);
    CHECK(!nb_session_watchdog_note_ready(&watchdog, 20000));
}

static void test_ready_and_matching_pong(void)
{
    struct nb_session_watchdog watchdog;
    uint64_t token;

    nb_session_watchdog_init(&watchdog, 0);
    CHECK(nb_session_watchdog_note_ready(&watchdog, 500));
    CHECK(nb_session_watchdog_note_ready(&watchdog, 1200));
    CHECK(advance(&watchdog, 1499, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(advance(&watchdog, 1500, &token) ==
          NB_SESSION_WATCHDOG_ACTION_SEND_PING);
    CHECK(token == 1);
    CHECK(!nb_session_watchdog_note_pong(&watchdog, 0, 2000));
    CHECK(!nb_session_watchdog_note_pong(&watchdog, 2, 2000));
    CHECK(advance(&watchdog, 6499, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(nb_session_watchdog_note_pong(&watchdog, 1, 6500));
    CHECK(advance(&watchdog, 7499, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(advance(&watchdog, 7500, &token) ==
          NB_SESSION_WATCHDOG_ACTION_SEND_PING);
    CHECK(token == 2);
}

static void test_pong_at_deadline_wins_when_noted_first(void)
{
    struct nb_session_watchdog watchdog;
    uint64_t token;

    nb_session_watchdog_init(&watchdog, 0);
    CHECK(nb_session_watchdog_note_ready(&watchdog, 0));
    CHECK(advance(&watchdog, 1000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_SEND_PING);
    CHECK(token == 1);
    CHECK(nb_session_watchdog_note_pong(&watchdog, 1, 6000));
    CHECK(advance(&watchdog, 6000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(advance(&watchdog, 7000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_SEND_PING);
    CHECK(token == 2);
}

static void test_heartbeat_expiry(void)
{
    struct nb_session_watchdog watchdog;
    uint64_t token;

    nb_session_watchdog_init(&watchdog, 0);
    CHECK(nb_session_watchdog_note_ready(&watchdog, 0));
    CHECK(advance(&watchdog, 1000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_SEND_PING);
    CHECK(advance(&watchdog, 5999, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(advance(&watchdog, 6000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_HEARTBEAT_EXPIRED);
    CHECK(token == 0);
    CHECK(advance(&watchdog, 7000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_HEARTBEAT_EXPIRED);
    CHECK(!nb_session_watchdog_note_pong(&watchdog, 1, 7000));
}

static void test_token_wrap_skips_zero(void)
{
    struct nb_session_watchdog watchdog;
    uint64_t token;

    nb_session_watchdog_init(&watchdog, 0);
    watchdog.next_token = UINT64_MAX;
    CHECK(nb_session_watchdog_note_ready(&watchdog, 0));
    CHECK(advance(&watchdog, 1000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_SEND_PING);
    CHECK(token == UINT64_MAX);
    CHECK(nb_session_watchdog_note_pong(&watchdog, token, 1000));
    CHECK(advance(&watchdog, 2000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_SEND_PING);
    CHECK(token == 1);
    CHECK(nb_session_watchdog_note_pong(&watchdog, token, 2000));

    watchdog.next_token = 0;
    CHECK(advance(&watchdog, 3000, &token) ==
          NB_SESSION_WATCHDOG_ACTION_SEND_PING);
    CHECK(token == 1);
}

static void test_saturating_deadline(void)
{
    struct nb_session_watchdog watchdog;
    uint64_t token;

    nb_session_watchdog_init(&watchdog, UINT64_MAX - 5);
    CHECK(advance(&watchdog, UINT64_MAX - 1, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(advance(&watchdog, UINT64_MAX, &token) ==
          NB_SESSION_WATCHDOG_ACTION_STARTUP_EXPIRED);
}

static void test_invalid_arguments(void)
{
    struct nb_session_watchdog watchdog;
    uint64_t token = 9;

    nb_session_watchdog_init(NULL, 0);
    CHECK(!nb_session_watchdog_note_ready(NULL, 0));
    CHECK(!nb_session_watchdog_note_pong(NULL, 1, 0));
    CHECK(nb_session_watchdog_advance(NULL, 0, &token) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(token == 0);

    nb_session_watchdog_init(&watchdog, 0);
    CHECK(nb_session_watchdog_advance(&watchdog, 0, NULL) ==
          NB_SESSION_WATCHDOG_ACTION_WAIT);
    CHECK(watchdog.phase == NB_SESSION_WATCHDOG_PHASE_STARTUP);
}

int main(void)
{
    test_production_timings();
    test_startup_deadline();
    test_ready_and_matching_pong();
    test_pong_at_deadline_wins_when_noted_first();
    test_heartbeat_expiry();
    test_token_wrap_skips_zero();
    test_saturating_deadline();
    test_invalid_arguments();

    if (failures != 0) {
        fprintf(stderr,
                "session watchdog tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("session watchdog tests: ok");
    return 0;
}
