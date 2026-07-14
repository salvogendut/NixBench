#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "wsdisplay_smoke_runner.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if !defined(__NetBSD__)

int nb_wsdisplay_smoke_run(
    const struct nb_wsdisplay_smoke_options *options)
{
    (void)options;
    fputs("nixbench-wsdisplay-smoke: NetBSD wsdisplay is unavailable on "
          "this platform\n",
          stderr);
    return 1;
}

#else

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplay_usl_io.h>

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "host_wsdisplay.h"

enum {
    NB_SMOKE_PATH_CAPACITY = 256,
    NB_SMOKE_PARENT_STARTUP_GRACE_MS = 2000,
    NB_SMOKE_TERM_GRACE_MS = 2000,
    NB_SMOKE_ACTIVE_RESTORE_MS = 1000,
    NB_SMOKE_WAIT_SLICE_MS = 20,
    NB_SMOKE_EVENT_SLICE_MS = 50,
    NB_SMOKE_STATE_VERSION = 1
};

static const uint32_t state_magic = UINT32_C(0x4e425753);
static const char state_path[] =
    "/var/run/nixbench-wsdisplay-smoke.state";

struct nb_smoke_console_snapshot {
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    int active_screen;
    unsigned int display_mode;
    unsigned int video;
    uint32_t video_available;
    struct vt_mode vt_mode;
    char status_device[NB_SMOKE_PATH_CAPACITY];
    char screen_device[NB_SMOKE_PATH_CAPACITY];
};

static const int parent_signals[] = {
    SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU
};
static volatile sig_atomic_t parent_signal_received;

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

static int open_device(const char *path, bool writable)
{
    int flags = (writable ? O_RDWR : O_RDONLY) | O_NONBLOCK | O_NOCTTY;
    int descriptor;

#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    do {
        descriptor = open(path, flags);
    } while (descriptor < 0 && errno == EINTR);
    return descriptor;
}

static bool character_device(int descriptor, const char *path)
{
    struct stat status;

    if (fstat(descriptor, &status) != 0) {
        fprintf(stderr, "Could not inspect %s: %s\n", path, strerror(errno));
        return false;
    }
    if (!S_ISCHR(status.st_mode)) {
        fprintf(stderr, "%s is not a character device\n", path);
        return false;
    }
    return true;
}

static bool unsupported_video_error(int system_error)
{
    return system_error == ENOTTY || system_error == EINVAL ||
           system_error == ENODEV || system_error == ENOSYS ||
           system_error == ENOTSUP || system_error == EOPNOTSUPP;
}

static bool copy_path(char destination[NB_SMOKE_PATH_CAPACITY],
                      const char *source)
{
    const int length = snprintf(destination,
                                NB_SMOKE_PATH_CAPACITY,
                                "%s",
                                source);

    return length >= 0 && (size_t)length < NB_SMOKE_PATH_CAPACITY;
}

static bool snapshot_console(
    const struct nb_wsdisplay_smoke_options *options,
    struct nb_smoke_console_snapshot *snapshot)
{
    int status_device = -1;
    int screen = -1;
    int active_from_screen = -1;
    int active_vt = -1;
    bool success = false;
    int length;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->magic = state_magic;
    snapshot->version = NB_SMOKE_STATE_VERSION;
    snapshot->record_size = (uint32_t)sizeof(*snapshot);
    if (!copy_path(snapshot->status_device,
                   options->status_device_path)) {
        fputs("Status-device path is too long\n", stderr);
        return false;
    }

    status_device = open_device(snapshot->status_device, false);
    if (status_device < 0) {
        fprintf(stderr, "Could not open %s: %s\n",
                snapshot->status_device, strerror(errno));
        goto cleanup;
    }
    if (!character_device(status_device, snapshot->status_device) ||
        ioctl(status_device,
              WSDISPLAYIO_GETACTIVESCREEN,
              &snapshot->active_screen) != 0) {
        if (errno != 0) {
            fprintf(stderr, "Could not query active wsdisplay screen: %s\n",
                    strerror(errno));
        }
        goto cleanup;
    }
    if (snapshot->active_screen < 0 || snapshot->active_screen > 255) {
        fprintf(stderr, "Invalid active wsdisplay screen: %d\n",
                snapshot->active_screen);
        goto cleanup;
    }
    if (!nb_wsdisplay_screen_index_to_vt_number(snapshot->active_screen,
                                                 &active_vt)) {
        fputs("Could not translate the active screen to a VT number\n",
              stderr);
        goto cleanup;
    }
    length = snprintf(snapshot->screen_device,
                      sizeof(snapshot->screen_device),
                      "%s%d",
                      options->screen_device_prefix,
                      snapshot->active_screen);
    if (length < 0 || (size_t)length >= sizeof(snapshot->screen_device)) {
        fputs("Active screen-device path is too long\n", stderr);
        goto cleanup;
    }

