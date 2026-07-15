#include "privsep_protocol.h"

#include <limits.h>
#include <string.h>

enum {
    CORE_HELLO_PAYLOAD_SIZE = 32,
    OUTPUT_PAYLOAD_SIZE = 32,
    READY_PAYLOAD_SIZE = 40,
    SUSPEND_PAYLOAD_SIZE = 24,
    FRAME_BEGIN_PAYLOAD_SIZE = 24,
    FRAME_REFERENCE_PAYLOAD_SIZE = 16,
    POINTER_MOTION_PAYLOAD_SIZE = 24,
    POINTER_BUTTON_PAYLOAD_SIZE = 32,
    KEY_PAYLOAD_SIZE = 24,
    FRAME_COMPLETE_PAYLOAD_SIZE = 24,
    TOKEN_PAYLOAD_SIZE = 8,
    FATAL_PAYLOAD_SIZE = 16
};

static void store_u16(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)(value >> 8);
    destination[1] = (unsigned char)value;
}

static void store_u32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static void store_u64(unsigned char *destination, uint64_t value)
{
    store_u32(destination, (uint32_t)(value >> 32));
    store_u32(destination + 4, (uint32_t)value);
}

static uint16_t load_u16(const unsigned char *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) |
                      (uint16_t)source[1]);
}

static uint32_t load_u32(const unsigned char *source)
{
    return ((uint32_t)source[0] << 24) |
           ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) |
           (uint32_t)source[3];
}

static uint64_t load_u64(const unsigned char *source)
{
    return ((uint64_t)load_u32(source) << 32) |
           (uint64_t)load_u32(source + 4);
}

static int32_t load_i32(const unsigned char *source)
{
    const uint32_t value = load_u32(source);

    if (value <= (uint32_t)INT32_MAX) {
        return (int32_t)value;
    }
    return (int32_t)(-1 - (int32_t)(UINT32_MAX - value));
}

static void store_i32(unsigned char *destination, int32_t value)
{
    store_u32(destination, (uint32_t)value);
}

bool nb_privsep_endpoint_is_valid(enum nb_privsep_endpoint endpoint)
{
    return endpoint == NB_PRIVSEP_ENDPOINT_CORE ||
           endpoint == NB_PRIVSEP_ENDPOINT_HELPER;
}

static bool message_type_is_from(
    enum nb_privsep_message_type type,
    enum nb_privsep_endpoint sender)
{
    if (sender == NB_PRIVSEP_ENDPOINT_CORE) {
        switch (type) {
        case NB_PRIVSEP_MESSAGE_CORE_HELLO:
        case NB_PRIVSEP_MESSAGE_FRAME_BEGIN:
        case NB_PRIVSEP_MESSAGE_FRAME_DATA:
        case NB_PRIVSEP_MESSAGE_FRAME_COMMIT:
        case NB_PRIVSEP_MESSAGE_FRAME_ABORT:
        case NB_PRIVSEP_MESSAGE_PONG:
        case NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST:
            return true;
        default:
            return false;
        }
    }
    if (sender == NB_PRIVSEP_ENDPOINT_HELPER) {
        switch (type) {
        case NB_PRIVSEP_MESSAGE_READY:
        case NB_PRIVSEP_MESSAGE_SUSPEND:
        case NB_PRIVSEP_MESSAGE_RESUME:
        case NB_PRIVSEP_MESSAGE_POINTER_MOTION:
        case NB_PRIVSEP_MESSAGE_POINTER_BUTTON:
        case NB_PRIVSEP_MESSAGE_KEY:
        case NB_PRIVSEP_MESSAGE_FRAME_COMPLETE:
        case NB_PRIVSEP_MESSAGE_PING:
        case NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED:
        case NB_PRIVSEP_MESSAGE_FATAL:
            return true;
        default:
            return false;
        }
    }
    return false;
}

