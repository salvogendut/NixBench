#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    char *sigterm_run[] = {
        "session", "--acknowledge-console-takeover",
        "--require-supervisor-sigterm"
    };
    char *crash_run[] = {
        "session", "--acknowledge-console-takeover",
        "--require-core-crash"
    };
    char *hang_run[] = {
        "session", "--require-core-hang",
        "--acknowledge-console-takeover"
    };
    char *core[] = {
        "session", "--core", "/opt/nixbench/core",
        "--acknowledge-console-takeover"
    };
    char *application[] = {
        "session", "--acknowledge-console-takeover",
        "--application", "/opt/Midori Browser/bin/midori"
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
    CHECK(options.application_path == NULL);
    CHECK(!options.require_supervisor_sigterm);
    CHECK(parse(3, sigterm_run, &options));
    CHECK(options.action == NB_WSDISPLAY_SESSION_ACTION_RUN);
    CHECK(options.acknowledge_console_takeover);
    CHECK(options.require_supervisor_sigterm);
    CHECK(options.required_core_failure ==
          NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE);
    CHECK(parse(3, crash_run, &options));
    CHECK(!options.require_supervisor_sigterm);
    CHECK(options.required_core_failure ==
          NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH);
    CHECK(parse(3, hang_run, &options));
    CHECK(!options.require_supervisor_sigterm);
    CHECK(options.required_core_failure ==
          NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG);
    CHECK(parse(4, core, &options));
    CHECK(strcmp(options.core_path, "/opt/nixbench/core") == 0);
    CHECK(parse(4, application, &options));
    CHECK(strcmp(options.application_path,
                 "/opt/Midori Browser/bin/midori") == 0);
    CHECK(options.core_path == NULL);
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
    char *relative_application[] = {
        "session", "--acknowledge-console-takeover",
        "--application", "midori"
    };
    char *missing_application[] = {
        "session", "--acknowledge-console-takeover", "--application"
    };
    char *duplicate_application[] = {
        "session", "--acknowledge-console-takeover",
        "--application", "/usr/pkg/bin/midori",
        "--application", "/usr/pkg/bin/gtk3-demo"
    };
    char *directory_application[] = {
        "session", "--acknowledge-console-takeover",
        "--application", "/usr/pkg/bin/"
    };
    char *root_application[] = {
        "session", "--acknowledge-console-takeover",
        "--application", "/"
    };
    char *control_application[] = {
        "session", "--acknowledge-console-takeover",
        "--application", "/usr/pkg/bin/midori\nspoof"
    };
    char *action_application[] = {
        "session", "--preflight", "--application", "/usr/pkg/bin/midori"
    };
    char *duplicate_ack[] = {
        "session", "--acknowledge-console-takeover",
        "--acknowledge-console-takeover"
    };
    char *action_sigterm[] = {
        "session", "--preflight", "--require-supervisor-sigterm"
    };
    char *sigterm_without_ack[] = {
        "session", "--require-supervisor-sigterm"
    };
    char *duplicate_sigterm[] = {
        "session", "--acknowledge-console-takeover",
        "--require-supervisor-sigterm",
        "--require-supervisor-sigterm"
    };
    char *action_crash[] = {
        "session", "--recover", "--require-core-crash"
    };
    char *crash_without_ack[] = {
        "session", "--require-core-crash"
    };
    char *mixed_core_gates[] = {
        "session", "--acknowledge-console-takeover",
        "--require-core-crash", "--require-core-hang"
    };
    char *mixed_sigterm_crash[] = {
        "session", "--acknowledge-console-takeover",
        "--require-supervisor-sigterm", "--require-core-crash"
    };
    char *duplicate_hang[] = {
        "session", "--acknowledge-console-takeover",
        "--require-core-hang", "--require-core-hang"
    };
    char oversized_application[NB_WSDISPLAY_SESSION_PATH_CAPACITY + 1U];
    char *oversized_application_arguments[] = {
        "session", "--acknowledge-console-takeover",
        "--application", oversized_application
    };

    memset(oversized_application, 'a', sizeof(oversized_application));
    oversized_application[0] = '/';
    oversized_application[sizeof(oversized_application) - 1U] = '\0';

    CHECK(!parse(1, none, &options));
    CHECK(!parse(2, unknown, &options));
    CHECK(!parse(3, two_actions, &options));
    CHECK(!parse(3, action_ack, &options));
    CHECK(!parse(4, relative_core, &options));
    CHECK(!parse(3, missing_core, &options));
    CHECK(!parse(4, relative_application, &options));
    CHECK(!parse(3, missing_application, &options));
    CHECK(!parse(6, duplicate_application, &options));
    CHECK(!parse(4, directory_application, &options));
    CHECK(!parse(4, root_application, &options));
    CHECK(!parse(4, control_application, &options));
    CHECK(!parse(4, action_application, &options));
    CHECK(!parse(4, oversized_application_arguments, &options));
    CHECK(!parse(3, duplicate_ack, &options));
    CHECK(!parse(3, action_sigterm, &options));
    CHECK(!parse(2, sigterm_without_ack, &options));
    CHECK(!parse(4, duplicate_sigterm, &options));
    CHECK(!parse(3, action_crash, &options));
    CHECK(!parse(2, crash_without_ack, &options));
    CHECK(!parse(4, mixed_core_gates, &options));
    CHECK(!parse(4, mixed_sigterm_crash, &options));
    CHECK(!parse(4, duplicate_hang, &options));
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

static void test_x11_socket_directory(void)
{
    char parent[] = "/tmp/nixbench-x11-socket-XXXXXX";
    char path[NB_WSDISPLAY_SESSION_PATH_CAPACITY];
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY];
    struct stat status;
    int length;

    CHECK(mkdtemp(parent) != NULL);
    length = snprintf(path, sizeof(path), "%s/.X11-unix", parent);
    CHECK(length > 0 && (size_t)length < sizeof(path));
    CHECK(nb_wsdisplay_session_prepare_x11_socket_directory(path, error));
    CHECK(lstat(path, &status) == 0);
    CHECK(S_ISDIR(status.st_mode));
    CHECK(status.st_uid == geteuid());
    CHECK((status.st_mode & 07777) == 01777);
    CHECK(nb_wsdisplay_session_prepare_x11_socket_directory(path, error));
    CHECK(rmdir(path) == 0);

    CHECK(mkdir(path, 0700) == 0);
    CHECK(!nb_wsdisplay_session_prepare_x11_socket_directory(path, error));
    CHECK(error[0] != '\0');
    CHECK(rmdir(path) == 0);

    CHECK(symlink("/tmp", path) == 0);
    CHECK(!nb_wsdisplay_session_prepare_x11_socket_directory(path, error));
    CHECK(error[0] != '\0');
    CHECK(unlink(path) == 0);

    CHECK(rmdir(parent) == 0);
    CHECK(!nb_wsdisplay_session_prepare_x11_socket_directory(NULL, error));
    CHECK(!nb_wsdisplay_session_prepare_x11_socket_directory("relative",
                                                             error));
}

