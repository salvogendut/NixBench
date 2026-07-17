#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "wsdisplay_session.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY],
                      const char *format,
                      ...)
{
    va_list arguments;

    if (error == NULL) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error,
                    NB_WSDISPLAY_SESSION_ERROR_CAPACITY,
                    format,
                    arguments);
    va_end(arguments);
}

static bool application_path_is_valid(const char *path)
{
    size_t length;
    size_t index;

    if (path == NULL) {
        return true;
    }
    length = strnlen(path, NB_WSDISPLAY_SESSION_PATH_CAPACITY);
    if (length < 2U || length >= NB_WSDISPLAY_SESSION_PATH_CAPACITY ||
        path[0] != '/' || path[length - 1U] == '/') {
        return false;
    }
    for (index = 0; index < length; ++index) {
        const unsigned char character = (unsigned char)path[index];

        if (character < 0x20U || character == 0x7fU) {
            return false;
        }
    }
    return true;
}

void nb_wsdisplay_session_options_init(
    struct nb_wsdisplay_session_options *options)
{
    if (options != NULL) {
        memset(options, 0, sizeof(*options));
        options->action = NB_WSDISPLAY_SESSION_ACTION_RUN;
    }
}

bool nb_wsdisplay_session_parse_options(
    int argc,
    char *argv[],
    struct nb_wsdisplay_session_options *options,
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY])
{
    struct nb_wsdisplay_session_options parsed;
    bool action_seen = false;
    bool core_seen = false;
    bool application_seen = false;
    bool acknowledge_seen = false;
    bool recovery_gate_seen = false;
    int index;

    if (options != NULL) {
        nb_wsdisplay_session_options_init(options);
    }
    if (error != NULL) {
        error[0] = '\0';
    }
    if (argc <= 0 || argv == NULL || argv[0] == NULL ||
        argv[0][0] == '\0' || options == NULL) {
        set_error(error, "invalid session command-line arguments");
        return false;
    }
    nb_wsdisplay_session_options_init(&parsed);
    parsed.program_path = argv[0];

    for (index = 1; index < argc; ++index) {
        enum nb_wsdisplay_session_action action;

        if (strcmp(argv[index], "--help") == 0) {
            action = NB_WSDISPLAY_SESSION_ACTION_HELP;
        } else if (strcmp(argv[index], "--preflight") == 0) {
            action = NB_WSDISPLAY_SESSION_ACTION_PREFLIGHT;
        } else if (strcmp(argv[index], "--recover") == 0) {
            action = NB_WSDISPLAY_SESSION_ACTION_RECOVER;
        } else if (strcmp(argv[index],
                          "--acknowledge-console-takeover") == 0) {
            if (acknowledge_seen) {
                set_error(error, "duplicate takeover acknowledgement");
                return false;
            }
            acknowledge_seen = true;
            parsed.acknowledge_console_takeover = true;
            continue;
        } else if (strcmp(argv[index],
                          "--require-supervisor-sigterm") == 0) {
            if (recovery_gate_seen) {
                set_error(error,
                          "select exactly one required recovery gate");
                return false;
            }
            recovery_gate_seen = true;
            parsed.require_supervisor_sigterm = true;
            continue;
        } else if (strcmp(argv[index], "--require-core-crash") == 0 ||
                   strcmp(argv[index], "--require-core-hang") == 0) {
            if (recovery_gate_seen) {
                set_error(error,
                          "select exactly one required recovery gate");
                return false;
            }
            recovery_gate_seen = true;
            parsed.required_core_failure =
                strcmp(argv[index], "--require-core-crash") == 0
                    ? NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                    : NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG;
            continue;
        } else if (strcmp(argv[index], "--core") == 0) {
            if (core_seen || index + 1 >= argc) {
                set_error(error, core_seen ? "duplicate --core"
                                           : "--core requires a path");
                return false;
            }
            parsed.core_path = argv[++index];
            if (parsed.core_path[0] != '/') {
                set_error(error, "--core must be an absolute path");
                return false;
            }
            core_seen = true;
            continue;
        } else if (strcmp(argv[index], "--application") == 0) {
            if (application_seen || index + 1 >= argc) {
                set_error(error,
                          application_seen
                              ? "duplicate --application"
                              : "--application requires a path");
                return false;
            }
            parsed.application_path = argv[++index];
            if (!application_path_is_valid(parsed.application_path)) {
                set_error(error,
                          "--application must be a bounded absolute "
                          "path without a trailing slash or control "
                          "characters");
                return false;
            }
            application_seen = true;
            continue;
        } else {
            set_error(error, "unknown option: %s", argv[index]);
            return false;
        }

        if (action_seen) {
            set_error(error, "select exactly one session action");
            return false;
        }
        parsed.action = action;
        action_seen = true;
    }

    if (action_seen &&
        (acknowledge_seen || core_seen || application_seen ||
         recovery_gate_seen)) {
        set_error(error,
                  "run-only options cannot be combined with an action");
        return false;
    }
    if (!action_seen && !acknowledge_seen) {
        set_error(error,
                  "a run requires --acknowledge-console-takeover");
        return false;
    }
    *options = parsed;
    return true;
}

bool nb_wsdisplay_session_sigterm_gate_passes(
    const struct nb_wsdisplay_session_sigterm_gate *gate)
{
    return gate != NULL &&
           gate->sigterm_received &&
           gate->sigterm_drove_shutdown &&
           !gate->independent_failure &&
           gate->worker_gone &&
           gate->core_session_gone &&
           gate->console_restored &&
           gate->recovery_record_removed;
}

bool nb_wsdisplay_session_core_failure_gate_passes(
    const struct nb_wsdisplay_session_core_failure_gate *gate)
{
    return gate != NULL &&
           (gate->expected == NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH ||
            gate->expected == NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG) &&
           gate->observed == gate->expected &&
           gate->fault_trigger_received &&
           gate->fault_injection_delivered &&
           !gate->supervisor_signal_received &&
           !gate->independent_failure &&
           gate->worker_gone &&
           gate->core_session_gone &&
           gate->console_restored &&
           gate->recovery_record_removed;
}

static bool absolute_path_is_valid(const char *path)
{
    return path != NULL && path[0] == '/' && path[1] != '\0' &&
           path[strlen(path) - 1] != '/';
}

bool nb_wsdisplay_session_derive_core_path(
    const char *program_path,
    const char *explicit_core_path,
    char destination[NB_WSDISPLAY_SESSION_PATH_CAPACITY],
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY])
{
    const char *slash;
    int length;

    if (destination != NULL) {
        destination[0] = '\0';
    }
    if (error != NULL) {
        error[0] = '\0';
    }
    if (destination == NULL || !absolute_path_is_valid(program_path)) {
        set_error(error, "session executable path must be absolute");
        return false;
    }
    if (explicit_core_path != NULL) {
        if (!absolute_path_is_valid(explicit_core_path)) {
            set_error(error, "explicit core path must be absolute");
            return false;
        }
        length = snprintf(destination,
                          NB_WSDISPLAY_SESSION_PATH_CAPACITY,
                          "%s",
                          explicit_core_path);
    } else {
        slash = strrchr(program_path, '/');
        if (slash == program_path) {
            length = snprintf(destination,
                              NB_WSDISPLAY_SESSION_PATH_CAPACITY,
                              "/nixbench-session-core");
        } else {
            const size_t directory_length = (size_t)(slash - program_path);

            if (directory_length >= NB_WSDISPLAY_SESSION_PATH_CAPACITY) {
                set_error(error, "session executable directory is too long");
                return false;
            }
            length = snprintf(destination,
                              NB_WSDISPLAY_SESSION_PATH_CAPACITY,
                              "%.*s/nixbench-session-core",
                              (int)directory_length,
                              program_path);
        }
    }
    if (length < 0 ||
        (size_t)length >= NB_WSDISPLAY_SESSION_PATH_CAPACITY) {
        destination[0] = '\0';
        set_error(error, "derived core path is too long");
        return false;
    }
    return true;
}

