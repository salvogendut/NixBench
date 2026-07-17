#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "host_wsdisplay.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

enum {
    NB_HOST_WSDISPLAY_ERROR_CAPACITY = 256
};

static const char default_device_path[] = "/dev/ttyE0";
static char creation_error[NB_HOST_WSDISPLAY_ERROR_CAPACITY];
static int creation_system_error;

static void copy_error(char destination[NB_HOST_WSDISPLAY_ERROR_CAPACITY],
                       const char *operation,
                       int system_error,
                       const char *detail)
{
    if (detail != NULL && detail[0] != '\0') {
        (void)snprintf(destination,
                       NB_HOST_WSDISPLAY_ERROR_CAPACITY,
                       "%s: %s",
                       operation,
                       detail);
    } else if (system_error != 0) {
        (void)snprintf(destination,
                       NB_HOST_WSDISPLAY_ERROR_CAPACITY,
                       "%s: %s",
                       operation,
                       strerror(system_error));
    } else {
        (void)snprintf(destination,
                       NB_HOST_WSDISPLAY_ERROR_CAPACITY,
                       "%s",
                       operation);
    }
}

static void set_creation_error(const char *operation,
                               int system_error,
                               const char *detail)
{
    creation_system_error = system_error;
    copy_error(creation_error, operation, system_error, detail);
}

void nb_host_wsdisplay_options_init(
    struct nb_host_wsdisplay_options *options)
{
    if (options != NULL) {
        options->device_path = default_device_path;
        options->expected_active_vt = 0;
    }
}

const char *nb_host_wsdisplay_creation_error(void)
{
    return creation_error;
}

int nb_host_wsdisplay_creation_system_error(void)
{
    return creation_system_error;
}

#if !defined(__NetBSD__)

struct nb_host *nb_host_wsdisplay_create(
    const struct nb_host_wsdisplay_options *options)
{
    creation_error[0] = '\0';
    creation_system_error = 0;
    if (options == NULL || options->device_path == NULL ||
        options->device_path[0] == '\0' ||
        options->expected_active_vt < 0) {
        set_creation_error("Invalid wsdisplay host options", EINVAL, NULL);
        return NULL;
    }
#ifdef ENOTSUP
    set_creation_error("NetBSD wsdisplay is unavailable", ENOTSUP, NULL);
#else
    set_creation_error("NetBSD wsdisplay is unavailable", ENOSYS, NULL);
#endif
    return NULL;
}

enum nb_host_result nb_host_wsdisplay_request_vt_switch(
    struct nb_host *host,
    int vt_number)
{
    (void)host;
    if (host == NULL || vt_number < 1 || vt_number > 12) {
        errno = EINVAL;
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
#ifdef ENOTSUP
    errno = ENOTSUP;
#else
    errno = ENOSYS;
#endif
    return NB_HOST_RESULT_UNSUPPORTED;
}

enum nb_host_event_status
nb_host_wsdisplay_wait_event_with_descriptors(
    struct nb_host *host,
    const int *external_descriptors,
    size_t external_descriptor_count,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event,
    struct nb_host_fd_wait_result *wait_result)
{
    (void)host;
    (void)external_descriptors;
    (void)external_descriptor_count;
    (void)timeout_milliseconds;
    if (event != NULL) {
        memset(event, 0, sizeof(*event));
    }
    if (wait_result != NULL) {
        memset(wait_result, 0, sizeof(*wait_result));
#ifdef ENOTSUP
        wait_result->system_error = ENOTSUP;
#else
        wait_result->system_error = ENOSYS;
#endif
    }
    return NB_HOST_EVENT_STATUS_ERROR;
}

#else

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplay_usl_io.h>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "framebuffer_format.h"
#include "framebuffer_shadow.h"
#include "host_backend.h"

enum {
    NB_HOST_WSDISPLAY_EVENT_CAPACITY = 64,
    NB_HOST_WSDISPLAY_SIGNAL_COUNT = 6
};

struct nb_wsdisplay_layout {
    struct wsdisplayio_fbinfo information;
    struct nb_framebuffer_format format;
    struct nb_host_output output;
    size_t framebuffer_size;
    size_t visible_offset;
    size_t mapping_length;
};

struct nb_host_wsdisplay_context {
    int display_fd;
    int signal_pipe[2];
    void *mapping;
    unsigned char *visible_framebuffer;
    size_t mapping_length;
    size_t framebuffer_size;
    size_t framebuffer_stride;
    struct nb_framebuffer_format framebuffer_format;
    struct nb_framebuffer_shadow *framebuffer_shadow;
    size_t framebuffer_shadow_width;
    size_t framebuffer_shadow_height;
    struct nb_host_output output;
    struct nb_host_event events[NB_HOST_WSDISPLAY_EVENT_CAPACITY];
    size_t event_head;
    size_t event_count;
    enum nb_host_state state;
    int system_error;
    char error[NB_HOST_WSDISPLAY_ERROR_CAPACITY];
    struct vt_mode saved_vt_mode;
    unsigned int saved_video;
    struct sigaction saved_signal_actions[NB_HOST_WSDISPLAY_SIGNAL_COUNT];
    size_t installed_signal_count;
    uint64_t release_event_milliseconds;
    uint64_t acquire_event_milliseconds;
    uint64_t quit_event_milliseconds;
    uint64_t output_event_milliseconds;
    bool has_output;
    bool has_error;
    bool failure_event_pending;
    bool release_event_pending;
    bool acquire_event_pending;
    bool quit_event_pending;
    bool output_event_pending;
    bool release_signal_pending;
    bool acquire_signal_pending;
    bool video_supported;
    bool video_forced;
    bool graphics_mode;
    bool vt_mode_saved;
    bool vt_process_set;
    bool signals_installed;
    bool instance_claimed;
};

static const int managed_signals[NB_HOST_WSDISPLAY_SIGNAL_COUNT] = {
    SIGUSR1,
    SIGUSR2,
    SIGINT,
    SIGTERM,
    SIGHUP,
    SIGQUIT
};

static atomic_flag instance_claimed = ATOMIC_FLAG_INIT;
static volatile sig_atomic_t signal_pipe_write_fd = -1;
static volatile sig_atomic_t release_signal_received;
static volatile sig_atomic_t acquire_signal_received;
static volatile sig_atomic_t termination_signal_received;

static const struct nb_host_backend_operations wsdisplay_operations;

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;
    uint64_t seconds;
    uint64_t milliseconds;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return 0;
    }
    seconds = (uint64_t)now.tv_sec;
    if (seconds > UINT64_MAX / UINT64_C(1000)) {
        return UINT64_MAX;
    }
    milliseconds = seconds * UINT64_C(1000);
    if ((uint64_t)now.tv_nsec / UINT64_C(1000000) >
        UINT64_MAX - milliseconds) {
        return UINT64_MAX;
    }
    return milliseconds +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void remember_error(struct nb_host_wsdisplay_context *context,
                           const char *operation,
                           int system_error,
                           const char *detail)
{
    if (context->has_error) {
        return;
    }
    context->system_error = system_error;
    copy_error(context->error, operation, system_error, detail);
    context->has_error = true;
}