static bool key_name_is_valid(
    const char name[NB_PRIVSEP_XKB_KEY_NAME_CAPACITY])
{
    size_t index;

    for (index = 0; index < NB_PRIVSEP_XKB_KEY_NAME_CAPACITY; ++index) {
        const unsigned char character = (unsigned char)name[index];

        if (character == '\0') {
            return index != 0;
        }
        if (character < 0x21 || character > 0x7e) {
            return false;
        }
    }
    return false;
}

bool nb_privsep_output_is_valid(const struct nb_privsep_output *output)
{
    uint64_t stride;
    uint64_t frame_bytes;

    if (output == NULL || output->logical_width == 0 ||
        output->logical_height == 0 || output->pixel_width == 0 ||
        output->pixel_height == 0 ||
        output->logical_width > NB_PRIVSEP_MAX_OUTPUT_DIMENSION ||
        output->logical_height > NB_PRIVSEP_MAX_OUTPUT_DIMENSION ||
        output->pixel_width > NB_PRIVSEP_MAX_OUTPUT_DIMENSION ||
        output->pixel_height > NB_PRIVSEP_MAX_OUTPUT_DIMENSION ||
        output->refresh_millihertz >
            NB_PRIVSEP_MAX_REFRESH_MILLIHERTZ ||
        output->format != NB_PRIVSEP_PIXEL_FORMAT_XRGB8888) {
        return false;
    }
    stride = (uint64_t)output->pixel_width * UINT64_C(4);
    frame_bytes = stride * (uint64_t)output->pixel_height;
    return stride <= UINT32_MAX && output->stride == (uint32_t)stride &&
           frame_bytes <= NB_PRIVSEP_MAX_FRAME_BYTES &&
           output->frame_bytes == (uint32_t)frame_bytes;
}

static bool fatal_reason_is_valid(enum nb_privsep_fatal_reason reason)
{
    return reason >= NB_PRIVSEP_FATAL_PROTOCOL &&
           reason <= NB_PRIVSEP_FATAL_INTERNAL;
}

bool nb_privsep_message_is_valid(
    enum nb_privsep_endpoint sender,
    const struct nb_privsep_message *message)
{
    uint64_t data_end;

    if (!nb_privsep_endpoint_is_valid(sender) || message == NULL ||
        !message_type_is_from(message->type, sender)) {
        return false;
    }

    switch (message->type) {
    case NB_PRIVSEP_MESSAGE_CORE_HELLO:
        return message->data.credentials.process_id != 0;
    case NB_PRIVSEP_MESSAGE_FRAME_BEGIN:
        return message->data.frame_begin.generation != 0 &&
               message->data.frame_begin.serial != 0 &&
               message->data.frame_begin.frame_bytes != 0 &&
               message->data.frame_begin.frame_bytes <=
                   NB_PRIVSEP_MAX_FRAME_BYTES;
    case NB_PRIVSEP_MESSAGE_FRAME_DATA:
        if (message->data.frame_data.generation == 0 ||
            message->data.frame_data.serial == 0 ||
            message->data.frame_data.size == 0 ||
            message->data.frame_data.size >
                NB_PRIVSEP_FRAME_DATA_CAPACITY ||
            message->data.frame_data.bytes == NULL) {
            return false;
        }
        data_end = (uint64_t)message->data.frame_data.offset +
                   (uint64_t)message->data.frame_data.size;
        return data_end <= NB_PRIVSEP_MAX_FRAME_BYTES;
    case NB_PRIVSEP_MESSAGE_FRAME_COMMIT:
    case NB_PRIVSEP_MESSAGE_FRAME_ABORT:
        return message->data.frame_reference.generation != 0 &&
               message->data.frame_reference.serial != 0;
    case NB_PRIVSEP_MESSAGE_PONG:
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST:
    case NB_PRIVSEP_MESSAGE_PING:
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED:
        return message->data.token != 0;
    case NB_PRIVSEP_MESSAGE_READY:
    case NB_PRIVSEP_MESSAGE_RESUME:
        return message->data.ready.generation != 0 &&
               nb_privsep_output_is_valid(&message->data.ready.output);
    case NB_PRIVSEP_MESSAGE_SUSPEND:
        return message->data.suspend.generation != 0;
    case NB_PRIVSEP_MESSAGE_POINTER_MOTION:
        return message->data.pointer_motion.generation != 0;
    case NB_PRIVSEP_MESSAGE_POINTER_BUTTON:
        return message->data.pointer_button.generation != 0 &&
               message->data.pointer_button.button >=
                   NB_PRIVSEP_POINTER_BUTTON_LEFT &&
               message->data.pointer_button.button <
                   NB_PRIVSEP_POINTER_BUTTON_COUNT;
    case NB_PRIVSEP_MESSAGE_KEY:
        return message->data.key.generation != 0 &&
               key_name_is_valid(message->data.key.xkb_key_name) &&
               (!message->data.key.repeat || message->data.key.pressed);
    case NB_PRIVSEP_MESSAGE_FRAME_COMPLETE:
        return message->data.frame_complete.generation != 0 &&
               message->data.frame_complete.serial != 0;
    case NB_PRIVSEP_MESSAGE_FATAL:
        return fatal_reason_is_valid(message->data.fatal.reason);
    default:
        return false;
    }
}