void nb_wsdisplay_session_frame_state_init(
    struct nb_wsdisplay_session_frame_state *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

bool nb_wsdisplay_session_frame_submitted(
    struct nb_wsdisplay_session_frame_state *state,
    uint64_t serial)
{
    if (state == NULL || state->submitted || serial == 0 ||
        serial <= state->last_abandoned_serial) {
        return false;
    }
    state->submitted = true;
    state->submitted_serial = serial;
    return true;
}

void nb_wsdisplay_session_frame_abandon(
    struct nb_wsdisplay_session_frame_state *state)
{
    if (state != NULL && state->submitted) {
        state->last_abandoned_serial = state->submitted_serial;
        state->submitted = false;
        state->submitted_serial = 0;
    }
}

enum nb_wsdisplay_session_frame_completion
nb_wsdisplay_session_frame_completed(
    struct nb_wsdisplay_session_frame_state *state,
    uint64_t serial)
{
    if (state == NULL || serial == 0) {
        return NB_WSDISPLAY_SESSION_FRAME_INVALID;
    }
    if (serial <= state->last_abandoned_serial) {
        return NB_WSDISPLAY_SESSION_FRAME_ABANDONED;
    }
    if (!state->submitted || serial != state->submitted_serial) {
        return NB_WSDISPLAY_SESSION_FRAME_INVALID;
    }
    state->submitted = false;
    state->submitted_serial = 0;
    return NB_WSDISPLAY_SESSION_FRAME_CURRENT;
}

#if !defined(__NetBSD__)

int nb_wsdisplay_session_run(
    const struct nb_wsdisplay_session_options *options)
{
    (void)options;
    fputs("nixbench-wsdisplay-session: NetBSD wsdisplay is unavailable\n",
          stderr);
    return 1;
}

#else

#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "host_wsdisplay.h"
#include "privsep_helper.h"
#include "session_console_shortcuts.h"
#include "session_credentials.h"
#include "session_runtime_sentinel.h"
#include "session_watchdog.h"
#include "wscons_input.h"
#include "wsdisplay_console_session.h"
#include "wsdisplay_recovery.h"

enum {
    NB_SESSION_ACK_FLUSH_TIMEOUT_MS = 2000,
    NB_SESSION_CORE_ORDERLY_GRACE_MS = 4000,
    NB_SESSION_CORE_SIGNAL_GRACE_MS = 2000,
    NB_SESSION_RUNTIME_SENTINEL_TIMEOUT_MS = 3000,
    NB_SESSION_RUNTIME_SENTINEL_SIGNAL_MS = 2000,
    NB_SESSION_RUNTIME_SENTINEL_KILL_MS = 2000,
    NB_SESSION_WORKER_ORDERLY_GRACE_MS =
        NB_SESSION_ACK_FLUSH_TIMEOUT_MS +
        NB_SESSION_CORE_ORDERLY_GRACE_MS +
        4 * NB_SESSION_CORE_SIGNAL_GRACE_MS +
        2 * NB_SESSION_RUNTIME_SENTINEL_TIMEOUT_MS +
        NB_SESSION_RUNTIME_SENTINEL_SIGNAL_MS +
        NB_SESSION_RUNTIME_SENTINEL_KILL_MS + 1000,
    NB_SESSION_WORKER_SIGNAL_GRACE_MS = 2000,
    NB_SESSION_FAULT_GATE_TIMEOUT_MS =
        NB_SESSION_WATCHDOG_PING_INTERVAL_MS +
        NB_SESSION_WATCHDOG_PONG_TIMEOUT_MS +
        NB_SESSION_WORKER_ORDERLY_GRACE_MS,
    NB_SESSION_CORE_REPORT_TIMEOUT_MS =
        NB_SESSION_WATCHDOG_STARTUP_GRACE_MS + 10000,
    NB_SESSION_WAIT_SLICE_MS = 20,
    NB_SESSION_IO_BATCH = 64,
    NB_SESSION_WORKER_CLEANUP_INCOMPLETE_EXIT = 2,
    NB_SESSION_WORKER_CORE_CRASH_EXIT = 3,
    NB_SESSION_WORKER_CORE_HANG_EXIT = 4
};

enum nb_session_fault_command {
    NB_SESSION_FAULT_COMMAND_CRASH = 1,
    NB_SESSION_FAULT_COMMAND_HANG = 2
};

static const char status_device[] = "/dev/ttyEstat";
static const char screen_prefix[] = "/dev/ttyE";
static const char recovery_path[] =
    "/var/run/nixbench-wsdisplay-session.state";
static const char session_lock_path[] =
    "/var/run/nixbench-wsdisplay-session.lock";
static const struct nb_wsdisplay_recovery_options recovery_options = {
    .record_path = recovery_path,
    .status_device_path = status_device,
    .screen_device_prefix = screen_prefix,
    .record_owner = 0
};

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return 0;
    }
    if ((uint64_t)now.tv_sec > UINT64_MAX / UINT64_C(1000)) {
        return UINT64_MAX;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static uint64_t add_milliseconds(uint64_t start, uint64_t duration)
{
    return duration > UINT64_MAX - start ? UINT64_MAX : start + duration;
}

static void sleep_milliseconds(unsigned int milliseconds)
{
    struct timespec request;

    request.tv_sec = (time_t)(milliseconds / 1000U);
    request.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

static bool write_all(int descriptor, const void *data, size_t size)
{
    const unsigned char *bytes = data;

    while (size != 0) {
        const ssize_t written = write(descriptor, bytes, size);

        if (written > 0) {
            bytes += (size_t)written;
            size -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool capture_console(struct nb_wsdisplay_console_state *state)
{
    const struct nb_wsdisplay_console_capture_options options = {
        .status_device_path = status_device,
        .screen_device_prefix = screen_prefix
    };

    return nb_wsdisplay_console_capture(&options, state);
}

static void print_state(const struct nb_wsdisplay_console_state *state)
{
    printf("Active wsdisplay screen: %d (%s)\n",
           state->active_screen,
           state->screen_device);
    printf("Display mode: %u (emulation)\n", state->display_mode);
    printf("VT mode: %d (automatic)\n", state->vt_mode.mode);
    if (state->video_available) {
        printf("Video state: %u\n", state->video);
    }
}

static bool set_cloexec(int descriptor, bool enabled)
{
    const int flags = fcntl(descriptor, F_GETFD);

    return flags >= 0 &&
           fcntl(descriptor,
                 F_SETFD,
                 enabled ? flags | FD_CLOEXEC : flags & ~FD_CLOEXEC) == 0;
}

static int acquire_session_lock(void)
{
    struct stat status;
    int flags = O_RDWR | O_CREAT;
    int descriptor;
    int saved_error;

#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    descriptor = open(session_lock_path,
                      flags,
                      S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        fprintf(stderr,
                "Could not open session lock %s: %s\n",
                session_lock_path,
                strerror(errno));
        return -1;
    }
    errno = 0;
    if (!set_cloexec(descriptor, true) ||
        fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_uid != 0 ||
        status.st_nlink != 1 ||
        (status.st_mode & (S_IRWXG | S_IRWXO)) != 0 ||
        (status.st_mode & S_IRWXU) != (S_IRUSR | S_IWUSR)) {
        saved_error = errno != 0 ? errno : EPERM;
        (void)close(descriptor);
        errno = saved_error;
        fprintf(stderr,
                "Session lock %s is not a root-owned 0600 regular file: "
                "%s\n",
                session_lock_path,
                strerror(errno));
        return -1;
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        saved_error = errno;
        (void)close(descriptor);
        errno = saved_error;
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            fputs("Another NixBench wsdisplay session or recovery command "
                  "is active\n",
                  stderr);
        } else {
            fprintf(stderr,
                    "Could not lock %s: %s\n",
                    session_lock_path,
                    strerror(errno));
        }
        return -1;
    }
    return descriptor;
}

static bool set_nonblocking(int descriptor)
{
    const int flags = fcntl(descriptor, F_GETFL);

    return flags >= 0 &&
           fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool resolve_program_paths(
    const struct nb_wsdisplay_session_options *options,
    char core[NB_WSDISPLAY_SESSION_PATH_CAPACITY])
{
    char program[NB_WSDISPLAY_SESSION_PATH_CAPACITY];
    char candidate[NB_WSDISPLAY_SESSION_PATH_CAPACITY];
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY];
    struct stat status;

    error[0] = '\0';
    errno = 0;
    if (realpath(options->program_path, program) == NULL ||
        !nb_wsdisplay_session_derive_core_path(program,
                                               options->core_path,
                                               candidate,
                                               error) ||
        realpath(candidate, core) == NULL ||
        stat(core, &status) != 0 || !S_ISREG(status.st_mode)) {
        fprintf(stderr,
                "Could not resolve the session core: %s\n",
                errno != 0 ? strerror(errno) : error);
        return false;
    }
    return true;
}

struct worker_context {
    struct nb_host *host;
    struct nb_wscons_input *input;
    struct nb_privsep_helper *helper;
    int socket_fd;
    int fault_fd;
    pid_t core_pid;
    struct nb_wsdisplay_session_frame_state frame;
    struct nb_session_console_shortcuts console_shortcuts;
    bool emergency_shutdown_requested;
    uint64_t submitted_generation;
    uint64_t release_completions;
    uint64_t acquire_completions;
};

static bool convert_output(const struct nb_host_output *host,
                           struct nb_privsep_output *output)
{
    uint64_t stride;
    uint64_t bytes;

    if (!nb_host_output_is_valid(host)) {
        return false;
    }
    stride = (uint64_t)(unsigned int)host->pixel_width * UINT64_C(4);
    bytes = stride * (uint64_t)(unsigned int)host->pixel_height;
    if (stride > UINT32_MAX || bytes > UINT32_MAX) {
        return false;
    }
    memset(output, 0, sizeof(*output));
    output->logical_width = (uint32_t)host->logical_width;
    output->logical_height = (uint32_t)host->logical_height;
    output->pixel_width = (uint32_t)host->pixel_width;
    output->pixel_height = (uint32_t)host->pixel_height;
    output->refresh_millihertz = (uint32_t)host->refresh_millihertz;
    output->stride = (uint32_t)stride;
    output->frame_bytes = (uint32_t)bytes;
    output->format = NB_PRIVSEP_PIXEL_FORMAT_XRGB8888;
    return nb_privsep_output_is_valid(output);
}

static bool present_frame(void *opaque,
                          uint64_t generation,
                          const struct nb_host_frame *frame)
{
    struct worker_context *worker = opaque;

    if (worker->frame.submitted ||
        nb_host_present(worker->host, frame) != NB_HOST_RESULT_OK) {
        return false;
    }
    if (!nb_wsdisplay_session_frame_submitted(&worker->frame,
                                              frame->serial)) {
        return false;
    }
    worker->submitted_generation = generation;
    return true;
}

static void print_helper_error(const struct nb_privsep_helper *helper)
{
    enum nb_privsep_helper_error error;
    uint32_t system_error;
    char message[NB_PRIVSEP_HELPER_ERROR_CAPACITY];

    if (nb_privsep_helper_get_last_error(helper,
                                         &error,
                                         &system_error,
                                         message,
                                         sizeof(message))) {
        fprintf(stderr,
                "Privileged session protocol failed: %s%s%s\n",
                message,
                system_error != 0 ? ": " : "",
                system_error != 0 ? strerror((int)system_error) : "");
    }
}

static bool flush_helper(struct worker_context *worker)
{
    for (;;) {
        const unsigned char *bytes;
        size_t size;
        ssize_t written;

        if (!nb_privsep_helper_peek_outbound(worker->helper,
                                             &bytes,
                                             &size)) {
            return false;
        }
        if (size == 0) {
            return true;
        }
        written = write(worker->socket_fd, bytes, size);
        if (written > 0) {
            if (!nb_privsep_helper_consume_outbound(worker->helper,
                                                    (size_t)written)) {
                return false;
            }
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else if (written < 0 &&
                   (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        } else {
            return false;
        }
    }
}

static bool drain_core(struct worker_context *worker, bool *eof)
{
    unsigned char buffer[8192];
    size_t batch;

    *eof = false;
    for (batch = 0; batch < NB_SESSION_IO_BATCH; ++batch) {
        const ssize_t count = read(worker->socket_fd,
                                   buffer,
                                   sizeof(buffer));

        if (count > 0) {
            size_t consumed = 0;

            if (!nb_privsep_helper_feed(worker->helper,
                                        buffer,
                                        (size_t)count,
                                        &consumed) ||
                consumed != (size_t)count) {
                print_helper_error(worker->helper);
                return false;
            }
        } else if (count == 0) {
            *eof = true;
            return true;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        } else {
            return false;
        }
    }
    return true;
}

static bool take_fault_command(struct worker_context *worker,
                               bool *received,
                               enum nb_session_fault_command *command)
{
    unsigned char value;
    ssize_t count;

    *received = false;
    do {
        count = read(worker->fault_fd, &value, sizeof(value));
    } while (count < 0 && errno == EINTR);
    if (count == 0) {
        errno = EPIPE;
        return false;
    }
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return true;
    }
    if (count != (ssize_t)sizeof(value) ||
        (value != NB_SESSION_FAULT_COMMAND_CRASH &&
         value != NB_SESSION_FAULT_COMMAND_HANG)) {
        return false;
    }
    *received = true;
    *command = (enum nb_session_fault_command)value;
    return true;
}

static bool drain_input(struct worker_context *worker)
{
    size_t batch;

    if (!nb_wscons_input_is_active(worker->input)) {
        return true;
    }
    for (batch = 0; batch < NB_SESSION_IO_BATCH; ++batch) {
        struct nb_host_event event;
        struct nb_session_console_shortcut shortcut;
        const enum nb_host_event_status status =
            nb_wscons_input_poll(worker->input, &event);

        if (status == NB_HOST_EVENT_STATUS_EMPTY) {
            return true;
        }
        if (status == NB_HOST_EVENT_STATUS_ERROR) {
            return false;
        }
        shortcut = nb_session_console_shortcuts_apply(
            &worker->console_shortcuts,
            &event);
        if (shortcut.type ==
            NB_SESSION_CONSOLE_SHORTCUT_EMERGENCY_SHUTDOWN) {
            worker->emergency_shutdown_requested = true;
            nb_wscons_input_suspend(worker->input);
            return true;
        }
        if (shortcut.type == NB_SESSION_CONSOLE_SHORTCUT_SWITCH_VT) {
            const enum nb_host_result result =
                nb_host_wsdisplay_request_vt_switch(worker->host,
                                                    shortcut.vt_number);
            const int switch_error = errno;

            if (result == NB_HOST_RESULT_OK) {
                printf("Console Ctrl+Alt+F%d requested VT %d.\n",
                       shortcut.vt_number,
                       shortcut.vt_number);
                (void)fflush(stdout);
                return true;
            }
            fprintf(stderr,
                    "Could not switch to VT %d: %s\n",
                    shortcut.vt_number,
                    switch_error != 0 ? strerror(switch_error)
                                      : "request was rejected");
            continue;
        }
        if (shortcut.consumed) {
            continue;
        }
        if (!nb_privsep_helper_send_input(worker->helper, &event)) {
            return false;
        }
    }
    return true;
}

static bool resume_input_and_helper(struct worker_context *worker)
{
    struct nb_host_output host_output;
    struct nb_privsep_output output;

    nb_session_console_shortcuts_reset(&worker->console_shortcuts);
    if (!nb_host_get_output(worker->host, &host_output) ||
        !convert_output(&host_output, &output) ||
        !nb_wscons_input_set_bounds(worker->input,
                                    host_output.logical_width,
                                    host_output.logical_height) ||
        !nb_wscons_input_resume(worker->input) ||
        !nb_privsep_helper_resume(worker->helper, &output)) {
        return false;
    }
    return true;
}

static bool handle_host_event(struct worker_context *worker,
                              const struct nb_host_event *event)
{
    switch (event->type) {
    case NB_HOST_EVENT_FRAME_COMPLETE:
    {
        const uint64_t serial =
            event->data.frame_complete.frame_serial;
        const enum nb_wsdisplay_session_frame_completion completion =
            nb_wsdisplay_session_frame_completed(&worker->frame, serial);

        if (completion == NB_WSDISPLAY_SESSION_FRAME_ABANDONED) {
            return true;
        }
        if (completion != NB_WSDISPLAY_SESSION_FRAME_CURRENT ||
            !nb_privsep_helper_complete_frame(worker->helper,
                                              worker->submitted_generation,
                                              serial,
                                              event->milliseconds)) {
            return false;
        }
        return true;
    }
    case NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED:
        nb_wscons_input_suspend(worker->input);
        nb_session_console_shortcuts_reset(&worker->console_shortcuts);
        nb_wsdisplay_session_frame_abandon(&worker->frame);
        if (nb_host_complete_console_release(worker->host) !=
                NB_HOST_RESULT_OK ||
            !nb_privsep_helper_suspend(worker->helper,
                                       event->milliseconds)) {
            return false;
        }
        ++worker->release_completions;
        return true;
    case NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED:
        if (nb_host_complete_console_acquire(worker->host) !=
                NB_HOST_RESULT_OK ||
            !resume_input_and_helper(worker)) {
            return false;
        }
        ++worker->acquire_completions;
        return true;
    case NB_HOST_EVENT_OUTPUT_CHANGED:
        return true;
    case NB_HOST_EVENT_QUIT:
    case NB_HOST_EVENT_FAILED:
        return false;
    default:
        return true;
    }
}

static bool wait_for_worker_event(
    struct worker_context *worker,
    const struct nb_session_watchdog *watchdog)
{
    int descriptors[2 + NB_WSCONS_INPUT_WAIT_DESCRIPTOR_COUNT];
    size_t count = 2;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;
    enum nb_host_event_status status;
    const uint32_t timeout =
        nb_privsep_helper_outbound_size(worker->helper) != 0
            ? NB_SESSION_WAIT_SLICE_MS
            : nb_session_watchdog_wait_timeout(
                  watchdog,
                  monotonic_milliseconds());

    descriptors[0] = worker->socket_fd;
    descriptors[1] = worker->fault_fd;
    if (nb_wscons_input_is_active(worker->input)) {
        if (!nb_wscons_input_get_wait_descriptors(worker->input,
                                                   descriptors + 2)) {
            return false;
        }
        count += NB_WSCONS_INPUT_WAIT_DESCRIPTOR_COUNT;
    }
    status = nb_host_wsdisplay_wait_event_with_descriptors(
        worker->host,
        descriptors,
        count,
        timeout,
        &event,
        &result);
    if (status == NB_HOST_EVENT_STATUS_ERROR) {
        return false;
    }
    return status != NB_HOST_EVENT_STATUS_AVAILABLE ||
           handle_host_event(worker, &event);
}

static bool wait_child(pid_t child,
                       uint64_t deadline,
                       int *status)
{
    for (;;) {
        const pid_t result = waitpid(child, status, WNOHANG);

        if (result == child) {
            return true;
        }
        if (result < 0 && errno != EINTR) {
            return false;
        }
        if (monotonic_milliseconds() >= deadline) {
            return false;
        }
        sleep_milliseconds(NB_SESSION_WAIT_SLICE_MS);
    }
}

static void signal_core_session(pid_t core, int signal_number)
{
    if (core <= 0) {
        return;
    }
    if (kill(-core, signal_number) != 0 && errno != ESRCH) {
        fprintf(stderr,
                "Could not signal core process group %ld: %s\n",
                (long)core,
                strerror(errno));
    }
    if (kill(core, signal_number) != 0 && errno != ESRCH) {
        fprintf(stderr,
                "Could not signal core process %ld: %s\n",
                (long)core,
                strerror(errno));
    }
}

static bool core_process_group_is_gone(pid_t core)
{
    return core <= 0 || (kill(-core, 0) != 0 && errno == ESRCH);
}

static bool wait_core_process_group_gone(pid_t core, uint64_t deadline)
{
    while (!core_process_group_is_gone(core)) {
        if (monotonic_milliseconds() >= deadline) {
            return false;
        }
        sleep_milliseconds(NB_SESSION_WAIT_SLICE_MS);
    }
    return true;
}

static void signal_core_process_group(pid_t core, int signal_number)
{
    if (core > 0 && kill(-core, signal_number) != 0 && errno != ESRCH) {
        fprintf(stderr,
                "Could not signal core process group %ld: %s\n",
                (long)core,
                strerror(errno));
    }
}

static bool stop_reaped_core_process_group(pid_t core)
{
    uint64_t deadline;

    if (core_process_group_is_gone(core)) {
        return true;
    }
    signal_core_process_group(core, SIGTERM);
    signal_core_process_group(core, SIGCONT);
    deadline = add_milliseconds(monotonic_milliseconds(),
                                NB_SESSION_CORE_SIGNAL_GRACE_MS);
    if (wait_core_process_group_gone(core, deadline)) {
        return true;
    }
    signal_core_process_group(core, SIGKILL);
    deadline = add_milliseconds(monotonic_milliseconds(),
                                NB_SESSION_CORE_SIGNAL_GRACE_MS);
    return wait_core_process_group_gone(core, deadline);
}

static bool stop_core(pid_t child,
                      bool orderly,
                      int *status,
                      bool *cleanup_complete)
{
    bool reaped = false;

    if (orderly) {
        reaped = wait_child(
            child,
            add_milliseconds(monotonic_milliseconds(),
                             NB_SESSION_CORE_ORDERLY_GRACE_MS),
            status);
    }

    if (!reaped) {
        signal_core_session(child, SIGTERM);
        signal_core_session(child, SIGCONT);
        reaped = wait_child(
            child,
            add_milliseconds(monotonic_milliseconds(),
                             NB_SESSION_CORE_SIGNAL_GRACE_MS),
            status);
    }
    if (!reaped) {
        signal_core_session(child, SIGKILL);
        reaped = wait_child(
            child,
            add_milliseconds(monotonic_milliseconds(),
                             NB_SESSION_CORE_SIGNAL_GRACE_MS),
            status);
    }
    *cleanup_complete = reaped &&
                        stop_reaped_core_process_group(child);
    return *cleanup_complete &&
           (!orderly ||
            (WIFEXITED(*status) && WEXITSTATUS(*status) == 0));
}

static bool contain_crashed_core(pid_t child,
                                 int *status,
                                 bool *cleanup_complete)
{
    bool reaped;

    reaped = wait_child(
        child,
        add_milliseconds(monotonic_milliseconds(),
                         NB_SESSION_CORE_SIGNAL_GRACE_MS),
        status);
    if (!reaped) {
        if (kill(child, SIGKILL) != 0 && errno != ESRCH) {
            fprintf(stderr,
                    "Could not re-signal crashed core %ld: %s\n",
                    (long)child,
                    strerror(errno));
        }
        reaped = wait_child(
            child,
            add_milliseconds(monotonic_milliseconds(),
                             NB_SESSION_CORE_SIGNAL_GRACE_MS),
            status);
    }
    *cleanup_complete = reaped &&
                        stop_reaped_core_process_group(child);
    return *cleanup_complete && WIFSIGNALED(*status) &&
           WTERMSIG(*status) == SIGKILL;
}

static void signal_runtime_sentinel(pid_t child, int signal_number)
{
    if (child > 0 && kill(child, signal_number) != 0 && errno != ESRCH) {
        fprintf(stderr,
                "Could not signal runtime sentinel %ld: %s\n",
                (long)child,
                strerror(errno));
    }
}

static bool finish_runtime_sentinel(pid_t child,
                                    int *controller_fd,
                                    bool ready,
                                    bool core_session_gone,
                                    bool *reaped,
                                    int *status)
{
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY];
    bool cleanup_reported = !ready;
    bool abort_cleanup = !core_session_gone;
    bool forced = false;

    if (child <= 0 || controller_fd == NULL || reaped == NULL ||
        status == NULL) {
        return child <= 0;
    }
    if (*reaped) {
        if (*controller_fd >= 0) {
            (void)close(*controller_fd);
            *controller_fd = -1;
        }
        return false;
    }
    if (!core_session_gone) {
        forced = true;
        signal_runtime_sentinel(child, SIGKILL);
    } else {
        if (ready && *controller_fd >= 0) {
            cleanup_reported =
                nb_session_runtime_sentinel_request_cleanup(
                    *controller_fd,
                    NB_SESSION_RUNTIME_SENTINEL_TIMEOUT_MS,
                    error);
            if (!cleanup_reported) {
                fprintf(stderr,
                        "Runtime sentinel cleanup failed: %s\n",
                        error[0] != '\0' ? error : "no CLEANED response");
            }
        }
        if (*controller_fd >= 0) {
            (void)close(*controller_fd);
            *controller_fd = -1;
        }
    }

    *reaped = wait_child(
        child,
        add_milliseconds(monotonic_milliseconds(),
                         forced ? NB_SESSION_RUNTIME_SENTINEL_KILL_MS
                                : NB_SESSION_RUNTIME_SENTINEL_TIMEOUT_MS),
        status);
    if (!*reaped) {
        forced = true;
        if (!abort_cleanup) {
            signal_runtime_sentinel(child, SIGTERM);
            signal_runtime_sentinel(child, SIGCONT);
            *reaped = wait_child(
                child,
                add_milliseconds(monotonic_milliseconds(),
                                 NB_SESSION_RUNTIME_SENTINEL_SIGNAL_MS),
                status);
        }
        if (!*reaped) {
            signal_runtime_sentinel(child, SIGKILL);
            *reaped = wait_child(
                child,
                add_milliseconds(monotonic_milliseconds(),
                                 NB_SESSION_RUNTIME_SENTINEL_KILL_MS),
                status);
        }
    }
    if (*controller_fd >= 0) {
        (void)close(*controller_fd);
        *controller_fd = -1;
    }
    if (!*reaped) {
        fprintf(stderr,
                "Runtime sentinel %ld could not be reaped\n",
                (long)child);
        return false;
    }
    return !forced && cleanup_reported && WIFEXITED(*status) &&
           WEXITSTATUS(*status) == 0;
}

static bool core_credentials(pid_t pid,
                             const struct nb_session_credentials *credentials,
                             struct nb_privsep_credentials *expected)
{
    const uint32_t wire_pid = (uint32_t)pid;
    const uint32_t wire_uid = (uint32_t)credentials->uid;
    const uint32_t wire_gid = (uint32_t)credentials->gid;

    if (pid <= 0 || (pid_t)wire_pid != pid ||
        (uid_t)wire_uid != credentials->uid ||
        (gid_t)wire_gid != credentials->gid) {
        return false;
    }
    memset(expected, 0, sizeof(*expected));
    expected->process_id = wire_pid;
    expected->real_user_id = wire_uid;
    expected->effective_user_id = wire_uid;
    expected->saved_user_id = wire_uid;
    expected->real_group_id = wire_gid;
    expected->effective_group_id = wire_gid;
    expected->saved_group_id = wire_gid;
    return true;
}

static bool block_worker_termination(sigset_t *previous)
{
    sigset_t signals;

    return sigemptyset(&signals) == 0 &&
           sigaddset(&signals, SIGINT) == 0 &&
           sigaddset(&signals, SIGTERM) == 0 &&
           sigaddset(&signals, SIGHUP) == 0 &&
           sigaddset(&signals, SIGQUIT) == 0 &&
           sigprocmask(SIG_BLOCK, &signals, previous) == 0;
}

static bool ignore_worker_sigpipe(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_IGN;
    return sigemptyset(&action.sa_mask) == 0 &&
           sigaction(SIGPIPE, &action, NULL) == 0;
}

static int run_device_worker(
    const struct nb_wsdisplay_console_state *saved,
    const struct nb_session_credentials *credentials,
    const char *core_path,
    const char *application_path,
    int core_report_fd,
    int fault_fd,
    enum nb_wsdisplay_session_core_failure required_core_failure,
    int session_lock_fd)
{
    struct nb_host_wsdisplay_options host_options;
    struct nb_host_output host_output;
    struct nb_privsep_output output;
    struct nb_privsep_credentials expected;
    struct nb_privsep_helper_options helper_options;
    struct nb_session_watchdog watchdog;
    struct worker_context worker;
    int sockets[2] = {-1, -1};
    int sentinel_sockets[2] = {-1, -1};
    int sentinel_fd = -1;
    sigset_t previous_mask;
    char runtime_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY] = {0};
    char runtime_error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY];
    char *sentinel_argv[] = {
        (char *)core_path,
        "--runtime-sentinel",
        "--ipc-fd",
        "3",
        NULL
    };
    char *core_argv[8];
    size_t core_argc = 0;
    bool signals_blocked = false;
    bool success = false;
    bool orderly = false;
    bool core_reported = false;
    bool core_reaped = false;
    bool core_cleanup_complete = true;
    bool sentinel_ready = false;
    bool sentinel_reaped = false;
    bool runtime_cleanup_complete = true;
    bool crash_containment = false;
    bool heartbeat_expired = false;
    enum nb_wsdisplay_session_core_failure injected_core_failure =
        NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE;
    int core_status = 0;
    int sentinel_status = 0;
    pid_t sentinel_pid = -1;
    int one = 1;

    memset(&worker, 0, sizeof(worker));
    worker.socket_fd = -1;
    worker.fault_fd = fault_fd;
    core_argv[core_argc++] = (char *)core_path;
    core_argv[core_argc++] = "--ipc-fd";
    core_argv[core_argc++] = "3";
    if (application_path != NULL) {
        core_argv[core_argc++] = "--launch";
        core_argv[core_argc++] = (char *)application_path;
    }
    core_argv[core_argc++] = "--runtime-dir";
    core_argv[core_argc++] = runtime_path;
    core_argv[core_argc] = NULL;
    nb_wsdisplay_session_frame_state_init(&worker.frame);
    nb_session_console_shortcuts_reset(&worker.console_shortcuts);
    if (!block_worker_termination(&previous_mask)) {
        goto cleanup;
    }
    signals_blocked = true;
    if (!ignore_worker_sigpipe()) {
        goto cleanup;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sentinel_sockets) != 0 ||
        !set_cloexec(sentinel_sockets[0], true) ||
        !set_cloexec(sentinel_sockets[1], true) ||
        setsockopt(sentinel_sockets[0],
                   SOL_SOCKET,
                   SO_NOSIGPIPE,
                   &one,
                   sizeof(one)) != 0 ||
        setsockopt(sentinel_sockets[1],
                   SOL_SOCKET,
                   SO_NOSIGPIPE,
                   &one,
                   sizeof(one)) != 0) {
        goto cleanup;
    }
    sentinel_pid = fork();
    if (sentinel_pid < 0) {
        goto cleanup;
    }
    if (sentinel_pid == 0) {
        (void)close(sentinel_sockets[0]);
        (void)close(core_report_fd);
        (void)close(fault_fd);
        (void)close(session_lock_fd);
        nb_session_credentials_drop_and_exec(credentials,
                                             sentinel_sockets[1],
                                             core_path,
                                             sentinel_argv);
    }
    runtime_cleanup_complete = false;
    (void)close(sentinel_sockets[1]);
    sentinel_sockets[1] = -1;
    sentinel_fd = sentinel_sockets[0];
    sentinel_sockets[0] = -1;
    if (!nb_session_runtime_sentinel_wait_ready(
            sentinel_fd,
            NB_SESSION_RUNTIME_SENTINEL_TIMEOUT_MS,
            runtime_path,
            runtime_error)) {
        fprintf(stderr,
                "Runtime sentinel startup failed: %s\n",
                runtime_error[0] != '\0' ? runtime_error
                                         : "no READY response");
        goto cleanup;
    }
    sentinel_ready = true;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0 ||
        !set_cloexec(sockets[0], true) ||
        !set_cloexec(sockets[1], true) ||
        setsockopt(sockets[0],
                   SOL_SOCKET,
                   SO_NOSIGPIPE,
                   &one,
                   sizeof(one)) != 0 ||
        setsockopt(sockets[1],
                   SOL_SOCKET,
                   SO_NOSIGPIPE,
                   &one,
                   sizeof(one)) != 0) {
        goto cleanup;
    }
    worker.core_pid = fork();
    if (worker.core_pid < 0) {
        goto cleanup;
    }
    if (worker.core_pid == 0) {
        (void)close(sockets[0]);
        (void)close(sentinel_fd);
        (void)close(core_report_fd);
        (void)close(session_lock_fd);
        nb_session_credentials_drop_and_exec(credentials,
                                             sockets[1],
                                             core_path,
                                             core_argv);
    }
    (void)close(sockets[1]);
    sockets[1] = -1;
    worker.socket_fd = sockets[0];
    sockets[0] = -1;
    if (!set_nonblocking(worker.socket_fd) ||
        !set_nonblocking(worker.fault_fd)) {
        goto cleanup;
    }

    nb_host_wsdisplay_options_init(&host_options);
    host_options.device_path = saved->screen_device;
    host_options.expected_active_vt = saved->active_screen + 1;
    worker.host = nb_host_wsdisplay_create(&host_options);
    if (worker.host == NULL ||
        !nb_host_get_output(worker.host, &host_output) ||
        !convert_output(&host_output, &output) ||
        !core_credentials(worker.core_pid, credentials, &expected)) {
        goto cleanup;
    }
    nb_privsep_helper_options_init(&helper_options);
    helper_options.expected_credentials = expected;
    helper_options.output = output;
    helper_options.present = present_frame;
    helper_options.present_data = &worker;
    worker.helper = nb_privsep_helper_create(&helper_options);
    worker.input = nb_wscons_input_create(host_output.logical_width,
                                          host_output.logical_height);
    if (worker.helper == NULL || worker.input == NULL ||
        !nb_wscons_input_set_pointer_profile(
            worker.input,
            NB_WSCONS_POINTER_PROFILE_ADAPTIVE) ||
        !nb_wscons_input_resume(worker.input)) {
        goto cleanup;
    }
    if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) != 0) {
        goto cleanup;
    }
    signals_blocked = false;

    nb_session_watchdog_init(&watchdog, monotonic_milliseconds());
    for (;;) {
        bool eof;
        uint64_t now;
        uint64_t pong;
        uint64_t ping_token;
        uint64_t shutdown_id;
        enum nb_session_watchdog_action watchdog_action;
        pid_t waited;
        bool fault_received;
        enum nb_session_fault_command fault_command;

        waited = waitpid(sentinel_pid, &sentinel_status, WNOHANG);
        if (waited == sentinel_pid) {
            sentinel_reaped = true;
            break;
        }
        if (waited < 0 && errno != EINTR) {
            break;
        }
        waited = waitpid(worker.core_pid, &core_status, WNOHANG);
        if (waited == worker.core_pid) {
            core_reaped = true;
            break;
        }
        if (waited < 0 && errno != EINTR) {
            break;
        }
        if (!take_fault_command(&worker,
                                &fault_received,
                                &fault_command)) {
            break;
        }
        if (fault_received) {
            const enum nb_wsdisplay_session_core_failure requested =
                fault_command == NB_SESSION_FAULT_COMMAND_CRASH
                    ? NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                    : NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG;
            const int signal_number =
                requested == NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                    ? SIGKILL
                    : SIGSTOP;

            if (injected_core_failure !=
                    NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE ||
                requested != required_core_failure ||
                kill(worker.core_pid, signal_number) != 0) {
                break;
            }
            injected_core_failure = requested;
            printf("Device worker injected the required core %s.\n",
                   requested == NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                       ? "crash"
                       : "hang");
            (void)fflush(stdout);
            if (requested == NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH) {
                crash_containment = true;
                break;
            }
        }
        if (!drain_core(&worker, &eof) || eof) {
            break;
        }
        now = monotonic_milliseconds();
        if (nb_privsep_helper_is_ready(worker.helper) &&
            !nb_session_watchdog_note_ready(&watchdog, now)) {
            break;
        }
        if (!core_reported &&
            nb_privsep_helper_is_ready(worker.helper)) {
            if (!write_all(core_report_fd,
                           &worker.core_pid,
                           sizeof(worker.core_pid))) {
                break;
            }
            (void)close(core_report_fd);
            core_report_fd = -1;
            core_reported = true;
        }
        if (nb_privsep_helper_shutdown_requested(worker.helper,
                                                 &shutdown_id)) {
            const uint64_t deadline = add_milliseconds(
                monotonic_milliseconds(),
                NB_SESSION_ACK_FLUSH_TIMEOUT_MS);

            (void)shutdown_id;
            orderly = true;
            nb_wscons_input_suspend(worker.input);
            for (;;) {
                if (!flush_helper(&worker)) {
                    break;
                }
                if (nb_privsep_helper_outbound_size(worker.helper) == 0) {
                    success = true;
                    break;
                }
                waited = waitpid(worker.core_pid,
                                 &core_status,
                                 WNOHANG);
                if (waited == worker.core_pid) {
                    core_reaped = true;
                    break;
                }
                if ((waited < 0 && errno != EINTR) ||
                    monotonic_milliseconds() >= deadline) {
                    break;
                }
                sleep_milliseconds(NB_SESSION_WAIT_SLICE_MS);
            }
            break;
        }
        if (!drain_input(&worker)) {
            break;
        }
        if (worker.emergency_shutdown_requested) {
            success = true;
            puts("Emergency Ctrl+Alt+Backspace received; terminating the "
                 "desktop session and restoring the console.");
            (void)fflush(stdout);
            break;
        }
        if (!flush_helper(&worker)) {
            break;
        }
        now = monotonic_milliseconds();
        if (nb_privsep_helper_take_pong(worker.helper, &pong) &&
            !nb_session_watchdog_note_pong(&watchdog, pong, now)) {
            break;
        }
        watchdog_action = nb_session_watchdog_advance(&watchdog,
                                                       now,
                                                       &ping_token);
        if (watchdog_action ==
            NB_SESSION_WATCHDOG_ACTION_STARTUP_EXPIRED) {
            (void)nb_privsep_helper_report_fatal(
                worker.helper,
                NB_PRIVSEP_FATAL_CORE_UNRESPONSIVE,
                0,
                "desktop core handshake timed out");
            (void)flush_helper(&worker);
            break;
        }
        if (watchdog_action ==
            NB_SESSION_WATCHDOG_ACTION_HEARTBEAT_EXPIRED) {
            heartbeat_expired = true;
            (void)nb_privsep_helper_report_fatal(
                worker.helper,
                NB_PRIVSEP_FATAL_CORE_UNRESPONSIVE,
                0,
                "desktop core heartbeat expired");
            (void)flush_helper(&worker);
            break;
        }
        if (watchdog_action == NB_SESSION_WATCHDOG_ACTION_SEND_PING &&
            !nb_privsep_helper_send_ping(worker.helper, ping_token)) {
            break;
        }
        if (!wait_for_worker_event(&worker, &watchdog)) {
            break;
        }
    }

