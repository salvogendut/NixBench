#include "privsep_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    NB_PRIVSEP_HELPER_MAX_OUTBOUND_MESSAGE =
        NB_PRIVSEP_HEADER_SIZE + 40
};

struct nb_privsep_helper {
    struct nb_privsep_parser parser;
    struct nb_privsep_credentials expected_credentials;
    struct nb_privsep_output output;
    nb_privsep_helper_present_fn present;
    void *present_data;
    unsigned char *staging;
    size_t staging_size;

    unsigned char outbound[NB_PRIVSEP_HELPER_OUTBOUND_CAPACITY];
    size_t outbound_head;
    size_t outbound_size;
    uint64_t next_outbound_sequence;
    bool outbound_sequence_exhausted;

    uint64_t generation;
    uint64_t last_submitted_serial;
    bool hello_received;
    bool ready_sent;
    bool suspended;

    bool frame_open;
    bool frame_stale;
    uint64_t frame_generation;
    uint64_t frame_serial;
    uint32_t frame_expected;
    uint32_t frame_received;

    bool presentation_pending;
    uint64_t presentation_generation;
    uint64_t presentation_serial;
    uint64_t abandoned_generation;
    uint64_t abandoned_serial;

    bool ping_outstanding;
    uint64_t ping_token;
    bool pong_available;
    uint64_t pong_token;

    bool shutdown_requested;
    uint64_t shutdown_request_id;

    enum nb_privsep_helper_error error;
    uint32_t system_error;
    char error_message[NB_PRIVSEP_HELPER_ERROR_CAPACITY];
    bool failed;
};

static bool credentials_equal(const struct nb_privsep_credentials *left,
                              const struct nb_privsep_credentials *right)
{
    return left->process_id == right->process_id &&
           left->real_user_id == right->real_user_id &&
           left->effective_user_id == right->effective_user_id &&
           left->saved_user_id == right->saved_user_id &&
           left->real_group_id == right->real_group_id &&
           left->effective_group_id == right->effective_group_id &&
           left->saved_group_id == right->saved_group_id;
}

static void remember_error(struct nb_privsep_helper *helper,
                           enum nb_privsep_helper_error error,
                           uint32_t system_error,
                           const char *message)
{
    if (helper->error != NB_PRIVSEP_HELPER_ERROR_NONE) {
        return;
    }
    helper->error = error;
    helper->system_error = system_error;
    (void)snprintf(helper->error_message,
                   sizeof(helper->error_message),
                   "%s",
                   message != NULL ? message : "privsep helper failed");
}

static bool enqueue_message(struct nb_privsep_helper *helper,
                            const struct nb_privsep_message *message,
                            bool input)
{
    unsigned char wire[NB_PRIVSEP_HELPER_MAX_OUTBOUND_MESSAGE];
    size_t wire_size = 0;
    size_t available;
    size_t tail;
    size_t first;

    if (helper->outbound_sequence_exhausted ||
        !nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                   helper->next_outbound_sequence,
                                   message,
                                   wire,
                                   sizeof(wire),
                                   &wire_size)) {
        return false;
    }
    available = NB_PRIVSEP_HELPER_OUTBOUND_CAPACITY -
                helper->outbound_size;
    if (wire_size > available ||
        (input && available - wire_size <
                      NB_PRIVSEP_HELPER_CONTROL_RESERVE)) {
        return false;
    }
    tail = (helper->outbound_head + helper->outbound_size) %
           NB_PRIVSEP_HELPER_OUTBOUND_CAPACITY;
    first = NB_PRIVSEP_HELPER_OUTBOUND_CAPACITY - tail;
    if (first > wire_size) {
        first = wire_size;
    }
    memcpy(helper->outbound + tail, wire, first);
    memcpy(helper->outbound, wire + first, wire_size - first);
    helper->outbound_size += wire_size;
    if (helper->next_outbound_sequence == UINT64_MAX) {
        helper->outbound_sequence_exhausted = true;
    } else {
        ++helper->next_outbound_sequence;
    }
    return true;
}

