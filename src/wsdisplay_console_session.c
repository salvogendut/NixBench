#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "wsdisplay_console_session_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    NB_WSDISPLAY_ACTIVE_SCREEN_MAX = 255,
    NB_WSDISPLAY_OPEN_RETRY_COUNT = 20,
    NB_WSDISPLAY_OPEN_RETRY_MS = 50,
    NB_WSDISPLAY_ACTIVE_POLL_MS = 20,
    NB_WSDISPLAY_ACTIVE_RESTORE_MS = 1000
};

static int result_error(int result)
{
    return result < 0 && result != INT_MIN ? -result : EIO;
}

static uint64_t add_milliseconds(uint64_t start, uint64_t duration)
{
    return duration > UINT64_MAX - start ? UINT64_MAX : start + duration;
}

static bool copy_path(char destination[NB_WSDISPLAY_CONSOLE_PATH_CAPACITY],
                      const char *source)
{
    const int length = snprintf(destination,
                                NB_WSDISPLAY_CONSOLE_PATH_CAPACITY,
                                "%s",
                                source);

    return length >= 0 &&
           (size_t)length < NB_WSDISPLAY_CONSOLE_PATH_CAPACITY;
}

static bool unsupported_video_error(int system_error)
{
    return system_error == ENOTTY || system_error == EINVAL ||
           system_error == ENODEV || system_error == ENOSYS ||
           system_error == ENOTSUP || system_error == EOPNOTSUPP;
}

static void report_errno(FILE *stream,
                         const char *operation,
                         int system_error)
{
    if (stream != NULL) {
        fprintf(stream, "%s: %s\n", operation, strerror(system_error));
    }
}

static void report_path_errno(FILE *stream,
                              const char *operation,
                              const char *path,
                              int system_error)
{
    if (stream != NULL) {
        fprintf(stream,
                "%s %s: %s\n",
                operation,
                path,
                strerror(system_error));
    }
}

static bool operations_are_valid(
    const struct nb_wsdisplay_console_operations *operations)
{
    return operations != NULL && operations->open_device != NULL &&
           operations->close_device != NULL &&
           operations->inspect_character_device != NULL &&
           operations->get_active_screen != NULL &&
           operations->get_display_mode != NULL &&
           operations->set_display_mode != NULL &&
           operations->get_video != NULL &&
           operations->set_video != NULL &&
           operations->get_vt_mode != NULL &&
           operations->set_vt_mode != NULL &&
           operations->get_active_vt != NULL &&
           operations->activate_vt != NULL &&
           operations->monotonic_milliseconds != NULL &&
           operations->sleep_milliseconds != NULL;
}

static bool state_paths_are_valid(
    const struct nb_wsdisplay_console_state *state)
{
    return state->status_device[0] == '/' &&
           state->screen_device[0] == '/' &&
           memchr(state->status_device,
                  '\0',
                  sizeof(state->status_device)) != NULL &&
           memchr(state->screen_device,
                  '\0',
                  sizeof(state->screen_device)) != NULL;
}

static bool inspect_character(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    const char *path,
    FILE *error_stream)
{
    bool is_character = false;
    const int result = operations->inspect_character_device(
        opaque, descriptor, &is_character);

    if (result != 0) {
        report_path_errno(error_stream,
                          "Could not inspect",
                          path,
                          result_error(result));
        return false;
    }
    if (!is_character) {
        if (error_stream != NULL) {
            fprintf(error_stream, "%s is not a character device\n", path);
        }
        return false;
    }
    return true;
}