cleanup:
    if (core_report_fd >= 0) {
        (void)close(core_report_fd);
    }
    if (worker.input != NULL) {
        nb_wscons_input_destroy(worker.input);
    }
    if (worker.host != NULL) {
        nb_host_destroy(worker.host);
    }
    nb_privsep_helper_destroy(worker.helper);
    if (worker.socket_fd >= 0) {
        (void)close(worker.socket_fd);
    }
    if (worker.fault_fd >= 0) {
        (void)close(worker.fault_fd);
    }
    if (sockets[0] >= 0) {
        (void)close(sockets[0]);
    }
    if (sockets[1] >= 0) {
        (void)close(sockets[1]);
    }
    if (sentinel_sockets[0] >= 0) {
        (void)close(sentinel_sockets[0]);
    }
    if (sentinel_sockets[1] >= 0) {
        (void)close(sentinel_sockets[1]);
    }
    if (worker.core_pid > 0 && !core_reaped && crash_containment) {
        (void)contain_crashed_core(worker.core_pid,
                                   &core_status,
                                   &core_cleanup_complete);
    } else if (worker.core_pid > 0 && !core_reaped) {
        success = stop_core(worker.core_pid,
                            success && orderly,
                            &core_status,
                            &core_cleanup_complete) && success;
    } else if (core_reaped) {
        core_cleanup_complete =
            stop_reaped_core_process_group(worker.core_pid);
        success = success && core_cleanup_complete && orderly &&
                  WIFEXITED(core_status) && WEXITSTATUS(core_status) == 0;
    }
    if (sentinel_pid > 0) {
        runtime_cleanup_complete =
            finish_runtime_sentinel(sentinel_pid,
                                    &sentinel_fd,
                                    sentinel_ready,
                                    core_cleanup_complete,
                                    &sentinel_reaped,
                                    &sentinel_status);
    } else if (sentinel_fd >= 0) {
        (void)close(sentinel_fd);
        sentinel_fd = -1;
    }
    if (session_lock_fd >= 0) {
        (void)close(session_lock_fd);
    }
    printf("VT lifecycle: release-completions=%" PRIu64
           " acquire-completions=%" PRIu64 "\n",
           worker.release_completions,
           worker.acquire_completions);
    (void)fflush(stdout);
    if (signals_blocked) {
        (void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);
    }
    if (!core_cleanup_complete || !runtime_cleanup_complete) {
        return NB_SESSION_WORKER_CLEANUP_INCOMPLETE_EXIT;
    }
    if (crash_containment && injected_core_failure ==
            NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH &&
        WIFSIGNALED(core_status) && WTERMSIG(core_status) == SIGKILL) {
        return NB_SESSION_WORKER_CORE_CRASH_EXIT;
    }
    if (injected_core_failure == NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG &&
        heartbeat_expired) {
        return NB_SESSION_WORKER_CORE_HANG_EXIT;
    }
    return success ? 0 : 1;
}

