#ifndef NIXBENCH_WSDISPLAY_SESSION_H
#define NIXBENCH_WSDISPLAY_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_WSDISPLAY_SESSION_PATH_CAPACITY = 4096,
    NB_WSDISPLAY_SESSION_ERROR_CAPACITY = 256
};

enum nb_wsdisplay_session_action {
    NB_WSDISPLAY_SESSION_ACTION_RUN,
    NB_WSDISPLAY_SESSION_ACTION_HELP,
    NB_WSDISPLAY_SESSION_ACTION_PREFLIGHT,
    NB_WSDISPLAY_SESSION_ACTION_RECOVER
};

struct nb_wsdisplay_session_options {
    enum nb_wsdisplay_session_action action;
    const char *program_path;
    const char *core_path;
    bool acknowledge_console_takeover;
    bool require_supervisor_sigterm;
};

struct nb_wsdisplay_session_sigterm_gate {
    bool sigterm_received;
    bool sigterm_drove_shutdown;
    bool independent_failure;
    bool worker_gone;
    bool core_session_gone;
    bool console_restored;
    bool recovery_record_removed;
};

bool nb_wsdisplay_session_sigterm_gate_passes(
    const struct nb_wsdisplay_session_sigterm_gate *gate);

enum nb_wsdisplay_session_frame_completion {
    NB_WSDISPLAY_SESSION_FRAME_CURRENT,
    NB_WSDISPLAY_SESSION_FRAME_ABANDONED,
    NB_WSDISPLAY_SESSION_FRAME_INVALID
};

struct nb_wsdisplay_session_frame_state {
    uint64_t submitted_serial;
    uint64_t last_abandoned_serial;
    bool submitted;
};

void nb_wsdisplay_session_frame_state_init(
    struct nb_wsdisplay_session_frame_state *state);
bool nb_wsdisplay_session_frame_submitted(
    struct nb_wsdisplay_session_frame_state *state,
    uint64_t serial);
void nb_wsdisplay_session_frame_abandon(
    struct nb_wsdisplay_session_frame_state *state);
enum nb_wsdisplay_session_frame_completion
nb_wsdisplay_session_frame_completed(
    struct nb_wsdisplay_session_frame_state *state,
    uint64_t serial);

void nb_wsdisplay_session_options_init(
    struct nb_wsdisplay_session_options *options);
bool nb_wsdisplay_session_parse_options(
    int argc,
    char *argv[],
    struct nb_wsdisplay_session_options *options,
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY]);

/* Pure lexical helper. program_path and an explicit core_path must be absolute. */
bool nb_wsdisplay_session_derive_core_path(
    const char *program_path,
    const char *explicit_core_path,
    char destination[NB_WSDISPLAY_SESSION_PATH_CAPACITY],
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY]);

int nb_wsdisplay_session_run(
    const struct nb_wsdisplay_session_options *options);

#endif