static void fail_host(struct nb_host_wsdisplay_context *context,
                      const char *operation,
                      int system_error,
                      const char *detail)
{
    remember_error(context, operation, system_error, detail);
    if (context->state == NB_HOST_STATE_FAILED) {
        return;
    }
    context->state = NB_HOST_STATE_FAILED;
    context->failure_event_pending = true;
    context->release_event_pending = false;
    context->acquire_event_pending = false;
    context->output_event_pending = false;
    context->release_signal_pending = false;
    context->acquire_signal_pending = false;
}

static bool add_size(size_t left, size_t right, size_t *sum)
{
    if (right > SIZE_MAX - left) {
        return false;
    }
    *sum = left + right;
    return true;
}

static bool multiply_size(size_t left, size_t right, size_t *product)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return false;
    }
    *product = left * right;
    return true;
}

static bool outputs_equal(const struct nb_host_output *left,
                          const struct nb_host_output *right)
{
    return left->logical_width == right->logical_width &&
           left->logical_height == right->logical_height &&
           left->pixel_width == right->pixel_width &&
           left->pixel_height == right->pixel_height &&
           left->refresh_millihertz == right->refresh_millihertz;
}

static bool queue_event(struct nb_host_wsdisplay_context *context,
                        const struct nb_host_event *event)
{
    size_t tail;

    if (context->event_count >= NB_HOST_WSDISPLAY_EVENT_CAPACITY) {
        return false;
    }
    tail = (context->event_head + context->event_count) %
           NB_HOST_WSDISPLAY_EVENT_CAPACITY;
    context->events[tail] = *event;
    ++context->event_count;
    return true;
}

static void wsdisplay_signal_handler(int signal_number)
{
    const int saved_errno = errno;
    const unsigned char marker = (unsigned char)signal_number;
    const sig_atomic_t write_fd = signal_pipe_write_fd;

    if (signal_number == SIGUSR1) {
        release_signal_received = 1;
    } else if (signal_number == SIGUSR2) {
        acquire_signal_received = 1;
    } else {
        termination_signal_received = 1;
    }
    if (write_fd >= 0) {
        ssize_t result;

        do {
            result = write((int)write_fd, &marker, sizeof(marker));
        } while (result < 0 && errno == EINTR);
    }
    errno = saved_errno;
}

static bool initialize_signal_set(sigset_t *signals)
{
    size_t index;

    if (sigemptyset(signals) != 0) {
        return false;
    }
    for (index = 0; index < NB_HOST_WSDISPLAY_SIGNAL_COUNT; ++index) {
        if (sigaddset(signals, managed_signals[index]) != 0) {
            return false;
        }
    }
    return true;
}

static bool make_pipe_nonblocking_and_cloexec(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return false;
    }
    flags = fcntl(fd, F_GETFD);
    return flags != -1 &&
           fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
}

static bool create_signal_pipe(struct nb_host_wsdisplay_context *context)
{
    if (pipe(context->signal_pipe) != 0) {
        remember_error(context, "Could not create wsdisplay signal pipe",
                       errno, NULL);
        return false;
    }
    if (!make_pipe_nonblocking_and_cloexec(context->signal_pipe[0]) ||
        !make_pipe_nonblocking_and_cloexec(context->signal_pipe[1])) {
        remember_error(context, "Could not configure wsdisplay signal pipe",
                       errno, NULL);
        return false;
    }
    return true;
}

static bool install_signal_handlers(
    struct nb_host_wsdisplay_context *context)
{
    struct sigaction action;
    sigset_t signals;
    sigset_t previous_mask;
    sigset_t current_mask;
    size_t index;
    int saved_errno;

    if (!initialize_signal_set(&signals) ||
        sigprocmask(SIG_BLOCK, NULL, &current_mask) != 0) {
        remember_error(context, "Could not inspect process signal mask",
                       errno, NULL);
        return false;
    }
    if (sigismember(&current_mask, SIGUSR1) != 0 ||
        sigismember(&current_mask, SIGUSR2) != 0) {
        remember_error(context,
                       "SIGUSR1 or SIGUSR2 is blocked; VT switching would "
                       "time out",
                       EINVAL,
                       NULL);
        return false;
    }
    if (sigprocmask(SIG_BLOCK, &signals, &previous_mask) != 0) {
        remember_error(context, "Could not block wsdisplay signals",
                       errno, NULL);
        return false;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = wsdisplay_signal_handler;
    action.sa_mask = signals;
    action.sa_flags = 0;
    release_signal_received = 0;
    acquire_signal_received = 0;
    termination_signal_received = 0;
    signal_pipe_write_fd = (sig_atomic_t)context->signal_pipe[1];

    for (index = 0; index < NB_HOST_WSDISPLAY_SIGNAL_COUNT; ++index) {
        if (sigaction(managed_signals[index],
                      &action,
                      &context->saved_signal_actions[index]) != 0) {
            saved_errno = errno;
            while (index > 0) {
                --index;
                (void)sigaction(managed_signals[index],
                                &context->saved_signal_actions[index],
                                NULL);
            }
            context->installed_signal_count = 0;
            signal_pipe_write_fd = -1;
            (void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);
            remember_error(context,
                           "Could not install wsdisplay signal handler",
                           saved_errno,
                           NULL);
            return false;
        }
        context->installed_signal_count = index + 1;
    }
    context->signals_installed = true;
    if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) != 0) {
        saved_errno = errno;
        remember_error(context,
                       "Could not restore process signal mask",
                       saved_errno,
                       NULL);
        return false;
    }
    return true;
}

static void uninstall_signal_handlers(
    struct nb_host_wsdisplay_context *context)
{
    sigset_t signals;
    sigset_t previous_mask;
    bool mask_blocked = false;
    size_t index;