static const int supervisor_signals[] = {
    SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, SIGUSR1,
    SIGTSTP, SIGTTIN, SIGTTOU
};
static volatile sig_atomic_t supervisor_signal;
static volatile sig_atomic_t supervisor_fault_trigger;
static volatile sig_atomic_t supervisor_fault_trigger_early;
static volatile sig_atomic_t supervisor_fault_armed;

struct supervisor_signal_state {
    struct sigaction saved[sizeof(supervisor_signals) /
                           sizeof(supervisor_signals[0])];
    sigset_t set;
    sigset_t previous;
    struct sigaction saved_sigchld;
    size_t installed;
    bool mask_saved;
    bool sigchld_saved;
};

static void supervisor_signal_handler(int signal_number)
{
    if (signal_number == SIGUSR1) {
        if (supervisor_fault_armed) {
            supervisor_fault_trigger = 1;
        } else {
            supervisor_fault_trigger_early = 1;
        }
    } else if (supervisor_signal == 0) {
        supervisor_signal = signal_number;
    }
}

static bool prepare_supervisor_signals(
    struct supervisor_signal_state *state)
{
    struct sigaction action;
    size_t index;

    memset(state, 0, sizeof(*state));
    if (sigemptyset(&state->set) != 0) {
        return false;
    }
    for (index = 0;
         index < sizeof(supervisor_signals) / sizeof(supervisor_signals[0]);
         ++index) {
        if (sigaddset(&state->set, supervisor_signals[index]) != 0) {
            return false;
        }
    }
    if (sigaddset(&state->set, SIGCHLD) != 0) {
        return false;
    }
    if (sigprocmask(SIG_BLOCK, &state->set, &state->previous) != 0) {
        return false;
    }
    state->mask_saved = true;
    memset(&action, 0, sizeof(action));
    action.sa_handler = supervisor_signal_handler;
    action.sa_mask = state->set;
    supervisor_signal = 0;
    supervisor_fault_trigger = 0;
    supervisor_fault_trigger_early = 0;
    supervisor_fault_armed = 0;
    for (index = 0;
         index < sizeof(supervisor_signals) / sizeof(supervisor_signals[0]);
         ++index) {
        if (sigaction(supervisor_signals[index],
                      &action,
                      &state->saved[index]) != 0) {
            return false;
        }
        state->installed = index + 1;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGCHLD, &action, &state->saved_sigchld) != 0) {
        return false;
    }
    state->sigchld_saved = true;
    return true;
}