static bool fixed_payload_size(enum nb_privsep_message_type type,
                               size_t *payload_size)
{
    switch (type) {
    case NB_PRIVSEP_MESSAGE_CORE_HELLO:
        *payload_size = CORE_HELLO_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_FRAME_BEGIN:
        *payload_size = FRAME_BEGIN_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_FRAME_COMMIT:
    case NB_PRIVSEP_MESSAGE_FRAME_ABORT:
        *payload_size = FRAME_REFERENCE_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_PONG:
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST:
    case NB_PRIVSEP_MESSAGE_PING:
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED:
        *payload_size = TOKEN_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_READY:
    case NB_PRIVSEP_MESSAGE_RESUME:
        *payload_size = READY_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_SUSPEND:
        *payload_size = SUSPEND_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_POINTER_MOTION:
        *payload_size = POINTER_MOTION_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_POINTER_BUTTON:
        *payload_size = POINTER_BUTTON_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_KEY:
        *payload_size = KEY_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_FRAME_COMPLETE:
        *payload_size = FRAME_COMPLETE_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_FATAL:
        *payload_size = FATAL_PAYLOAD_SIZE;
        return true;
    case NB_PRIVSEP_MESSAGE_FRAME_DATA:
    default:
        return false;
    }
}

static bool wire_payload_size_is_valid(enum nb_privsep_message_type type,
                                       size_t payload_size)
{
    size_t expected;

    if (type == NB_PRIVSEP_MESSAGE_FRAME_DATA) {
        return payload_size > NB_PRIVSEP_FRAME_DATA_PREFIX_SIZE &&
               payload_size <= NB_PRIVSEP_MAX_PAYLOAD_SIZE;
    }
    return fixed_payload_size(type, &expected) &&
           payload_size == expected;
}

static bool message_payload_size(const struct nb_privsep_message *message,
                                 size_t *payload_size)
{
    if (message->type == NB_PRIVSEP_MESSAGE_FRAME_DATA) {
        *payload_size = NB_PRIVSEP_FRAME_DATA_PREFIX_SIZE +
                        (size_t)message->data.frame_data.size;
        return true;
    }
    return fixed_payload_size(message->type, payload_size);
}

static void encode_output(unsigned char *destination,
                          const struct nb_privsep_output *output)
{
    store_u32(destination, output->logical_width);
    store_u32(destination + 4, output->logical_height);
    store_u32(destination + 8, output->pixel_width);
    store_u32(destination + 12, output->pixel_height);
    store_u32(destination + 16, output->refresh_millihertz);
    store_u32(destination + 20, output->stride);
    store_u32(destination + 24, output->frame_bytes);
    store_u32(destination + 28, (uint32_t)output->format);
}

