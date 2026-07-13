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
           (size_t)frame->height <= SIZE_MAX / frame->stride;
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
    case NB_HOST_EVENT_SUSPENDED:
    case NB_HOST_EVENT_RESUMED:
    case NB_HOST_EVENT_POINTER_MOTION:
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
    case NB_HOST_EVENT_PRESENTED:
        return event->data.presented.frame_serial != 0;
    case NB_HOST_EVENT_NONE:
    default:
        return false;
    }
}

static bool operations_are_valid(
    const struct nb_host_backend_operations *operations)
{
    return operations != NULL && operations->get_output != NULL &&
           operations->monotonic_milliseconds != NULL &&
           operations->poll_event != NULL &&
           operations->wait_event != NULL &&
           operations->set_pointer_capture != NULL &&
           operations->present != NULL && operations->destroy != NULL;
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

enum nb_host_present_status nb_host_present(
    struct nb_host *host,
    const struct nb_host_frame *frame)
{
    enum nb_host_present_status status;

    if (host == NULL || !nb_host_frame_is_valid(frame) ||
        frame->serial <= host->last_submitted_serial) {
        return NB_HOST_PRESENT_STATUS_ERROR;
    }
    status = host->operations->present(host->context, frame);
    if (status == NB_HOST_PRESENT_STATUS_SUSPENDED) {
        return status;
    }
    if (status != NB_HOST_PRESENT_STATUS_ACCEPTED) {
        return NB_HOST_PRESENT_STATUS_ERROR;
    }
    host->last_submitted_serial = frame->serial;
    return status;
}

void nb_host_destroy(struct nb_host *host)
{
    if (host == NULL) {
        return;
    }
    host->operations->destroy(host->context);
    free(host);
}