static bool activate_supervisor_signals(
    const struct supervisor_signal_state *state)
{
    return state->mask_saved &&
           sigprocmask(SIG_SETMASK, &state->previous, NULL) == 0;
}

static void restore_supervisor_signals(
    const struct supervisor_signal_state *state,
    bool restore_sigchld)
{
    size_t index;

    if (state->mask_saved) {
        (void)sigprocmask(SIG_BLOCK, &state->set, NULL);
    }
    for (index = 0; index < state->installed; ++index) {
        (void)sigaction(supervisor_signals[index],
                        &state->saved[index],
                        NULL);
    }
    if (restore_sigchld && state->sigchld_saved) {
        (void)sigaction(SIGCHLD, &state->saved_sigchld, NULL);
    }
    if (state->mask_saved) {
        (void)sigprocmask(SIG_SETMASK, &state->previous, NULL);
    }
}

static bool process_is_gone(pid_t process)
{
    return process <= 0 || (kill(process, 0) != 0 && errno == ESRCH);
}

static bool core_session_is_gone(pid_t core)
{
    bool process_gone;
    bool group_gone;

    if (core <= 0) {
        return true;
    }
    process_gone = process_is_gone(core);
    group_gone = core_process_group_is_gone(core);
    return process_gone && group_gone;
}