    if (!context->signals_installed &&
        context->installed_signal_count == 0) {
        return;
    }
    if (initialize_signal_set(&signals) &&
        sigprocmask(SIG_BLOCK, &signals, &previous_mask) == 0) {
        mask_blocked = true;
    }
    signal_pipe_write_fd = -1;
    for (index = context->installed_signal_count; index > 0; --index) {
        (void)sigaction(managed_signals[index - 1],
                        &context->saved_signal_actions[index - 1],
                        NULL);
    }
    release_signal_received = 0;
    acquire_signal_received = 0;
    termination_signal_received = 0;
    context->installed_signal_count = 0;
    context->signals_installed = false;
    if (mask_blocked) {
        (void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);
    }
}

static void discard_pending_vt_signals(void)
{
    const struct timespec no_wait = {0, 0};
    sigset_t vt_signals;

    if (sigemptyset(&vt_signals) != 0 ||
        sigaddset(&vt_signals, SIGUSR1) != 0 ||
        sigaddset(&vt_signals, SIGUSR2) != 0) {
        return;
    }
    for (;;) {
        if (sigtimedwait(&vt_signals, NULL, &no_wait) >= 0) {
            continue;
        }
        if (errno != EINTR) {
            return;
        }
    }
}

static bool drain_signal_pipe(struct nb_host_wsdisplay_context *context)
{
    unsigned char buffer[64];

    for (;;) {
        const ssize_t count = read(context->signal_pipe[0],
                                   buffer,
                                   sizeof(buffer));

        if (count > 0) {
            continue;
        }
        if (count == 0) {
            fail_host(context,
                      "wsdisplay signal pipe closed unexpectedly",
                      EPIPE,
                      NULL);
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        fail_host(context, "Could not read wsdisplay signal pipe",
                  errno, NULL);
        return false;
    }
}

static void process_lifecycle_signals(
    struct nb_host_wsdisplay_context *context,
    uint64_t milliseconds)
{
    if (context->state == NB_HOST_STATE_FAILED) {
        context->release_signal_pending = false;
        context->acquire_signal_pending = false;
        return;
    }

    if (context->release_signal_pending) {
        if (context->state == NB_HOST_STATE_ACTIVE) {
            context->state = NB_HOST_STATE_RELEASE_PENDING;
            context->release_event_pending = true;
            context->release_event_milliseconds = milliseconds;
        }
        context->release_signal_pending = false;
    }

    if (context->acquire_signal_pending) {
        if (context->state == NB_HOST_STATE_SUSPENDED) {
            context->state = NB_HOST_STATE_ACQUIRE_PENDING;
            context->acquire_event_pending = true;
            context->acquire_event_milliseconds = milliseconds;
            context->acquire_signal_pending = false;
        } else if (context->state != NB_HOST_STATE_RELEASE_PENDING) {
            context->acquire_signal_pending = false;
        }
    }
}

static bool take_signal_flags(struct nb_host_wsdisplay_context *context)
{
    sigset_t signals;
    sigset_t previous_mask;
    sig_atomic_t release_received;
    sig_atomic_t acquire_received;
    sig_atomic_t terminate_received;
    uint64_t milliseconds;

    if (!initialize_signal_set(&signals) ||
        sigprocmask(SIG_BLOCK, &signals, &previous_mask) != 0) {
        fail_host(context, "Could not block wsdisplay signals", errno, NULL);
        return false;
    }
    release_received = release_signal_received;
    acquire_received = acquire_signal_received;
    terminate_received = termination_signal_received;
    release_signal_received = 0;
    acquire_signal_received = 0;
    termination_signal_received = 0;
    if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) != 0) {
        fail_host(context,
                  "Could not restore process signal mask",
                  errno,
                  NULL);
        return false;
    }

    milliseconds = monotonic_milliseconds();
    if (release_received != 0) {
        context->release_signal_pending = true;
    }
    if (acquire_received != 0) {
        context->acquire_signal_pending = true;
    }
    if (terminate_received != 0 && !context->quit_event_pending) {
        context->quit_event_pending = true;
        context->quit_event_milliseconds = milliseconds;
    }
    process_lifecycle_signals(context, milliseconds);
    return context->state != NB_HOST_STATE_FAILED;
}

static bool ingest_signals(struct nb_host_wsdisplay_context *context)
{
    if (!context->signals_installed) {
        return true;
    }
    return drain_signal_pipe(context) && take_signal_flags(context);
}

static bool unsupported_video_error(int system_error)
{
    return system_error == ENOTTY || system_error == EINVAL ||
           system_error == ENODEV || system_error == ENOSYS ||
           system_error == ENOTSUP || system_error == EOPNOTSUPP;
}

static bool save_video_state(struct nb_host_wsdisplay_context *context)
{
    unsigned int video;

    if (ioctl(context->display_fd, WSDISPLAYIO_GVIDEO, &video) == 0) {
        context->saved_video = video;
        context->video_supported = true;
        return true;
    }
    if (unsupported_video_error(errno)) {
        context->video_supported = false;
        return true;
    }
    remember_error(context, "Could not query wsdisplay video state",
                   errno, NULL);
    return false;
}

static bool force_video_on(struct nb_host_wsdisplay_context *context,
                           bool remember_failure)
{
    unsigned int video = WSDISPLAYIO_VIDEO_ON;

    if (!context->video_supported) {
        return true;
    }
    if (ioctl(context->display_fd, WSDISPLAYIO_SVIDEO, &video) != 0) {
        if (remember_failure) {
            remember_error(context, "Could not enable wsdisplay video",
                           errno, NULL);
        }
        return false;
    }
    context->video_forced = true;
    return true;
}

static bool restore_video_state(
    struct nb_host_wsdisplay_context *context,
    bool remember_failure)
{
    if (!context->video_supported || !context->video_forced) {
        return true;
    }
    if (ioctl(context->display_fd,
              WSDISPLAYIO_SVIDEO,
              &context->saved_video) != 0) {
        if (remember_failure) {
            remember_error(context, "Could not restore wsdisplay video",
                           errno, NULL);
        }
        return false;
    }
    context->video_forced = false;
    return true;
}