static bool fail_helper(struct nb_privsep_helper *helper,
                        enum nb_privsep_helper_error error,
                        enum nb_privsep_fatal_reason reason,
                        uint32_t system_error,
                        const char *message)
{
    struct nb_privsep_message fatal = {0};

    if (helper->failed) {
        return false;
    }
    remember_error(helper, error, system_error, message);
    fatal.type = NB_PRIVSEP_MESSAGE_FATAL;
    fatal.data.fatal.reason = reason;
    fatal.data.fatal.system_error = system_error;
    fatal.data.fatal.generation = helper->generation;
    (void)enqueue_message(helper, &fatal, false);
    helper->failed = true;
    helper->frame_open = false;
    helper->presentation_pending = false;
    return false;
}

static bool enqueue_or_fail(struct nb_privsep_helper *helper,
                            const struct nb_privsep_message *message,
                            bool input)
{
    if (enqueue_message(helper, message, input)) {
        return true;
    }
    return fail_helper(helper,
                       NB_PRIVSEP_HELPER_ERROR_OUTBOUND_FULL,
                       NB_PRIVSEP_FATAL_CORE_UNRESPONSIVE,
                       0,
                       "privsep helper outbound queue is full");
}

void nb_privsep_helper_options_init(
    struct nb_privsep_helper_options *options)
{
    if (options != NULL) {
        memset(options, 0, sizeof(*options));
    }
}

struct nb_privsep_helper *nb_privsep_helper_create(
    const struct nb_privsep_helper_options *options)
{
    struct nb_privsep_helper *helper;

    if (options == NULL || options->present == NULL ||
        options->expected_credentials.process_id == 0 ||
        !nb_privsep_output_is_valid(&options->output)) {
        return NULL;
    }
    helper = calloc(1, sizeof(*helper));
    if (helper == NULL) {
        return NULL;
    }
    helper->staging = malloc(options->output.frame_bytes);
    if (helper->staging == NULL) {
        free(helper);
        return NULL;
    }
    nb_privsep_parser_init(&helper->parser, NB_PRIVSEP_ENDPOINT_CORE);
    helper->expected_credentials = options->expected_credentials;
    helper->output = options->output;
    helper->present = options->present;
    helper->present_data = options->present_data;
    helper->staging_size = options->output.frame_bytes;
    helper->generation = 1;
    helper->next_outbound_sequence = 1;
    return helper;
}

void nb_privsep_helper_destroy(struct nb_privsep_helper *helper)
{
    if (helper == NULL) {
        return;
    }
    free(helper->staging);
    helper->staging = NULL;
    free(helper);
}

static bool queue_ready(struct nb_privsep_helper *helper,
                        enum nb_privsep_message_type type)
{
    struct nb_privsep_message message = {0};

    message.type = type;
    message.data.ready.generation = helper->generation;
    message.data.ready.output = helper->output;
    return enqueue_or_fail(helper, &message, false);
}

static void clear_frame(struct nb_privsep_helper *helper)
{
    helper->frame_open = false;
    helper->frame_stale = false;
    helper->frame_generation = 0;
    helper->frame_serial = 0;
    helper->frame_expected = 0;
    helper->frame_received = 0;
}

static bool handle_frame_begin(struct nb_privsep_helper *helper,
                               const struct nb_privsep_frame_begin *begin)
{
    const bool stale = begin->generation < helper->generation;

    if (!helper->ready_sent || helper->frame_open ||
        helper->presentation_pending ||
        begin->generation > helper->generation ||
        (!stale && helper->suspended) ||
        begin->serial <= helper->last_submitted_serial ||
        (!stale && begin->frame_bytes != helper->output.frame_bytes)) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "out-of-order FRAME_BEGIN");
    }
    helper->frame_open = true;
    helper->frame_stale = stale;
    helper->frame_generation = begin->generation;
    helper->frame_serial = begin->serial;
    helper->frame_expected = begin->frame_bytes;
    helper->frame_received = 0;
    return true;
}

static bool handle_frame_data(struct nb_privsep_helper *helper,
                              const struct nb_privsep_frame_data *data)
{
    uint64_t end;

    if (!helper->frame_open ||
        data->generation != helper->frame_generation ||
        data->serial != helper->frame_serial ||
        data->offset != helper->frame_received) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "out-of-order FRAME_DATA");
    }
    end = (uint64_t)helper->frame_received + data->size;
    if (end > helper->frame_expected) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "FRAME_DATA exceeds declared frame size");
    }
    if (!helper->frame_stale) {
        if (end > helper->staging_size) {
            return fail_helper(helper,
                               NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                               NB_PRIVSEP_FATAL_PROTOCOL,
                               0,
                               "FRAME_DATA exceeds trusted output size");
        }
        memcpy(helper->staging + helper->frame_received,
               data->bytes,
               data->size);
    }
    helper->frame_received = (uint32_t)end;
    return true;
}

