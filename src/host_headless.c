#include "host_headless.h"

#include <stdlib.h>
#include <string.h>

#include "host_backend.h"

struct nb_host_headless_context {
    struct nb_host_output output;
    struct nb_host_event events[NB_HOST_HEADLESS_EVENT_CAPACITY];
    size_t event_head;
    size_t event_count;
    uint64_t milliseconds;
    unsigned char *presented_pixels;
    struct nb_host_frame presented_frame;
    size_t presentation_count;
    bool pointer_captured;
    bool suspended;
};

static const struct nb_host_backend_operations headless_operations;

static struct nb_host_headless_context *headless_context(
    struct nb_host *host)
{
    return nb_host_backend_context(host, &headless_operations);
}

static const struct nb_host_headless_context *headless_context_const(
    const struct nb_host *host)
{
    return nb_host_backend_context_const(host, &headless_operations);
}

static bool queue_event(struct nb_host_headless_context *context,
                        const struct nb_host_event *event)
{
    size_t tail;

    if (context->event_count >= NB_HOST_HEADLESS_EVENT_CAPACITY) {
        return false;
    }
    tail = (context->event_head + context->event_count) %
           NB_HOST_HEADLESS_EVENT_CAPACITY;
    context->events[tail] = *event;
    ++context->event_count;
    return true;
}

static enum nb_host_event_status pop_event(
    struct nb_host_headless_context *context,
    struct nb_host_event *event)
{
    if (context->event_count == 0) {
        memset(event, 0, sizeof(*event));
        return NB_HOST_EVENT_STATUS_EMPTY;
    }
    *event = context->events[context->event_head];
    context->event_head = (context->event_head + 1) %
                          NB_HOST_HEADLESS_EVENT_CAPACITY;
    --context->event_count;
    return NB_HOST_EVENT_STATUS_AVAILABLE;
}

static bool headless_get_output(const void *opaque,
                                struct nb_host_output *output)
{
    const struct nb_host_headless_context *context = opaque;

    *output = context->output;
    return true;
}

static uint64_t headless_monotonic_milliseconds(const void *opaque)
{
    const struct nb_host_headless_context *context = opaque;

    return context->milliseconds;
}

static enum nb_host_event_status headless_poll_event(
    void *opaque,
    struct nb_host_event *event)
{
    return pop_event(opaque, event);
}

static enum nb_host_event_status headless_wait_event(
    void *opaque,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event)
{
    struct nb_host_headless_context *context = opaque;

    if (context->event_count > 0) {
        return pop_event(context, event);
    }
    if (UINT64_MAX - context->milliseconds < timeout_milliseconds) {
        memset(event, 0, sizeof(*event));
        return NB_HOST_EVENT_STATUS_ERROR;
    }
    context->milliseconds += timeout_milliseconds;
    memset(event, 0, sizeof(*event));
    return NB_HOST_EVENT_STATUS_EMPTY;
}

static bool headless_set_pointer_capture(void *opaque, bool captured)
{
    struct nb_host_headless_context *context = opaque;

    context->pointer_captured = captured;
    return true;
}

static enum nb_host_present_status headless_present(
    void *opaque,
    const struct nb_host_frame *frame)
{
    struct nb_host_headless_context *context = opaque;
    struct nb_host_event presented = {0};
    unsigned char *copy;
    const unsigned char *source = frame->pixels;
    const size_t row_bytes =
        (size_t)frame->width * NB_HOST_BYTES_PER_PIXEL;
    const size_t byte_count = row_bytes * (size_t)frame->height;
    size_t row;

    if (context->suspended) {
        return NB_HOST_PRESENT_STATUS_SUSPENDED;
    }
    if (frame->width != context->output.pixel_width ||
        frame->height != context->output.pixel_height ||
        context->event_count >= NB_HOST_HEADLESS_EVENT_CAPACITY ||
        context->presentation_count == SIZE_MAX) {
        return NB_HOST_PRESENT_STATUS_ERROR;
    }
    copy = malloc(byte_count);
    if (copy == NULL) {
        return NB_HOST_PRESENT_STATUS_ERROR;
    }
    for (row = 0; row < (size_t)frame->height; ++row) {
        memcpy(copy + (row * row_bytes),
               source + (row * frame->stride),
               row_bytes);
    }

    presented.type = NB_HOST_EVENT_PRESENTED;
    presented.milliseconds = context->milliseconds;
    presented.data.presented.frame_serial = frame->serial;
    if (!queue_event(context, &presented)) {
        free(copy);
        return NB_HOST_PRESENT_STATUS_ERROR;
    }

    free(context->presented_pixels);
    context->presented_pixels = copy;
    context->presented_frame = *frame;
    context->presented_frame.pixels = copy;
    context->presented_frame.stride = row_bytes;
    ++context->presentation_count;
    return NB_HOST_PRESENT_STATUS_ACCEPTED;
}

