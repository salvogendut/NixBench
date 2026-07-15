#ifndef NIXBENCH_PRIVSEP_PROTOCOL_H
#define NIXBENCH_PRIVSEP_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Version-one helper/core messages use an inherited local byte stream.  All
 * control integers are big endian on the wire.  FRAME_DATA bytes are opaque;
 * for version one they contain tightly packed, native-endian XRGB8888 words.
 */
enum {
    NB_PRIVSEP_PROTOCOL_MAGIC = 0x4e425043,
    NB_PRIVSEP_PROTOCOL_VERSION = 1,
    NB_PRIVSEP_HEADER_SIZE = 24,
    NB_PRIVSEP_FRAME_DATA_CAPACITY = 65536,
    NB_PRIVSEP_FRAME_DATA_PREFIX_SIZE = 24,
    NB_PRIVSEP_MAX_PAYLOAD_SIZE =
        NB_PRIVSEP_FRAME_DATA_PREFIX_SIZE +
        NB_PRIVSEP_FRAME_DATA_CAPACITY,
    NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE =
        NB_PRIVSEP_HEADER_SIZE + NB_PRIVSEP_MAX_PAYLOAD_SIZE,
    NB_PRIVSEP_MAX_OUTPUT_DIMENSION = 8192,
    NB_PRIVSEP_MAX_REFRESH_MILLIHERTZ = 1000000,
    NB_PRIVSEP_MAX_FRAME_BYTES = 128 * 1024 * 1024,
    NB_PRIVSEP_XKB_KEY_NAME_CAPACITY = 5
};

enum nb_privsep_endpoint {
    NB_PRIVSEP_ENDPOINT_CORE,
    NB_PRIVSEP_ENDPOINT_HELPER
};

enum nb_privsep_message_type {
    NB_PRIVSEP_MESSAGE_CORE_HELLO = 0x0001,
    NB_PRIVSEP_MESSAGE_FRAME_BEGIN = 0x0002,
    NB_PRIVSEP_MESSAGE_FRAME_DATA = 0x0003,
    NB_PRIVSEP_MESSAGE_FRAME_COMMIT = 0x0004,
    NB_PRIVSEP_MESSAGE_FRAME_ABORT = 0x0005,
    NB_PRIVSEP_MESSAGE_PONG = 0x0006,
    NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST = 0x0007,

    NB_PRIVSEP_MESSAGE_READY = 0x8001,
    NB_PRIVSEP_MESSAGE_SUSPEND = 0x8002,
    NB_PRIVSEP_MESSAGE_RESUME = 0x8003,
    NB_PRIVSEP_MESSAGE_POINTER_MOTION = 0x8004,
    NB_PRIVSEP_MESSAGE_POINTER_BUTTON = 0x8005,
    NB_PRIVSEP_MESSAGE_KEY = 0x8006,
    NB_PRIVSEP_MESSAGE_FRAME_COMPLETE = 0x8007,
    NB_PRIVSEP_MESSAGE_PING = 0x8008,
    NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED = 0x8009,
    NB_PRIVSEP_MESSAGE_FATAL = 0x800a
};

enum nb_privsep_pixel_format {
    NB_PRIVSEP_PIXEL_FORMAT_XRGB8888 = 1
};

enum nb_privsep_pointer_button {
    NB_PRIVSEP_POINTER_BUTTON_LEFT,
    NB_PRIVSEP_POINTER_BUTTON_MIDDLE,
    NB_PRIVSEP_POINTER_BUTTON_RIGHT,
    NB_PRIVSEP_POINTER_BUTTON_SIDE,
    NB_PRIVSEP_POINTER_BUTTON_EXTRA,
    NB_PRIVSEP_POINTER_BUTTON_COUNT
};

enum nb_privsep_fatal_reason {
    NB_PRIVSEP_FATAL_PROTOCOL = 1,
    NB_PRIVSEP_FATAL_DEVICE = 2,
    NB_PRIVSEP_FATAL_CORE_UNRESPONSIVE = 3,
    NB_PRIVSEP_FATAL_HELPER_TERMINATING = 4,
    NB_PRIVSEP_FATAL_INTERNAL = 5
};

struct nb_privsep_credentials {
    uint32_t process_id;
    uint32_t real_user_id;
    uint32_t effective_user_id;
    uint32_t saved_user_id;
    uint32_t real_group_id;
    uint32_t effective_group_id;
    uint32_t saved_group_id;
};

