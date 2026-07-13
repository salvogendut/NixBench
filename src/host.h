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
 * A complete software-composited frame. Pixels are borrowed only for the
 * duration of nb_host_present(). Serial numbers are nonzero and strictly
 * increasing for the lifetime of a host. Stride is measured in bytes.
 */
struct nb_host_frame {
    const void *pixels;
    int width;
    int height;
    size_t stride;
    enum nb_host_pixel_format format;
    uint64_t serial;
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
    NB_HOST_EVENT_SUSPENDED,
    NB_HOST_EVENT_RESUMED,
    NB_HOST_EVENT_POINTER_MOTION,
    NB_HOST_EVENT_POINTER_BUTTON,
    NB_HOST_EVENT_KEY,
    NB_HOST_EVENT_PRESENTED
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
        } presented;
    } data;
};

enum nb_host_event_status {
    NB_HOST_EVENT_STATUS_ERROR = -1,
    NB_HOST_EVENT_STATUS_EMPTY,
    NB_HOST_EVENT_STATUS_AVAILABLE
};

enum nb_host_present_status {
    NB_HOST_PRESENT_STATUS_ERROR = -1,
    NB_HOST_PRESENT_STATUS_SUSPENDED,
    NB_HOST_PRESENT_STATUS_ACCEPTED
};

struct nb_host;

bool nb_host_output_is_valid(const struct nb_host_output *output);
bool nb_host_frame_is_valid(const struct nb_host_frame *frame);
bool nb_host_event_is_valid(const struct nb_host_event *event);

bool nb_host_get_output(const struct nb_host *host,
                        struct nb_host_output *output);
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
 * Acceptance does not necessarily mean that a frame is visible. A backend
 * emits NB_HOST_EVENT_PRESENTED with the matching serial after display. A
 * synchronous backend may enqueue that event before this call returns.
 *
 * A suspended host retains the caller's serial as unsubmitted. After a
 * NB_HOST_EVENT_RESUMED event, the caller can submit that serial again. This
 * models virtual-terminal release/acquire without turning it into a fatal
 * rendering error.
 */
enum nb_host_present_status nb_host_present(
    struct nb_host *host,
    const struct nb_host_frame *frame);

void nb_host_destroy(struct nb_host *host);

#endif