bool nb_wsdisplay_console_capture_with_operations(
    const struct nb_wsdisplay_console_capture_options *options,
    struct nb_wsdisplay_console_state *state,
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    FILE *error_stream)
{
    int status_device = -1;
    int screen = -1;
    int active_from_screen = -1;
    int active_vt;
    int result;
    int length;
    bool success = false;

    if (options == NULL || state == NULL ||
        options->status_device_path == NULL ||
        options->screen_device_prefix == NULL ||
        !operations_are_valid(operations)) {
        report_errno(error_stream, "Invalid console capture arguments", EINVAL);
        return false;
    }

    memset(state, 0, sizeof(*state));
    if (!copy_path(state->status_device, options->status_device_path)) {
        if (error_stream != NULL) {
            fputs("Status-device path is too long\n", error_stream);
        }
        return false;
    }

    status_device = operations->open_device(
        opaque, state->status_device, false);
    if (status_device < 0) {
        report_path_errno(error_stream,
                          "Could not open",
                          state->status_device,
                          result_error(status_device));
        goto cleanup;
    }
    if (!inspect_character(operations,
                           opaque,
                           status_device,
                           state->status_device,
                           error_stream)) {
        goto cleanup;
    }
    result = operations->get_active_screen(
        opaque, status_device, &state->active_screen);
    if (result != 0) {
        report_errno(error_stream,
                     "Could not query active wsdisplay screen",
                     result_error(result));
        goto cleanup;
    }
    if (state->active_screen < 0 ||
        state->active_screen > NB_WSDISPLAY_ACTIVE_SCREEN_MAX) {
        if (error_stream != NULL) {
            fprintf(error_stream,
                    "Invalid active wsdisplay screen: %d\n",
                    state->active_screen);
        }
        goto cleanup;
    }
    active_vt = state->active_screen + 1;
    length = snprintf(state->screen_device,
                      sizeof(state->screen_device),
                      "%s%d",
                      options->screen_device_prefix,
                      state->active_screen);
    if (length < 0 || (size_t)length >= sizeof(state->screen_device)) {
        if (error_stream != NULL) {
            fputs("Active screen-device path is too long\n", error_stream);
        }
        goto cleanup;
    }

    screen = operations->open_device(opaque, state->screen_device, false);
    if (screen < 0) {
        report_path_errno(error_stream,
                          "Could not open",
                          state->screen_device,
                          result_error(screen));
        goto cleanup;
    }
    if (!inspect_character(operations,
                           opaque,
                           screen,
                           state->screen_device,
                           error_stream)) {
        goto cleanup;
    }
    result = operations->get_display_mode(
        opaque, screen, &state->display_mode);
    if (result != 0) {
        report_errno(error_stream,
                     "Could not query wsdisplay mode",
                     result_error(result));
        goto cleanup;
    }
    if (state->display_mode != operations->emulation_display_mode) {
        if (error_stream != NULL) {
            fprintf(error_stream,
                    "Refusing takeover: active screen mode is %u, not "
                    "emulation\n",
                    state->display_mode);
        }
        goto cleanup;
    }
    result = operations->get_vt_mode(opaque, screen, &state->vt_mode);
    if (result != 0) {
        report_errno(error_stream,
                     "Could not query wsdisplay VT mode",
                     result_error(result));
        goto cleanup;
    }
    if (state->vt_mode.mode != operations->automatic_vt_mode) {
        if (error_stream != NULL) {
            fputs("Refusing takeover: active VT is already "
                  "process-controlled\n",
                  error_stream);
        }
        goto cleanup;
    }
    result = operations->get_active_vt(
        opaque, screen, &active_from_screen);
    if (result != 0 || active_from_screen != active_vt) {
        if (error_stream != NULL) {
            fprintf(error_stream,
                    "Active-screen state changed during preflight "
                    "(screen index %d, VT %d)\n",
                    state->active_screen,
                    active_from_screen);
        }
        goto cleanup;
    }

    result = operations->get_video(opaque, screen, &state->video);
    if (result == 0) {
        state->video_available = true;
    } else if (!unsupported_video_error(result_error(result))) {
        report_errno(error_stream,
                     "Could not query wsdisplay video state",
                     result_error(result));
        goto cleanup;
    }
    success = true;

cleanup:
    if (screen >= 0) {
        (void)operations->close_device(opaque, screen);
    }
    if (status_device >= 0) {
        (void)operations->close_device(opaque, status_device);
    }
    return success;
}