static bool decode_output(const unsigned char *source,
                          struct nb_privsep_output *output)
{
    output->logical_width = load_u32(source);
    output->logical_height = load_u32(source + 4);
    output->pixel_width = load_u32(source + 8);
    output->pixel_height = load_u32(source + 12);
    output->refresh_millihertz = load_u32(source + 16);
    output->stride = load_u32(source + 20);
    output->frame_bytes = load_u32(source + 24);
    output->format = (enum nb_privsep_pixel_format)load_u32(source + 28);
    return nb_privsep_output_is_valid(output);
}

static void encode_payload(const struct nb_privsep_message *message,
                           unsigned char *payload)
{
    switch (message->type) {
    case NB_PRIVSEP_MESSAGE_CORE_HELLO:
        store_u32(payload, message->data.credentials.process_id);
        store_u32(payload + 4, message->data.credentials.real_user_id);
        store_u32(payload + 8,
                  message->data.credentials.effective_user_id);
        store_u32(payload + 12, message->data.credentials.saved_user_id);
        store_u32(payload + 16, message->data.credentials.real_group_id);
        store_u32(payload + 20,
                  message->data.credentials.effective_group_id);
        store_u32(payload + 24, message->data.credentials.saved_group_id);
        store_u32(payload + 28, 0);
        break;
    case NB_PRIVSEP_MESSAGE_FRAME_BEGIN:
        store_u64(payload, message->data.frame_begin.generation);
        store_u64(payload + 8, message->data.frame_begin.serial);
        store_u32(payload + 16, message->data.frame_begin.frame_bytes);
        store_u32(payload + 20, 0);
        break;
    case NB_PRIVSEP_MESSAGE_FRAME_DATA:
        store_u64(payload, message->data.frame_data.generation);
        store_u64(payload + 8, message->data.frame_data.serial);
        store_u32(payload + 16, message->data.frame_data.offset);
        store_u32(payload + 20, message->data.frame_data.size);
        memcpy(payload + NB_PRIVSEP_FRAME_DATA_PREFIX_SIZE,
               message->data.frame_data.bytes,
               message->data.frame_data.size);
        break;
    case NB_PRIVSEP_MESSAGE_FRAME_COMMIT:
    case NB_PRIVSEP_MESSAGE_FRAME_ABORT:
        store_u64(payload, message->data.frame_reference.generation);
        store_u64(payload + 8, message->data.frame_reference.serial);
        break;
    case NB_PRIVSEP_MESSAGE_READY:
    case NB_PRIVSEP_MESSAGE_RESUME:
        store_u64(payload, message->data.ready.generation);
        encode_output(payload + 8, &message->data.ready.output);
        break;
    case NB_PRIVSEP_MESSAGE_SUSPEND:
        store_u64(payload, message->data.suspend.generation);
        store_u64(payload + 8, message->data.suspend.milliseconds);
        store_u64(payload + 16,
                  message->data.suspend.abandoned_frame_serial);
        break;
    case NB_PRIVSEP_MESSAGE_POINTER_MOTION:
        store_u64(payload, message->data.pointer_motion.generation);
        store_u64(payload + 8, message->data.pointer_motion.milliseconds);
        store_i32(payload + 16, message->data.pointer_motion.x);
        store_i32(payload + 20, message->data.pointer_motion.y);
        break;
    case NB_PRIVSEP_MESSAGE_POINTER_BUTTON:
        store_u64(payload, message->data.pointer_button.generation);
        store_u64(payload + 8, message->data.pointer_button.milliseconds);
        store_i32(payload + 16, message->data.pointer_button.x);
        store_i32(payload + 20, message->data.pointer_button.y);
        store_u32(payload + 24,
                  (uint32_t)message->data.pointer_button.button);
        payload[28] = message->data.pointer_button.pressed ? 1U : 0U;
        memset(payload + 29, 0, 3);
        break;
    case NB_PRIVSEP_MESSAGE_KEY:
        store_u64(payload, message->data.key.generation);
        store_u64(payload + 8, message->data.key.milliseconds);
        memcpy(payload + 16,
               message->data.key.xkb_key_name,
               NB_PRIVSEP_XKB_KEY_NAME_CAPACITY);
        payload[21] = message->data.key.pressed ? 1U : 0U;
        payload[22] = message->data.key.repeat ? 1U : 0U;
        payload[23] = 0;
        break;
    case NB_PRIVSEP_MESSAGE_FRAME_COMPLETE:
        store_u64(payload, message->data.frame_complete.generation);
        store_u64(payload + 8, message->data.frame_complete.serial);
        store_u64(payload + 16,
                  message->data.frame_complete.milliseconds);
        break;
    case NB_PRIVSEP_MESSAGE_PONG:
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST:
    case NB_PRIVSEP_MESSAGE_PING:
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED:
        store_u64(payload, message->data.token);
        break;
    case NB_PRIVSEP_MESSAGE_FATAL:
        store_u32(payload, (uint32_t)message->data.fatal.reason);
        store_u32(payload + 4, message->data.fatal.system_error);
        store_u64(payload + 8, message->data.fatal.generation);
        break;
    default:
        break;
    }
}

