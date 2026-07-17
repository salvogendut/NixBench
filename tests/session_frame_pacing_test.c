#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "session_frame_pacing.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void test_refresh_configuration(void)
{
    struct nb_session_frame_pacing pacing;

    nb_session_frame_pacing_init(&pacing);
    CHECK(pacing.interval_milliseconds == 17);
    CHECK(pacing.deadline_milliseconds == 0);

    nb_session_frame_pacing_configure(&pacing, 60000);
    CHECK(pacing.interval_milliseconds == 17);
    nb_session_frame_pacing_configure(&pacing, 120000);
    CHECK(pacing.interval_milliseconds == 9);
    nb_session_frame_pacing_configure(&pacing, 1000);
    CHECK(pacing.interval_milliseconds == 1000);
    nb_session_frame_pacing_configure(&pacing, 1);
    CHECK(pacing.interval_milliseconds == 1000);
    nb_session_frame_pacing_configure(&pacing, 0);
    CHECK(pacing.interval_milliseconds == 17);
    nb_session_frame_pacing_configure(&pacing, -1);
    CHECK(pacing.interval_milliseconds == 17);
}

static void test_deadline_and_wait(void)
{
    struct nb_session_frame_pacing pacing;

    nb_session_frame_pacing_init(&pacing);
    CHECK(nb_session_frame_pacing_ready(&pacing, 100));
    nb_session_frame_pacing_presented(&pacing, 100);
    CHECK(pacing.deadline_milliseconds == 117);
    CHECK(!nb_session_frame_pacing_ready(&pacing, 116));
    CHECK(nb_session_frame_pacing_ready(&pacing, 117));
    CHECK(nb_session_frame_pacing_wait_timeout(&pacing,
                                               105,
                                               true,
                                               false,
                                               60000) == 12);
    CHECK(nb_session_frame_pacing_wait_timeout(&pacing,
                                               117,
                                               true,
                                               false,
                                               60000) == 0);
    CHECK(nb_session_frame_pacing_wait_timeout(&pacing,
                                               105,
                                               false,
                                               false,
                                               60000) == 60000);
    CHECK(nb_session_frame_pacing_wait_timeout(&pacing,
                                               105,
                                               true,
                                               true,
                                               60000) == 60000);
    CHECK(nb_session_frame_pacing_wait_timeout(&pacing,
                                               105,
                                               true,
                                               false,
                                               5) == 5);

    nb_session_frame_pacing_presented(&pacing, UINT64_MAX - 5U);
    CHECK(pacing.deadline_milliseconds == UINT64_MAX);
}

static void test_defensive_arguments(void)
{
    nb_session_frame_pacing_init(NULL);
    nb_session_frame_pacing_configure(NULL, 60000);
    nb_session_frame_pacing_presented(NULL, 1);
    CHECK(!nb_session_frame_pacing_ready(NULL, 1));
    CHECK(nb_session_frame_pacing_wait_timeout(NULL,
                                               1,
                                               true,
                                               false,
                                               42) == 42);
}

int main(void)
{
    test_refresh_configuration();
    test_deadline_and_wait();
    test_defensive_arguments();

    if (failures != 0) {
        fprintf(stderr, "session frame pacing tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("session frame pacing tests: ok");
    return 0;
}