static int retry_set_display_mode(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    unsigned int mode)
{
    int result;

    do {
        result = operations->set_display_mode(
            opaque, descriptor, mode);
    } while (result == -EINTR);
    return result;
}

static int retry_set_video(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    unsigned int video)
{
    int result;

    do {
        result = operations->set_video(opaque, descriptor, video);
    } while (result == -EINTR);
    return result;
}

static int retry_set_vt_mode(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    const struct nb_wsdisplay_console_vt_mode *mode)
{
    int result;

    do {
        result = operations->set_vt_mode(opaque, descriptor, mode);
    } while (result == -EINTR);
    return result;
}

static int retry_get_active_screen(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    int *active_screen)
{
    int result;

    do {
        result = operations->get_active_screen(
            opaque, descriptor, active_screen);
    } while (result == -EINTR);
    return result;
}

static int retry_activate_vt(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    int vt_number)
{
    int result;

    do {
        result = operations->activate_vt(
            opaque, descriptor, vt_number);
    } while (result == -EINTR);
    return result;
}

static int retry_get_display_mode(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    unsigned int *mode)
{
    int result;

    do {
        result = operations->get_display_mode(opaque, descriptor, mode);
    } while (result == -EINTR);
    return result;
}

static int retry_get_video(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    unsigned int *video)
{
    int result;

    do {
        result = operations->get_video(opaque, descriptor, video);
    } while (result == -EINTR);
    return result;
}

static int retry_get_vt_mode(
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    int descriptor,
    struct nb_wsdisplay_console_vt_mode *mode)
{
    int result;

    do {
        result = operations->get_vt_mode(opaque, descriptor, mode);
    } while (result == -EINTR);
    return result;
}

static void mark_restore_error(FILE *stream,
                               const char *operation,
                               int result,
                               bool *success)
{
    if (stream != NULL) {
        fprintf(stream,
                "RESTORE ERROR: %s: %s\n",
                operation,
                strerror(result_error(result)));
    }
    *success = false;
}

