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
    bool acknowledge_seen = false;
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
        (acknowledge_seen || core_seen)) {
        set_error(error,
                  "takeover and core options are valid only for a run");
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
#include "session_credentials.h"
#include "wscons_input.h"
#include "wsdisplay_console_session.h"
#include "wsdisplay_recovery.h"

enum {
    NB_SESSION_ACK_FLUSH_TIMEOUT_MS = 2000,
    NB_SESSION_CORE_ORDERLY_GRACE_MS = 4000,
    NB_SESSION_CORE_SIGNAL_GRACE_MS = 2000,
    NB_SESSION_WORKER_ORDERLY_GRACE_MS = 7000,
    NB_SESSION_WORKER_SIGNAL_GRACE_MS = 2000,
    NB_SESSION_WAIT_SLICE_MS = 20,
    NB_SESSION_IDLE_WAIT_MS = 50,
    NB_SESSION_HEARTBEAT_INTERVAL_MS = 1000,
    NB_SESSION_HEARTBEAT_TIMEOUT_MS = 3000,
    NB_SESSION_IO_BATCH = 64,
    NB_SESSION_WORKER_CLEANUP_INCOMPLETE_EXIT = 2
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

static bool derive_sibling(const char *path,
                           const char *name,
                           char destination[NB_WSDISPLAY_SESSION_PATH_CAPACITY])
{
    const char *slash = strrchr(path, '/');
    int length;

    if (!absolute_path_is_valid(path) || slash == NULL ||
        name == NULL || name[0] == '\0') {
        return false;
    }
    if (slash == path) {
        length = snprintf(destination,
                          NB_WSDISPLAY_SESSION_PATH_CAPACITY,
                          "/%s",
                          name);
    } else {
        const size_t directory_length = (size_t)(slash - path);

        if (directory_length > (size_t)INT_MAX) {
            return false;
        }
        length = snprintf(destination,
                          NB_WSDISPLAY_SESSION_PATH_CAPACITY,
                          "%.*s/%s",
                          (int)directory_length,
                          path,
                          name);
    }
    return length > 0 &&
           (size_t)length < NB_WSDISPLAY_SESSION_PATH_CAPACITY;
}

static bool resolve_program_paths(
    const struct nb_wsdisplay_session_options *options,
    char core[NB_WSDISPLAY_SESSION_PATH_CAPACITY],
    char nixclock[NB_WSDISPLAY_SESSION_PATH_CAPACITY])
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
        stat(core, &status) != 0 || !S_ISREG(status.st_mode) ||
        !derive_sibling(core, "nixclock", candidate) ||
        realpath(candidate, nixclock) == NULL ||
        stat(nixclock, &status) != 0 || !S_ISREG(status.st_mode)) {
        fprintf(stderr,
                "Could not resolve the session core and NixClock: %s\n",
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
    pid_t core_pid;
    struct nb_wsdisplay_session_frame_state frame;
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

static bool drain_input(struct worker_context *worker)
{
    size_t batch;

    if (!nb_wscons_input_is_active(worker->input)) {
        return true;
    }
    for (batch = 0; batch < NB_SESSION_IO_BATCH; ++batch) {
        struct nb_host_event event;
        const enum nb_host_event_status status =
            nb_wscons_input_poll(worker->input, &event);

        if (status == NB_HOST_EVENT_STATUS_EMPTY) {
            return true;
        }
        if (status == NB_HOST_EVENT_STATUS_ERROR ||
            !nb_privsep_helper_send_input(worker->helper, &event)) {
            return false;
        }
    }
    return true;
}

static bool resume_input_and_helper(struct worker_context *worker)
{
    struct nb_host_output host_output;
    struct nb_privsep_output output;

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

static bool wait_for_worker_event(struct worker_context *worker)
{
    int descriptors[1 + NB_WSCONS_INPUT_WAIT_DESCRIPTOR_COUNT];
    size_t count = 1;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;
    enum nb_host_event_status status;
    uint32_t timeout = nb_privsep_helper_outbound_size(worker->helper) != 0
                           ? NB_SESSION_WAIT_SLICE_MS
                           : NB_SESSION_IDLE_WAIT_MS;

    descriptors[0] = worker->socket_fd;
    if (nb_wscons_input_is_active(worker->input)) {
        if (!nb_wscons_input_get_wait_descriptors(worker->input,
                                                   descriptors + 1)) {
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
    bool reaped = wait_child(
        child,
        add_milliseconds(monotonic_milliseconds(),
                         NB_SESSION_CORE_ORDERLY_GRACE_MS),
        status);

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
    const char *nixclock_path,
    int core_report_fd,
    int session_lock_fd)
{
    struct nb_host_wsdisplay_options host_options;
    struct nb_host_output host_output;
    struct nb_privsep_output output;
    struct nb_privsep_credentials expected;
    struct nb_privsep_helper_options helper_options;
    struct worker_context worker;
    int sockets[2] = {-1, -1};
    sigset_t previous_mask;
    char *core_argv[] = {
        (char *)core_path,
        "--ipc-fd",
        "3",
        "--launch",
        (char *)nixclock_path,
        NULL
    };
    uint64_t startup_deadline;
    uint64_t next_ping;
    uint64_t pong_deadline = 0;
    uint64_t ping_token = 1;
    bool signals_blocked = false;
    bool success = false;
    bool orderly = false;
    bool core_reaped = false;
    bool core_cleanup_complete = true;
    int core_status = 0;
    int one = 1;

    memset(&worker, 0, sizeof(worker));
    worker.socket_fd = -1;
    nb_wsdisplay_session_frame_state_init(&worker.frame);
    if (!block_worker_termination(&previous_mask)) {
        goto cleanup;
    }
    signals_blocked = true;
    if (!ignore_worker_sigpipe()) {
        goto cleanup;
    }
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
        (void)close(core_report_fd);
        (void)close(session_lock_fd);
        nb_session_credentials_drop_and_exec(credentials,
                                             sockets[1],
                                             core_path,
                                             core_argv);
    }
    if (!write_all(core_report_fd,
                   &worker.core_pid,
                   sizeof(worker.core_pid))) {
        goto cleanup;
    }
    (void)close(core_report_fd);
    core_report_fd = -1;
    (void)close(sockets[1]);
    sockets[1] = -1;
    worker.socket_fd = sockets[0];
    sockets[0] = -1;
    if (!set_nonblocking(worker.socket_fd)) {
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

    startup_deadline = add_milliseconds(monotonic_milliseconds(),
                                        NB_SESSION_HEARTBEAT_TIMEOUT_MS);
    next_ping = add_milliseconds(monotonic_milliseconds(),
                                 NB_SESSION_HEARTBEAT_INTERVAL_MS);
    for (;;) {
        bool eof;
        uint64_t now;
        uint64_t pong;
        uint64_t shutdown_id;
        pid_t waited;

        waited = waitpid(worker.core_pid, &core_status, WNOHANG);
        if (waited == worker.core_pid) {
            core_reaped = true;
            break;
        }
        if (waited < 0 && errno != EINTR) {
            break;
        }
        if (!drain_core(&worker, &eof) || eof) {
            break;
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
        if (!drain_input(&worker) || !flush_helper(&worker)) {
            break;
        }
        now = monotonic_milliseconds();
        if (!nb_privsep_helper_is_ready(worker.helper)) {
            if (now >= startup_deadline) {
                (void)nb_privsep_helper_report_fatal(
                    worker.helper,
                    NB_PRIVSEP_FATAL_CORE_UNRESPONSIVE,
                    0,
                    "desktop core handshake timed out");
                (void)flush_helper(&worker);
                break;
            }
        } else {
            if (nb_privsep_helper_take_pong(worker.helper, &pong)) {
                (void)pong;
                next_ping = add_milliseconds(
                    now,
                    NB_SESSION_HEARTBEAT_INTERVAL_MS);
            }
            if (nb_privsep_helper_ping_outstanding(worker.helper)) {
                if (now >= pong_deadline) {
                    (void)nb_privsep_helper_report_fatal(
                        worker.helper,
                        NB_PRIVSEP_FATAL_CORE_UNRESPONSIVE,
                        0,
                        "desktop core heartbeat expired");
                    (void)flush_helper(&worker);
                    break;
                }
            } else if (now >= next_ping) {
                if (!nb_privsep_helper_send_ping(worker.helper,
                                                 ping_token)) {
                    break;
                }
                ++ping_token;
                if (ping_token == 0) {
                    ping_token = 1;
                }
                pong_deadline = add_milliseconds(
                    now,
                    NB_SESSION_HEARTBEAT_TIMEOUT_MS);
            }
        }
        if (!wait_for_worker_event(&worker)) {
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
    if (sockets[0] >= 0) {
        (void)close(sockets[0]);
    }
    if (sockets[1] >= 0) {
        (void)close(sockets[1]);
    }
    if (worker.core_pid > 0 && !core_reaped) {
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
    if (!core_cleanup_complete) {
        return NB_SESSION_WORKER_CLEANUP_INCOMPLETE_EXIT;
    }
    return success ? 0 : 1;
}

static const int supervisor_signals[] = {
    SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE,
    SIGTSTP, SIGTTIN, SIGTTOU
};
static volatile sig_atomic_t supervisor_signal;

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
    supervisor_signal = signal_number;
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

static void signal_process(pid_t process, int signal_number)
{
    if (process > 0 && kill(process, signal_number) != 0 && errno != ESRCH) {
        fprintf(stderr,
                "Could not signal process %ld: %s\n",
                (long)process,
                strerror(errno));
    }
}

struct supervision_result {
    int status;
    bool worker_gone;
    bool core_session_gone;
};

static struct supervision_result supervise_device_worker(
    const struct nb_wsdisplay_console_state *saved,
    const struct nb_session_credentials *credentials,
    const char *core_path,
    const char *nixclock_path,
    int session_lock_fd,
    const struct supervisor_signal_state *signals)
{
    struct supervision_result outcome = {
        .status = 1,
        .worker_gone = true,
        .core_session_gone = true
    };
    int report_pipe[2] = {-1, -1};
    pid_t worker;
    pid_t core = -1;
    int status = 0;
    bool reaped = false;
    bool forced = false;
    bool report_complete = false;
    bool report_reliable = true;
    bool term_sent = false;
    bool kill_sent = false;
    bool wait_failed = false;
    uint64_t report_deadline;
    uint64_t orderly_deadline = 0;
    uint64_t escalation_deadline = 0;

    if (pipe(report_pipe) != 0 ||
        !set_cloexec(report_pipe[0], true) ||
        !set_cloexec(report_pipe[1], true)) {
        if (report_pipe[0] >= 0) {
            (void)close(report_pipe[0]);
        }
        if (report_pipe[1] >= 0) {
            (void)close(report_pipe[1]);
        }
        return outcome;
    }
    worker = fork();
    if (worker < 0) {
        (void)close(report_pipe[0]);
        (void)close(report_pipe[1]);
        return outcome;
    }
    outcome.worker_gone = false;
    outcome.core_session_gone = false;
    if (worker == 0) {
        int result;

        (void)close(report_pipe[0]);
        restore_supervisor_signals(signals, false);
        result = run_device_worker(saved,
                                   credentials,
                                   core_path,
                                   nixclock_path,
                                   report_pipe[1],
                                   session_lock_fd);
        _exit(result);
    }
    (void)close(report_pipe[1]);
    report_deadline = add_milliseconds(monotonic_milliseconds(),
                                       NB_SESSION_HEARTBEAT_TIMEOUT_MS);
    if (!set_nonblocking(report_pipe[0])) {
        fputs("Could not establish the supervisor report channel\n", stderr);
        (void)close(report_pipe[0]);
        report_pipe[0] = -1;
        report_complete = true;
        report_reliable = false;
        forced = true;
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
            } else if (count == 0) {
                report_complete = true;
                report_reliable = false;
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
            break;
        }
        if (supervisor_signal != 0) {
            forced = true;
        }
        now = monotonic_milliseconds();
        if (!report_complete && now >= report_deadline) {
            fputs("Timed out waiting for the core PID report\n", stderr);
            (void)close(report_pipe[0]);
            report_pipe[0] = -1;
            report_complete = true;
            report_reliable = false;
            forced = true;
            orderly_deadline = add_milliseconds(
                now,
                NB_SESSION_WORKER_ORDERLY_GRACE_MS);
        }
        if (reaped && report_complete) {
            break;
        }
        if (forced && !reaped && report_complete) {
            if (!report_reliable && now < orderly_deadline) {
                sleep_milliseconds(NB_SESSION_WAIT_SLICE_MS);
                continue;
            }
            if (!term_sent) {
                signal_process(worker, SIGTERM);
                signal_process(worker, SIGCONT);
                term_sent = true;
                escalation_deadline = add_milliseconds(
                    now,
                    NB_SESSION_WORKER_SIGNAL_GRACE_MS);
            } else if (!kill_sent && now >= escalation_deadline) {
                signal_process(worker, SIGKILL);
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
    outcome.worker_gone = reaped;
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
    if (!wait_failed && outcome.worker_gone &&
        outcome.core_session_gone && !forced && WIFEXITED(status) &&
        WEXITSTATUS(status) == 0) {
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
    struct supervision_result supervision;
    char credential_error[NB_SESSION_CREDENTIALS_ERROR_CAPACITY];
    char recovery_error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY];
    char core_path[NB_WSDISPLAY_SESSION_PATH_CAPACITY];
    char nixclock_path[NB_WSDISPLAY_SESSION_PATH_CAPACITY];
    int session_lock_fd;
    int result = 1;
    bool signals_prepared = false;

    credential_error[0] = '\0';
    recovery_error[0] = '\0';
    if (options == NULL ||
        options->action < NB_WSDISPLAY_SESSION_ACTION_RUN ||
        options->action > NB_WSDISPLAY_SESSION_ACTION_RECOVER) {
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
        !resolve_program_paths(options, core_path, nixclock_path)) {
        if (credential_error[0] != '\0') {
            fprintf(stderr, "%s\n", credential_error);
        }
        goto release_lock;
    }
    if (!nb_wsdisplay_recovery_store(&recovery_options,
                                     &state,
                                     recovery_error)) {
        fprintf(stderr, "%s\n", recovery_error);
        goto release_lock;
    }
    printf("Taking over %s for a privilege-separated NixBench session.\n"
           "Recovery state: %s\n"
           "Core: %s\n"
           "Initial application: %s\n",
           state.screen_device,
           recovery_path,
           core_path,
           nixclock_path);
    printf("Supervisor PID: %ld (second-SSH cancellation: sudo kill -TERM "
           "%ld)\n",
           (long)getpid(),
           (long)getpid());
    (void)fflush(NULL);
    supervision = supervise_device_worker(&state,
                                          &credentials,
                                          core_path,
                                          nixclock_path,
                                          session_lock_fd,
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
    }

release_lock:
    if (options->action == NB_WSDISPLAY_SESSION_ACTION_RUN &&
        supervisor_signal != 0) {
        result = 1;
    }
    if (signals_prepared) {
        restore_supervisor_signals(&signals, true);
    }
    (void)close(session_lock_fd);
    return result;
}

#endif
