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
    enum nb_host_state state;
    struct nb_host_event lifecycle_event;
    bool lifecycle_event_pending;
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

static bool event_is_input(const struct nb_host_event *event)
{
    return event->type == NB_HOST_EVENT_FOCUS_CHANGED ||
           event->type == NB_HOST_EVENT_POINTER_MOTION ||
           event->type == NB_HOST_EVENT_POINTER_BUTTON ||
           event->type == NB_HOST_EVENT_POINTER_LEAVE ||
           event->type == NB_HOST_EVENT_KEY;
}

static void purge_input_events(struct nb_host_headless_context *context)
{
    size_t read_index;
    size_t retained_count = 0;

    for (read_index = 0; read_index < context->event_count; ++read_index) {
        const size_t source_index =
            (context->event_head + read_index) %
            NB_HOST_HEADLESS_EVENT_CAPACITY;
        const struct nb_host_event queued = context->events[source_index];

        if (!event_is_input(&queued)) {
            const size_t destination_index =
                (context->event_head + retained_count) %
                NB_HOST_HEADLESS_EVENT_CAPACITY;

            context->events[destination_index] = queued;
            ++retained_count;
        }
    }
    context->event_count = retained_count;
}

static enum nb_host_event_status pop_event(
    struct nb_host_headless_context *context,
    struct nb_host_event *event)
{
    if (context->lifecycle_event_pending) {
        *event = context->lifecycle_event;
        memset(&context->lifecycle_event,
               0,
               sizeof(context->lifecycle_event));
        context->lifecycle_event_pending = false;
        return NB_HOST_EVENT_STATUS_AVAILABLE;
    }
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

static enum nb_host_state headless_get_state(const void *opaque)
{
    const struct nb_host_headless_context *context = opaque;

    return context->state;
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

    if (captured && context->state != NB_HOST_STATE_ACTIVE) {
        return false;
    }
    context->pointer_captured = captured;
    return true;
}

static enum nb_host_result headless_present(
    void *opaque,
    const struct nb_host_frame *frame)
{
    struct nb_host_headless_context *context = opaque;
    struct nb_host_event completed = {0};
    unsigned char *copy;
    const unsigned char *source = frame->pixels;
    const size_t row_bytes =
        (size_t)frame->width * NB_HOST_BYTES_PER_PIXEL;
    const size_t byte_count = row_bytes * (size_t)frame->height;
    size_t row;

    if (context->state != NB_HOST_STATE_ACTIVE) {
        return context->state == NB_HOST_STATE_FAILED
                   ? NB_HOST_RESULT_ERROR
                   : NB_HOST_RESULT_SUSPENDED;
    }
    if (context->event_count >= NB_HOST_HEADLESS_EVENT_CAPACITY) {
        return NB_HOST_RESULT_WOULD_BLOCK;
    }
    if (frame->width != context->output.pixel_width ||
        frame->height != context->output.pixel_height) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    if (context->presentation_count == SIZE_MAX) {
        return NB_HOST_RESULT_ERROR;
    }
    copy = malloc(byte_count);
    if (copy == NULL) {
        return NB_HOST_RESULT_ERROR;
    }
    for (row = 0; row < (size_t)frame->height; ++row) {
        memcpy(copy + (row * row_bytes),
               source + (row * frame->stride),
               row_bytes);
    }

    completed.type = NB_HOST_EVENT_FRAME_COMPLETE;
    completed.milliseconds = context->milliseconds;
    completed.data.frame_complete.frame_serial = frame->serial;
    if (!queue_event(context, &completed)) {
        free(copy);
        return NB_HOST_RESULT_WOULD_BLOCK;
    }

    free(context->presented_pixels);
    context->presented_pixels = copy;
    context->presented_frame = *frame;
    context->presented_frame.pixels = copy;
    context->presented_frame.stride = row_bytes;
    ++context->presentation_count;
    return NB_HOST_RESULT_OK;
}

static enum nb_host_result headless_complete_console_release(void *opaque)
{
    struct nb_host_headless_context *context = opaque;

    if (context->state != NB_HOST_STATE_RELEASE_PENDING) {
        return NB_HOST_RESULT_INVALID_STATE;
    }

    purge_input_events(context);
    context->pointer_captured = false;
    context->state = NB_HOST_STATE_SUSPENDED;
    return NB_HOST_RESULT_OK;
}

static enum nb_host_result headless_complete_console_acquire(void *opaque)
{
    struct nb_host_headless_context *context = opaque;

    if (context->state != NB_HOST_STATE_ACQUIRE_PENDING) {
        return NB_HOST_RESULT_INVALID_STATE;
    }
    context->state = NB_HOST_STATE_ACTIVE;
    return NB_HOST_RESULT_OK;
}

static bool headless_get_last_error(const void *opaque,
                                    int *system_error,
                                    char *message,
                                    size_t message_size)
{
    (void)opaque;
    *system_error = 0;
    if (message_size > 0) {
        message[0] = '\0';
    }
    return false;
}

static void headless_destroy(void *opaque)
{
    struct nb_host_headless_context *context = opaque;

    free(context->presented_pixels);
    free(context);
}

static const struct nb_host_backend_operations headless_operations = {
    .get_output = headless_get_output,
    .get_state = headless_get_state,
    .monotonic_milliseconds = headless_monotonic_milliseconds,
    .poll_event = headless_poll_event,
    .wait_event = headless_wait_event,
    .set_pointer_capture = headless_set_pointer_capture,
    .present = headless_present,
    .complete_console_release = headless_complete_console_release,
    .complete_console_acquire = headless_complete_console_acquire,
    .get_last_error = headless_get_last_error,
    .destroy = headless_destroy
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
    context->state = NB_HOST_STATE_ACTIVE;
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
        (context->state != NB_HOST_STATE_ACTIVE &&
         event->type != NB_HOST_EVENT_QUIT) ||
        event->milliseconds > context->milliseconds ||
        event->type == NB_HOST_EVENT_OUTPUT_CHANGED ||
        event->type == NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED ||
        event->type == NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED ||
        event->type == NB_HOST_EVENT_FRAME_COMPLETE ||
        event->type == NB_HOST_EVENT_FAILED) {
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

bool nb_host_headless_request_console_release(struct nb_host *host,
                                              uint64_t milliseconds)
{
    struct nb_host_headless_context *context = headless_context(host);
    struct nb_host_event event = {0};

    if (context == NULL || milliseconds > context->milliseconds ||
        context->state != NB_HOST_STATE_ACTIVE) {
        return false;
    }
    event.type = NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED;
    event.milliseconds = milliseconds;
    context->lifecycle_event = event;
    context->lifecycle_event_pending = true;
    purge_input_events(context);
    context->state = NB_HOST_STATE_RELEASE_PENDING;
    return true;
}

bool nb_host_headless_request_console_acquire(struct nb_host *host,
                                              uint64_t milliseconds)
{
    struct nb_host_headless_context *context = headless_context(host);
    struct nb_host_event event = {0};

    if (context == NULL || milliseconds > context->milliseconds ||
        context->state != NB_HOST_STATE_SUSPENDED) {
        return false;
    }
    event.type = NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED;
    event.milliseconds = milliseconds;
    context->lifecycle_event = event;
    context->lifecycle_event_pending = true;
    context->state = NB_HOST_STATE_ACQUIRE_PENDING;
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

    return context == NULL
               ? 0
               : context->event_count +
                     (context->lifecycle_event_pending ? 1U : 0U);
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