static bool wait_core_session_gone(pid_t core, uint64_t deadline)
{
    while (!core_session_is_gone(core)) {
        if (monotonic_milliseconds() >= deadline) {
            return false;
        }
        sleep_milliseconds(NB_SESSION_WAIT_SLICE_MS);
    }
    return true;
}

static bool signal_process(pid_t process, int signal_number)
{
    if (process <= 0) {
        return false;
    }
    if (kill(process, signal_number) == 0) {
        return true;
    }
    if (errno != ESRCH) {
        fprintf(stderr,
                "Could not signal process %ld: %s\n",
                (long)process,
                strerror(errno));
    }
    return false;
}

static bool arm_supervisor_fault_trigger(
    enum nb_wsdisplay_session_core_failure required_core_failure)
{
    sigset_t blocked;
    sigset_t previous;
    bool printed;

    if (required_core_failure ==
        NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE) {
        return true;
    }
    if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGUSR1) != 0 ||
        sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        return false;
    }
    printed = printf("Required core %s trigger armed: sudo -n "
                     "/bin/kill -USR1 %ld\n",
                     required_core_failure ==
                             NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                         ? "crash"
                         : "hang",
                     (long)getpid()) >= 0 &&
              fflush(stdout) == 0;
    if (printed) {
        supervisor_fault_armed = 1;
    }
    if (sigprocmask(SIG_SETMASK, &previous, NULL) != 0) {
        supervisor_fault_armed = 0;
        return false;
    }
    return printed;
}

struct supervision_result {
    int status;
    bool worker_gone;
    bool worker_cleanup_complete;
    bool core_session_gone;
    bool sigterm_drove_shutdown;
    bool fault_trigger_received;
    bool fault_injection_delivered;
    enum nb_wsdisplay_session_core_failure observed_core_failure;
    bool independent_failure;
};