static bool set_display_mode(struct nb_host_wsdisplay_context *context,
                             unsigned int mode,
                             bool remember_failure)
{
    if (ioctl(context->display_fd, WSDISPLAYIO_SMODE, &mode) != 0) {
        if (remember_failure) {
            remember_error(context,
                           mode == WSDISPLAYIO_MODE_DUMBFB
                               ? "Could not enter wsdisplay DUMBFB mode"
                               : "Could not restore wsdisplay emulation mode",
                           errno,
                           NULL);
        }
        return false;
    }
    context->graphics_mode = mode == WSDISPLAYIO_MODE_DUMBFB;
    return true;
}

static bool validate_fbinfo(struct nb_host_wsdisplay_context *context,
                            const struct wsdisplayio_fbinfo *information,
                            struct nb_wsdisplay_layout *layout)
{
    enum nb_framebuffer_status format_status;
    size_t pixel_size;
    size_t row_size;
    size_t preceding_rows;
    size_t visible_required;
    size_t span;
    size_t page_size;
    size_t rounded;
    long system_page_size;

    if (information->fbi_pixeltype != WSFB_RGB) {
        remember_error(context,
                       "wsdisplay framebuffer is not RGB",
                       0,
                       NULL);
        return false;
    }
    if ((information->fbi_flags & WSFB_VRAM_IS_SPLIT) != 0) {
        remember_error(context,
                       "Split wsdisplay framebuffer memory is unsupported",
                       0,
                       NULL);
        return false;
    }
    if (information->fbi_width == 0 || information->fbi_height == 0 ||
        information->fbi_width > (uint32_t)INT_MAX ||
        information->fbi_height > (uint32_t)INT_MAX ||
        information->fbi_fbsize == 0 ||
        information->fbi_fbsize > (uint64_t)SIZE_MAX ||
        information->fbi_fboffset > (uint64_t)SIZE_MAX) {
        remember_error(context,
                       "Invalid wsdisplay framebuffer dimensions or size",
                       0,
                       NULL);
        return false;
    }

    memset(layout, 0, sizeof(*layout));
    layout->information = *information;
    layout->format.bits_per_pixel = information->fbi_bitsperpixel;
    layout->format.red.offset =
        information->fbi_subtype.fbi_rgbmasks.red_offset;
    layout->format.red.size =
        information->fbi_subtype.fbi_rgbmasks.red_size;
    layout->format.green.offset =
        information->fbi_subtype.fbi_rgbmasks.green_offset;
    layout->format.green.size =
        information->fbi_subtype.fbi_rgbmasks.green_size;
    layout->format.blue.offset =
        information->fbi_subtype.fbi_rgbmasks.blue_offset;
    layout->format.blue.size =
        information->fbi_subtype.fbi_rgbmasks.blue_size;
    layout->format.alpha.offset =
        information->fbi_subtype.fbi_rgbmasks.alpha_offset;
    layout->format.alpha.size =
        information->fbi_subtype.fbi_rgbmasks.alpha_size;
    format_status = nb_framebuffer_format_validate(&layout->format);
    if (format_status != NB_FRAMEBUFFER_OK) {
        remember_error(context,
                       "Unsupported wsdisplay RGB layout",
                       0,
                       nb_framebuffer_status_string(format_status));
        return false;
    }

    pixel_size = (size_t)information->fbi_bitsperpixel / 8;
    if (!multiply_size((size_t)information->fbi_width,
                       pixel_size,
                       &row_size) ||
        row_size > (size_t)information->fbi_stride ||
        !multiply_size((size_t)information->fbi_height - 1,
                       (size_t)information->fbi_stride,
                       &preceding_rows) ||
        !add_size(preceding_rows, row_size, &visible_required) ||
        visible_required > (size_t)information->fbi_fbsize) {
        remember_error(context,
                       "Invalid wsdisplay framebuffer stride or visible size",
                       0,
                       NULL);
        return false;
    }
    if (information->fbi_fboffset >
        UINT64_MAX - information->fbi_fbsize ||
        !add_size((size_t)information->fbi_fboffset,
                  (size_t)information->fbi_fbsize,
                  &span)) {
        remember_error(context,
                       "wsdisplay framebuffer mapping size overflow",
                       0,
                       NULL);
        return false;
    }
    errno = 0;
    system_page_size = sysconf(_SC_PAGESIZE);
    if (system_page_size <= 0) {
        remember_error(context,
                       "Could not query system page size",
                       errno != 0 ? errno : EINVAL,
                       NULL);
        return false;
    }
    page_size = (size_t)system_page_size;
    if (!add_size(span, page_size - 1, &rounded)) {
        remember_error(context,
                       "wsdisplay page-rounded mapping size overflow",
                       0,
                       NULL);
        return false;
    }
    rounded -= rounded % page_size;
    if (rounded == 0) {
        remember_error(context,
                       "Invalid wsdisplay framebuffer mapping length",
                       0,
                       NULL);
        return false;
    }

    layout->output.logical_width = (int)information->fbi_width;
    layout->output.logical_height = (int)information->fbi_height;
    layout->output.pixel_width = (int)information->fbi_width;
    layout->output.pixel_height = (int)information->fbi_height;
    layout->output.refresh_millihertz = 0;
    layout->framebuffer_size = (size_t)information->fbi_fbsize;
    layout->visible_offset = (size_t)information->fbi_fboffset;
    layout->mapping_length = rounded;
    return true;
}

static bool query_layout(struct nb_host_wsdisplay_context *context,
                         struct nb_wsdisplay_layout *layout)
{
    struct wsdisplayio_fbinfo information;

    memset(&information, 0, sizeof(information));
    if (ioctl(context->display_fd,
              WSDISPLAYIO_GET_FBINFO,
              &information) != 0) {
        remember_error(context,
                       "Could not query wsdisplay framebuffer information",
                       errno,
                       NULL);
        return false;
    }
    return validate_fbinfo(context, &information, layout);
}