static bool handle_frame_reference(
    struct nb_privsep_helper *helper,
    enum nb_privsep_message_type type,
    const struct nb_privsep_frame_reference *reference)
{
    struct nb_host_frame frame;
    bool stale;

    if (!helper->frame_open ||
        reference->generation != helper->frame_generation ||
        reference->serial != helper->frame_serial) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "frame terminator does not match open frame");
    }
    stale = helper->frame_stale;
    if (type == NB_PRIVSEP_MESSAGE_FRAME_ABORT) {
        clear_frame(helper);
        return true;
    }
    if (helper->frame_received != helper->frame_expected) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "FRAME_COMMIT arrived before the complete frame");
    }
    if (stale) {
        clear_frame(helper);
        return true;
    }

    memset(&frame, 0, sizeof(frame));
    frame.pixels = helper->staging;
    frame.width = (int)helper->output.pixel_width;
    frame.height = (int)helper->output.pixel_height;
    frame.stride = helper->output.stride;
    frame.format = NB_HOST_PIXEL_FORMAT_XRGB8888;
    frame.serial = helper->frame_serial;
    if (!helper->present(helper->present_data,
                         helper->generation,
                         &frame)) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PRESENTATION,
                           NB_PRIVSEP_FATAL_DEVICE,
                           0,
                           "privileged presentation callback failed");
    }
    helper->presentation_pending = true;
    helper->presentation_generation = helper->generation;
    helper->presentation_serial = helper->frame_serial;
    helper->last_submitted_serial = helper->frame_serial;
    clear_frame(helper);
    return true;
}

static bool handle_pong(struct nb_privsep_helper *helper, uint64_t token)
{
    if (!helper->ping_outstanding || token != helper->ping_token) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "unexpected heartbeat response");
    }
    helper->ping_outstanding = false;
    helper->pong_available = true;
    helper->pong_token = token;
    return true;
}

static bool handle_shutdown(struct nb_privsep_helper *helper,
                            uint64_t request_id)
{
    struct nb_privsep_message accepted = {0};

    if (helper->shutdown_requested) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "duplicate shutdown request");
    }
    helper->shutdown_requested = true;
    helper->shutdown_request_id = request_id;
    clear_frame(helper);
    helper->presentation_pending = false;
    accepted.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED;
    accepted.data.token = request_id;
    return enqueue_or_fail(helper, &accepted, false);
}

static bool handle_message(struct nb_privsep_helper *helper,
                           const struct nb_privsep_message *message)
{
    if (!helper->hello_received) {
        if (message->type != NB_PRIVSEP_MESSAGE_CORE_HELLO) {
            return fail_helper(helper,
                               NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                               NB_PRIVSEP_FATAL_PROTOCOL,
                               0,
                               "CORE_HELLO must be the first message");
        }
        if (!credentials_equal(&message->data.credentials,
                               &helper->expected_credentials)) {
            return fail_helper(helper,
                               NB_PRIVSEP_HELPER_ERROR_CREDENTIALS,
                               NB_PRIVSEP_FATAL_PROTOCOL,
                               0,
                               "CORE_HELLO credentials do not match child");
        }
        helper->hello_received = true;
        if (!helper->suspended) {
            if (!queue_ready(helper, NB_PRIVSEP_MESSAGE_READY)) {
                return false;
            }
            helper->ready_sent = true;
        }
        return true;
    }
    if (helper->shutdown_requested) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "message received after shutdown request");
    }

    switch (message->type) {
    case NB_PRIVSEP_MESSAGE_CORE_HELLO:
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "duplicate CORE_HELLO");
    case NB_PRIVSEP_MESSAGE_FRAME_BEGIN:
        return handle_frame_begin(helper, &message->data.frame_begin);
    case NB_PRIVSEP_MESSAGE_FRAME_DATA:
        return handle_frame_data(helper, &message->data.frame_data);
    case NB_PRIVSEP_MESSAGE_FRAME_COMMIT:
    case NB_PRIVSEP_MESSAGE_FRAME_ABORT:
        return handle_frame_reference(helper,
                                      message->type,
                                      &message->data.frame_reference);
    case NB_PRIVSEP_MESSAGE_PONG:
        return handle_pong(helper, message->data.token);
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST:
        return handle_shutdown(helper, message->data.token);
    default:
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                           NB_PRIVSEP_FATAL_PROTOCOL,
                           0,
                           "invalid core message");
    }
}