static struct supervision_result supervise_device_worker(
    const struct nb_wsdisplay_console_state *saved,
    const struct nb_session_credentials *credentials,
    const char *core_path,
    const char *application_path,
    int session_lock_fd,
    enum nb_wsdisplay_session_core_failure required_core_failure,
    const struct supervisor_signal_state *signals)
{
    struct supervision_result outcome = {
        .status = 1,
        .worker_gone = true,
        .worker_cleanup_complete = true,
        .core_session_gone = true,
        .sigterm_drove_shutdown = false,
        .fault_trigger_received = false,
        .fault_injection_delivered = false,
        .observed_core_failure =
            NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE,
        .independent_failure = true
    };
    int report_pipe[2] = {-1, -1};
    int fault_pipe[2] = {-1, -1};
    pid_t worker;
    pid_t core = -1;
    int status = 0;
    bool reaped = false;
    bool forced = false;
    bool report_complete = false;
    bool report_reliable = true;
    bool core_sigterm_sent = false;
    bool term_sent = false;
    bool kill_sent = false;
    bool wait_failed = false;
    bool fault_trigger_handled = false;
    bool fault_command_sent = false;
    uint64_t report_deadline;
    uint64_t orderly_deadline = 0;
    uint64_t escalation_deadline = 0;
    uint64_t fault_deadline = 0;

    if (pipe(report_pipe) != 0 || pipe(fault_pipe) != 0 ||
        !set_cloexec(report_pipe[0], true) ||
        !set_cloexec(report_pipe[1], true) ||
        !set_cloexec(fault_pipe[0], true) ||
        !set_cloexec(fault_pipe[1], true)) {
        if (report_pipe[0] >= 0) {
            (void)close(report_pipe[0]);
        }
        if (report_pipe[1] >= 0) {
            (void)close(report_pipe[1]);
        }
        if (fault_pipe[0] >= 0) {
            (void)close(fault_pipe[0]);
        }
        if (fault_pipe[1] >= 0) {
            (void)close(fault_pipe[1]);
        }
        return outcome;
    }
    worker = fork();
    if (worker < 0) {
        (void)close(report_pipe[0]);
        (void)close(report_pipe[1]);
        (void)close(fault_pipe[0]);
        (void)close(fault_pipe[1]);
        return outcome;
    }
    outcome.worker_gone = false;
    outcome.worker_cleanup_complete = false;
    outcome.core_session_gone = false;
    outcome.independent_failure = false;
    if (worker == 0) {
        int result;

        (void)close(report_pipe[0]);
        (void)close(fault_pipe[1]);
        restore_supervisor_signals(signals, false);
        result = run_device_worker(saved,
                                   credentials,
                                   core_path,
                                   application_path,
                                   report_pipe[1],
                                   fault_pipe[0],
                                   required_core_failure,
                                   session_lock_fd);
        _exit(result);
    }
    (void)close(report_pipe[1]);
    (void)close(fault_pipe[0]);
    report_deadline = add_milliseconds(monotonic_milliseconds(),
                                       NB_SESSION_CORE_REPORT_TIMEOUT_MS);
    if (!set_nonblocking(report_pipe[0])) {
        fputs("Could not establish the supervisor report channel\n", stderr);
        (void)close(report_pipe[0]);
        report_pipe[0] = -1;
        report_complete = true;
        report_reliable = false;
        forced = true;
        outcome.independent_failure = true;
        orderly_deadline = add_milliseconds(
            monotonic_milliseconds(),
            NB_SESSION_WORKER_ORDERLY_GRACE_MS);
    }
    for (;;) {
        pid_t wait_result;
        uint64_t now;

        if (!report_complete) {
            const ssize_t count = read(report_pipe[0],
                                       &core,
                                       sizeof(core));

            if (count == (ssize_t)sizeof(core) && core > 0) {
                report_complete = true;
                if (!arm_supervisor_fault_trigger(
                        required_core_failure)) {
                    fputs("Could not arm the required core fault trigger.\n",
                          stderr);
                    forced = true;
                    outcome.independent_failure = true;
                }
            } else if (count == 0) {
                report_complete = true;
                report_reliable = false;
                forced = true;
                outcome.independent_failure = true;
                orderly_deadline = add_milliseconds(
                    monotonic_milliseconds(),
                    NB_SESSION_WORKER_ORDERLY_GRACE_MS);
            } else if (count > 0) {
                fputs("Supervisor received a partial core PID report\n",
                      stderr);
                core = -1;
                report_complete = true;
                report_reliable = false;
                forced = true;
                outcome.independent_failure = true;
                orderly_deadline = add_milliseconds(
                    monotonic_milliseconds(),
                    NB_SESSION_WORKER_ORDERLY_GRACE_MS);
            } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
                       errno != EINTR) {
                fprintf(stderr,
                        "Could not read the core PID report: %s\n",
                        strerror(errno));
                core = -1;
                report_complete = true;
                report_reliable = false;
                forced = true;
                outcome.independent_failure = true;
                orderly_deadline = add_milliseconds(
                    monotonic_milliseconds(),
                    NB_SESSION_WORKER_ORDERLY_GRACE_MS);
            }
        }
        wait_result = waitpid(worker, &status, WNOHANG);
        if (wait_result == worker) {
            reaped = true;
        } else if (wait_result < 0 && errno != EINTR) {
            fprintf(stderr,
                    "Could not reap the device worker: %s\n",
                    strerror(errno));
            forced = true;
            wait_failed = true;
            outcome.independent_failure = true;
            break;
        }
        if (supervisor_signal != 0) {
            forced = true;
        }
        now = monotonic_milliseconds();
        if (supervisor_fault_trigger_early != 0 &&
            !fault_trigger_handled) {
            outcome.fault_trigger_received = true;
            fault_trigger_handled = true;
            outcome.independent_failure = true;
            forced = true;
            orderly_deadline = now;
            fputs("Core fault trigger arrived before the validated core "
                  "was armed.\n",
                  stderr);
        }
        if (supervisor_fault_trigger != 0 && !fault_trigger_handled) {
            outcome.fault_trigger_received = true;
            if (required_core_failure ==
                NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE) {
                fault_trigger_handled = true;
                outcome.independent_failure = true;
                forced = true;
            } else if (report_complete && report_reliable && core > 0) {
                const unsigned char command =
                    required_core_failure ==
                            NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                        ? NB_SESSION_FAULT_COMMAND_CRASH
                        : NB_SESSION_FAULT_COMMAND_HANG;

                fault_trigger_handled = true;
                if (!write_all(fault_pipe[1], &command, sizeof(command))) {
                    outcome.independent_failure = true;
                    forced = true;
                } else {
                    fault_command_sent = true;
                    fault_deadline = add_milliseconds(
                        now,
                        NB_SESSION_FAULT_GATE_TIMEOUT_MS);
                    printf("Supervisor delivered the required core %s "
                           "injection command.\n",
                           required_core_failure ==
                                   NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                               ? "crash"
                               : "hang");
                    (void)fflush(stdout);
                }
            }
        }
        if (fault_command_sent && !reaped && now >= fault_deadline) {
            fputs("Required core fault did not reach verified containment "
                  "before its hard deadline.\n",
                  stderr);
            outcome.independent_failure = true;
            forced = true;
            orderly_deadline = now;
        }
        if (!report_complete && now >= report_deadline) {
            fputs("Timed out waiting for the core PID report\n", stderr);
            (void)close(report_pipe[0]);
            report_pipe[0] = -1;
            report_complete = true;
            report_reliable = false;
            forced = true;
            outcome.independent_failure = true;
            orderly_deadline = add_milliseconds(
                now,
                NB_SESSION_WORKER_ORDERLY_GRACE_MS);
        }
        if (supervisor_signal == SIGTERM && !reaped &&
            report_complete && report_reliable && core > 0 &&
            !core_sigterm_sent) {
            const bool core_sigterm_delivered =
                signal_process(core, SIGTERM);

            core_sigterm_sent = true;
            if (!core_sigterm_delivered) {
                outcome.independent_failure = true;
            } else {
                outcome.sigterm_drove_shutdown = true;
                puts("Supervisor received SIGTERM; requesting orderly core "
                     "shutdown.");
                (void)fflush(stdout);
            }
            orderly_deadline = add_milliseconds(
                now,
                NB_SESSION_WORKER_ORDERLY_GRACE_MS);
        }
        if (reaped && report_complete) {
            break;
        }
        if (forced && !reaped && report_complete) {
            if (outcome.sigterm_drove_shutdown && !term_sent &&
                now < orderly_deadline) {
                sleep_milliseconds(NB_SESSION_WAIT_SLICE_MS);
                continue;
            }
            if (!report_reliable && now < orderly_deadline) {
                sleep_milliseconds(NB_SESSION_WAIT_SLICE_MS);
                continue;
            }
            if (!term_sent) {
                if (!signal_process(worker, SIGTERM)) {
                    outcome.independent_failure = true;
                }
                (void)signal_process(worker, SIGCONT);
                if (outcome.sigterm_drove_shutdown) {
                    outcome.independent_failure = true;
                }
                term_sent = true;
                escalation_deadline = add_milliseconds(
                    now,
                    NB_SESSION_WORKER_SIGNAL_GRACE_MS);
            } else if (!kill_sent && now >= escalation_deadline) {
                (void)signal_process(worker, SIGKILL);
                kill_sent = true;
                escalation_deadline = add_milliseconds(
                    now,
                    NB_SESSION_WORKER_SIGNAL_GRACE_MS);
            } else if (kill_sent && now >= escalation_deadline) {
                break;
            }
        }
        sleep_milliseconds(NB_SESSION_WAIT_SLICE_MS);
    }
    if (report_pipe[0] >= 0) {
        (void)close(report_pipe[0]);
    }
    (void)close(fault_pipe[1]);
    outcome.worker_gone = reaped;
    if (reaped) {
        if (WIFEXITED(status)) {
            outcome.worker_cleanup_complete =
                WEXITSTATUS(status) !=
                    NB_SESSION_WORKER_CLEANUP_INCOMPLETE_EXIT;
            if (WEXITSTATUS(status) ==
                NB_SESSION_WORKER_CORE_CRASH_EXIT) {
                outcome.observed_core_failure =
                    NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH;
                outcome.fault_injection_delivered = true;
            } else if (WEXITSTATUS(status) ==
                       NB_SESSION_WORKER_CORE_HANG_EXIT) {
                outcome.observed_core_failure =
                    NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG;
                outcome.fault_injection_delivered = true;
            } else if (WEXITSTATUS(status) != 0) {
                outcome.independent_failure = true;
            }
        } else {
            outcome.independent_failure = true;
        }
    }
    outcome.sigterm_drove_shutdown =
        outcome.sigterm_drove_shutdown && !term_sent && reaped &&
        WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (reaped && WIFEXITED(status) &&
        WEXITSTATUS(status) !=
            NB_SESSION_WORKER_CLEANUP_INCOMPLETE_EXIT) {
        /* The worker returns normally only after its core cleanup/reap path. */
        outcome.core_session_gone = true;
    } else if (reaped && core > 0) {
        uint64_t deadline = add_milliseconds(
            monotonic_milliseconds(),
            NB_SESSION_CORE_ORDERLY_GRACE_MS);

        if (!wait_core_session_gone(core, deadline)) {
            forced = true;
            signal_core_session(core, SIGTERM);
            signal_core_session(core, SIGCONT);
            deadline = add_milliseconds(monotonic_milliseconds(),
                                        NB_SESSION_CORE_SIGNAL_GRACE_MS);
            if (!wait_core_session_gone(core, deadline)) {
                signal_core_session(core, SIGKILL);
                deadline = add_milliseconds(
                    monotonic_milliseconds(),
                    NB_SESSION_CORE_SIGNAL_GRACE_MS);
                (void)wait_core_session_gone(core, deadline);
            }
        }
        outcome.core_session_gone = core_session_is_gone(core);
    }
    if (supervisor_signal != 0) {
        forced = true;
    }
    if (!wait_failed && !outcome.independent_failure && outcome.worker_gone &&
        outcome.worker_cleanup_complete && outcome.core_session_gone &&
        WIFEXITED(status) &&
        WEXITSTATUS(status) == 0 &&
        outcome.observed_core_failure ==
            NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE) {
        outcome.status = 0;
    }
    return outcome;
}