bool nb_wsdisplay_console_restore_with_operations(
    const struct nb_wsdisplay_console_state *state,
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    FILE *error_stream)
{
    unsigned int current_mode = UINT_MAX;
    unsigned int current_video = UINT_MAX;
    struct nb_wsdisplay_console_vt_mode current_vt;
    int current_active = -1;
    int screen = -1;
    int status_device = -1;
    int result;
    bool success = true;
    unsigned int retry;

    if (state == NULL || !operations_are_valid(operations) ||
        state->active_screen < 0 ||
        state->active_screen > NB_WSDISPLAY_ACTIVE_SCREEN_MAX ||
        state->display_mode != operations->emulation_display_mode ||
        state->vt_mode.mode != operations->automatic_vt_mode ||
        !state_paths_are_valid(state)) {
        if (error_stream != NULL) {
            fputs("RESTORE ERROR: invalid saved console state\n",
                  error_stream);
        }
        return false;
    }

    for (retry = 0; retry < NB_WSDISPLAY_OPEN_RETRY_COUNT; ++retry) {
        screen = operations->open_device(
            opaque, state->screen_device, true);
        if (screen >= 0 ||
            (result_error(screen) != EBUSY &&
             result_error(screen) != EINTR)) {
            break;
        }
        operations->sleep_milliseconds(
            opaque, NB_WSDISPLAY_OPEN_RETRY_MS);
    }
    if (screen < 0) {
        if (error_stream != NULL) {
            fprintf(error_stream,
                    "RESTORE ERROR: could not open %s: %s\n",
                    state->screen_device,
                    strerror(result_error(screen)));
        }
        return false;
    }
    if (!inspect_character(operations,
                           opaque,
                           screen,
                           state->screen_device,
                           error_stream)) {
        success = false;
    }

    result = retry_set_display_mode(
        operations, opaque, screen, state->display_mode);
    if (result != 0) {
        mark_restore_error(error_stream,
                           "could not restore emulation mode",
                           result,
                           &success);
    }
    if (state->video_available) {
        result = retry_set_video(
            operations, opaque, screen, state->video);
        if (result != 0) {
            mark_restore_error(error_stream,
                               "could not restore video state",
                               result,
                               &success);
        }
    }
    result = retry_set_vt_mode(
        operations, opaque, screen, &state->vt_mode);
    if (result != 0) {
        mark_restore_error(error_stream,
                           "could not restore automatic VT mode",
                           result,
                           &success);
    }

    status_device = operations->open_device(
        opaque, state->status_device, false);
    if (status_device < 0 ||
        !inspect_character(operations,
                           opaque,
                           status_device,
                           state->status_device,
                           error_stream)) {
        if (error_stream != NULL) {
            fprintf(error_stream,
                    "RESTORE ERROR: could not open status device %s: %s\n",
                    state->status_device,
                    strerror(status_device < 0
                                 ? result_error(status_device)
                                 : ENOTTY));
        }
        success = false;
    } else {
        result = retry_get_active_screen(
            operations, opaque, status_device, &current_active);
        if (result != 0) {
            mark_restore_error(error_stream,
                               "could not query active screen",
                               result,
                               &success);
        } else if (current_active != state->active_screen) {
            const uint64_t deadline = add_milliseconds(
                operations->monotonic_milliseconds(opaque),
                NB_WSDISPLAY_ACTIVE_RESTORE_MS);

            result = retry_activate_vt(
                operations,
                opaque,
                screen,
                state->active_screen + 1);
            if (result != 0) {
                mark_restore_error(error_stream,
                                   "could not reactivate saved screen",
                                   result,
                                   &success);
            } else {
                do {
                    operations->sleep_milliseconds(
                        opaque, NB_WSDISPLAY_ACTIVE_POLL_MS);
                    result = retry_get_active_screen(
                        operations,
                        opaque,
                        status_device,
                        &current_active);
                    if (result != 0) {
                        mark_restore_error(
                            error_stream,
                            "could not verify active screen",
                            result,
                            &success);
                        break;
                    }
                } while (current_active != state->active_screen &&
                         operations->monotonic_milliseconds(opaque) <
                             deadline);
                if (current_active != state->active_screen) {
                    if (error_stream != NULL) {
                        fputs("RESTORE ERROR: saved screen did not become "
                              "active\n",
                              error_stream);
                    }
                    success = false;
                }
            }
        }
    }

    memset(&current_vt, 0, sizeof(current_vt));
    result = retry_get_display_mode(
        operations, opaque, screen, &current_mode);
    if (result != 0 || current_mode != state->display_mode) {
        if (error_stream != NULL) {
            fputs("RESTORE ERROR: display mode verification failed\n",
                  error_stream);
        }
        success = false;
    }
    if (state->video_available) {
        result = retry_get_video(
            operations, opaque, screen, &current_video);
        if (result != 0 || current_video != state->video) {
            if (error_stream != NULL) {
                fputs("RESTORE ERROR: video-state verification failed\n",
                      error_stream);
            }
            success = false;
        }
    }
    result = retry_get_vt_mode(
        operations, opaque, screen, &current_vt);
    if (result != 0 ||
        current_vt.mode != state->vt_mode.mode ||
        current_vt.waitv != state->vt_mode.waitv ||
        current_vt.relsig != state->vt_mode.relsig ||
        current_vt.acqsig != state->vt_mode.acqsig ||
        current_vt.frsig != state->vt_mode.frsig) {
        if (error_stream != NULL) {
            fputs("RESTORE ERROR: VT-mode verification failed\n",
                  error_stream);
        }
        success = false;
    }
    if (status_device >= 0) {
        result = retry_get_active_screen(
            operations, opaque, status_device, &current_active);
        if (result != 0 || current_active != state->active_screen) {
            if (error_stream != NULL) {
                fputs("RESTORE ERROR: active-screen verification failed\n",
                      error_stream);
            }
            success = false;
        }
    }

    if (status_device >= 0) {
        (void)operations->close_device(opaque, status_device);
    }
    (void)operations->close_device(opaque, screen);
    return success;
}