struct nb_privsep_output {
    uint32_t logical_width;
    uint32_t logical_height;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t refresh_millihertz;
    uint32_t stride;
    uint32_t frame_bytes;
    enum nb_privsep_pixel_format format;
};

struct nb_privsep_ready {
    uint64_t generation;
    struct nb_privsep_output output;
};

struct nb_privsep_suspend {
    uint64_t generation;
    uint64_t milliseconds;
    uint64_t abandoned_frame_serial;
};

struct nb_privsep_frame_begin {
    uint64_t generation;
    uint64_t serial;
    uint32_t frame_bytes;
};

struct nb_privsep_frame_data {
    uint64_t generation;
    uint64_t serial;
    uint32_t offset;
    uint32_t size;
    const unsigned char *bytes;
};

struct nb_privsep_frame_reference {
    uint64_t generation;
    uint64_t serial;
};

struct nb_privsep_pointer_motion {
    uint64_t generation;
    uint64_t milliseconds;
    int32_t x;
    int32_t y;
};

struct nb_privsep_pointer_button_event {
    uint64_t generation;
    uint64_t milliseconds;
    int32_t x;
    int32_t y;
    enum nb_privsep_pointer_button button;
    bool pressed;
};

struct nb_privsep_key_event {
    uint64_t generation;
    uint64_t milliseconds;
    char xkb_key_name[NB_PRIVSEP_XKB_KEY_NAME_CAPACITY];
    bool pressed;
    bool repeat;
};

struct nb_privsep_frame_complete {
    uint64_t generation;
    uint64_t serial;
    uint64_t milliseconds;
};

struct nb_privsep_fatal {
    enum nb_privsep_fatal_reason reason;
    uint32_t system_error;
    uint64_t generation;
};

struct nb_privsep_message {
    enum nb_privsep_message_type type;
    uint64_t sequence;
    union {
        struct nb_privsep_credentials credentials;
        struct nb_privsep_ready ready;
        struct nb_privsep_suspend suspend;
        struct nb_privsep_frame_begin frame_begin;
        struct nb_privsep_frame_data frame_data;
        struct nb_privsep_frame_reference frame_reference;
        struct nb_privsep_pointer_motion pointer_motion;
        struct nb_privsep_pointer_button_event pointer_button;
        struct nb_privsep_key_event key;
        struct nb_privsep_frame_complete frame_complete;
        struct nb_privsep_fatal fatal;
        uint64_t token;
    } data;
};

enum nb_privsep_parse_status {
    NB_PRIVSEP_PARSE_ERROR = -1,
    NB_PRIVSEP_PARSE_NEED_MORE,
    NB_PRIVSEP_PARSE_MESSAGE
};

/*
 * A parser is initialized with the endpoint which sends its input.  FRAME_DATA
 * bytes returned in a message borrow parser storage and remain valid only until
 * the next call to nb_privsep_parser_feed() for that parser.
 */
struct nb_privsep_parser {
    enum nb_privsep_endpoint sender;
    uint64_t expected_sequence;
    unsigned char header[NB_PRIVSEP_HEADER_SIZE];
    unsigned char payload[NB_PRIVSEP_MAX_PAYLOAD_SIZE];
    size_t header_used;
    size_t payload_used;
    size_t payload_expected;
    enum nb_privsep_message_type pending_type;
    uint64_t pending_sequence;
    bool header_decoded;
    bool sequence_exhausted;
    bool failed;
};

bool nb_privsep_endpoint_is_valid(enum nb_privsep_endpoint endpoint);
bool nb_privsep_output_is_valid(const struct nb_privsep_output *output);
bool nb_privsep_message_is_valid(
    enum nb_privsep_endpoint sender,
    const struct nb_privsep_message *message);

/* The sequence is supplied separately and must be nonzero. */
bool nb_privsep_message_encode(
    enum nb_privsep_endpoint sender,
    uint64_t sequence,
    const struct nb_privsep_message *message,
    unsigned char *destination,
    size_t destination_size,
    size_t *encoded_size);

void nb_privsep_parser_init(struct nb_privsep_parser *parser,
                            enum nb_privsep_endpoint sender);
enum nb_privsep_parse_status nb_privsep_parser_feed(
    struct nb_privsep_parser *parser,
    const void *bytes,
    size_t size,
    size_t *consumed,
    struct nb_privsep_message *message);
bool nb_privsep_parser_at_message_boundary(
    const struct nb_privsep_parser *parser);
bool nb_privsep_parser_failed(const struct nb_privsep_parser *parser);

#endif