    screen = open_device(snapshot->screen_device, false);
    if (screen < 0) {
        fprintf(stderr, "Could not open %s: %s\n",
                snapshot->screen_device, strerror(errno));
        goto cleanup;
    }
    if (!character_device(screen, snapshot->screen_device)) {
        goto cleanup;
    }
    if (ioctl(screen,
              WSDISPLAYIO_GMODE,
              &snapshot->display_mode) != 0) {
        fprintf(stderr, "Could not query wsdisplay mode: %s\n",
                strerror(errno));
        goto cleanup;
    }
    if (snapshot->display_mode != WSDISPLAYIO_MODE_EMUL) {
        fprintf(stderr,
                "Refusing takeover: active screen mode is %u, not emulation\n",
                snapshot->display_mode);
        goto cleanup;
    }
    memset(&snapshot->vt_mode, 0, sizeof(snapshot->vt_mode));
    if (ioctl(screen, VT_GETMODE, &snapshot->vt_mode) != 0) {
        fprintf(stderr, "Could not query wsdisplay VT mode: %s\n",
                strerror(errno));
        goto cleanup;
    }
    if (snapshot->vt_mode.mode != VT_AUTO) {
        fputs("Refusing takeover: active VT is already process-controlled\n",
              stderr);
        goto cleanup;
    }
    if (ioctl(screen, VT_GETACTIVE, &active_from_screen) != 0 ||
        active_from_screen != active_vt) {
        fprintf(stderr,
                "Active-screen state changed during preflight "
                "(screen index %d, VT %d)\n",
                snapshot->active_screen,
                active_from_screen);
        goto cleanup;
    }

    errno = 0;
    if (ioctl(screen, WSDISPLAYIO_GVIDEO, &snapshot->video) == 0) {
        snapshot->video_available = 1;
    } else if (!unsupported_video_error(errno)) {
        fprintf(stderr, "Could not query wsdisplay video state: %s\n",
                strerror(errno));
        goto cleanup;
    }
    success = true;

cleanup:
    if (screen >= 0) {
        (void)close(screen);
    }
    if (status_device >= 0) {
        (void)close(status_device);
    }
    return success;
}