static bool map_layout(struct nb_host_wsdisplay_context *context,
                       const struct nb_wsdisplay_layout *layout)
{
    void *mapping;

    mapping = mmap(NULL,
                   layout->mapping_length,
                   PROT_READ | PROT_WRITE,
                   MAP_SHARED,
                   context->display_fd,
                   (off_t)0);
    if (mapping == MAP_FAILED) {
        remember_error(context, "Could not map wsdisplay framebuffer",
                       errno, NULL);
        return false;
    }
    context->mapping = mapping;
    context->mapping_length = layout->mapping_length;
    context->visible_framebuffer =
        (unsigned char *)mapping + layout->visible_offset;
    context->framebuffer_size = layout->framebuffer_size;
    context->framebuffer_stride =
        (size_t)layout->information.fbi_stride;
    context->framebuffer_format = layout->format;
    context->output = layout->output;
    context->has_output = true;
    if (context->framebuffer_shadow != NULL &&
        (context->framebuffer_shadow_width !=
             (size_t)layout->output.pixel_width ||
         context->framebuffer_shadow_height !=
             (size_t)layout->output.pixel_height)) {
        nb_framebuffer_shadow_destroy(context->framebuffer_shadow);
        context->framebuffer_shadow = NULL;
        context->framebuffer_shadow_width = 0;
        context->framebuffer_shadow_height = 0;
    }
    if (context->framebuffer_shadow == NULL) {
        context->framebuffer_shadow = nb_framebuffer_shadow_create(
            (size_t)layout->output.pixel_width,
            (size_t)layout->output.pixel_height);
        if (context->framebuffer_shadow != NULL) {
            context->framebuffer_shadow_width =
                (size_t)layout->output.pixel_width;
            context->framebuffer_shadow_height =
                (size_t)layout->output.pixel_height;
        }
    }
    nb_framebuffer_shadow_invalidate(context->framebuffer_shadow);
    return true;
}

static bool unmap_framebuffer(struct nb_host_wsdisplay_context *context,
                              bool remember_failure)
{
    nb_framebuffer_shadow_invalidate(context->framebuffer_shadow);
    if (context->mapping == NULL) {
        return true;
    }
    if (munmap(context->mapping, context->mapping_length) != 0) {
        if (remember_failure) {
            remember_error(context,
                           "Could not unmap wsdisplay framebuffer",
                           errno,
                           NULL);
        }
        return false;
    }
    context->mapping = NULL;
    context->visible_framebuffer = NULL;
    context->mapping_length = 0;
    context->framebuffer_size = 0;
    context->framebuffer_stride = 0;
    memset(&context->framebuffer_format,
           0,
           sizeof(context->framebuffer_format));
    return true;
}

static bool enter_graphics(struct nb_host_wsdisplay_context *context)
{
    struct nb_wsdisplay_layout layout;

    return set_display_mode(context,
                            WSDISPLAYIO_MODE_DUMBFB,
                            true) &&
           query_layout(context, &layout) &&
           map_layout(context, &layout) &&
           force_video_on(context, true);
}

static void leave_graphics_best_effort(
    struct nb_host_wsdisplay_context *context)
{
    (void)unmap_framebuffer(context, false);
    if (context->graphics_mode) {
        (void)set_display_mode(context,
                               WSDISPLAYIO_MODE_EMUL,
                               false);
    }
    (void)restore_video_state(context, false);
}

static bool pop_pending_event(struct nb_host_wsdisplay_context *context,
                              struct nb_host_event *event)
{
    memset(event, 0, sizeof(*event));
    if (context->failure_event_pending) {
        event->type = NB_HOST_EVENT_FAILED;
        event->milliseconds = monotonic_milliseconds();
        event->data.failed.system_error = context->system_error;
        context->failure_event_pending = false;
        return true;
    }
    if (context->release_event_pending) {
        event->type = NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED;
        event->milliseconds = context->release_event_milliseconds;
        context->release_event_pending = false;
        return true;
    }
    if (context->acquire_event_pending) {
        event->type = NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED;
        event->milliseconds = context->acquire_event_milliseconds;
        context->acquire_event_pending = false;
        return true;
    }
    if (context->quit_event_pending) {
        event->type = NB_HOST_EVENT_QUIT;
        event->milliseconds = context->quit_event_milliseconds;
        context->quit_event_pending = false;
        return true;
    }
    if (context->output_event_pending) {
        event->type = NB_HOST_EVENT_OUTPUT_CHANGED;
        event->milliseconds = context->output_event_milliseconds;
        event->data.output = context->output;
        context->output_event_pending = false;
        return true;
    }
    return false;
}

static enum nb_host_event_status pop_event(
    struct nb_host_wsdisplay_context *context,
    struct nb_host_event *event)
{
    if (pop_pending_event(context, event)) {
        return NB_HOST_EVENT_STATUS_AVAILABLE;
    }
    if (context->event_count != 0) {
        *event = context->events[context->event_head];
        context->event_head = (context->event_head + 1) %
                              NB_HOST_WSDISPLAY_EVENT_CAPACITY;
        --context->event_count;
        return NB_HOST_EVENT_STATUS_AVAILABLE;
    }
    memset(event, 0, sizeof(*event));
    return context->state == NB_HOST_STATE_FAILED
               ? NB_HOST_EVENT_STATUS_ERROR
               : NB_HOST_EVENT_STATUS_EMPTY;
}

static bool wsdisplay_get_output(const void *opaque,
                                 struct nb_host_output *output)
{
    const struct nb_host_wsdisplay_context *context = opaque;

    if (!context->has_output || context->state == NB_HOST_STATE_FAILED) {
        return false;
    }
    *output = context->output;
    return true;
}

static enum nb_host_state wsdisplay_get_state(const void *opaque)
{
    const struct nb_host_wsdisplay_context *context = opaque;

    return context->state;
}

static uint64_t wsdisplay_monotonic_milliseconds(const void *opaque)
{
    (void)opaque;
    return monotonic_milliseconds();
}

static enum nb_host_event_status wsdisplay_poll_event(
    void *opaque,
    struct nb_host_event *event)
{
    struct nb_host_wsdisplay_context *context = opaque;

    (void)ingest_signals(context);
    return pop_event(context, event);
}

static enum nb_host_event_status wsdisplay_wait_event_with_descriptors(
    struct nb_host_wsdisplay_context *context,
    const int *external_descriptors,
    size_t external_descriptor_count,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event,
    struct nb_host_fd_wait_result *wait_result)
{
    enum nb_host_event_status status;

    status = nb_host_fd_wait_event(
        context->signal_pipe[0],
        external_descriptors,
        external_descriptor_count,
        timeout_milliseconds,
        wsdisplay_poll_event,
        context,
        event,
        wait_result);
    if (status == NB_HOST_EVENT_STATUS_ERROR &&
        wait_result->system_error != 0) {
        fail_host(context,
                  "Could not wait for wsdisplay activity",
                  wait_result->system_error,
                  NULL);
        return pop_event(context, event);
    }
    return status;
}

