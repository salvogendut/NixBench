#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wsdisplay_session.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static bool parse(int argc,
                  char *argv[],
                  struct nb_wsdisplay_session_options *options)
{
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY];

    return nb_wsdisplay_session_parse_options(argc,
                                               argv,
                                               options,
                                               error);
}

static void test_actions(void)
{
    struct nb_wsdisplay_session_options options;
    char *help[] = {"session", "--help"};
    char *preflight[] = {"session", "--preflight"};
    char *recover[] = {"session", "--recover"};
    char *run[] = {"session", "--acknowledge-console-takeover"};
    char *core[] = {
        "session", "--core", "/opt/nixbench/core",
        "--acknowledge-console-takeover"
    };

    CHECK(parse(2, help, &options));
    CHECK(options.action == NB_WSDISPLAY_SESSION_ACTION_HELP);
    CHECK(parse(2, preflight, &options));
    CHECK(options.action == NB_WSDISPLAY_SESSION_ACTION_PREFLIGHT);
    CHECK(parse(2, recover, &options));
    CHECK(options.action == NB_WSDISPLAY_SESSION_ACTION_RECOVER);
    CHECK(parse(2, run, &options));
    CHECK(options.action == NB_WSDISPLAY_SESSION_ACTION_RUN);
    CHECK(options.acknowledge_console_takeover);
    CHECK(parse(4, core, &options));
    CHECK(strcmp(options.core_path, "/opt/nixbench/core") == 0);
}

static void test_rejections(void)
{
    struct nb_wsdisplay_session_options options;
    char *none[] = {"session"};
    char *unknown[] = {"session", "--wat"};
    char *two_actions[] = {"session", "--help", "--recover"};
    char *action_ack[] = {
        "session", "--preflight", "--acknowledge-console-takeover"
    };
    char *relative_core[] = {
        "session", "--acknowledge-console-takeover", "--core", "core"
    };
    char *missing_core[] = {
        "session", "--acknowledge-console-takeover", "--core"
    };
    char *duplicate_ack[] = {
        "session", "--acknowledge-console-takeover",
        "--acknowledge-console-takeover"
    };

    CHECK(!parse(1, none, &options));
    CHECK(!parse(2, unknown, &options));
    CHECK(!parse(3, two_actions, &options));
    CHECK(!parse(3, action_ack, &options));
    CHECK(!parse(4, relative_core, &options));
    CHECK(!parse(3, missing_core, &options));
    CHECK(!parse(3, duplicate_ack, &options));
    CHECK(!nb_wsdisplay_session_parse_options(0, NULL, &options, NULL));
    CHECK(!nb_wsdisplay_session_parse_options(1, none, NULL, NULL));
}

static void test_core_paths(void)
{
    char path[NB_WSDISPLAY_SESSION_PATH_CAPACITY];
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY];

    CHECK(nb_wsdisplay_session_derive_core_path(
        "/usr/pkg/bin/nixbench-wsdisplay-session", NULL, path, error));
    CHECK(strcmp(path, "/usr/pkg/bin/nixbench-session-core") == 0);
    CHECK(nb_wsdisplay_session_derive_core_path(
        "/session", NULL, path, error));
    CHECK(strcmp(path, "/nixbench-session-core") == 0);
    CHECK(nb_wsdisplay_session_derive_core_path(
        "/session", "/custom/core", path, error));
    CHECK(strcmp(path, "/custom/core") == 0);
    CHECK(!nb_wsdisplay_session_derive_core_path(
        "relative/session", NULL, path, error));
    CHECK(!nb_wsdisplay_session_derive_core_path(
        "/session", "relative/core", path, error));
    CHECK(!nb_wsdisplay_session_derive_core_path(
        "/session/", NULL, path, error));
    CHECK(!nb_wsdisplay_session_derive_core_path(
        "/session", "/", path, error));
    CHECK(!nb_wsdisplay_session_derive_core_path(
        "/session", NULL, NULL, error));
}

static void test_frame_completion_across_vt_cycles(void)
{
    struct nb_wsdisplay_session_frame_state state;

    nb_wsdisplay_session_frame_state_init(&state);
    CHECK(nb_wsdisplay_session_frame_submitted(&state, 1));
    nb_wsdisplay_session_frame_abandon(&state);
    CHECK(nb_wsdisplay_session_frame_submitted(&state, 2));
    CHECK(nb_wsdisplay_session_frame_completed(&state, 1) ==
          NB_WSDISPLAY_SESSION_FRAME_ABANDONED);
    CHECK(nb_wsdisplay_session_frame_completed(&state, 2) ==
          NB_WSDISPLAY_SESSION_FRAME_CURRENT);

    CHECK(nb_wsdisplay_session_frame_submitted(&state, 3));
    nb_wsdisplay_session_frame_abandon(&state);
    CHECK(nb_wsdisplay_session_frame_submitted(&state, 4));
    nb_wsdisplay_session_frame_abandon(&state);
    CHECK(nb_wsdisplay_session_frame_completed(&state, 3) ==
          NB_WSDISPLAY_SESSION_FRAME_ABANDONED);
    CHECK(nb_wsdisplay_session_frame_completed(&state, 4) ==
          NB_WSDISPLAY_SESSION_FRAME_ABANDONED);
    CHECK(nb_wsdisplay_session_frame_submitted(&state, 5));
    CHECK(!nb_wsdisplay_session_frame_submitted(&state, 6));
    CHECK(nb_wsdisplay_session_frame_completed(&state, 6) ==
          NB_WSDISPLAY_SESSION_FRAME_INVALID);
    CHECK(nb_wsdisplay_session_frame_completed(&state, 5) ==
          NB_WSDISPLAY_SESSION_FRAME_CURRENT);
}

int main(void)
{
    test_actions();
    test_rejections();
    test_core_paths();
    test_frame_completion_across_vt_cycles();

    if (failures != 0) {
        fprintf(stderr, "wsdisplay session tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("wsdisplay session tests: ok");
    return 0;
}
