#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "host_privsep_client.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "host_backend.h"
#include "privsep_protocol.h"

enum {
    NB_PRIVSEP_CLIENT_ERROR_CAPACITY = 256,
    NB_PRIVSEP_CLIENT_CONTROL_CAPACITY = 32,
    NB_PRIVSEP_CLIENT_EVENT_CAPACITY = 64,
    NB_PRIVSEP_CLIENT_READ_CAPACITY = 16384,
    NB_PRIVSEP_CLIENT_IO_MESSAGE_BUDGET = 256
};

enum nb_privsep_client_frame_phase {
    NB_PRIVSEP_CLIENT_FRAME_NONE,
    NB_PRIVSEP_CLIENT_FRAME_BEGIN,
    NB_PRIVSEP_CLIENT_FRAME_DATA,
    NB_PRIVSEP_CLIENT_FRAME_COMMIT,
    NB_PRIVSEP_CLIENT_FRAME_WAIT_COMPLETE,
    NB_PRIVSEP_CLIENT_FRAME_ABORT
};

enum nb_privsep_client_wire_kind {
    NB_PRIVSEP_CLIENT_WIRE_NONE,
    NB_PRIVSEP_CLIENT_WIRE_CONTROL,
    NB_PRIVSEP_CLIENT_WIRE_FRAME_BEGIN,
    NB_PRIVSEP_CLIENT_WIRE_FRAME_DATA,
    NB_PRIVSEP_CLIENT_WIRE_FRAME_COMMIT,
    NB_PRIVSEP_CLIENT_WIRE_FRAME_ABORT
};

struct nb_privsep_client_frame {
    unsigned char *pixels;
    size_t size;
    size_t offset;
    uint64_t generation;
    uint64_t serial;
    enum nb_privsep_client_frame_phase phase;
    bool begun;
    bool cancel_requested;
    bool drop_after_wire;
};

struct nb_host_privsep_client_context {
    int descriptor;
    struct nb_privsep_parser parser;
    struct nb_privsep_message
        controls[NB_PRIVSEP_CLIENT_CONTROL_CAPACITY];
    size_t control_head;
    size_t control_count;
    struct nb_host_event events[NB_PRIVSEP_CLIENT_EVENT_CAPACITY];
    size_t event_head;
    size_t event_count;
    unsigned char read_buffer[NB_PRIVSEP_CLIENT_READ_CAPACITY];
    size_t read_used;
    size_t read_offset;
    unsigned char wire_buffer[NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE];
    size_t wire_size;
    size_t wire_offset;
    size_t wire_frame_data_size;
    enum nb_privsep_client_wire_kind wire_kind;
    enum nb_privsep_message_type wire_type;
    struct nb_privsep_client_frame frame;
    struct nb_host_output output;
    uint64_t generation;
    uint64_t suspend_milliseconds;
    uint64_t resume_milliseconds;
    uint64_t next_sequence;
    uint64_t shutdown_token;
    enum nb_host_state state;
    int system_error;
    char error[NB_PRIVSEP_CLIENT_ERROR_CAPACITY];
    bool has_output;
    bool helper_active;
    bool hello_sent;
    bool shutdown_requested;
    bool shutdown_message_sent;
    bool shutdown_accepted;
    bool failure_event_pending;
    bool failed;
};

static char creation_error[NB_PRIVSEP_CLIENT_ERROR_CAPACITY];
static int creation_system_error;
static const struct nb_host_backend_operations privsep_client_operations;

static bool uintmax_to_u32(uintmax_t value, uint32_t *converted)
{
    if (value > UINT32_MAX) {
        return false;
    }
    *converted = (uint32_t)value;
    return true;
}

static void copy_error(char destination[NB_PRIVSEP_CLIENT_ERROR_CAPACITY],
                       const char *operation,
                       int system_error)
{
    const char *parts[3];
    size_t destination_used = 0;
    size_t part_count;
    size_t part_index;

    parts[0] = operation != NULL ? operation : "Privsep host error";
    if (system_error != 0) {
        parts[1] = ": ";
        parts[2] = strerror(system_error);
        part_count = 3;
    } else {
        part_count = 1;
    }
    for (part_index = 0; part_index < part_count; ++part_index) {
        const char *source = parts[part_index];

        while (*source != '\0' &&
               destination_used + 1U <
                   NB_PRIVSEP_CLIENT_ERROR_CAPACITY) {
            destination[destination_used++] = *source++;
        }
    }
    destination[destination_used] = '\0';
}

static void set_creation_error(const char *operation, int system_error)
{
    creation_system_error = system_error;
    copy_error(creation_error, operation, system_error);
}

const char *nb_host_privsep_client_creation_error(void)
{
    return creation_error;
}

int nb_host_privsep_client_creation_system_error(void)
{
    return creation_system_error;
}

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;
    uint64_t milliseconds;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return 0;
    }
    if ((uint64_t)now.tv_sec > UINT64_MAX / UINT64_C(1000)) {
        return UINT64_MAX;
    }
    milliseconds = (uint64_t)now.tv_sec * UINT64_C(1000);
    if ((uint64_t)now.tv_nsec / UINT64_C(1000000) >
        UINT64_MAX - milliseconds) {
        return UINT64_MAX;
    }
    return milliseconds +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void clear_frame(struct nb_host_privsep_client_context *context)
{
    free(context->frame.pixels);
    memset(&context->frame, 0, sizeof(context->frame));
}

