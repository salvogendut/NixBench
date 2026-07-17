#ifndef NIXBENCH_HOST_H
#define NIXBENCH_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_HOST_BYTES_PER_PIXEL = 4,
    NB_HOST_XKB_KEY_NAME_CAPACITY = 5
};

/*
 * Coordinates exposed by a host are always NixBench logical desktop
 * coordinates. Pixel dimensions describe the frame accepted by present().
 * A refresh value of zero means that the host cannot report it.
 */
struct nb_host_output {
    int logical_width;
    int logical_height;
    int pixel_width;
    int pixel_height;
    int refresh_millihertz;
};

/* Native-endian 32-bit words with the indicated 0xAARRGGBB interpretation. */
enum nb_host_pixel_format {
    NB_HOST_PIXEL_FORMAT_XRGB8888,
    NB_HOST_PIXEL_FORMAT_ARGB8888_PREMULTIPLIED
};

/*
 * A retained software-composited frame. Pixels are borrowed only for the
 * duration of nb_host_present(). The damage rectangle selects pixels changed
 * since the prior accepted frame; the first frame after creation or resume is
 * complete. Accepted serial numbers are nonzero and strictly increasing for
 * the lifetime of a host; an unaccepted serial may be retried. Stride is
 * measured in bytes.
 */
struct nb_host_frame {
    const void *pixels;
    int width;
    int height;
    size_t stride;
    enum nb_host_pixel_format format;
    uint64_t serial;
    /*
     * A zero-sized damage rectangle means the complete frame. Otherwise the
     * rectangle is validated inside the frame and only those pixels need to
     * be transferred or copied to the output.
     */
    int damage_x;
    int damage_y;
    int damage_width;
    int damage_height;
};

enum nb_host_pointer_button {
    NB_HOST_POINTER_BUTTON_LEFT,
    NB_HOST_POINTER_BUTTON_MIDDLE,
    NB_HOST_POINTER_BUTTON_RIGHT,
    NB_HOST_POINTER_BUTTON_SIDE,
    NB_HOST_POINTER_BUTTON_EXTRA,
    NB_HOST_POINTER_BUTTON_COUNT
};

enum nb_host_event_type {
    NB_HOST_EVENT_NONE,
    NB_HOST_EVENT_QUIT,
    NB_HOST_EVENT_OUTPUT_CHANGED,
    NB_HOST_EVENT_FOCUS_CHANGED,
    NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED,
    NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED,
    NB_HOST_EVENT_POINTER_MOTION,
    NB_HOST_EVENT_POINTER_BUTTON,
    NB_HOST_EVENT_POINTER_LEAVE,
    NB_HOST_EVENT_KEY,
    NB_HOST_EVENT_FRAME_COMPLETE,
    NB_HOST_EVENT_FAILED
};

struct nb_host_event {
    enum nb_host_event_type type;
    uint64_t milliseconds;
    union {
        struct nb_host_output output;
        struct {
            bool focused;
        } focus;
        struct {
            int x;
            int y;
        } pointer_motion;
        struct {
            int x;
            int y;
            enum nb_host_pointer_button button;
            bool pressed;
        } pointer_button;
        struct {
            /* XKB physical key name, for example "AC01" or "RTRN". */
            char xkb_key_name[NB_HOST_XKB_KEY_NAME_CAPACITY];
            bool pressed;
            bool repeat;
        } key;
        struct {
            uint64_t frame_serial;
        } frame_complete;
        struct {
            int system_error;
        } failed;
    } data;
};

enum nb_host_event_status {
    NB_HOST_EVENT_STATUS_ERROR = -1,
    NB_HOST_EVENT_STATUS_EMPTY,
    NB_HOST_EVENT_STATUS_AVAILABLE
};

enum nb_host_state {
    NB_HOST_STATE_ACTIVE,
    NB_HOST_STATE_RELEASE_PENDING,
    NB_HOST_STATE_SUSPENDED,
    NB_HOST_STATE_ACQUIRE_PENDING,
    NB_HOST_STATE_FAILED
};

enum nb_host_result {
    NB_HOST_RESULT_ERROR = -1,
    NB_HOST_RESULT_OK,
    NB_HOST_RESULT_WOULD_BLOCK,
    NB_HOST_RESULT_SUSPENDED,
    NB_HOST_RESULT_UNSUPPORTED,
    NB_HOST_RESULT_INVALID_ARGUMENT,
    NB_HOST_RESULT_INVALID_STATE
};

struct nb_host;

bool nb_host_output_is_valid(const struct nb_host_output *output);
bool nb_host_frame_is_valid(const struct nb_host_frame *frame);
bool nb_host_frame_damage(const struct nb_host_frame *frame,
                          int *x,
                          int *y,
                          int *width,
                          int *height);
bool nb_host_event_is_valid(const struct nb_host_event *event);

bool nb_host_get_output(const struct nb_host *host,
                        struct nb_host_output *output);
enum nb_host_state nb_host_get_state(const struct nb_host *host);
uint64_t nb_host_monotonic_milliseconds(const struct nb_host *host);

/* poll is nonblocking; wait blocks for at most timeout_milliseconds. */
enum nb_host_event_status nb_host_poll_event(
    struct nb_host *host,
    struct nb_host_event *event);
enum nb_host_event_status nb_host_wait_event(
    struct nb_host *host,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event);

/* Capture is a request and may be rejected by a backend. */
bool nb_host_set_pointer_capture(struct nb_host *host, bool captured);

/*
 * NB_HOST_RESULT_OK means that the backend accepted the serial and borrowed
 * pixels no longer need to remain valid. The backend emits
 * NB_HOST_EVENT_FRAME_COMPLETE when its presentation operation completes. For
 * page-flipped outputs this can be scanout completion; for a memory-mapped
 * framebuffer it means that the copy into device memory finished.
 *
 * WOULD_BLOCK and SUSPENDED retain the caller's serial as unsubmitted, so the
 * same frame can be retried. INVALID_ARGUMENT and INVALID_STATE describe a
 * caller contract error. ERROR reports a backend failure; callers should
 * inspect nb_host_get_state() and nb_host_get_last_error() for severity.
 */
enum nb_host_result nb_host_present(
    struct nb_host *host,
    const struct nb_host_frame *frame);

/*
 * Console lifecycle requests let the shell cancel focus, held input, and
 * pointer capture before acknowledging a NetBSD virtual-terminal switch.
 * Hosted backends return NB_HOST_RESULT_UNSUPPORTED.
 *
 * After acquire completion succeeds, the caller must re-query output state,
 * process any queued output/focus changes, and perform a full redraw before
 * resuming incremental presentation.
 */
enum nb_host_result nb_host_complete_console_release(struct nb_host *host);
enum nb_host_result nb_host_complete_console_acquire(struct nb_host *host);

bool nb_host_get_last_error(const struct nb_host *host,
                            int *system_error,
                            char *message,
                            size_t message_size);

void nb_host_destroy(struct nb_host *host);

#endif