static enum nb_host_event_status wsdisplay_wait_event(
    void *opaque,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event)
{
    struct nb_host_wsdisplay_context *context = opaque;
    struct nb_host_fd_wait_result wait_result;

    return wsdisplay_wait_event_with_descriptors(context,
                                                 NULL,
                                                 0,
                                                 timeout_milliseconds,
                                                 event,
                                                 &wait_result);
}

enum nb_host_event_status
nb_host_wsdisplay_wait_event_with_descriptors(
    struct nb_host *host,
    const int *external_descriptors,
    size_t external_descriptor_count,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event,
    struct nb_host_fd_wait_result *wait_result)
{
    struct nb_host_wsdisplay_context *context;
    size_t index;
    size_t second;

    if (event != NULL) {
        memset(event, 0, sizeof(*event));
    }
    if (wait_result != NULL) {
        memset(wait_result, 0, sizeof(*wait_result));
    }
    if (host == NULL || event == NULL || wait_result == NULL ||
        external_descriptor_count > NB_HOST_FD_WAIT_MAX_EXTERNAL ||
        (external_descriptor_count != 0 &&
         external_descriptors == NULL)) {
        if (wait_result != NULL) {
            wait_result->system_error = EINVAL;
        }
        return NB_HOST_EVENT_STATUS_ERROR;
    }
    for (index = 0; index < external_descriptor_count; ++index) {
        if (external_descriptors[index] < 0) {
            wait_result->system_error = EINVAL;
            return NB_HOST_EVENT_STATUS_ERROR;
        }
        for (second = index + 1;
             second < external_descriptor_count;
             ++second) {
            if (external_descriptors[index] ==
                external_descriptors[second]) {
                wait_result->system_error = EINVAL;
                return NB_HOST_EVENT_STATUS_ERROR;
            }
        }
    }
    context = nb_host_backend_context(host, &wsdisplay_operations);
    if (context == NULL) {
        wait_result->system_error = EINVAL;
        return NB_HOST_EVENT_STATUS_ERROR;
    }
    for (index = 0; index < external_descriptor_count; ++index) {
        if (external_descriptors[index] == context->signal_pipe[0]) {
            wait_result->system_error = EINVAL;
            return NB_HOST_EVENT_STATUS_ERROR;
        }
    }
    return wsdisplay_wait_event_with_descriptors(
        context,
        external_descriptors,
        external_descriptor_count,
        timeout_milliseconds,
        event,
        wait_result);
}

static bool wsdisplay_set_pointer_capture(void *opaque, bool captured)
{
    (void)opaque;
    (void)captured;
    return false;
}

static enum nb_host_result wsdisplay_present(
    void *opaque,
    const struct nb_host_frame *frame)
{
    struct nb_host_wsdisplay_context *context = opaque;
    struct nb_host_event completed;
    enum nb_framebuffer_source_format source_format;
    enum nb_framebuffer_status conversion_status;
    size_t source_size;
    int damage_x;
    int damage_y;
    int damage_width;
    int damage_height;

    if (!ingest_signals(context)) {
        return NB_HOST_RESULT_ERROR;
    }
    if (context->state != NB_HOST_STATE_ACTIVE) {
        return context->state == NB_HOST_STATE_FAILED
                   ? NB_HOST_RESULT_ERROR
                   : NB_HOST_RESULT_SUSPENDED;
    }
    if (frame->width != context->output.pixel_width ||
        frame->height != context->output.pixel_height ||
        !nb_host_frame_damage(frame,
                              &damage_x,
                              &damage_y,
                              &damage_width,
                              &damage_height)) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    if (context->event_count >= NB_HOST_WSDISPLAY_EVENT_CAPACITY) {
        return NB_HOST_RESULT_WOULD_BLOCK;
    }
    if (!multiply_size(frame->stride,
                       (size_t)frame->height,
                       &source_size)) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    source_format =
        frame->format == NB_HOST_PIXEL_FORMAT_XRGB8888
            ? NB_FRAMEBUFFER_SOURCE_XRGB8888
            : NB_FRAMEBUFFER_SOURCE_ARGB8888;
    if (damage_x != 0 || damage_y != 0 ||
        damage_width != frame->width || damage_height != frame->height) {
        const size_t destination_pixel_size =
            context->framebuffer_format.bits_per_pixel / 8U;
        const size_t source_offset =
            (size_t)damage_y * frame->stride +
            (size_t)damage_x * NB_HOST_BYTES_PER_PIXEL;
        const size_t destination_offset =
            (size_t)damage_y * context->framebuffer_stride +
            (size_t)damage_x * destination_pixel_size;

        conversion_status = nb_framebuffer_convert(
            (const unsigned char *)frame->pixels + source_offset,
            source_size - source_offset,
            frame->stride,
            source_format,
            (unsigned char *)context->visible_framebuffer +
                destination_offset,
            context->framebuffer_size - destination_offset,
            context->framebuffer_stride,
            (size_t)damage_width,
            (size_t)damage_height,
            &context->framebuffer_format);
        nb_framebuffer_shadow_invalidate(context->framebuffer_shadow);
    } else if (context->framebuffer_shadow != NULL) {
        conversion_status = nb_framebuffer_shadow_present(
            context->framebuffer_shadow,
            frame->pixels,
            source_size,
            frame->stride,
            source_format,
            context->visible_framebuffer,
            context->framebuffer_size,
            context->framebuffer_stride,
            &context->framebuffer_format,
            NULL);
    } else {
        conversion_status = nb_framebuffer_convert(
            frame->pixels,
            source_size,
            frame->stride,
            source_format,
            context->visible_framebuffer,
            context->framebuffer_size,
            context->framebuffer_stride,
            (size_t)frame->width,
            (size_t)frame->height,
            &context->framebuffer_format);
    }
    if (conversion_status != NB_FRAMEBUFFER_OK) {
        fail_host(context,
                  "Could not convert frame for wsdisplay",
                  0,
                  nb_framebuffer_status_string(conversion_status));
        return NB_HOST_RESULT_ERROR;
    }

    memset(&completed, 0, sizeof(completed));
    completed.type = NB_HOST_EVENT_FRAME_COMPLETE;
    completed.milliseconds = monotonic_milliseconds();
    completed.data.frame_complete.frame_serial = frame->serial;
    if (!queue_event(context, &completed)) {
        fail_host(context,
                  "Could not queue completed wsdisplay frame",
                  0,
                  NULL);
        return NB_HOST_RESULT_ERROR;
    }
    return NB_HOST_RESULT_OK;
}

