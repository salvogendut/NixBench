#include "host_backend.h"

#include <stdlib.h>
#include <string.h>

struct nb_host {
    const struct nb_host_backend_operations *operations;
    void *context;
    uint64_t last_submitted_serial;
};

static bool pixel_format_is_valid(enum nb_host_pixel_format format)
{
    return format == NB_HOST_PIXEL_FORMAT_XRGB8888 ||
           format == NB_HOST_PIXEL_FORMAT_ARGB8888_PREMULTIPLIED;
}

bool nb_host_output_is_valid(const struct nb_host_output *output)
{
    return output != NULL && output->logical_width > 0 &&
           output->logical_height > 0 && output->pixel_width > 0 &&
           output->pixel_height > 0 && output->refresh_millihertz >= 0;
}

bool nb_host_frame_is_valid(const struct nb_host_frame *frame)
{
    size_t row_bytes;
    int damage_x;
    int damage_y;
    int damage_width;
    int damage_height;

    if (frame == NULL || frame->pixels == NULL || frame->width <= 0 ||
        frame->height <= 0 || frame->serial == 0 ||
        !pixel_format_is_valid(frame->format)) {
        return false;
    }
    if ((size_t)frame->width > SIZE_MAX / NB_HOST_BYTES_PER_PIXEL) {
        return false;
    }
    row_bytes = (size_t)frame->width * NB_HOST_BYTES_PER_PIXEL;
    return frame->stride >= row_bytes &&
           (size_t)frame->height <= SIZE_MAX / frame->stride &&
           nb_host_frame_damage(frame,
                                &damage_x,
                                &damage_y,
                                &damage_width,
                                &damage_height);
}

bool nb_host_frame_damage(const struct nb_host_frame *frame,
                          int *x,
                          int *y,
                          int *width,
                          int *height)
{
    if (frame == NULL || x == NULL || y == NULL || width == NULL ||
        height == NULL || frame->width <= 0 || frame->height <= 0) {
        return false;
    }
    if (frame->damage_width == 0 && frame->damage_height == 0) {
        *x = 0;
        *y = 0;
        *width = frame->width;
        *height = frame->height;
        return true;
    }
    if (frame->damage_x < 0 || frame->damage_y < 0 ||
        frame->damage_width <= 0 || frame->damage_height <= 0 ||
        frame->damage_x >= frame->width ||
        frame->damage_y >= frame->height ||
        frame->damage_width > frame->width - frame->damage_x ||
        frame->damage_height > frame->height - frame->damage_y) {
        return false;
    }
    *x = frame->damage_x;
    *y = frame->damage_y;
    *width = frame->damage_width;
    *height = frame->damage_height;
    return true;
}

static bool xkb_key_name_is_valid(
    const char key_name[NB_HOST_XKB_KEY_NAME_CAPACITY])
{
    size_t index;

    for (index = 0; index < NB_HOST_XKB_KEY_NAME_CAPACITY; ++index) {
        const unsigned char character = (unsigned char)key_name[index];

        if (character == '\0') {
            return index > 0;
        }
        if (character < 0x21 || character > 0x7e) {
            return false;
        }
    }
    return false;
}

bool nb_host_event_is_valid(const struct nb_host_event *event)
{
    if (event == NULL) {
        return false;
    }

    switch (event->type) {
    case NB_HOST_EVENT_QUIT:
    case NB_HOST_EVENT_FOCUS_CHANGED:
    case NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED:
    case NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED:
    case NB_HOST_EVENT_POINTER_MOTION:
    case NB_HOST_EVENT_POINTER_LEAVE:
        return true;
    case NB_HOST_EVENT_OUTPUT_CHANGED:
        return nb_host_output_is_valid(&event->data.output);
    case NB_HOST_EVENT_POINTER_BUTTON:
        return event->data.pointer_button.button >=
                   NB_HOST_POINTER_BUTTON_LEFT &&
               event->data.pointer_button.button <
                   NB_HOST_POINTER_BUTTON_COUNT;
    case NB_HOST_EVENT_KEY:
        return xkb_key_name_is_valid(event->data.key.xkb_key_name) &&
               (!event->data.key.repeat || event->data.key.pressed);
    case NB_HOST_EVENT_FRAME_COMPLETE:
        return event->data.frame_complete.frame_serial != 0;
    case NB_HOST_EVENT_FAILED:
        return true;
    case NB_HOST_EVENT_NONE:
    default:
        return false;
    }
}

static bool operations_are_valid(
    const struct nb_host_backend_operations *operations)
{
    return operations != NULL && operations->get_output != NULL &&
           operations->get_state != NULL &&
           operations->monotonic_milliseconds != NULL &&
           operations->poll_event != NULL &&
           operations->wait_event != NULL &&
           operations->set_pointer_capture != NULL &&
           operations->present != NULL &&
           operations->complete_console_release != NULL &&
           operations->complete_console_acquire != NULL &&
           operations->get_last_error != NULL &&
           operations->destroy != NULL;
}