static void fail_client(struct nb_host_privsep_client_context *context,
                        const char *operation,
                        int system_error)
{
    if (context->failed) {
        return;
    }
    context->system_error = system_error;
    copy_error(context->error, operation, system_error);
    context->failed = true;
    context->state = NB_HOST_STATE_FAILED;
    context->failure_event_pending = true;
    context->control_count = 0;
    context->control_head = 0;
    context->wire_size = 0;
    context->wire_offset = 0;
    context->wire_kind = NB_PRIVSEP_CLIENT_WIRE_NONE;
    clear_frame(context);
}

static bool push_event(struct nb_host_privsep_client_context *context,
                       const struct nb_host_event *event)
{
    size_t tail;

    if (context->event_count >= NB_PRIVSEP_CLIENT_EVENT_CAPACITY) {
        fail_client(context, "Privsep host event queue overflow", EOVERFLOW);
        return false;
    }
    tail = (context->event_head + context->event_count) %
           NB_PRIVSEP_CLIENT_EVENT_CAPACITY;
    context->events[tail] = *event;
    ++context->event_count;
    return true;
}

static bool event_is_input(const struct nb_host_event *event)
{
    return event->type == NB_HOST_EVENT_POINTER_MOTION ||
           event->type == NB_HOST_EVENT_POINTER_BUTTON ||
           event->type == NB_HOST_EVENT_POINTER_LEAVE ||
           event->type == NB_HOST_EVENT_KEY;
}

static bool discard_oldest_queued_input(
    struct nb_host_privsep_client_context *context)
{
    size_t index;

    for (index = 0; index < context->event_count; ++index) {
        const size_t candidate =
            (context->event_head + index) %
            NB_PRIVSEP_CLIENT_EVENT_CAPACITY;
        size_t shift;

        if (!event_is_input(&context->events[candidate])) {
            continue;
        }
        for (shift = index; shift + 1 < context->event_count; ++shift) {
            const size_t destination =
                (context->event_head + shift) %
                NB_PRIVSEP_CLIENT_EVENT_CAPACITY;
            const size_t source =
                (context->event_head + shift + 1) %
                NB_PRIVSEP_CLIENT_EVENT_CAPACITY;

            context->events[destination] = context->events[source];
        }
        --context->event_count;
        return true;
    }
    return false;
}

static bool push_input_event(
    struct nb_host_privsep_client_context *context,
    const struct nb_host_event *event)
{
    if (context->event_count >= NB_PRIVSEP_CLIENT_EVENT_CAPACITY) {
        if (!discard_oldest_queued_input(context)) {
            return true;
        }
    }
    return push_event(context, event);
}

static void discard_queued_input(
    struct nb_host_privsep_client_context *context)
{
    size_t read_index;
    size_t write_index = context->event_head;
    size_t kept = 0;

    for (read_index = 0; read_index < context->event_count; ++read_index) {
        const size_t source =
            (context->event_head + read_index) %
            NB_PRIVSEP_CLIENT_EVENT_CAPACITY;

        if (!event_is_input(&context->events[source])) {
            context->events[write_index] = context->events[source];
            write_index =
                (write_index + 1) % NB_PRIVSEP_CLIENT_EVENT_CAPACITY;
            ++kept;
        }
    }
    context->event_count = kept;
}

static bool queue_control(
    struct nb_host_privsep_client_context *context,
    const struct nb_privsep_message *message)
{
    size_t tail;

    if (context->failed ||
        context->control_count >= NB_PRIVSEP_CLIENT_CONTROL_CAPACITY) {
        if (!context->failed) {
            fail_client(context,
                        "Privsep control queue overflow",
                        EOVERFLOW);
        }
        return false;
    }
    tail = (context->control_head + context->control_count) %
           NB_PRIVSEP_CLIENT_CONTROL_CAPACITY;
    context->controls[tail] = *message;
    ++context->control_count;
    return true;
}

static bool take_sequence(struct nb_host_privsep_client_context *context,
                          uint64_t *sequence)
{
    if (context->next_sequence == 0) {
        fail_client(context,
                    "Privsep outgoing sequence exhausted",
                    EOVERFLOW);
        return false;
    }
    *sequence = context->next_sequence;
    context->next_sequence = context->next_sequence == UINT64_MAX
                                 ? 0
                                 : context->next_sequence + 1;
    return true;
}

static bool stage_message(struct nb_host_privsep_client_context *context,
                          const struct nb_privsep_message *message,
                          enum nb_privsep_client_wire_kind kind)
{
    uint64_t sequence;

    if (!take_sequence(context, &sequence) ||
        !nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_CORE,
                                   sequence,
                                   message,
                                   context->wire_buffer,
                                   sizeof(context->wire_buffer),
                                   &context->wire_size)) {
        if (!context->failed) {
            fail_client(context,
                        "Could not encode a privsep message",
                        EPROTO);
        }
        return false;
    }
    context->wire_offset = 0;
    context->wire_kind = kind;
    context->wire_type = message->type;
    context->wire_frame_data_size =
        message->type == NB_PRIVSEP_MESSAGE_FRAME_DATA
            ? (size_t)message->data.frame_data.size
            : 0;
    return true;
}

static bool stage_next_message(
    struct nb_host_privsep_client_context *context)
{
    struct nb_privsep_message message;