static void print_snapshot(const struct nb_smoke_console_snapshot *snapshot)
{
    printf("Active wsdisplay screen: %d (%s)\n",
           snapshot->active_screen,
           snapshot->screen_device);
    printf("Display mode: %u (emulation)\n", snapshot->display_mode);
    printf("VT mode: %d (automatic)\n", snapshot->vt_mode.mode);
    if (snapshot->video_available != 0) {
        printf("Video state: %u\n", snapshot->video);
    } else {
        puts("Video-state query: unsupported");
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

static bool read_all(int descriptor, void *data, size_t size)
{
    unsigned char *bytes = data;

    while (size != 0) {
        const ssize_t count = read(descriptor, bytes, size);

        if (count > 0) {
            bytes += (size_t)count;
            size -= (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool persist_snapshot(
    const struct nb_smoke_console_snapshot *snapshot)
{
    int flags = O_WRONLY | O_CREAT | O_EXCL;
    int descriptor;
    bool success = true;
    int saved_error = 0;

#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    descriptor = open(state_path, flags, S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        fprintf(stderr,
                "Could not create recovery state %s: %s\n"
                "Run --recover if an earlier smoke test left this file.\n",
                state_path,
                strerror(errno));
        return false;
    }
    if (!write_all(descriptor, snapshot, sizeof(*snapshot))) {
        success = false;
        saved_error = errno != 0 ? errno : EIO;
    } else if (fsync(descriptor) != 0) {
        success = false;
        saved_error = errno;
    }
    if (close(descriptor) != 0) {
        success = false;
        if (saved_error == 0) {
            saved_error = errno;
        }
    }
    if (!success) {
        (void)unlink(state_path);
        fprintf(stderr, "Could not persist recovery state: %s\n",
                strerror(saved_error != 0 ? saved_error : EIO));
    }
    return success;
}

static bool snapshot_is_valid(
    const struct nb_smoke_console_snapshot *snapshot)
{
    return snapshot->magic == state_magic &&
           snapshot->version == NB_SMOKE_STATE_VERSION &&
           snapshot->record_size == (uint32_t)sizeof(*snapshot) &&
           snapshot->active_screen >= 0 &&
           snapshot->active_screen <= 255 &&
           snapshot->display_mode == WSDISPLAYIO_MODE_EMUL &&
           snapshot->vt_mode.mode == VT_AUTO &&
           (snapshot->video_available == 0 ||
            snapshot->video_available == 1) &&
           snapshot->status_device[0] == '/' &&
           snapshot->screen_device[0] == '/' &&
           memchr(snapshot->status_device,
                  '\0',
                  sizeof(snapshot->status_device)) != NULL &&
           memchr(snapshot->screen_device,
                  '\0',
                  sizeof(snapshot->screen_device)) != NULL;
}

static bool load_snapshot(struct nb_smoke_console_snapshot *snapshot)
{
    struct stat status;
    int flags = O_RDONLY;
    int descriptor;
    bool success;

#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    descriptor = open(state_path, flags);
    if (descriptor < 0) {
        fprintf(stderr, "Could not open recovery state %s: %s\n",
                state_path, strerror(errno));
        return false;
    }
    errno = 0;
    success = fstat(descriptor, &status) == 0 &&
              S_ISREG(status.st_mode) && status.st_uid == 0 &&
              status.st_nlink == 1 &&
              (status.st_mode & (S_IRWXG | S_IRWXO)) == 0 &&
              status.st_size == (off_t)sizeof(*snapshot) &&
              read_all(descriptor, snapshot, sizeof(*snapshot));
    if (close(descriptor) != 0) {
        success = false;
    }
    success = success && snapshot_is_valid(snapshot);
    if (!success) {
        const int saved_error = errno;

        fprintf(stderr, "Recovery state is invalid or unreadable: %s\n",
                saved_error != 0 ? strerror(saved_error) : "invalid record");
    }
    return success;
}

static void report_restore_error(const char *operation, bool *success)
{
    fprintf(stderr, "RESTORE ERROR: %s: %s\n", operation, strerror(errno));
    *success = false;
}

static int restore_ioctl_pointer(int descriptor,
                                 unsigned long request,
                                 const void *argument)
{
    int result;

    do {
        result = ioctl(descriptor, request, argument);
    } while (result != 0 && errno == EINTR);
    return result;
}

static int restore_ioctl_scalar(int descriptor,
                                unsigned long request,
                                int argument)
{
    int result;

    do {
        result = ioctl(descriptor, request, argument);
    } while (result != 0 && errno == EINTR);
    return result;
}

static bool restore_console(
    const struct nb_smoke_console_snapshot *snapshot)
{
    unsigned int mode = snapshot->display_mode;
    unsigned int current_mode = UINT_MAX;
    unsigned int current_video = UINT_MAX;
    struct vt_mode current_vt;
    int current_active = -1;
    int saved_vt = -1;
    int screen = -1;
    int status_device = -1;
    bool success = true;
    unsigned int retry;

    if (!nb_wsdisplay_screen_index_to_vt_number(snapshot->active_screen,
                                                 &saved_vt)) {
        fputs("RESTORE ERROR: invalid saved active-screen index\n", stderr);
        return false;
    }

    for (retry = 0; retry < 20; ++retry) {
        screen = open_device(snapshot->screen_device, true);
        if (screen >= 0 || (errno != EBUSY && errno != EINTR)) {
            break;
        }
        sleep_milliseconds(50);
    }
    if (screen < 0) {
        fprintf(stderr, "RESTORE ERROR: could not open %s: %s\n",
                snapshot->screen_device, strerror(errno));
        return false;
    }
    if (!character_device(screen, snapshot->screen_device)) {
        success = false;
    }
    if (restore_ioctl_pointer(screen, WSDISPLAYIO_SMODE, &mode) != 0) {
        report_restore_error("could not restore emulation mode", &success);
    }
    if (snapshot->video_available != 0 &&
        restore_ioctl_pointer(screen,
                              WSDISPLAYIO_SVIDEO,
                              &snapshot->video) != 0) {
        report_restore_error("could not restore video state", &success);
    }
    if (restore_ioctl_pointer(screen,
                              VT_SETMODE,
                              &snapshot->vt_mode) != 0) {
        report_restore_error("could not restore automatic VT mode", &success);
    }

    status_device = open_device(snapshot->status_device, false);
    if (status_device < 0 ||
        !character_device(status_device, snapshot->status_device)) {
        fprintf(stderr, "RESTORE ERROR: could not open status device %s: %s\n",
                snapshot->status_device, strerror(errno));
        success = false;
    } else if (restore_ioctl_pointer(status_device,
                                     WSDISPLAYIO_GETACTIVESCREEN,
                                     &current_active) != 0) {
        report_restore_error("could not query active screen", &success);
    } else if (current_active != snapshot->active_screen) {
        const uint64_t deadline = add_milliseconds(
            monotonic_milliseconds(), NB_SMOKE_ACTIVE_RESTORE_MS);

        if (restore_ioctl_scalar(screen, VT_ACTIVATE, saved_vt) != 0) {
            report_restore_error("could not reactivate saved screen", &success);
        } else {
            do {
                sleep_milliseconds(20);
                if (restore_ioctl_pointer(status_device,
                                          WSDISPLAYIO_GETACTIVESCREEN,
                                          &current_active) != 0) {
                    report_restore_error("could not verify active screen",
                                         &success);
                    break;
                }
            } while (current_active != snapshot->active_screen &&
                     monotonic_milliseconds() < deadline);
            if (current_active != snapshot->active_screen) {
                fputs("RESTORE ERROR: saved screen did not become active\n",
                      stderr);
                success = false;
            }
        }
    }

    memset(&current_vt, 0, sizeof(current_vt));
    if (restore_ioctl_pointer(screen,
                              WSDISPLAYIO_GMODE,
                              &current_mode) != 0 ||
        current_mode != snapshot->display_mode) {
        fputs("RESTORE ERROR: display mode verification failed\n", stderr);
        success = false;
    }
    if (snapshot->video_available != 0 &&
        (restore_ioctl_pointer(screen,
                               WSDISPLAYIO_GVIDEO,
                               &current_video) != 0 ||
         current_video != snapshot->video)) {
        fputs("RESTORE ERROR: video-state verification failed\n", stderr);
        success = false;
    }
    if (restore_ioctl_pointer(screen, VT_GETMODE, &current_vt) != 0 ||
        current_vt.mode != snapshot->vt_mode.mode ||
        current_vt.waitv != snapshot->vt_mode.waitv ||
        current_vt.relsig != snapshot->vt_mode.relsig ||
        current_vt.acqsig != snapshot->vt_mode.acqsig ||
        current_vt.frsig != snapshot->vt_mode.frsig) {
        fputs("RESTORE ERROR: VT-mode verification failed\n", stderr);
        success = false;
    }
    if (status_device >= 0 &&
        (restore_ioctl_pointer(status_device,
                               WSDISPLAYIO_GETACTIVESCREEN,
                               &current_active) != 0 ||
         current_active != snapshot->active_screen)) {
        fputs("RESTORE ERROR: active-screen verification failed\n", stderr);
        success = false;
    }
    if (status_device >= 0) {
        (void)close(status_device);
    }
    (void)close(screen);
    if (success) {
        puts("Supervisor verified console restoration: emulation, video, "
             "VT mode, and active screen match the saved state.");
    }
    return success;
}

static void print_host_error(struct nb_host *host, const char *operation)
{
    char message[256];
    int system_error;

    if (nb_host_get_last_error(host,
                               &system_error,
                               message,
                               sizeof(message))) {
        if (system_error != 0) {
            fprintf(stderr, "%s: %s (%s)\n",
                    operation, message, strerror(system_error));
        } else {
            fprintf(stderr, "%s: %s\n", operation, message);
        }
    } else {
        fprintf(stderr, "%s\n", operation);
    }
}

static bool present_pattern(struct nb_host *host,
                            struct nb_wsdisplay_smoke_image *image,
                            uint64_t *serial)
{
    struct nb_host_output output;
    struct nb_host_frame frame;

    if (!nb_host_get_output(host, &output)) {
        print_host_error(host, "Could not query wsdisplay output");
        return false;
    }
    nb_wsdisplay_smoke_image_destroy(image);
    if (!nb_wsdisplay_smoke_image_create(image,
                                         output.pixel_width,
                                         output.pixel_height)) {
        fputs("Could not allocate the diagnostic frame\n", stderr);
        return false;
    }
    if (*serial == UINT64_MAX) {
        fputs("Frame serial exhausted\n", stderr);
        return false;
    }
    ++*serial;
    if (!nb_wsdisplay_smoke_image_frame(image, *serial, &frame) ||
        nb_host_present(host, &frame) != NB_HOST_RESULT_OK) {
        print_host_error(host, "Could not present the diagnostic frame");
        return false;
    }
    return true;
}

static int run_worker(const struct nb_wsdisplay_smoke_options *options,
                      const char *screen_device,
                      int expected_active_vt)
{
    struct nb_host_wsdisplay_options host_options;
    struct nb_wsdisplay_smoke_image image;
    struct nb_host *host = NULL;
    uint64_t serial = 0;
    uint64_t deadline;
    bool frame_completed = false;
    bool success = false;

    memset(&image, 0, sizeof(image));
    nb_host_wsdisplay_options_init(&host_options);
    host_options.device_path = screen_device;
    host_options.expected_active_vt = expected_active_vt;
    host = nb_host_wsdisplay_create(&host_options);
    if (host == NULL) {
        fprintf(stderr, "Could not take over %s: %s\n",
                screen_device,
                nb_host_wsdisplay_creation_error());
        goto cleanup;
    }
    deadline = add_milliseconds(nb_host_monotonic_milliseconds(host),
                                options->duration_ms);
    if (!present_pattern(host, &image, &serial)) {
        goto cleanup;
    }

    while (nb_host_monotonic_milliseconds(host) < deadline) {
        struct nb_host_event event;
        const uint64_t now = nb_host_monotonic_milliseconds(host);
        const uint64_t remaining = deadline > now ? deadline - now : 0;
        const uint32_t wait = remaining > NB_SMOKE_EVENT_SLICE_MS
                                  ? NB_SMOKE_EVENT_SLICE_MS
                                  : (uint32_t)remaining;
        const enum nb_host_event_status status =
            nb_host_wait_event(host, wait, &event);

        if (status == NB_HOST_EVENT_STATUS_ERROR) {
            print_host_error(host, "wsdisplay event loop failed");
            goto cleanup;
        }
        if (status == NB_HOST_EVENT_STATUS_EMPTY) {
            continue;
        }
        switch (event.type) {
        case NB_HOST_EVENT_FRAME_COMPLETE:
            if (event.data.frame_complete.frame_serial == serial) {
                frame_completed = true;
            }
            break;
        case NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED:
            if (nb_host_complete_console_release(host) !=
                NB_HOST_RESULT_OK) {
                print_host_error(host, "Could not release the console");
                goto cleanup;
            }
            break;
        case NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED:
            if (nb_host_complete_console_acquire(host) !=
                    NB_HOST_RESULT_OK ||
                !present_pattern(host, &image, &serial)) {
                print_host_error(host, "Could not reacquire the console");
                goto cleanup;
            }
            break;
        case NB_HOST_EVENT_OUTPUT_CHANGED:
            if (nb_host_get_state(host) == NB_HOST_STATE_ACTIVE &&
                !present_pattern(host, &image, &serial)) {
                goto cleanup;
            }
            break;
        case NB_HOST_EVENT_QUIT:
            fputs("Smoke worker received a termination request\n", stderr);
            goto cleanup;
        case NB_HOST_EVENT_FAILED:
            print_host_error(host, "wsdisplay host reported failure");
            goto cleanup;
        default:
            break;
        }
    }
    success = frame_completed;
    if (!frame_completed) {
        fputs("No diagnostic frame completion was observed\n", stderr);
    }

cleanup:
    if (host != NULL) {
        nb_host_destroy(host);
    }
    nb_wsdisplay_smoke_image_destroy(&image);
    return success ? 0 : 1;
}

static void parent_signal_handler(int signal_number)
{
    parent_signal_received = signal_number;
}

struct parent_signal_state {
    struct sigaction saved[sizeof(parent_signals) / sizeof(parent_signals[0])];
    sigset_t set;
    sigset_t previous_mask;
    size_t installed;
};

static bool prepare_parent_signals(struct parent_signal_state *state)
{
    struct sigaction action;
    size_t index;

    memset(state, 0, sizeof(*state));
    if (sigemptyset(&state->set) != 0) {
        return false;
    }
    for (index = 0;
         index < sizeof(parent_signals) / sizeof(parent_signals[0]);
         ++index) {
        if (sigaddset(&state->set, parent_signals[index]) != 0) {
            return false;
        }
    }
    if (sigprocmask(SIG_BLOCK, &state->set, &state->previous_mask) != 0) {
        return false;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = parent_signal_handler;
    action.sa_mask = state->set;
    parent_signal_received = 0;
    for (index = 0;
         index < sizeof(parent_signals) / sizeof(parent_signals[0]);
         ++index) {
        if (sigaction(parent_signals[index],
                      &action,
                      &state->saved[index]) != 0) {
            while (index > 0) {
                --index;
                (void)sigaction(parent_signals[index],
                                &state->saved[index],
                                NULL);
            }
            (void)sigprocmask(SIG_SETMASK, &state->previous_mask, NULL);
            return false;
        }
        state->installed = index + 1;
    }
    return true;
}

static void restore_child_signals(const struct parent_signal_state *state)
{
    size_t index;

    for (index = 0; index < state->installed; ++index) {
        (void)sigaction(parent_signals[index], &state->saved[index], NULL);
    }
    (void)sigprocmask(SIG_SETMASK, &state->previous_mask, NULL);
}

static void unblock_parent_signals(
    const struct parent_signal_state *state)
{
    (void)sigprocmask(SIG_SETMASK, &state->previous_mask, NULL);
}

static bool wait_for_child(pid_t child,
                           uint64_t deadline,
                           int *status)
{
    for (;;) {
        const pid_t result = waitpid(child, status, WNOHANG);

        if (result == child) {
            return true;
        }
        if (result < 0 && errno != EINTR) {
            fprintf(stderr, "Could not wait for smoke worker: %s\n",
                    strerror(errno));
            return false;
        }
        if (parent_signal_received != 0 ||
            monotonic_milliseconds() >= deadline) {
            return false;
        }
        sleep_milliseconds(NB_SMOKE_WAIT_SLICE_MS);
    }
}

static bool reap_child(pid_t child, int *status)
{
    pid_t result;

    do {
        result = waitpid(child, status, 0);
    } while (result < 0 && errno == EINTR);
    return result == child;
}

static int supervise_worker(
    const struct nb_wsdisplay_smoke_options *options,
    const struct nb_smoke_console_snapshot *snapshot)
{
    struct parent_signal_state signal_state;
    const uint64_t parent_budget =
        (uint64_t)options->duration_ms +
        NB_SMOKE_PARENT_STARTUP_GRACE_MS;
    uint64_t deadline;
    pid_t child;
    int status = 0;
    bool reaped;
    bool forced = false;
    int child_result = 1;
    int expected_active_vt;

    if (!nb_wsdisplay_screen_index_to_vt_number(snapshot->active_screen,
                                                 &expected_active_vt)) {
        fputs("Could not translate the saved screen to a VT number\n",
              stderr);
        return 1;
    }

    if (!prepare_parent_signals(&signal_state)) {
        fprintf(stderr, "Could not prepare supervisor signals: %s\n",
                strerror(errno));
        return 1;
    }
    (void)fflush(NULL);
    deadline = add_milliseconds(monotonic_milliseconds(), parent_budget);
    child = fork();
    if (child < 0) {
        fprintf(stderr, "Could not fork smoke worker: %s\n", strerror(errno));
        unblock_parent_signals(&signal_state);
        return 1;
    }
    if (child == 0) {
        int result;

        restore_child_signals(&signal_state);
        result = run_worker(options,
                            snapshot->screen_device,
                            expected_active_vt);
        (void)fflush(NULL);
        _exit(result);
    }
    unblock_parent_signals(&signal_state);

    reaped = wait_for_child(child, deadline, &status);
    if (!reaped) {
        forced = true;
        if (parent_signal_received != 0) {
            fprintf(stderr, "Supervisor received signal %d; stopping worker\n",
                    (int)parent_signal_received);
        } else {
            fputs("Worker exceeded its hard runtime; sending SIGTERM\n",
                  stderr);
        }
        if (kill(child, SIGTERM) != 0 && errno != ESRCH) {
            fprintf(stderr, "Could not terminate smoke worker: %s\n",
                    strerror(errno));
        }
        if (kill(child, SIGCONT) != 0 && errno != ESRCH) {
            fprintf(stderr, "Could not continue a stopped smoke worker: %s\n",
                    strerror(errno));
        }
        parent_signal_received = 0;
        reaped = wait_for_child(
            child,
            add_milliseconds(monotonic_milliseconds(),
                             NB_SMOKE_TERM_GRACE_MS),
            &status);
    }
    if (!reaped) {
        fputs("Worker did not stop after SIGTERM; sending SIGKILL\n", stderr);
        if (kill(child, SIGKILL) != 0 && errno != ESRCH) {
            fprintf(stderr, "Could not kill smoke worker: %s\n",
                    strerror(errno));
        }
        reaped = reap_child(child, &status);
    }
    if (!reaped) {
        fputs("Could not reap smoke worker\n", stderr);
        return 1;
    }
    if (WIFEXITED(status)) {
        child_result = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "Smoke worker died from signal %d\n",
                WTERMSIG(status));
    }
    return !forced && child_result == 0 ? 0 : 1;
}

static bool remove_recovery_state(void)
{
    if (unlink(state_path) == 0 || errno == ENOENT) {
        return true;
    }
    fprintf(stderr, "Console is restored, but could not remove %s: %s\n",
            state_path, strerror(errno));
    return false;
}

int nb_wsdisplay_smoke_run(
    const struct nb_wsdisplay_smoke_options *options)
{
    struct nb_smoke_console_snapshot snapshot;
    bool restored;
    int worker_result;

    if (options == NULL) {
        fputs("Invalid wsdisplay smoke options\n", stderr);
        return 2;
    }
    if (options->action != NB_WSDISPLAY_SMOKE_ACTION_RECOVER &&
        (options->status_device_path == NULL ||
         options->status_device_path[0] != '/' ||
         options->screen_device_prefix == NULL ||
         options->screen_device_prefix[0] != '/')) {
        fputs("Invalid wsdisplay device paths\n", stderr);
        return 2;
    }
    if (options->action == NB_WSDISPLAY_SMOKE_ACTION_RUN &&
        (!options->acknowledge_console_takeover ||
         !options->acknowledge_no_crash_watchdog ||
         options->duration_ms < NB_WSDISPLAY_SMOKE_MIN_DURATION_MS ||
         options->duration_ms > NB_WSDISPLAY_SMOKE_MAX_DURATION_MS)) {
        fputs("Invalid or unacknowledged wsdisplay takeover options\n",
              stderr);
        return 2;
    }
    if (geteuid() != 0) {
        fputs("nixbench-wsdisplay-smoke must run as root (use sudo)\n",
              stderr);
        return 1;
    }
    if (options->action == NB_WSDISPLAY_SMOKE_ACTION_RECOVER) {
        if (!load_snapshot(&snapshot)) {
            return 1;
        }
        print_snapshot(&snapshot);
        restored = restore_console(&snapshot);
        return restored && remove_recovery_state() ? 0 : 1;
    }
    if (!snapshot_console(options, &snapshot)) {
        return 1;
    }
    print_snapshot(&snapshot);
    if (options->action == NB_WSDISPLAY_SMOKE_ACTION_PREFLIGHT) {
        puts("Preflight passed; no display state was changed.");
        return 0;
    }
    if (options->action != NB_WSDISPLAY_SMOKE_ACTION_RUN) {
        fputs("Invalid wsdisplay smoke action\n", stderr);
        return 2;
    }
    if (!persist_snapshot(&snapshot)) {
        return 1;
    }
    printf("Taking over %s for at most %u ms. Recovery state: %s\n",
           snapshot.screen_device,
           options->duration_ms,
           state_path);
    puts("The parent supervisor remains unmapped and will restore and verify "
         "the saved console state after the worker exits.");
    (void)fflush(NULL);

    worker_result = supervise_worker(options, &snapshot);
    restored = restore_console(&snapshot);
    if (restored && !remove_recovery_state()) {
        restored = false;
    }
    if (!restored) {
        fprintf(stderr,
                "CONSOLE RESTORATION WAS NOT VERIFIED. Keep SSH open and run:\n"
                "  sudo %s --recover\n",
                options->program_path != NULL
                    ? options->program_path
                    : "./build/nixbench-wsdisplay-smoke");
    }
    return worker_result == 0 && restored ? 0 : 1;
}

#endif