struct nb_host *nb_host_backend_create(
    const struct nb_host_backend_operations *operations,
    void *context)
{
    struct nb_host *host;

    if (!operations_are_valid(operations) || context == NULL) {
        return NULL;
    }
    host = calloc(1, sizeof(*host));
    if (host == NULL) {
        return NULL;
    }
    host->operations = operations;
    host->context = context;
    return host;
}

void *nb_host_backend_context(
    struct nb_host *host,
    const struct nb_host_backend_operations *operations)
{
    return host != NULL && host->operations == operations
               ? host->context
               : NULL;
}

const void *nb_host_backend_context_const(
    const struct nb_host *host,
    const struct nb_host_backend_operations *operations)
{
    return host != NULL && host->operations == operations
               ? host->context
               : NULL;
}

bool nb_host_get_output(const struct nb_host *host,
                        struct nb_host_output *output)
{
    struct nb_host_output current;

    if (host == NULL || output == NULL ||
        !host->operations->get_output(host->context, &current) ||
        !nb_host_output_is_valid(&current)) {
        return false;
    }
    *output = current;
    return true;
}

static bool host_state_is_valid(enum nb_host_state state)
{
    return state >= NB_HOST_STATE_ACTIVE && state <= NB_HOST_STATE_FAILED;
}

enum nb_host_state nb_host_get_state(const struct nb_host *host)
{
    enum nb_host_state state;

    if (host == NULL) {
        return NB_HOST_STATE_FAILED;
    }
    state = host->operations->get_state(host->context);
    return host_state_is_valid(state) ? state : NB_HOST_STATE_FAILED;
}

uint64_t nb_host_monotonic_milliseconds(const struct nb_host *host)
{
    return host == NULL
               ? 0
               : host->operations->monotonic_milliseconds(host->context);
}

static enum nb_host_event_status validate_event_result(
    enum nb_host_event_status status,
    struct nb_host_event *event)
{
    if (status == NB_HOST_EVENT_STATUS_AVAILABLE &&
        nb_host_event_is_valid(event)) {
        return status;
    }
    if (event != NULL) {
        memset(event, 0, sizeof(*event));
    }
    return status == NB_HOST_EVENT_STATUS_EMPTY
               ? NB_HOST_EVENT_STATUS_EMPTY
               : NB_HOST_EVENT_STATUS_ERROR;
}

enum nb_host_event_status nb_host_poll_event(
    struct nb_host *host,
    struct nb_host_event *event)
{
    enum nb_host_event_status status;

    if (host == NULL || event == NULL) {
        return NB_HOST_EVENT_STATUS_ERROR;
    }
    status = host->operations->poll_event(host->context, event);
    return validate_event_result(status, event);
}

enum nb_host_event_status nb_host_wait_event(
    struct nb_host *host,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event)
{
    enum nb_host_event_status status;

    if (host == NULL || event == NULL) {
        return NB_HOST_EVENT_STATUS_ERROR;
    }
    status = host->operations->wait_event(host->context,
                                          timeout_milliseconds,
                                          event);
    return validate_event_result(status, event);
}

bool nb_host_set_pointer_capture(struct nb_host *host, bool captured)
{
    return host != NULL &&
           host->operations->set_pointer_capture(host->context, captured);
}

static bool host_result_is_valid(enum nb_host_result result)
{
    return result >= NB_HOST_RESULT_ERROR &&
           result <= NB_HOST_RESULT_INVALID_STATE;
}

enum nb_host_result nb_host_present(
    struct nb_host *host,
    const struct nb_host_frame *frame)
{
    enum nb_host_result result;

    if (host == NULL || !nb_host_frame_is_valid(frame)) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    if (frame->serial <= host->last_submitted_serial) {
        return NB_HOST_RESULT_INVALID_STATE;
    }
    result = host->operations->present(host->context, frame);
    if (result == NB_HOST_RESULT_OK) {
        host->last_submitted_serial = frame->serial;
    }
    if (!host_result_is_valid(result)) {
        return NB_HOST_RESULT_ERROR;
    }
    return result;
}

enum nb_host_result nb_host_complete_console_release(struct nb_host *host)
{
    enum nb_host_result result;

    if (host == NULL) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    result = host->operations->complete_console_release(host->context);
    return host_result_is_valid(result) ? result : NB_HOST_RESULT_ERROR;
}

enum nb_host_result nb_host_complete_console_acquire(struct nb_host *host)
{
    enum nb_host_result result;

    if (host == NULL) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    result = host->operations->complete_console_acquire(host->context);
    return host_result_is_valid(result) ? result : NB_HOST_RESULT_ERROR;
}

bool nb_host_get_last_error(const struct nb_host *host,
                            int *system_error,
                            char *message,
                            size_t message_size)
{
    if (host == NULL || system_error == NULL || message == NULL ||
        message_size == 0) {
        return false;
    }
    return host->operations->get_last_error(host->context,
                                            system_error,
                                            message,
                                            message_size);
}

void nb_host_destroy(struct nb_host *host)
{
    if (host == NULL) {
        return;
    }
    host->operations->destroy(host->context);
    free(host);
}