#if defined(__NetBSD__)

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplay_usl_io.h>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

static int current_error_or_io(void)
{
    return errno > 0 ? errno : EIO;
}

static int real_open_device(void *opaque,
                            const char *path,
                            bool writable)
{
    int flags = (writable ? O_RDWR : O_RDONLY) | O_NONBLOCK | O_NOCTTY;
    int descriptor;

    (void)opaque;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    do {
        errno = 0;
        descriptor = open(path, flags);
    } while (descriptor < 0 && errno == EINTR);
    return descriptor >= 0 ? descriptor : -current_error_or_io();
}

static int real_close_device(void *opaque, int descriptor)
{
    (void)opaque;
    errno = 0;
    return close(descriptor) == 0 ? 0 : -current_error_or_io();
}

static int real_inspect_character_device(void *opaque,
                                         int descriptor,
                                         bool *is_character)
{
    struct stat status;

    (void)opaque;
    errno = 0;
    if (fstat(descriptor, &status) != 0) {
        return -current_error_or_io();
    }
    *is_character = S_ISCHR(status.st_mode);
    return 0;
}

static int real_get_active_screen(void *opaque,
                                  int descriptor,
                                  int *active_screen)
{
    (void)opaque;
    errno = 0;
    return ioctl(descriptor,
                 WSDISPLAYIO_GETACTIVESCREEN,
                 active_screen) == 0
               ? 0
               : -current_error_or_io();
}

static int real_get_display_mode(void *opaque,
                                 int descriptor,
                                 unsigned int *mode)
{
    (void)opaque;
    errno = 0;
    return ioctl(descriptor, WSDISPLAYIO_GMODE, mode) == 0
               ? 0
               : -current_error_or_io();
}

static int real_set_display_mode(void *opaque,
                                 int descriptor,
                                 unsigned int mode)
{
    (void)opaque;
    errno = 0;
    return ioctl(descriptor, WSDISPLAYIO_SMODE, &mode) == 0
               ? 0
               : -current_error_or_io();
}

static int real_get_video(void *opaque,
                          int descriptor,
                          unsigned int *video)
{
    (void)opaque;
    errno = 0;
    return ioctl(descriptor, WSDISPLAYIO_GVIDEO, video) == 0
               ? 0
               : -current_error_or_io();
}

static int real_set_video(void *opaque,
                          int descriptor,
                          unsigned int video)
{
    (void)opaque;
    errno = 0;
    return ioctl(descriptor, WSDISPLAYIO_SVIDEO, &video) == 0
               ? 0
               : -current_error_or_io();
}

static void vt_mode_from_native(
    struct nb_wsdisplay_console_vt_mode *destination,
    const struct vt_mode *source)
{
    destination->mode = source->mode;
    destination->waitv = source->waitv;
    destination->relsig = source->relsig;
    destination->acqsig = source->acqsig;
    destination->frsig = source->frsig;
}

static bool vt_mode_to_native(
    struct vt_mode *destination,
    const struct nb_wsdisplay_console_vt_mode *source)
{
    if (source->mode < CHAR_MIN || source->mode > CHAR_MAX ||
        source->waitv < CHAR_MIN || source->waitv > CHAR_MAX ||
        source->relsig < SHRT_MIN || source->relsig > SHRT_MAX ||
        source->acqsig < SHRT_MIN || source->acqsig > SHRT_MAX ||
        source->frsig < SHRT_MIN || source->frsig > SHRT_MAX) {
        return false;
    }
    memset(destination, 0, sizeof(*destination));
    destination->mode = (char)source->mode;
    destination->waitv = (char)source->waitv;
    destination->relsig = (short)source->relsig;
    destination->acqsig = (short)source->acqsig;
    destination->frsig = (short)source->frsig;
    return true;
}