static void test_sigterm_gate(void)
{
    struct nb_wsdisplay_session_sigterm_gate gate = {
        .sigterm_received = true,
        .sigterm_drove_shutdown = true,
        .independent_failure = false,
        .worker_gone = true,
        .core_session_gone = true,
        .console_restored = true,
        .recovery_record_removed = true
    };

    CHECK(nb_wsdisplay_session_sigterm_gate_passes(&gate));
    CHECK(!nb_wsdisplay_session_sigterm_gate_passes(NULL));

    gate.sigterm_received = false;
    CHECK(!nb_wsdisplay_session_sigterm_gate_passes(&gate));
    gate.sigterm_received = true;
    gate.sigterm_drove_shutdown = false;
    CHECK(!nb_wsdisplay_session_sigterm_gate_passes(&gate));
    gate.sigterm_drove_shutdown = true;
    gate.independent_failure = true;
    CHECK(!nb_wsdisplay_session_sigterm_gate_passes(&gate));
    gate.independent_failure = false;
    gate.worker_gone = false;
    CHECK(!nb_wsdisplay_session_sigterm_gate_passes(&gate));
    gate.worker_gone = true;
    gate.core_session_gone = false;
    CHECK(!nb_wsdisplay_session_sigterm_gate_passes(&gate));
    gate.core_session_gone = true;
    gate.console_restored = false;
    CHECK(!nb_wsdisplay_session_sigterm_gate_passes(&gate));
    gate.console_restored = true;
    gate.recovery_record_removed = false;
    CHECK(!nb_wsdisplay_session_sigterm_gate_passes(&gate));
}

static void test_core_failure_gate(void)
{
    struct nb_wsdisplay_session_core_failure_gate gate = {
        .expected = NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH,
        .observed = NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH,
        .fault_trigger_received = true,
        .fault_injection_delivered = true,
        .supervisor_signal_received = false,
        .independent_failure = false,
        .worker_gone = true,
        .core_session_gone = true,
        .console_restored = true,
        .recovery_record_removed = true
    };

    CHECK(nb_wsdisplay_session_core_failure_gate_passes(&gate));
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(NULL));

    gate.expected = NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.expected = (enum nb_wsdisplay_session_core_failure)99;
    gate.observed = gate.expected;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.expected = NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG;
    gate.observed = NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.observed = NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG;
    CHECK(nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.fault_trigger_received = false;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.fault_trigger_received = true;
    gate.fault_injection_delivered = false;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.fault_injection_delivered = true;
    gate.supervisor_signal_received = true;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.supervisor_signal_received = false;
    gate.independent_failure = true;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.independent_failure = false;
    gate.worker_gone = false;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.worker_gone = true;
    gate.core_session_gone = false;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.core_session_gone = true;
    gate.console_restored = false;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
    gate.console_restored = true;
    gate.recovery_record_removed = false;
    CHECK(!nb_wsdisplay_session_core_failure_gate_passes(&gate));
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
    test_x11_socket_directory();
    test_sigterm_gate();
    test_core_failure_gate();
    test_frame_completion_across_vt_cycles();

    if (failures != 0) {
        fprintf(stderr, "wsdisplay session tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("wsdisplay session tests: ok");
    return 0;
}