bool nb_privsep_helper_feed(struct nb_privsep_helper *helper,
                            const void *bytes,
                            size_t size,
                            size_t *consumed)
{
    const unsigned char *source = bytes;
    size_t total = 0;

    if (consumed == NULL || helper == NULL ||
        (bytes == NULL && size != 0) || helper->failed) {
        return false;
    }
    *consumed = 0;
    while (total < size) {
        struct nb_privsep_message message;
        size_t amount = 0;
        const enum nb_privsep_parse_status status =
            nb_privsep_parser_feed(&helper->parser,
                                    source + total,
                                    size - total,
                                    &amount,
                                    &message);

        total += amount;
        *consumed = total;
        if (status == NB_PRIVSEP_PARSE_ERROR) {
            return fail_helper(helper,
                               NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
                               NB_PRIVSEP_FATAL_PROTOCOL,
                               0,
                               "malformed core protocol message");
        }
        if (status == NB_PRIVSEP_PARSE_NEED_MORE) {
            return true;
        }
        if (!handle_message(helper, &message)) {
            return false;
        }
    }
    return true;
}

bool nb_privsep_helper_peek_outbound(
    const struct nb_privsep_helper *helper,
    const unsigned char **bytes,
    size_t *size)
{
    size_t contiguous;

    if (helper == NULL || bytes == NULL || size == NULL) {
        return false;
    }
    if (helper->outbound_size == 0) {
        *bytes = NULL;
        *size = 0;
        return true;
    }
    contiguous = NB_PRIVSEP_HELPER_OUTBOUND_CAPACITY -
                 helper->outbound_head;
    if (contiguous > helper->outbound_size) {
        contiguous = helper->outbound_size;
    }
    *bytes = helper->outbound + helper->outbound_head;
    *size = contiguous;
    return true;
}

bool nb_privsep_helper_consume_outbound(
    struct nb_privsep_helper *helper,
    size_t size)
{
    if (helper == NULL || size > helper->outbound_size) {
        return false;
    }
    helper->outbound_head = (helper->outbound_head + size) %
                            NB_PRIVSEP_HELPER_OUTBOUND_CAPACITY;
    helper->outbound_size -= size;
    if (helper->outbound_size == 0) {
        helper->outbound_head = 0;
    }
    return true;
}

size_t nb_privsep_helper_outbound_size(
    const struct nb_privsep_helper *helper)
{
    return helper != NULL ? helper->outbound_size : 0;
}

bool nb_privsep_helper_suspend(struct nb_privsep_helper *helper,
                               uint64_t milliseconds)
{
    struct nb_privsep_message message = {0};
    uint64_t abandoned = 0;

    if (helper == NULL || helper->failed || helper->shutdown_requested ||
        helper->suspended || helper->generation == UINT64_MAX) {
        return false;
    }
    if (helper->presentation_pending) {
        abandoned = helper->presentation_serial;
        helper->abandoned_generation = helper->presentation_generation;
        helper->abandoned_serial = helper->presentation_serial;
        helper->presentation_pending = false;
    } else if (helper->frame_open) {
        abandoned = helper->frame_serial;
    }
    if (helper->frame_open) {
        helper->frame_stale = true;
    }
    ++helper->generation;
    helper->suspended = true;
    if (!helper->ready_sent) {
        return true;
    }
    message.type = NB_PRIVSEP_MESSAGE_SUSPEND;
    message.data.suspend.generation = helper->generation;
    message.data.suspend.milliseconds = milliseconds;
    message.data.suspend.abandoned_frame_serial = abandoned;
    return enqueue_or_fail(helper, &message, false);
}