    if (context->wire_kind != NB_PRIVSEP_CLIENT_WIRE_NONE ||
        context->failed) {
        return !context->failed;
    }
    if (context->control_count != 0) {
        message = context->controls[context->control_head];
        context->control_head =
            (context->control_head + 1) %
            NB_PRIVSEP_CLIENT_CONTROL_CAPACITY;
        --context->control_count;
        return stage_message(context,
                             &message,
                             NB_PRIVSEP_CLIENT_WIRE_CONTROL);
    }

    if (context->shutdown_requested &&
        !context->shutdown_message_sent &&
        context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_NONE) {
        memset(&message, 0, sizeof(message));
        message.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST;
        message.data.token = context->shutdown_token;
        return stage_message(context,
                             &message,
                             NB_PRIVSEP_CLIENT_WIRE_CONTROL);
    }

    memset(&message, 0, sizeof(message));
    if (context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_BEGIN) {
        message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
        message.data.frame_begin.generation = context->frame.generation;
        message.data.frame_begin.serial = context->frame.serial;
        message.data.frame_begin.frame_bytes = (uint32_t)context->frame.size;
        return stage_message(context,
                             &message,
                             NB_PRIVSEP_CLIENT_WIRE_FRAME_BEGIN);
    }
    if (context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_DATA) {
        const size_t remaining = context->frame.size - context->frame.offset;
        const size_t amount =
            remaining > NB_PRIVSEP_FRAME_DATA_CAPACITY
                ? NB_PRIVSEP_FRAME_DATA_CAPACITY
                : remaining;

        message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
        message.data.frame_data.generation = context->frame.generation;
        message.data.frame_data.serial = context->frame.serial;
        message.data.frame_data.offset = (uint32_t)context->frame.offset;
        message.data.frame_data.size = (uint32_t)amount;
        message.data.frame_data.bytes =
            context->frame.pixels + context->frame.offset;
        return stage_message(context,
                             &message,
                             NB_PRIVSEP_CLIENT_WIRE_FRAME_DATA);
    }
    if (context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_COMMIT) {
        message.type = NB_PRIVSEP_MESSAGE_FRAME_COMMIT;
        message.data.frame_reference.generation = context->frame.generation;
        message.data.frame_reference.serial = context->frame.serial;
        return stage_message(context,
                             &message,
                             NB_PRIVSEP_CLIENT_WIRE_FRAME_COMMIT);
    }
    if (context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_ABORT) {
        message.type = NB_PRIVSEP_MESSAGE_FRAME_ABORT;
        message.data.frame_reference.generation = context->frame.generation;
        message.data.frame_reference.serial = context->frame.serial;
        return stage_message(context,
                             &message,
                             NB_PRIVSEP_CLIENT_WIRE_FRAME_ABORT);
    }
    return true;
}

static void complete_staged_message(
    struct nb_host_privsep_client_context *context)
{
    const enum nb_privsep_client_wire_kind kind = context->wire_kind;
    const enum nb_privsep_message_type type = context->wire_type;
    const size_t data_size = context->wire_frame_data_size;

    context->wire_size = 0;
    context->wire_offset = 0;
    context->wire_frame_data_size = 0;
    context->wire_kind = NB_PRIVSEP_CLIENT_WIRE_NONE;

    if (kind == NB_PRIVSEP_CLIENT_WIRE_CONTROL) {
        if (type == NB_PRIVSEP_MESSAGE_CORE_HELLO) {
            context->hello_sent = true;
        } else if (type == NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST) {
            context->shutdown_message_sent = true;
        }
        return;
    }
    if (kind == NB_PRIVSEP_CLIENT_WIRE_FRAME_BEGIN) {
        context->frame.begun = true;
        context->frame.phase = context->frame.cancel_requested
                                   ? NB_PRIVSEP_CLIENT_FRAME_ABORT
                                   : NB_PRIVSEP_CLIENT_FRAME_DATA;
    } else if (kind == NB_PRIVSEP_CLIENT_WIRE_FRAME_DATA) {
        context->frame.offset += data_size;
        if (context->frame.cancel_requested) {
            context->frame.phase = NB_PRIVSEP_CLIENT_FRAME_ABORT;
        } else {
            context->frame.phase =
                context->frame.offset == context->frame.size
                    ? NB_PRIVSEP_CLIENT_FRAME_COMMIT
                    : NB_PRIVSEP_CLIENT_FRAME_DATA;
        }
    } else if (kind == NB_PRIVSEP_CLIENT_WIRE_FRAME_COMMIT) {
        if (context->frame.drop_after_wire) {
            clear_frame(context);
        } else {
            free(context->frame.pixels);
            context->frame.pixels = NULL;
            context->frame.size = 0;
            context->frame.offset = 0;
            context->frame.phase = NB_PRIVSEP_CLIENT_FRAME_WAIT_COMPLETE;
        }
    } else if (kind == NB_PRIVSEP_CLIENT_WIRE_FRAME_ABORT) {
        clear_frame(context);
    }
}