static enum nb_host_result wsdisplay_complete_console_release(void *opaque)
{
    struct nb_host_wsdisplay_context *context = opaque;
    int first_error = 0;
    const char *first_operation = NULL;

    if (!ingest_signals(context)) {
        return NB_HOST_RESULT_ERROR;
    }
    if (context->state != NB_HOST_STATE_RELEASE_PENDING) {
        return context->state == NB_HOST_STATE_FAILED
                   ? NB_HOST_RESULT_ERROR
                   : NB_HOST_RESULT_INVALID_STATE;
    }
    if (!unmap_framebuffer(context, true)) {
        first_error = errno;
        first_operation = "Could not unmap wsdisplay framebuffer";
    } else if (!set_display_mode(context,
                                 WSDISPLAYIO_MODE_EMUL,
                                 true)) {
        first_error = errno;
        first_operation = "Could not restore wsdisplay emulation mode";
    } else if (!restore_video_state(context, true)) {
        first_error = errno;
        first_operation = "Could not restore wsdisplay video";
    } else if (ioctl(context->display_fd, VT_RELDISP, VT_TRUE) != 0) {
        first_error = errno;
        first_operation = "Could not release wsdisplay VT";
        remember_error(context, first_operation, first_error, NULL);
    } else {
        context->state = NB_HOST_STATE_SUSPENDED;
        process_lifecycle_signals(context, monotonic_milliseconds());
        return NB_HOST_RESULT_OK;
    }

    (void)ioctl(context->display_fd, VT_RELDISP, VT_FALSE);
    fail_host(context,
              first_operation != NULL
                  ? first_operation
                  : "Could not complete wsdisplay VT release",
              first_error,
              NULL);
    return NB_HOST_RESULT_ERROR;
}

static enum nb_host_result wsdisplay_complete_console_acquire(void *opaque)
{
    struct nb_host_wsdisplay_context *context = opaque;
    const struct nb_host_output previous_output = context->output;
    bool graphics_ready;
    int acknowledge_error = 0;

    if (!ingest_signals(context)) {
        return NB_HOST_RESULT_ERROR;
    }
    if (context->state != NB_HOST_STATE_ACQUIRE_PENDING) {
        return context->state == NB_HOST_STATE_FAILED
                   ? NB_HOST_RESULT_ERROR
                   : NB_HOST_RESULT_INVALID_STATE;
    }

    graphics_ready = enter_graphics(context);
    if (ioctl(context->display_fd, VT_RELDISP, VT_ACKACQ) != 0) {
        acknowledge_error = errno;
        if (graphics_ready) {
            remember_error(context,
                           "Could not acknowledge wsdisplay VT acquisition",
                           acknowledge_error,
                           NULL);
        }
        graphics_ready = false;
    }
    if (!graphics_ready) {
        leave_graphics_best_effort(context);
        if (!context->has_error) {
            remember_error(context,
                           "Could not prepare acquired wsdisplay VT",
                           acknowledge_error,
                           NULL);
        }
        fail_host(context,
                  "Could not prepare acquired wsdisplay VT",
                  acknowledge_error,
                  NULL);
        return NB_HOST_RESULT_ERROR;
    }

    context->state = NB_HOST_STATE_ACTIVE;
    if (!outputs_equal(&previous_output, &context->output)) {
        context->output_event_pending = true;
        context->output_event_milliseconds = monotonic_milliseconds();
    }
    if (!ingest_signals(context)) {
        return NB_HOST_RESULT_ERROR;
    }
    return NB_HOST_RESULT_OK;
}

static bool wsdisplay_get_last_error(const void *opaque,
                                     int *system_error,
                                     char *message,
                                     size_t message_size)
{
    const struct nb_host_wsdisplay_context *context = opaque;

    if (!context->has_error) {
        *system_error = 0;
        message[0] = '\0';
        return false;
    }
    *system_error = context->system_error;
    (void)snprintf(message, message_size, "%s", context->error);
    return true;
}

static void close_context_fd(int *fd)
{
    if (*fd >= 0) {
        (void)close(*fd);
        *fd = -1;
    }
}

static void cleanup_context(struct nb_host_wsdisplay_context *context)
{
    sigset_t signals;
    sigset_t previous_mask;
    bool signals_blocked = false;

    if (context == NULL) {
        return;
    }
    if (context->signals_installed && initialize_signal_set(&signals) &&
        sigprocmask(SIG_BLOCK, &signals, &previous_mask) == 0) {
        signals_blocked = true;
    }
    if (context->display_fd >= 0 && context->vt_process_set) {
        if (context->state == NB_HOST_STATE_RELEASE_PENDING) {
            (void)ioctl(context->display_fd, VT_RELDISP, VT_FALSE);
        } else if (context->state == NB_HOST_STATE_ACQUIRE_PENDING) {
            (void)ioctl(context->display_fd, VT_RELDISP, VT_ACKACQ);
        }
    }
    if (context->display_fd >= 0) {
        leave_graphics_best_effort(context);
        if (context->vt_process_set && context->vt_mode_saved) {
            (void)ioctl(context->display_fd,
                        VT_SETMODE,
                        &context->saved_vt_mode);
            context->vt_process_set = false;
        }
    }
    if (signals_blocked) {
        /* VT_AUTO has detached the kernel sync object; consume any VT signal
         * that raced with it before restoring the caller's dispositions. */
        discard_pending_vt_signals();
    }
    uninstall_signal_handlers(context);
    close_context_fd(&context->signal_pipe[0]);
    close_context_fd(&context->signal_pipe[1]);
    close_context_fd(&context->display_fd);
    nb_framebuffer_shadow_destroy(context->framebuffer_shadow);
    context->framebuffer_shadow = NULL;
    if (context->instance_claimed) {
        atomic_flag_clear_explicit(&instance_claimed,
                                   memory_order_release);
        context->instance_claimed = false;
    }
    if (signals_blocked) {
        (void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);
    }
}

static void wsdisplay_destroy(void *opaque)
{
    struct nb_host_wsdisplay_context *context = opaque;

    cleanup_context(context);
    free(context);
}