bool nb_privsep_helper_resume(struct nb_privsep_helper *helper,
                              const struct nb_privsep_output *output)
{
    unsigned char *replacement = NULL;
    enum nb_privsep_message_type type;

    if (helper == NULL || helper->failed || helper->shutdown_requested ||
        !helper->suspended || !nb_privsep_output_is_valid(output)) {
        return false;
    }
    if (output->frame_bytes != helper->staging_size) {
        replacement = malloc(output->frame_bytes);
        if (replacement == NULL) {
            return fail_helper(helper,
                               NB_PRIVSEP_HELPER_ERROR_ALLOCATION,
                               NB_PRIVSEP_FATAL_INTERNAL,
                               0,
                               "could not resize trusted frame staging");
        }
    }
    if (replacement != NULL) {
        free(helper->staging);
        helper->staging = replacement;
        helper->staging_size = output->frame_bytes;
    }
    helper->output = *output;
    helper->suspended = false;
    if (!helper->hello_received) {
        return true;
    }
    type = helper->ready_sent ? NB_PRIVSEP_MESSAGE_RESUME
                              : NB_PRIVSEP_MESSAGE_READY;
    if (!queue_ready(helper, type)) {
        return false;
    }
    helper->ready_sent = true;
    return true;
}

static bool input_event_is_supported(const struct nb_host_event *event)
{
    size_t index;

    if (event == NULL) {
        return false;
    }
    if (event->type == NB_HOST_EVENT_POINTER_MOTION) {
        return true;
    }
    if (event->type == NB_HOST_EVENT_POINTER_BUTTON) {
        return event->data.pointer_button.button >=
                   NB_HOST_POINTER_BUTTON_LEFT &&
               event->data.pointer_button.button <
                   NB_HOST_POINTER_BUTTON_COUNT;
    }
    if (event->type != NB_HOST_EVENT_KEY ||
        (event->data.key.repeat && !event->data.key.pressed)) {
        return false;
    }
    for (index = 0; index < NB_HOST_XKB_KEY_NAME_CAPACITY; ++index) {
        const unsigned char character =
            (unsigned char)event->data.key.xkb_key_name[index];

        if (character == '\0') {
            return index != 0;
        }
        if (character < 0x21 || character > 0x7e) {
            return false;
        }
    }
    return false;
}

bool nb_privsep_helper_send_input(struct nb_privsep_helper *helper,
                                  const struct nb_host_event *event)
{
    struct nb_privsep_message message = {0};

    if (helper == NULL || helper->failed || helper->shutdown_requested ||
        !helper->ready_sent || helper->suspended ||
        !input_event_is_supported(event)) {
        return false;
    }
    if (event->type == NB_HOST_EVENT_POINTER_MOTION) {
        message.type = NB_PRIVSEP_MESSAGE_POINTER_MOTION;
        message.data.pointer_motion.generation = helper->generation;
        message.data.pointer_motion.milliseconds = event->milliseconds;
        message.data.pointer_motion.x = event->data.pointer_motion.x;
        message.data.pointer_motion.y = event->data.pointer_motion.y;
    } else if (event->type == NB_HOST_EVENT_POINTER_BUTTON) {
        message.type = NB_PRIVSEP_MESSAGE_POINTER_BUTTON;
        message.data.pointer_button.generation = helper->generation;
        message.data.pointer_button.milliseconds = event->milliseconds;
        message.data.pointer_button.x = event->data.pointer_button.x;
        message.data.pointer_button.y = event->data.pointer_button.y;
        message.data.pointer_button.button =
            (enum nb_privsep_pointer_button)
                event->data.pointer_button.button;
        message.data.pointer_button.pressed =
            event->data.pointer_button.pressed;
    } else {
        message.type = NB_PRIVSEP_MESSAGE_KEY;
        message.data.key.generation = helper->generation;
        message.data.key.milliseconds = event->milliseconds;
        memcpy(message.data.key.xkb_key_name,
               event->data.key.xkb_key_name,
               sizeof(message.data.key.xkb_key_name));
        message.data.key.pressed = event->data.key.pressed;
        message.data.key.repeat = event->data.key.repeat;
    }
    return enqueue_or_fail(helper, &message, true);
}

bool nb_privsep_helper_complete_frame(struct nb_privsep_helper *helper,
                                      uint64_t generation,
                                      uint64_t serial,
                                      uint64_t milliseconds)
{
    struct nb_privsep_message message = {0};

    if (helper == NULL || helper->failed || helper->shutdown_requested) {
        return false;
    }
    /* A release invalidates device work without waiting for its callback. */
    if (generation < helper->generation && serial != 0 &&
        serial <= helper->last_submitted_serial) {
        if (generation == helper->abandoned_generation &&
            serial == helper->abandoned_serial) {
            helper->abandoned_generation = 0;
            helper->abandoned_serial = 0;
        }
        return true;
    }
    if (!helper->presentation_pending) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_INVALID_STATE,
                           NB_PRIVSEP_FATAL_INTERNAL,
                           0,
                           "unexpected privileged frame completion");
    }
    if (generation != helper->presentation_generation ||
        serial != helper->presentation_serial) {
        return fail_helper(helper,
                           NB_PRIVSEP_HELPER_ERROR_INVALID_STATE,
                           NB_PRIVSEP_FATAL_INTERNAL,
                           0,
                           "privileged frame completion serial mismatch");
    }
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMPLETE;
    message.data.frame_complete.generation = generation;
    message.data.frame_complete.serial = serial;
    message.data.frame_complete.milliseconds = milliseconds;
    if (!enqueue_or_fail(helper, &message, false)) {
        return false;
    }
    helper->presentation_pending = false;
    return true;
}