int nb_wsdisplay_session_run(
    const struct nb_wsdisplay_session_options *options)
{
    struct nb_wsdisplay_console_state state;
    struct nb_session_credentials credentials;
    struct supervisor_signal_state signals;
    struct supervision_result supervision = {
        .status = 1,
        .worker_gone = true,
        .worker_cleanup_complete = true,
        .core_session_gone = true,
        .sigterm_drove_shutdown = false,
        .fault_trigger_received = false,
        .fault_injection_delivered = false,
        .observed_core_failure =
            NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE,
        .independent_failure = true
    };
    char credential_error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY];
    char recovery_error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY];
    char core_path[NB_WSDISPLAY_SESSION_PATH_CAPACITY];
    const char *application_path;
    int session_lock_fd;
    int result = 1;
    bool console_restored = false;
    bool recovery_record_removed = false;
    bool signals_prepared = false;

    credential_error[0] = '\0';
    recovery_error[0] = '\0';
    if (options == NULL ||
        options->action < NB_WSDISPLAY_SESSION_ACTION_RUN ||
        options->action > NB_WSDISPLAY_SESSION_ACTION_RECOVER ||
        options->required_core_failure <
            NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE ||
        options->required_core_failure >
            NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG ||
        (options->require_supervisor_sigterm &&
         options->required_core_failure !=
             NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE) ||
        !application_path_is_valid(options->application_path)) {
        return 2;
    }
    if (!nb_session_credentials_prepare_parent_stdio()) {
        fprintf(stderr,
                "Could not prepare standard descriptors: %s\n",
                strerror(errno));
        return 1;
    }
    if (geteuid() != 0) {
        fputs("nixbench-wsdisplay-session must run as root (use sudo)\n",
              stderr);
        return 1;
    }
    session_lock_fd = acquire_session_lock();
    if (session_lock_fd < 0) {
        return 1;
    }
    if (!prepare_supervisor_signals(&signals)) {
        fputs("Could not establish recovery signal protection\n", stderr);
        restore_supervisor_signals(&signals, true);
        goto release_lock;
    }
    signals_prepared = true;
    if (!activate_supervisor_signals(&signals)) {
        fputs("Could not activate recovery signal protection\n", stderr);
        goto release_lock;
    }
    if (options->action == NB_WSDISPLAY_SESSION_ACTION_RECOVER) {
        if (!nb_wsdisplay_recovery_load(&recovery_options,
                                        &state,
                                        recovery_error)) {
            fprintf(stderr, "%s\n", recovery_error);
            goto release_lock;
        }
        print_state(&state);
        if (!nb_wsdisplay_console_restore_and_verify(&state)) {
            goto release_lock;
        }
        if (!nb_wsdisplay_recovery_remove(&recovery_options,
                                          recovery_error)) {
            fprintf(stderr, "%s\n", recovery_error);
            goto release_lock;
        }
        result = 0;
        goto release_lock;
    }
    if (!capture_console(&state)) {
        goto release_lock;
    }
    print_state(&state);
    if (options->action == NB_WSDISPLAY_SESSION_ACTION_PREFLIGHT) {
        puts("Preflight passed; no display state was changed.");
        result = 0;
        goto release_lock;
    }
    if (options->action != NB_WSDISPLAY_SESSION_ACTION_RUN ||
        !options->acknowledge_console_takeover ||
        !nb_session_credentials_resolve_sudo(&credentials,
                                             credential_error) ||
        !resolve_program_paths(options, core_path)) {
        if (credential_error[0] != '\0') {
            fprintf(stderr, "%s\n", credential_error);
        }
        goto release_lock;
    }
    application_path = options->application_path;
    if (!nb_wsdisplay_recovery_store(&recovery_options,
                                     &state,
                                     recovery_error)) {
        fprintf(stderr, "%s\n", recovery_error);
        goto release_lock;
    }
    printf("Taking over %s for a privilege-separated NixBench session.\n"
           "Recovery state: %s\n"
           "Core: %s\n",
           state.screen_device,
           recovery_path,
           core_path);
    if (application_path != NULL) {
        printf("Initial application: %s\n", application_path);
    } else {
        puts("Initial application: none (desktop starts empty)");
    }
    printf("Supervisor PID: %ld (second-SSH cancellation: sudo kill -TERM "
           "%ld)\n",
           (long)getpid(),
           (long)getpid());
    (void)fflush(NULL);
    supervision = supervise_device_worker(&state,
                                          &credentials,
                                          core_path,
                                          application_path,
                                          session_lock_fd,
                                          options->required_core_failure,
                                          &signals);
    result = supervision.status;
    if (!supervision.worker_gone) {
        fputs("DEVICE WORKER LIVENESS WAS NOT CLEARED. The recovery record "
              "was retained; do not restore or start another session until "
              "the worker is confirmed gone.\n",
              stderr);
        result = 1;
        goto release_lock;
    }
    if (!nb_wsdisplay_console_restore_and_verify(&state)) {
        fprintf(stderr,
                "CONSOLE RESTORATION WAS NOT VERIFIED. Keep SSH open and run "
                "sudo %s --recover.\n",
                options->program_path);
        result = 1;
        goto release_lock;
    }
    console_restored = true;
    if (!supervision.worker_cleanup_complete) {
        fputs("DEVICE WORKER CLEANUP WAS NOT VERIFIED. The console was "
              "restored, but the recovery record was retained.\n",
              stderr);
        result = 1;
        goto release_lock;
    }
    if (!supervision.core_session_gone) {
        fputs("CORE/APPLICATION LIVENESS WAS NOT CLEARED. The console was "
              "restored, but the recovery record was retained.\n",
              stderr);
        result = 1;
        goto release_lock;
    }
    if (!nb_wsdisplay_recovery_remove(&recovery_options,
                                      recovery_error)) {
        fprintf(stderr, "%s\n", recovery_error);
        result = 1;
    } else {
        puts("Supervisor verified console restoration and cleared the "
             "recovery record.");
        recovery_record_removed = true;
    }

release_lock:
    if (options->action == NB_WSDISPLAY_SESSION_ACTION_RUN) {
        if (options->require_supervisor_sigterm) {
            const struct nb_wsdisplay_session_sigterm_gate gate = {
                .sigterm_received = supervisor_signal == SIGTERM,
                .sigterm_drove_shutdown =
                    supervision.sigterm_drove_shutdown,
                .independent_failure = supervision.independent_failure,
                .worker_gone = supervision.worker_gone,
                .core_session_gone = supervision.core_session_gone,
                .console_restored = console_restored,
                .recovery_record_removed = recovery_record_removed
            };

            if (nb_wsdisplay_session_sigterm_gate_passes(&gate)) {
                puts("Required supervisor SIGTERM was received; worker and "
                     "core cleanup, console restoration, and recovery-record "
                     "removal completed.");
                result = 0;
            } else {
                if (supervisor_signal == 0) {
                    fputs("Required supervisor SIGTERM was not received.\n",
                          stderr);
                } else if (supervisor_signal != SIGTERM) {
                    fprintf(stderr,
                            "Required supervisor SIGTERM, but signal %d was "
                            "received.\n",
                            (int)supervisor_signal);
                }
                result = 1;
            }
        } else if (options->required_core_failure !=
                   NB_WSDISPLAY_SESSION_CORE_FAILURE_NONE) {
            const struct nb_wsdisplay_session_core_failure_gate gate = {
                .expected = options->required_core_failure,
                .observed = supervision.observed_core_failure,
                .fault_trigger_received =
                    supervision.fault_trigger_received,
                .fault_injection_delivered =
                    supervision.fault_injection_delivered,
                .supervisor_signal_received = supervisor_signal != 0,
                .independent_failure = supervision.independent_failure,
                .worker_gone = supervision.worker_gone,
                .core_session_gone = supervision.core_session_gone,
                .console_restored = console_restored,
                .recovery_record_removed = recovery_record_removed
            };

            if (nb_wsdisplay_session_core_failure_gate_passes(&gate)) {
                printf("Required core %s was injected and observed; worker "
                       "and core cleanup, console restoration, and "
                       "recovery-record removal completed.\n",
                       options->required_core_failure ==
                               NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                           ? "crash"
                           : "hang");
                result = 0;
            } else {
                fprintf(stderr,
                        "Required core %s recovery gate did not complete "
                        "exactly (observed: %s).\n",
                        options->required_core_failure ==
                                NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                            ? "crash"
                            : "hang",
                        supervision.observed_core_failure ==
                                NB_WSDISPLAY_SESSION_CORE_FAILURE_CRASH
                            ? "crash"
                        : supervision.observed_core_failure ==
                                  NB_WSDISPLAY_SESSION_CORE_FAILURE_HANG
                            ? "hang"
                            : "none");
                result = 1;
            }
        } else if (supervisor_signal != 0 ||
                   supervisor_fault_trigger != 0 ||
                   supervisor_fault_trigger_early != 0) {
            result = 1;
        }
    }
    if (signals_prepared) {
        restore_supervisor_signals(&signals, true);
    }
    (void)close(session_lock_fd);
    return result;
}

#endif