bool nb_privsep_message_encode(
    enum nb_privsep_endpoint sender,
    uint64_t sequence,
    const struct nb_privsep_message *message,
    unsigned char *destination,
    size_t destination_size,
    size_t *encoded_size)
{
    size_t payload_size;
    size_t total_size;

    if (encoded_size != NULL) {
        *encoded_size = 0;
    }
    if (sequence == 0 || destination == NULL || encoded_size == NULL ||
        !nb_privsep_message_is_valid(sender, message) ||
        !message_payload_size(message, &payload_size)) {
        return false;
    }
    total_size = NB_PRIVSEP_HEADER_SIZE + payload_size;
    if (total_size > destination_size) {
        return false;
    }

    store_u32(destination, NB_PRIVSEP_PROTOCOL_MAGIC);
    store_u16(destination + 4, NB_PRIVSEP_PROTOCOL_VERSION);
    store_u16(destination + 6, (uint16_t)message->type);
    store_u32(destination + 8, (uint32_t)payload_size);
    store_u32(destination + 12, 0);
    store_u64(destination + 16, sequence);
    encode_payload(message, destination + NB_PRIVSEP_HEADER_SIZE);
    *encoded_size = total_size;
    return true;
}

static bool decode_payload(enum nb_privsep_message_type type,
                           const unsigned char *payload,
                           size_t payload_size,
                           struct nb_privsep_message *message)
{
    uint32_t value;

    memset(message, 0, sizeof(*message));
    message->type = type;