static void headless_destroy(void *opaque)
{
    struct nb_host_headless_context *context = opaque;

    free(context->presented_pixels);
    free(context);
}

static const struct nb_host_backend_operations headless_operations = {
    headless_get_output,
    headless_monotonic_milliseconds,
    headless_poll_event,
    headless_wait_event,
    headless_set_pointer_capture,
    headless_present,
    headless_destroy
};

struct nb_host *nb_host_headless_create(
    const struct nb_host_output *output)
{
    struct nb_host_headless_context *context;
    struct nb_host *host;

    if (!nb_host_output_is_valid(output)) {
        return NULL;
    }
    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return NULL;
    }
    context->output = *output;
    host = nb_host_backend_create(&headless_operations, context);
    if (host == NULL) {
        free(context);
    }
    return host;
}

bool nb_host_headless_enqueue_event(struct nb_host *host,
                                    const struct nb_host_event *event)
{
    struct nb_host_headless_context *context = headless_context(host);

    if (context == NULL || !nb_host_event_is_valid(event) ||
        event->milliseconds > context->milliseconds ||
        event->type == NB_HOST_EVENT_OUTPUT_CHANGED ||
        event->type == NB_HOST_EVENT_SUSPENDED ||
        event->type == NB_HOST_EVENT_RESUMED ||
        event->type == NB_HOST_EVENT_PRESENTED) {
        return false;
    }
    return queue_event(context, event);
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

bool nb_host_headless_set_output(struct nb_host *host,
                                 const struct nb_host_output *output,
                                 uint64_t milliseconds)
{
    struct nb_host_headless_context *context = headless_context(host);
    struct nb_host_event event = {0};

    if (context == NULL || !nb_host_output_is_valid(output)) {
        return false;
    }
    if (outputs_equal(&context->output, output)) {
        return true;
    }
    if (milliseconds > context->milliseconds) {
        return false;
    }
    event.type = NB_HOST_EVENT_OUTPUT_CHANGED;
    event.milliseconds = milliseconds;
    event.data.output = *output;
    if (!queue_event(context, &event)) {
        return false;
    }
    context->output = *output;
    return true;
}

bool nb_host_headless_set_suspended(struct nb_host *host,
                                    bool suspended,
                                    uint64_t milliseconds)
{
    struct nb_host_headless_context *context = headless_context(host);
    struct nb_host_event event = {0};

    if (context == NULL || milliseconds > context->milliseconds) {
        return false;
    }
    if (context->suspended == suspended) {
        return true;
    }

    event.type = suspended ? NB_HOST_EVENT_SUSPENDED
                           : NB_HOST_EVENT_RESUMED;
    event.milliseconds = milliseconds;
    if (!queue_event(context, &event)) {
        return false;
    }
    context->suspended = suspended;
    if (suspended) {
        context->pointer_captured = false;
    }
    return true;
}

bool nb_host_headless_advance_time(struct nb_host *host,
                                   uint64_t milliseconds)
{
    struct nb_host_headless_context *context = headless_context(host);

    if (context == NULL ||
        UINT64_MAX - context->milliseconds < milliseconds) {
        return false;
    }
    context->milliseconds += milliseconds;
    return true;
}

size_t nb_host_headless_pending_event_count(const struct nb_host *host)
{
    const struct nb_host_headless_context *context =
        headless_context_const(host);

    return context == NULL ? 0 : context->event_count;
}

size_t nb_host_headless_presentation_count(const struct nb_host *host)
{
    const struct nb_host_headless_context *context =
        headless_context_const(host);

    return context == NULL ? 0 : context->presentation_count;
}

bool nb_host_headless_pointer_is_captured(const struct nb_host *host)
{
    const struct nb_host_headless_context *context =
        headless_context_const(host);

    return context != NULL && context->pointer_captured;
}

bool nb_host_headless_last_presented_frame(
    const struct nb_host *host,
    struct nb_host_frame *frame)
{
    const struct nb_host_headless_context *context =
        headless_context_const(host);

    if (context == NULL || frame == NULL ||
        context->presented_pixels == NULL) {
        return false;
    }
    *frame = context->presented_frame;
    return true;
}