bool nb_privsep_helper_send_ping(struct nb_privsep_helper *helper,
                                uint64_t token)
{
    struct nb_privsep_message message = {0};

    if (helper == NULL || helper->failed || helper->shutdown_requested ||
        !helper->ready_sent || helper->ping_outstanding || token == 0) {
        return false;
    }
    message.type = NB_PRIVSEP_MESSAGE_PING;
    message.data.token = token;
    if (!enqueue_or_fail(helper, &message, false)) {
        return false;
    }
    helper->ping_outstanding = true;
    helper->ping_token = token;
    return true;
}

bool nb_privsep_helper_report_fatal(
    struct nb_privsep_helper *helper,
    enum nb_privsep_fatal_reason reason,
    uint32_t system_error,
    const char *message)
{
    struct nb_privsep_message fatal = {0};
    bool queued;

    if (helper == NULL || helper->failed ||
        reason < NB_PRIVSEP_FATAL_PROTOCOL ||
        reason > NB_PRIVSEP_FATAL_INTERNAL) {
        return false;
    }
    remember_error(helper,
                   NB_PRIVSEP_HELPER_ERROR_PRESENTATION,
                   system_error,
                   message);
    fatal.type = NB_PRIVSEP_MESSAGE_FATAL;
    fatal.data.fatal.reason = reason;
    fatal.data.fatal.system_error = system_error;
    fatal.data.fatal.generation = helper->generation;
    queued = enqueue_message(helper, &fatal, false);
    helper->failed = true;
    helper->frame_open = false;
    helper->presentation_pending = false;
    return queued;
}

bool nb_privsep_helper_is_ready(const struct nb_privsep_helper *helper)
{
    return helper != NULL && helper->ready_sent && !helper->failed;
}

bool nb_privsep_helper_is_suspended(const struct nb_privsep_helper *helper)
{
    return helper != NULL && helper->suspended;
}

bool nb_privsep_helper_presentation_pending(
    const struct nb_privsep_helper *helper)
{
    return helper != NULL && helper->presentation_pending;
}

uint64_t nb_privsep_helper_generation(
    const struct nb_privsep_helper *helper)
{
    return helper != NULL ? helper->generation : 0;
}

bool nb_privsep_helper_ping_outstanding(
    const struct nb_privsep_helper *helper)
{
    return helper != NULL && helper->ping_outstanding;
}

bool nb_privsep_helper_take_pong(struct nb_privsep_helper *helper,
                                uint64_t *token)
{
    if (helper == NULL || token == NULL || !helper->pong_available) {
        return false;
    }
    *token = helper->pong_token;
    helper->pong_available = false;
    return true;
}

bool nb_privsep_helper_shutdown_requested(
    const struct nb_privsep_helper *helper,
    uint64_t *request_id)
{
    if (helper == NULL || request_id == NULL ||
        !helper->shutdown_requested) {
        return false;
    }
    *request_id = helper->shutdown_request_id;
    return true;
}

bool nb_privsep_helper_failed(const struct nb_privsep_helper *helper)
{
    return helper == NULL || helper->failed;
}

bool nb_privsep_helper_get_last_error(
    const struct nb_privsep_helper *helper,
    enum nb_privsep_helper_error *error,
    uint32_t *system_error,
    char *message,
    size_t message_size)
{
    if (helper == NULL || error == NULL || system_error == NULL ||
        message == NULL || message_size == 0 ||
        helper->error == NB_PRIVSEP_HELPER_ERROR_NONE) {
        return false;
    }
    *error = helper->error;
    *system_error = helper->system_error;
    (void)snprintf(message, message_size, "%s", helper->error_message);
    return true;
}