static bool flush_output(struct nb_host_privsep_client_context *context)
{
    unsigned int messages = 0;

    while (!context->failed &&
           messages < NB_PRIVSEP_CLIENT_IO_MESSAGE_BUDGET) {
        ssize_t written;

        if (!stage_next_message(context)) {
            return false;
        }
        if (context->wire_kind == NB_PRIVSEP_CLIENT_WIRE_NONE) {
            return true;
        }
        do {
            written = write(context->descriptor,
                            context->wire_buffer + context->wire_offset,
                            context->wire_size - context->wire_offset);
        } while (written < 0 && errno == EINTR);
        if (written > 0) {
            context->wire_offset += (size_t)written;
            if (context->wire_offset == context->wire_size) {
                complete_staged_message(context);
                ++messages;
            }
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        fail_client(context,
                    "Could not write to the privileged helper",
                    written < 0 ? errno : EPIPE);
        return false;
    }
    return !context->failed;
}

static void request_frame_abort(
    struct nb_host_privsep_client_context *context)
{
    const enum nb_privsep_client_wire_kind kind = context->wire_kind;

    if (context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_NONE) {
        return;
    }
    if (kind == NB_PRIVSEP_CLIENT_WIRE_FRAME_COMMIT) {
        free(context->frame.pixels);
        context->frame.pixels = NULL;
        context->frame.cancel_requested = true;
        context->frame.drop_after_wire = true;
        return;
    }
    if (context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_WAIT_COMPLETE) {
        clear_frame(context);
        return;
    }
    if (!context->frame.begun &&
        kind != NB_PRIVSEP_CLIENT_WIRE_FRAME_BEGIN) {
        clear_frame(context);
        return;
    }
    free(context->frame.pixels);
    context->frame.pixels = NULL;
    context->frame.cancel_requested = true;
    if (kind != NB_PRIVSEP_CLIENT_WIRE_FRAME_BEGIN &&
        kind != NB_PRIVSEP_CLIENT_WIRE_FRAME_DATA) {
        context->frame.phase = NB_PRIVSEP_CLIENT_FRAME_ABORT;
    }
}

static bool output_to_host(const struct nb_privsep_output *source,
                           struct nb_host_output *destination)
{
    if (!nb_privsep_output_is_valid(source) ||
        source->logical_width > (uint32_t)INT_MAX ||
        source->logical_height > (uint32_t)INT_MAX ||
        source->pixel_width > (uint32_t)INT_MAX ||
        source->pixel_height > (uint32_t)INT_MAX ||
        source->refresh_millihertz > (uint32_t)INT_MAX) {
        return false;
    }
    destination->logical_width = (int)source->logical_width;
    destination->logical_height = (int)source->logical_height;
    destination->pixel_width = (int)source->pixel_width;
    destination->pixel_height = (int)source->pixel_height;
    destination->refresh_millihertz = (int)source->refresh_millihertz;
    return true;
}

static bool generation_matches(
    const struct nb_host_privsep_client_context *context,
    uint64_t generation)
{
    return context->has_output && generation == context->generation;
}

static bool input_state_accepts(
    const struct nb_host_privsep_client_context *context,
    uint64_t generation)
{
    return context->helper_active &&
           generation_matches(context, generation) &&
           (context->state == NB_HOST_STATE_ACTIVE ||
            context->state == NB_HOST_STATE_ACQUIRE_PENDING);
}

static bool input_message_is_valid(
    const struct nb_host_privsep_client_context *context,
    uint64_t generation)
{
    return context->helper_active &&
           generation_matches(context, generation);
}

static bool coordinates_are_valid(
    const struct nb_host_privsep_client_context *context,
    int32_t x,
    int32_t y)
{
    return x >= 0 && y >= 0 && x < context->output.logical_width &&
           y < context->output.logical_height;
}

static bool handle_ready(struct nb_host_privsep_client_context *context,
                         const struct nb_privsep_message *message)
{
    struct nb_host_event event;

    if (!context->hello_sent || context->has_output ||
        context->state != NB_HOST_STATE_SUSPENDED ||
        !output_to_host(&message->data.ready.output, &context->output)) {
        return false;
    }
    context->generation = message->data.ready.generation;
    context->has_output = true;
    context->helper_active = true;
    context->state = NB_HOST_STATE_ACTIVE;
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_OUTPUT_CHANGED;
    event.milliseconds = monotonic_milliseconds();
    event.data.output = context->output;
    return push_event(context, &event);
}

static bool handle_suspend(struct nb_host_privsep_client_context *context,
                           const struct nb_privsep_message *message)
{
    struct nb_host_event event;
    const uint64_t abandoned =
        message->data.suspend.abandoned_frame_serial;

    if (!context->helper_active ||
        context->generation == UINT64_MAX ||
        message->data.suspend.generation != context->generation + 1 ||
        (abandoned != 0 &&
         (context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_NONE ||
          context->frame.serial != abandoned))) {
        return false;
    }
    request_frame_abort(context);
    discard_queued_input(context);
    context->generation = message->data.suspend.generation;
    context->suspend_milliseconds = message->data.suspend.milliseconds;
    context->helper_active = false;
    if (context->state == NB_HOST_STATE_RELEASE_PENDING ||
        context->state == NB_HOST_STATE_ACQUIRE_PENDING) {
        return true;
    }
    if (context->state != NB_HOST_STATE_ACTIVE) {
        return false;
    }
    context->state = NB_HOST_STATE_RELEASE_PENDING;
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED;
    event.milliseconds = context->suspend_milliseconds;
    return push_event(context, &event);
}

static bool handle_resume(struct nb_host_privsep_client_context *context,
                          const struct nb_privsep_message *message)
{
    struct nb_host_output output;
    struct nb_host_event event;

    if (context->helper_active ||
        message->data.ready.generation != context->generation ||
        !output_to_host(&message->data.ready.output, &output)) {
        return false;
    }
    context->helper_active = true;
    context->resume_milliseconds = monotonic_milliseconds();
    context->output = output;
    if (context->state == NB_HOST_STATE_RELEASE_PENDING ||
        context->state == NB_HOST_STATE_ACQUIRE_PENDING) {
        return true;
    }
    if (context->state != NB_HOST_STATE_SUSPENDED) {
        return false;
    }
    context->state = NB_HOST_STATE_ACQUIRE_PENDING;
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED;
    event.milliseconds = context->resume_milliseconds;
    return push_event(context, &event);
}

static bool handle_pointer_motion(
    struct nb_host_privsep_client_context *context,
    const struct nb_privsep_message *message)
{
    struct nb_host_event event;

    if (!input_message_is_valid(
            context, message->data.pointer_motion.generation) ||
        !coordinates_are_valid(context,
                               message->data.pointer_motion.x,
                               message->data.pointer_motion.y)) {
        return false;
    }
    if (!input_state_accepts(
            context, message->data.pointer_motion.generation)) {
        return true;
    }
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_MOTION;
    event.milliseconds = message->data.pointer_motion.milliseconds;
    event.data.pointer_motion.x = message->data.pointer_motion.x;
    event.data.pointer_motion.y = message->data.pointer_motion.y;
    return push_input_event(context, &event);
}

static bool handle_pointer_button(
    struct nb_host_privsep_client_context *context,
    const struct nb_privsep_message *message)
{
    struct nb_host_event event;

    if (!input_message_is_valid(
            context, message->data.pointer_button.generation) ||
        !coordinates_are_valid(context,
                               message->data.pointer_button.x,
                               message->data.pointer_button.y)) {
        return false;
    }
    if (!input_state_accepts(
            context, message->data.pointer_button.generation)) {
        return true;
    }
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_BUTTON;
    event.milliseconds = message->data.pointer_button.milliseconds;
    event.data.pointer_button.x = message->data.pointer_button.x;
    event.data.pointer_button.y = message->data.pointer_button.y;
    event.data.pointer_button.button =
        (enum nb_host_pointer_button)message->data.pointer_button.button;
    event.data.pointer_button.pressed =
        message->data.pointer_button.pressed;
    return push_input_event(context, &event);
}

static bool handle_key(struct nb_host_privsep_client_context *context,
                       const struct nb_privsep_message *message)
{
    struct nb_host_event event;

    if (!input_message_is_valid(context, message->data.key.generation)) {
        return false;
    }
    if (!input_state_accepts(context, message->data.key.generation)) {
        return true;
    }
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_KEY;
    event.milliseconds = message->data.key.milliseconds;
    memcpy(event.data.key.xkb_key_name,
           message->data.key.xkb_key_name,
           sizeof(event.data.key.xkb_key_name));
    event.data.key.pressed = message->data.key.pressed;
    event.data.key.repeat = message->data.key.repeat;
    return push_input_event(context, &event);
}

static bool handle_frame_complete(
    struct nb_host_privsep_client_context *context,
    const struct nb_privsep_message *message)
{
    struct nb_host_event event;

    if (context->state != NB_HOST_STATE_ACTIVE ||
        context->frame.phase != NB_PRIVSEP_CLIENT_FRAME_WAIT_COMPLETE ||
        message->data.frame_complete.generation !=
            context->frame.generation ||
        message->data.frame_complete.serial != context->frame.serial) {
        return false;
    }
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_FRAME_COMPLETE;
    event.milliseconds = message->data.frame_complete.milliseconds;
    event.data.frame_complete.frame_serial = context->frame.serial;
    clear_frame(context);
    return push_event(context, &event);
}

static bool handle_ping(struct nb_host_privsep_client_context *context,
                        const struct nb_privsep_message *message)
{
    struct nb_privsep_message response;

    if (context->shutdown_message_sent ||
        (context->wire_kind == NB_PRIVSEP_CLIENT_WIRE_CONTROL &&
         context->wire_type == NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST)) {
        return true;
    }

    memset(&response, 0, sizeof(response));
    response.type = NB_PRIVSEP_MESSAGE_PONG;
    response.data.token = message->data.token;
    return queue_control(context, &response);
}

static bool handle_shutdown_accepted(
    struct nb_host_privsep_client_context *context,
    const struct nb_privsep_message *message)
{
    struct nb_host_event event;

    if (!context->shutdown_requested ||
        !context->shutdown_message_sent ||
        message->data.token != context->shutdown_token) {
        return false;
    }
    context->shutdown_accepted = true;
    context->event_head = 0;
    context->event_count = 0;
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_QUIT;
    event.milliseconds = monotonic_milliseconds();
    return push_event(context, &event);
}

static bool handle_fatal(struct nb_host_privsep_client_context *context,
                         const struct nb_privsep_message *message)
{
    if (message->data.fatal.generation != 0 && context->has_output &&
        message->data.fatal.generation != context->generation) {
        return false;
    }
    fail_client(context,
                "Privileged helper reported a fatal session error",
                (int)message->data.fatal.system_error);
    return false;
}

static bool handle_message(struct nb_host_privsep_client_context *context,
                           const struct nb_privsep_message *message)
{
    switch (message->type) {
    case NB_PRIVSEP_MESSAGE_READY:
        return handle_ready(context, message);
    case NB_PRIVSEP_MESSAGE_SUSPEND:
        return handle_suspend(context, message);
    case NB_PRIVSEP_MESSAGE_RESUME:
        return handle_resume(context, message);
    case NB_PRIVSEP_MESSAGE_POINTER_MOTION:
        return handle_pointer_motion(context, message);
    case NB_PRIVSEP_MESSAGE_POINTER_BUTTON:
        return handle_pointer_button(context, message);
    case NB_PRIVSEP_MESSAGE_KEY:
        return handle_key(context, message);
    case NB_PRIVSEP_MESSAGE_FRAME_COMPLETE:
        return handle_frame_complete(context, message);
    case NB_PRIVSEP_MESSAGE_PING:
        return handle_ping(context, message);
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED:
        return handle_shutdown_accepted(context, message);
    case NB_PRIVSEP_MESSAGE_FATAL:
        return handle_fatal(context, message);
    default:
        return false;
    }
}

static bool ingest_input(struct nb_host_privsep_client_context *context)
{
    unsigned int messages = 0;

    while (!context->failed && !context->shutdown_accepted &&
           messages < NB_PRIVSEP_CLIENT_IO_MESSAGE_BUDGET) {
        struct nb_privsep_message message;
        enum nb_privsep_parse_status status;
        size_t consumed;

        if (context->read_offset == context->read_used) {
            ssize_t count;

            context->read_offset = 0;
            context->read_used = 0;
            do {
                count = read(context->descriptor,
                             context->read_buffer,
                             sizeof(context->read_buffer));
            } while (count < 0 && errno == EINTR);
            if (count > 0) {
                context->read_used = (size_t)count;
            } else if (count == 0) {
                fail_client(context,
                            "Privileged helper closed the session channel",
                            EPIPE);
                return false;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            } else {
                fail_client(context,
                            "Could not read from the privileged helper",
                            errno);
                return false;
            }
        }

        status = nb_privsep_parser_feed(
            &context->parser,
            context->read_buffer + context->read_offset,
            context->read_used - context->read_offset,
            &consumed,
            &message);
        context->read_offset += consumed;
        if (status == NB_PRIVSEP_PARSE_ERROR) {
            fail_client(context,
                        "Malformed privileged-helper protocol message",
                        EPROTO);
            return false;
        }
        if (status == NB_PRIVSEP_PARSE_MESSAGE) {
            ++messages;
            if (!handle_message(context, &message)) {
                if (!context->failed) {
                    fail_client(context,
                                "Out-of-order privileged-helper message",
                                EPROTO);
                }
                return false;
            }
            if (context->shutdown_accepted) {
                return true;
            }
        } else if (consumed == 0) {
            fail_client(context,
                        "Privsep parser made no progress",
                        EPROTO);
            return false;
        }
    }
    return !context->failed;
}

static enum nb_host_event_status pop_event(
    struct nb_host_privsep_client_context *context,
    struct nb_host_event *event)
{
    if (context->failure_event_pending) {
        memset(event, 0, sizeof(*event));
        event->type = NB_HOST_EVENT_FAILED;
        event->milliseconds = monotonic_milliseconds();
        event->data.failed.system_error = context->system_error;
        context->failure_event_pending = false;
        return NB_HOST_EVENT_STATUS_AVAILABLE;
    }
    if (context->event_count != 0) {
        *event = context->events[context->event_head];
        context->event_head =
            (context->event_head + 1) % NB_PRIVSEP_CLIENT_EVENT_CAPACITY;
        --context->event_count;
        return NB_HOST_EVENT_STATUS_AVAILABLE;
    }
    memset(event, 0, sizeof(*event));
    return context->failed ? NB_HOST_EVENT_STATUS_ERROR
                           : NB_HOST_EVENT_STATUS_EMPTY;
}

static enum nb_host_event_status service_nonblocking(
    struct nb_host_privsep_client_context *context,
    struct nb_host_event *event)
{
    enum nb_host_event_status status;

    if (context->failure_event_pending) {
        return pop_event(context, event);
    }
    (void)ingest_input(context);
    (void)flush_output(context);
    (void)ingest_input(context);
    status = pop_event(context, event);
    return status;
}

static bool privsep_get_output(const void *opaque,
                               struct nb_host_output *output)
{
    const struct nb_host_privsep_client_context *context = opaque;

    if (!context->has_output || context->failed) {
        return false;
    }
    *output = context->output;
    return true;
}

static enum nb_host_state privsep_get_state(const void *opaque)
{
    const struct nb_host_privsep_client_context *context = opaque;

    return context->state;
}

static uint64_t privsep_monotonic_milliseconds(const void *opaque)
{
    (void)opaque;
    return monotonic_milliseconds();
}

static enum nb_host_event_status privsep_poll_event(
    void *opaque,
    struct nb_host_event *event)
{
    return service_nonblocking(opaque, event);
}

static int poll_timeout_remaining(uint64_t start,
                                  uint32_t timeout_milliseconds)
{
    const uint64_t now = monotonic_milliseconds();
    uint64_t elapsed;
    uint64_t remaining;

    if (now < start) {
        return 0;
    }
    elapsed = now - start;
    if (elapsed >= timeout_milliseconds) {
        return 0;
    }
    remaining = (uint64_t)timeout_milliseconds - elapsed;
    return remaining > (uint64_t)INT_MAX ? INT_MAX : (int)remaining;
}

static enum nb_host_event_status privsep_wait_event(
    void *opaque,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event)
{
    struct nb_host_privsep_client_context *context = opaque;
    const uint64_t started = monotonic_milliseconds();
    bool first = true;

    for (;;) {
        struct pollfd descriptor;
        enum nb_host_event_status status =
            service_nonblocking(context, event);
        int timeout;
        int result;

        if (status != NB_HOST_EVENT_STATUS_EMPTY) {
            return status;
        }
        timeout = timeout_milliseconds == UINT32_MAX
                      ? -1
                      : poll_timeout_remaining(started,
                                               timeout_milliseconds);
        if (!first && timeout == 0) {
            return NB_HOST_EVENT_STATUS_EMPTY;
        }
        first = false;
        descriptor.fd = context->descriptor;
        descriptor.events = POLLIN;
        if (context->wire_kind != NB_PRIVSEP_CLIENT_WIRE_NONE ||
            context->control_count != 0 ||
            (context->shutdown_requested &&
             !context->shutdown_message_sent &&
             context->frame.phase == NB_PRIVSEP_CLIENT_FRAME_NONE) ||
            (context->frame.phase != NB_PRIVSEP_CLIENT_FRAME_NONE &&
             context->frame.phase !=
                 NB_PRIVSEP_CLIENT_FRAME_WAIT_COMPLETE)) {
            descriptor.events |= POLLOUT;
        }
        descriptor.revents = 0;
        do {
            result = poll(&descriptor, 1, timeout);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            fail_client(context, "Could not wait for session activity", errno);
            return pop_event(context, event);
        }
        if (result == 0) {
            return NB_HOST_EVENT_STATUS_EMPTY;
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
            fail_client(context,
                        "Privileged-helper session channel failed",
                        EIO);
            return pop_event(context, event);
        }
        /* POLLHUP is intentionally serviced through read() so queued bytes
         * are parsed before EOF becomes a failure. */
    }
}

static bool privsep_set_pointer_capture(void *opaque, bool captured)
{
    (void)opaque;
    (void)captured;
    return true;
}

static enum nb_host_result privsep_present(
    void *opaque,
    const struct nb_host_frame *frame)
{
    struct nb_host_privsep_client_context *context = opaque;
    unsigned char *pixels;
    size_t row_size;
    size_t frame_size;
    int row;

    if (context->failed) {
        return NB_HOST_RESULT_ERROR;
    }
    if (context->state != NB_HOST_STATE_ACTIVE) {
        return NB_HOST_RESULT_SUSPENDED;
    }
    if (context->frame.phase != NB_PRIVSEP_CLIENT_FRAME_NONE) {
        return NB_HOST_RESULT_WOULD_BLOCK;
    }
    if (frame->format != NB_HOST_PIXEL_FORMAT_XRGB8888 ||
        frame->width != context->output.pixel_width ||
        frame->height != context->output.pixel_height ||
        (size_t)frame->width > SIZE_MAX / NB_HOST_BYTES_PER_PIXEL) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    row_size = (size_t)frame->width * NB_HOST_BYTES_PER_PIXEL;
    if ((size_t)frame->height > SIZE_MAX / row_size) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    frame_size = row_size * (size_t)frame->height;
    if (frame_size == 0 || frame_size > NB_PRIVSEP_MAX_FRAME_BYTES) {
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    pixels = malloc(frame_size);
    if (pixels == NULL) {
        fail_client(context, "Could not copy a desktop frame", ENOMEM);
        return NB_HOST_RESULT_ERROR;
    }
    for (row = 0; row < frame->height; ++row) {
        memcpy(pixels + (size_t)row * row_size,
               (const unsigned char *)frame->pixels +
                   (size_t)row * frame->stride,
               row_size);
    }
    context->frame.pixels = pixels;
    context->frame.size = frame_size;
    context->frame.offset = 0;
    context->frame.generation = context->generation;
    context->frame.serial = frame->serial;
    context->frame.phase = NB_PRIVSEP_CLIENT_FRAME_BEGIN;
    context->frame.begun = false;
    context->frame.cancel_requested = false;
    context->frame.drop_after_wire = false;
    return NB_HOST_RESULT_OK;
}

static enum nb_host_result privsep_complete_console_release(void *opaque)
{
    struct nb_host_privsep_client_context *context = opaque;

    if (context->failed) {
        return NB_HOST_RESULT_ERROR;
    }
    if (context->state != NB_HOST_STATE_RELEASE_PENDING) {
        return NB_HOST_RESULT_INVALID_STATE;
    }
    if (context->helper_active) {
        struct nb_host_event event;

        context->state = NB_HOST_STATE_ACQUIRE_PENDING;
        memset(&event, 0, sizeof(event));
        event.type = NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED;
        event.milliseconds = context->resume_milliseconds;
        if (!push_event(context, &event)) {
            return NB_HOST_RESULT_ERROR;
        }
    } else {
        context->state = NB_HOST_STATE_SUSPENDED;
    }
    return NB_HOST_RESULT_OK;
}

static enum nb_host_result privsep_complete_console_acquire(void *opaque)
{
    struct nb_host_privsep_client_context *context = opaque;

    if (context->failed) {
        return NB_HOST_RESULT_ERROR;
    }
    if (context->state != NB_HOST_STATE_ACQUIRE_PENDING) {
        return NB_HOST_RESULT_INVALID_STATE;
    }
    if (!context->helper_active) {
        struct nb_host_event event;

        context->state = NB_HOST_STATE_RELEASE_PENDING;
        memset(&event, 0, sizeof(event));
        event.type = NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED;
        event.milliseconds = context->suspend_milliseconds;
        if (!push_event(context, &event)) {
            return NB_HOST_RESULT_ERROR;
        }
    } else {
        context->state = NB_HOST_STATE_ACTIVE;
    }
    return NB_HOST_RESULT_OK;
}

static bool privsep_get_last_error(const void *opaque,
                                   int *system_error,
                                   char *message,
                                   size_t message_size)
{
    const struct nb_host_privsep_client_context *context = opaque;

    if (context->error[0] == '\0') {
        *system_error = 0;
        message[0] = '\0';
        return false;
    }
    *system_error = context->system_error;
    (void)snprintf(message, message_size, "%s", context->error);
    return true;
}

static void privsep_destroy(void *opaque)
{
    struct nb_host_privsep_client_context *context = opaque;

    if (context == NULL) {
        return;
    }
    if (context->descriptor >= 0) {
        (void)close(context->descriptor);
        context->descriptor = -1;
    }
    clear_frame(context);
    free(context);
}

static const struct nb_host_backend_operations privsep_client_operations = {
    .get_output = privsep_get_output,
    .get_state = privsep_get_state,
    .monotonic_milliseconds = privsep_monotonic_milliseconds,
    .poll_event = privsep_poll_event,
    .wait_event = privsep_wait_event,
    .set_pointer_capture = privsep_set_pointer_capture,
    .present = privsep_present,
    .complete_console_release = privsep_complete_console_release,
    .complete_console_acquire = privsep_complete_console_acquire,
    .get_last_error = privsep_get_last_error,
    .destroy = privsep_destroy
};

static bool current_credentials(struct nb_privsep_credentials *credentials)
{
    uid_t real_uid;
    uid_t effective_uid;
    uid_t saved_uid;
    gid_t real_gid;
    gid_t effective_gid;
    gid_t saved_gid;
    const pid_t process_id = getpid();

#if defined(__linux__)
    if (getresuid(&real_uid, &effective_uid, &saved_uid) != 0 ||
        getresgid(&real_gid, &effective_gid, &saved_gid) != 0) {
        return false;
    }
#else
    real_uid = getuid();
    effective_uid = geteuid();
    saved_uid = effective_uid;
    real_gid = getgid();
    effective_gid = getegid();
    saved_gid = effective_gid;
#endif
    if (process_id <= 0 ||
        !uintmax_to_u32((uintmax_t)process_id,
                       &credentials->process_id) ||
        !uintmax_to_u32((uintmax_t)real_uid,
                       &credentials->real_user_id) ||
        !uintmax_to_u32((uintmax_t)effective_uid,
                       &credentials->effective_user_id) ||
        !uintmax_to_u32((uintmax_t)saved_uid,
                       &credentials->saved_user_id) ||
        !uintmax_to_u32((uintmax_t)real_gid,
                       &credentials->real_group_id) ||
        !uintmax_to_u32((uintmax_t)effective_gid,
                       &credentials->effective_group_id) ||
        !uintmax_to_u32((uintmax_t)saved_gid,
                       &credentials->saved_group_id)) {
        return false;
    }
    return true;
}

struct nb_host *nb_host_privsep_client_create(int descriptor)
{
    struct nb_host_privsep_client_context *context;
    struct nb_privsep_message hello;
    struct nb_host *host;
    int descriptor_flags;
    int status_flags;

    creation_error[0] = '\0';
    creation_system_error = 0;
    if (descriptor < 0) {
        set_creation_error("Invalid privsep session descriptor", EINVAL);
        return NULL;
    }
    descriptor_flags = fcntl(descriptor, F_GETFD);
    if (descriptor_flags < 0 ||
        fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) {
        set_creation_error("Could not protect the session descriptor",
                           errno);
        return NULL;
    }
    status_flags = fcntl(descriptor, F_GETFL);
    if (status_flags < 0 ||
        fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) != 0) {
        set_creation_error("Could not make the session descriptor nonblocking",
                           errno);
        return NULL;
    }

    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        set_creation_error("Could not allocate the privsep host client",
                           ENOMEM);
        return NULL;
    }
    context->descriptor = descriptor;
    context->next_sequence = 1;
    context->state = NB_HOST_STATE_SUSPENDED;
    nb_privsep_parser_init(&context->parser, NB_PRIVSEP_ENDPOINT_HELPER);

    memset(&hello, 0, sizeof(hello));
    hello.type = NB_PRIVSEP_MESSAGE_CORE_HELLO;
    if (!current_credentials(&hello.data.credentials) ||
        !queue_control(context, &hello)) {
        if (!context->failed) {
            set_creation_error("Could not collect core credentials", errno);
        } else {
            set_creation_error(context->error, context->system_error);
        }
        context->descriptor = -1;
        privsep_destroy(context);
        return NULL;
    }

    host = nb_host_backend_create(&privsep_client_operations, context);
    if (host == NULL) {
        set_creation_error("Could not allocate the privsep host facade",
                           ENOMEM);
        context->descriptor = -1;
        privsep_destroy(context);
        return NULL;
    }
    return host;
}

bool nb_host_privsep_client_request_shutdown(struct nb_host *host,
                                             uint64_t token)
{
    struct nb_host_privsep_client_context *context =
        nb_host_backend_context(host, &privsep_client_operations);
    if (context == NULL || context->failed || token == 0 ||
        context->shutdown_requested || !context->hello_sent) {
        return false;
    }
    context->shutdown_requested = true;
    context->shutdown_token = token;
    return true;
}

bool nb_host_privsep_client_is_ready(const struct nb_host *host)
{
    const struct nb_host_privsep_client_context *context =
        nb_host_backend_context_const(host, &privsep_client_operations);

    return context != NULL && context->has_output && !context->failed;
}