    switch (type) {
    case NB_PRIVSEP_MESSAGE_CORE_HELLO:
        if (load_u32(payload + 28) != 0) {
            return false;
        }
        message->data.credentials.process_id = load_u32(payload);
        message->data.credentials.real_user_id = load_u32(payload + 4);
        message->data.credentials.effective_user_id = load_u32(payload + 8);
        message->data.credentials.saved_user_id = load_u32(payload + 12);
        message->data.credentials.real_group_id = load_u32(payload + 16);
        message->data.credentials.effective_group_id = load_u32(payload + 20);
        message->data.credentials.saved_group_id = load_u32(payload + 24);
        break;
    case NB_PRIVSEP_MESSAGE_FRAME_BEGIN:
        if (load_u32(payload + 20) != 0) {
            return false;
        }
        message->data.frame_begin.generation = load_u64(payload);
        message->data.frame_begin.serial = load_u64(payload + 8);
        message->data.frame_begin.frame_bytes = load_u32(payload + 16);
        break;
    case NB_PRIVSEP_MESSAGE_FRAME_DATA:
        message->data.frame_data.generation = load_u64(payload);
        message->data.frame_data.serial = load_u64(payload + 8);
        message->data.frame_data.offset = load_u32(payload + 16);
        message->data.frame_data.size = load_u32(payload + 20);
        if ((size_t)message->data.frame_data.size !=
            payload_size - NB_PRIVSEP_FRAME_DATA_PREFIX_SIZE) {
            return false;
        }
        message->data.frame_data.bytes =
            payload + NB_PRIVSEP_FRAME_DATA_PREFIX_SIZE;
        break;
    case NB_PRIVSEP_MESSAGE_FRAME_COMMIT:
    case NB_PRIVSEP_MESSAGE_FRAME_ABORT:
        message->data.frame_reference.generation = load_u64(payload);
        message->data.frame_reference.serial = load_u64(payload + 8);
        break;
    case NB_PRIVSEP_MESSAGE_READY:
    case NB_PRIVSEP_MESSAGE_RESUME:
        message->data.ready.generation = load_u64(payload);
        if (!decode_output(payload + 8, &message->data.ready.output)) {
            return false;
        }
        break;
    case NB_PRIVSEP_MESSAGE_SUSPEND:
        message->data.suspend.generation = load_u64(payload);
        message->data.suspend.milliseconds = load_u64(payload + 8);
        message->data.suspend.abandoned_frame_serial =
            load_u64(payload + 16);
        break;
    case NB_PRIVSEP_MESSAGE_POINTER_MOTION:
        message->data.pointer_motion.generation = load_u64(payload);
        message->data.pointer_motion.milliseconds = load_u64(payload + 8);
        message->data.pointer_motion.x = load_i32(payload + 16);
        message->data.pointer_motion.y = load_i32(payload + 20);
        break;
    case NB_PRIVSEP_MESSAGE_POINTER_BUTTON:
        if ((payload[28] != 0 && payload[28] != 1) ||
            payload[29] != 0 || payload[30] != 0 || payload[31] != 0) {
            return false;
        }
        message->data.pointer_button.generation = load_u64(payload);
        message->data.pointer_button.milliseconds = load_u64(payload + 8);
        message->data.pointer_button.x = load_i32(payload + 16);
        message->data.pointer_button.y = load_i32(payload + 20);
        message->data.pointer_button.button =
            (enum nb_privsep_pointer_button)load_u32(payload + 24);
        message->data.pointer_button.pressed = payload[28] != 0;
        break;
    case NB_PRIVSEP_MESSAGE_KEY:
        if ((payload[21] != 0 && payload[21] != 1) ||
            (payload[22] != 0 && payload[22] != 1) ||
            payload[23] != 0) {
            return false;
        }
        message->data.key.generation = load_u64(payload);
        message->data.key.milliseconds = load_u64(payload + 8);
        memcpy(message->data.key.xkb_key_name,
               payload + 16,
               NB_PRIVSEP_XKB_KEY_NAME_CAPACITY);
        message->data.key.pressed = payload[21] != 0;
        message->data.key.repeat = payload[22] != 0;
        break;
    case NB_PRIVSEP_MESSAGE_FRAME_COMPLETE:
        message->data.frame_complete.generation = load_u64(payload);
        message->data.frame_complete.serial = load_u64(payload + 8);
        message->data.frame_complete.milliseconds = load_u64(payload + 16);
        break;
    case NB_PRIVSEP_MESSAGE_PONG:
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST:
    case NB_PRIVSEP_MESSAGE_PING:
    case NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED:
        message->data.token = load_u64(payload);
        break;
    case NB_PRIVSEP_MESSAGE_FATAL:
        value = load_u32(payload);
        message->data.fatal.reason =
            (enum nb_privsep_fatal_reason)value;
        message->data.fatal.system_error = load_u32(payload + 4);
        message->data.fatal.generation = load_u64(payload + 8);
        break;
    default:
        return false;
    }
    return true;
}

void nb_privsep_parser_init(struct nb_privsep_parser *parser,
                            enum nb_privsep_endpoint sender)
{
    if (parser == NULL) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->sender = sender;
    parser->expected_sequence = 1;
    parser->failed = !nb_privsep_endpoint_is_valid(sender);
}