static const struct nb_host_backend_operations wsdisplay_operations = {
    .get_output = wsdisplay_get_output,
    .get_state = wsdisplay_get_state,
    .monotonic_milliseconds = wsdisplay_monotonic_milliseconds,
    .poll_event = wsdisplay_poll_event,
    .wait_event = wsdisplay_wait_event,
    .set_pointer_capture = wsdisplay_set_pointer_capture,
    .present = wsdisplay_present,
    .complete_console_release = wsdisplay_complete_console_release,
    .complete_console_acquire = wsdisplay_complete_console_acquire,
    .get_last_error = wsdisplay_get_last_error,
    .destroy = wsdisplay_destroy
};

enum nb_host_result nb_host_wsdisplay_request_vt_switch(
    struct nb_host *host,
    int vt_number)
{
    struct nb_host_wsdisplay_context *context;

    if (host == NULL || vt_number < 1 || vt_number > 12) {
        errno = EINVAL;
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    context = nb_host_backend_context(host, &wsdisplay_operations);
    if (context == NULL) {
        errno = ENOTSUP;
        return NB_HOST_RESULT_UNSUPPORTED;
    }
    if (context->state != NB_HOST_STATE_ACTIVE) {
        errno = EBUSY;
        return context->state == NB_HOST_STATE_SUSPENDED
                   ? NB_HOST_RESULT_SUSPENDED
                   : NB_HOST_RESULT_INVALID_STATE;
    }
    if (ioctl(context->display_fd, VT_ACTIVATE, vt_number) != 0) {
        return NB_HOST_RESULT_ERROR;
    }
    return NB_HOST_RESULT_OK;
}

static void copy_context_creation_error(
    const struct nb_host_wsdisplay_context *context)
{
    if (context->has_error) {
        creation_system_error = context->system_error;
        (void)snprintf(creation_error,
                       sizeof(creation_error),
                       "%s",
                       context->error);
    } else {
        set_creation_error("Could not create wsdisplay host", 0, NULL);
    }
}

struct nb_host *nb_host_wsdisplay_create(
    const struct nb_host_wsdisplay_options *options)
{
    struct nb_host_wsdisplay_context *context;
    struct nb_wsdisplay_layout initial_layout;
    struct nb_host *host;
    struct stat status;
    unsigned int display_mode;
    struct vt_mode process_mode;
    int active_vt = -1;
    int open_flags = O_RDWR | O_NONBLOCK | O_EXCL | O_NOCTTY | O_CLOEXEC;

    creation_error[0] = '\0';
    creation_system_error = 0;
    if (options == NULL || options->device_path == NULL ||
        options->device_path[0] == '\0' ||
        options->expected_active_vt < 0) {
        set_creation_error("Invalid wsdisplay host options", EINVAL, NULL);
        return NULL;
    }
    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        set_creation_error("Could not allocate wsdisplay host", ENOMEM, NULL);
        return NULL;
    }
    context->display_fd = -1;
    context->signal_pipe[0] = -1;
    context->signal_pipe[1] = -1;
    context->state = NB_HOST_STATE_FAILED;
    if (atomic_flag_test_and_set_explicit(&instance_claimed,
                                          memory_order_acquire)) {
        set_creation_error("A wsdisplay host already exists in this process",
                           EBUSY,
                           NULL);
        free(context);
        return NULL;
    }
    context->instance_claimed = true;

    context->display_fd = open(options->device_path, open_flags);
    if (context->display_fd < 0) {
        remember_error(context, "Could not open wsdisplay device",
                       errno, NULL);
        goto failure;
    }
    if (fstat(context->display_fd, &status) != 0) {
        remember_error(context, "Could not inspect wsdisplay device",
                       errno, NULL);
        goto failure;
    }
    if (!S_ISCHR(status.st_mode)) {
        remember_error(context,
                       "wsdisplay path is not a character device",
                       ENOTTY,
                       NULL);
        goto failure;
    }
    if (ioctl(context->display_fd,
              WSDISPLAYIO_GMODE,
              &display_mode) != 0) {
        remember_error(context,
                       "Could not query initial wsdisplay mode",
                       errno,
                       NULL);
        goto failure;
    }
    if (display_mode != WSDISPLAYIO_MODE_EMUL) {
        remember_error(context,
                       "wsdisplay is not initially in emulation mode",
                       EBUSY,
                       NULL);
        goto failure;
    }
    if (!save_video_state(context)) {
        goto failure;
    }
    memset(&context->saved_vt_mode, 0, sizeof(context->saved_vt_mode));
    if (ioctl(context->display_fd,
              VT_GETMODE,
              &context->saved_vt_mode) != 0) {
        remember_error(context, "Could not query wsdisplay VT mode",
                       errno, NULL);
        goto failure;
    }
    context->vt_mode_saved = true;
    if (context->saved_vt_mode.mode != VT_AUTO) {
        remember_error(context,
                       "wsdisplay VT is already process-controlled",
                       EBUSY,
                       NULL);
        goto failure;
    }
    if (!query_layout(context, &initial_layout)) {
        goto failure;
    }
    if (!create_signal_pipe(context) ||
        !install_signal_handlers(context)) {
        goto failure;
    }

    memset(&process_mode, 0, sizeof(process_mode));
    process_mode.mode = VT_PROCESS;
    process_mode.relsig = SIGUSR1;
    process_mode.acqsig = SIGUSR2;
    if (ioctl(context->display_fd, VT_SETMODE, &process_mode) != 0) {
        remember_error(context,
                       "Could not enable process-controlled wsdisplay VT",
                       errno,
                       NULL);
        goto failure;
    }
    context->vt_process_set = true;
    if (options->expected_active_vt > 0) {
        if (ioctl(context->display_fd, VT_GETACTIVE, &active_vt) != 0) {
            remember_error(context,
                           "Could not verify the active wsdisplay VT",
                           errno,
                           NULL);
            goto failure;
        }
        if (active_vt != options->expected_active_vt) {
            remember_error(context,
                           "Target wsdisplay VT is no longer active",
                           EBUSY,
                           NULL);
            goto failure;
        }
    }
    if (!enter_graphics(context)) {
        goto failure;
    }
    context->state = NB_HOST_STATE_ACTIVE;

    host = nb_host_backend_create(&wsdisplay_operations, context);
    if (host == NULL) {
        remember_error(context,
                       "Could not allocate wsdisplay host facade",
                       ENOMEM,
                       NULL);
        goto failure;
    }
    return host;

failure:
    copy_context_creation_error(context);
    cleanup_context(context);
    free(context);
    return NULL;
}

#endif