static int real_get_vt_mode(
    void *opaque,
    int descriptor,
    struct nb_wsdisplay_console_vt_mode *mode)
{
    struct vt_mode native_mode;

    (void)opaque;
    memset(&native_mode, 0, sizeof(native_mode));
    errno = 0;
    if (ioctl(descriptor, VT_GETMODE, &native_mode) != 0) {
        return -current_error_or_io();
    }
    vt_mode_from_native(mode, &native_mode);
    return 0;
}

static int real_set_vt_mode(
    void *opaque,
    int descriptor,
    const struct nb_wsdisplay_console_vt_mode *mode)
{
    struct vt_mode native_mode;

    (void)opaque;
    if (!vt_mode_to_native(&native_mode, mode)) {
        return -EINVAL;
    }
    errno = 0;
    return ioctl(descriptor, VT_SETMODE, &native_mode) == 0
               ? 0
               : -current_error_or_io();
}

static int real_get_active_vt(void *opaque,
                              int descriptor,
                              int *active_vt)
{
    (void)opaque;
    errno = 0;
    return ioctl(descriptor, VT_GETACTIVE, active_vt) == 0
               ? 0
               : -current_error_or_io();
}

static int real_activate_vt(void *opaque,
                            int descriptor,
                            int vt_number)
{
    (void)opaque;
    errno = 0;
    return ioctl(descriptor, VT_ACTIVATE, vt_number) == 0
               ? 0
               : -current_error_or_io();
}

static uint64_t real_monotonic_milliseconds(void *opaque)
{
    struct timespec now;

    (void)opaque;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return 0;
    }
    if ((uint64_t)now.tv_sec > UINT64_MAX / UINT64_C(1000)) {
        return UINT64_MAX;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void real_sleep_milliseconds(void *opaque, unsigned int milliseconds)
{
    struct timespec request;

    (void)opaque;
    request.tv_sec = (time_t)(milliseconds / 1000U);
    request.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

static const struct nb_wsdisplay_console_operations real_operations = {
    .emulation_display_mode = WSDISPLAYIO_MODE_EMUL,
    .automatic_vt_mode = VT_AUTO,
    .open_device = real_open_device,
    .close_device = real_close_device,
    .inspect_character_device = real_inspect_character_device,
    .get_active_screen = real_get_active_screen,
    .get_display_mode = real_get_display_mode,
    .set_display_mode = real_set_display_mode,
    .get_video = real_get_video,
    .set_video = real_set_video,
    .get_vt_mode = real_get_vt_mode,
    .set_vt_mode = real_set_vt_mode,
    .get_active_vt = real_get_active_vt,
    .activate_vt = real_activate_vt,
    .monotonic_milliseconds = real_monotonic_milliseconds,
    .sleep_milliseconds = real_sleep_milliseconds
};

bool nb_wsdisplay_console_capture(
    const struct nb_wsdisplay_console_capture_options *options,
    struct nb_wsdisplay_console_state *state)
{
    return nb_wsdisplay_console_capture_with_operations(
        options, state, &real_operations, NULL, stderr);
}

bool nb_wsdisplay_console_restore_and_verify(
    const struct nb_wsdisplay_console_state *state)
{
    const bool success = nb_wsdisplay_console_restore_with_operations(
        state, &real_operations, NULL, stderr);

    if (success) {
        puts("Supervisor verified console restoration: emulation, video, "
             "VT mode, and active screen match the saved state.");
    }
    return success;
}

#else

bool nb_wsdisplay_console_capture(
    const struct nb_wsdisplay_console_capture_options *options,
    struct nb_wsdisplay_console_state *state)
{
    (void)options;
    (void)state;
    fputs("NetBSD wsdisplay console capture is unavailable\n", stderr);
    return false;
}

bool nb_wsdisplay_console_restore_and_verify(
    const struct nb_wsdisplay_console_state *state)
{
    (void)state;
    fputs("NetBSD wsdisplay console restoration is unavailable\n", stderr);
    return false;
}

#endif