static bool decode_header(struct nb_privsep_parser *parser)
{
    const uint32_t magic = load_u32(parser->header);
    const uint16_t version = load_u16(parser->header + 4);
    const uint16_t wire_type = load_u16(parser->header + 6);
    const uint32_t payload_size = load_u32(parser->header + 8);
    const uint32_t flags = load_u32(parser->header + 12);
    const uint64_t sequence = load_u64(parser->header + 16);
    const enum nb_privsep_message_type type =
        (enum nb_privsep_message_type)wire_type;

    if (magic != NB_PRIVSEP_PROTOCOL_MAGIC ||
        version != NB_PRIVSEP_PROTOCOL_VERSION || flags != 0 ||
        parser->sequence_exhausted ||
        sequence != parser->expected_sequence ||
        !message_type_is_from(type, parser->sender) ||
        payload_size > NB_PRIVSEP_MAX_PAYLOAD_SIZE ||
        !wire_payload_size_is_valid(type, payload_size)) {
        return false;
    }
    parser->pending_type = type;
    parser->pending_sequence = sequence;
    parser->payload_expected = payload_size;
    parser->header_decoded = true;
    return true;
}

static enum nb_privsep_parse_status fail_parser(
    struct nb_privsep_parser *parser)
{
    parser->failed = true;
    return NB_PRIVSEP_PARSE_ERROR;
}

enum nb_privsep_parse_status nb_privsep_parser_feed(
    struct nb_privsep_parser *parser,
    const void *bytes,
    size_t size,
    size_t *consumed,
    struct nb_privsep_message *message)
{
    const unsigned char *source = bytes;
    size_t available = size;
    size_t amount;

    if (consumed == NULL || message == NULL || parser == NULL) {
        return NB_PRIVSEP_PARSE_ERROR;
    }
    *consumed = 0;
    memset(message, 0, sizeof(*message));
    if (parser->failed || (bytes == NULL && size != 0)) {
        return NB_PRIVSEP_PARSE_ERROR;
    }

    if (parser->header_used < NB_PRIVSEP_HEADER_SIZE) {
        amount = NB_PRIVSEP_HEADER_SIZE - parser->header_used;
        if (amount > available) {
            amount = available;
        }
        if (amount != 0) {
            memcpy(parser->header + parser->header_used, source, amount);
            parser->header_used += amount;
            source += amount;
            available -= amount;
            *consumed += amount;
        }
        if (parser->header_used != NB_PRIVSEP_HEADER_SIZE) {
            return NB_PRIVSEP_PARSE_NEED_MORE;
        }
    }
    if (!parser->header_decoded && !decode_header(parser)) {
        return fail_parser(parser);
    }

    amount = parser->payload_expected - parser->payload_used;
    if (amount > available) {
        amount = available;
    }
    if (amount != 0) {
        memcpy(parser->payload + parser->payload_used, source, amount);
        parser->payload_used += amount;
        *consumed += amount;
    }
    if (parser->payload_used != parser->payload_expected) {
        return NB_PRIVSEP_PARSE_NEED_MORE;
    }
    if (!decode_payload(parser->pending_type,
                        parser->payload,
                        parser->payload_expected,
                        message) ||
        !nb_privsep_message_is_valid(parser->sender, message)) {
        return fail_parser(parser);
    }

    message->sequence = parser->pending_sequence;
    if (parser->pending_sequence == UINT64_MAX) {
        parser->sequence_exhausted = true;
    } else {
        parser->expected_sequence = parser->pending_sequence + 1;
    }
    parser->header_used = 0;
    parser->payload_used = 0;
    parser->payload_expected = 0;
    parser->pending_type = (enum nb_privsep_message_type)0;
    parser->pending_sequence = 0;
    parser->header_decoded = false;
    return NB_PRIVSEP_PARSE_MESSAGE;
}

bool nb_privsep_parser_at_message_boundary(
    const struct nb_privsep_parser *parser)
{
    return parser != NULL && !parser->failed && parser->header_used == 0 &&
           parser->payload_used == 0 && !parser->header_decoded;
}

bool nb_privsep_parser_failed(const struct nb_privsep_parser *parser)
{
    return parser == NULL || parser->failed;
}
