#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "privsep_protocol.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static const struct nb_privsep_output test_output = {
    1366,
    768,
    1366,
    768,
    60000,
    1366 * 4,
    1366 * 768 * 4,
    NB_PRIVSEP_PIXEL_FORMAT_XRGB8888
};

static void write_u16(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)(value >> 8);
    destination[1] = (unsigned char)value;
}

static void write_u32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static void write_u64(unsigned char *destination, uint64_t value)
{
    write_u32(destination, (uint32_t)(value >> 32));
    write_u32(destination + 4, (uint32_t)value);
}

static unsigned char *allocate_wire(void)
{
    unsigned char *wire = malloc(NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE);

    CHECK(wire != NULL);
    return wire;
}

static size_t encode(enum nb_privsep_endpoint sender,
                     uint64_t sequence,
                     const struct nb_privsep_message *message,
                     unsigned char *wire)
{
    size_t size = 0;

    CHECK(nb_privsep_message_encode(sender,
                                    sequence,
                                    message,
                                    wire,
                                    NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE,
                                    &size));
    CHECK(size >= NB_PRIVSEP_HEADER_SIZE);
    return size;
}

static void test_output_validation(void)
{
    struct nb_privsep_output output = test_output;

    CHECK(nb_privsep_endpoint_is_valid(NB_PRIVSEP_ENDPOINT_CORE));
    CHECK(nb_privsep_endpoint_is_valid(NB_PRIVSEP_ENDPOINT_HELPER));
    CHECK(!nb_privsep_endpoint_is_valid((enum nb_privsep_endpoint)2));
    CHECK(nb_privsep_output_is_valid(&output));
    CHECK(!nb_privsep_output_is_valid(NULL));

    output.logical_width = 0;
    CHECK(!nb_privsep_output_is_valid(&output));
    output = test_output;
    output.logical_height = NB_PRIVSEP_MAX_OUTPUT_DIMENSION + 1;
    CHECK(!nb_privsep_output_is_valid(&output));
    output = test_output;
    output.refresh_millihertz =
        NB_PRIVSEP_MAX_REFRESH_MILLIHERTZ + 1;
    CHECK(!nb_privsep_output_is_valid(&output));
    output = test_output;
    ++output.stride;
    CHECK(!nb_privsep_output_is_valid(&output));
    output = test_output;
    --output.frame_bytes;
    CHECK(!nb_privsep_output_is_valid(&output));
    output = test_output;
    output.format = (enum nb_privsep_pixel_format)2;
    CHECK(!nb_privsep_output_is_valid(&output));

    output.logical_width = 8192;
    output.logical_height = 4096;
    output.pixel_width = 8192;
    output.pixel_height = 4096;
    output.refresh_millihertz = 0;
    output.stride = 8192 * 4;
    output.frame_bytes = NB_PRIVSEP_MAX_FRAME_BYTES;
    output.format = NB_PRIVSEP_PIXEL_FORMAT_XRGB8888;
    CHECK(nb_privsep_output_is_valid(&output));
    output.pixel_height = 4097;
    output.frame_bytes = output.stride * output.pixel_height;
    CHECK(!nb_privsep_output_is_valid(&output));
}

static void test_core_round_trips(void)
{
    struct nb_privsep_parser parser;
    struct nb_privsep_message source = {0};
    struct nb_privsep_message decoded;
    unsigned char data[17];
    unsigned char *wire = allocate_wire();
    uint64_t sequence = 1;
    size_t size;
    size_t consumed;
    size_t index;

    if (wire == NULL) {
        return;
    }
    for (index = 0; index < sizeof(data); ++index) {
        data[index] = (unsigned char)(index * 7U);
    }
    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_CORE);

    source.type = NB_PRIVSEP_MESSAGE_CORE_HELLO;
    source.data.credentials.process_id = 1234;
    source.data.credentials.real_user_id = 1000;
    source.data.credentials.effective_user_id = 1000;
    source.data.credentials.saved_user_id = 1000;
    source.data.credentials.real_group_id = 100;
    source.data.credentials.effective_group_id = 100;
    source.data.credentials.saved_group_id = 100;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(consumed == size);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.type == NB_PRIVSEP_MESSAGE_CORE_HELLO);
    CHECK(decoded.data.credentials.process_id == 1234);
    CHECK(decoded.data.credentials.saved_group_id == 100);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    source.data.frame_begin.generation = 8;
    source.data.frame_begin.serial = 99;
    source.data.frame_begin.frame_bytes = sizeof(data);
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.frame_begin.frame_bytes == sizeof(data));

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    source.data.frame_begin.generation = 8;
    source.data.frame_begin.serial = 100;
    source.data.frame_begin.frame_bytes = 16;
    source.data.frame_begin.damage_x = 1;
    source.data.frame_begin.damage_y = 2;
    source.data.frame_begin.damage_width = 2;
    source.data.frame_begin.damage_height = 2;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.frame_begin.frame_bytes == 16);
    CHECK(decoded.data.frame_begin.damage_x == 1);
    CHECK(decoded.data.frame_begin.damage_y == 2);
    CHECK(decoded.data.frame_begin.damage_width == 2);
    CHECK(decoded.data.frame_begin.damage_height == 2);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    source.data.frame_begin.generation = 8;
    source.data.frame_begin.serial = 101;
    source.data.frame_begin.frame_bytes = 40;
    source.data.frame_begin.damage_count = 2;
    source.data.frame_begin.damage_rects[0].x = 3;
    source.data.frame_begin.damage_rects[0].y = 4;
    source.data.frame_begin.damage_rects[0].width = 2;
    source.data.frame_begin.damage_rects[0].height = 2;
    source.data.frame_begin.damage_rects[1].x = 20;
    source.data.frame_begin.damage_rects[1].y = 10;
    source.data.frame_begin.damage_rects[1].width = 3;
    source.data.frame_begin.damage_rects[1].height = 2;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.frame_begin.damage_count == 2);
    CHECK(decoded.data.frame_begin.damage_rects[0].x == 3);
    CHECK(decoded.data.frame_begin.damage_rects[0].height == 2);
    CHECK(decoded.data.frame_begin.damage_rects[1].x == 20);
    CHECK(decoded.data.frame_begin.damage_rects[1].width == 3);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    source.data.frame_data.generation = 8;
    source.data.frame_data.serial = 99;
    source.data.frame_data.offset = 123;
    source.data.frame_data.size = sizeof(data);
    source.data.frame_data.bytes = data;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.frame_data.offset == 123);
    CHECK(decoded.data.frame_data.size == sizeof(data));
    CHECK(memcmp(decoded.data.frame_data.bytes, data, sizeof(data)) == 0);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_FRAME_COMMIT;
    source.data.frame_reference.generation = 8;
    source.data.frame_reference.serial = 99;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.type == NB_PRIVSEP_MESSAGE_FRAME_COMMIT);

    source.type = NB_PRIVSEP_MESSAGE_FRAME_ABORT;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.type == NB_PRIVSEP_MESSAGE_FRAME_ABORT);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_PONG;
    source.data.token = UINT64_C(0x1020304050607080);
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.token == UINT64_C(0x1020304050607080));

    source.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST;
    source.data.token = 55;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence);
    CHECK(decoded.data.token == 55);
    CHECK(nb_privsep_parser_at_message_boundary(&parser));
    free(wire);
}

static void test_helper_round_trips(void)
{
    struct nb_privsep_parser parser;
    struct nb_privsep_message source = {0};
    struct nb_privsep_message decoded;
    unsigned char *wire = allocate_wire();
    uint64_t sequence = 1;
    size_t size;
    size_t consumed;

    if (wire == NULL) {
        return;
    }
    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_HELPER);

    source.type = NB_PRIVSEP_MESSAGE_READY;
    source.data.ready.generation = 1;
    source.data.ready.output = test_output;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.ready.output.pixel_width == 1366);

    source.type = NB_PRIVSEP_MESSAGE_RESUME;
    source.data.ready.generation = 2;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.ready.generation == 2);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_SUSPEND;
    source.data.suspend.generation = 2;
    source.data.suspend.milliseconds = 123456;
    source.data.suspend.abandoned_frame_serial = 77;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.suspend.abandoned_frame_serial == 77);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_POINTER_MOTION;
    source.data.pointer_motion.generation = 2;
    source.data.pointer_motion.milliseconds = 123457;
    source.data.pointer_motion.x = INT32_MIN;
    source.data.pointer_motion.y = INT32_MAX;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.pointer_motion.x == INT32_MIN);
    CHECK(decoded.data.pointer_motion.y == INT32_MAX);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_POINTER_BUTTON;
    source.data.pointer_button.generation = 2;
    source.data.pointer_button.milliseconds = 123458;
    source.data.pointer_button.x = -1;
    source.data.pointer_button.y = 768;
    source.data.pointer_button.button = NB_PRIVSEP_POINTER_BUTTON_EXTRA;
    source.data.pointer_button.pressed = true;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.pointer_button.x == -1);
    CHECK(decoded.data.pointer_button.button ==
          NB_PRIVSEP_POINTER_BUTTON_EXTRA);
    CHECK(decoded.data.pointer_button.pressed);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_KEY;
    source.data.key.generation = 2;
    source.data.key.milliseconds = 123459;
    memcpy(source.data.key.xkb_key_name, "RTRN", 5);
    source.data.key.pressed = true;
    source.data.key.repeat = true;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(strcmp(decoded.data.key.xkb_key_name, "RTRN") == 0);
    CHECK(decoded.data.key.repeat);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_FRAME_COMPLETE;
    source.data.frame_complete.generation = 2;
    source.data.frame_complete.serial = 90;
    source.data.frame_complete.milliseconds = 123460;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.frame_complete.serial == 90);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_PING;
    source.data.token = 700;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);
    CHECK(decoded.data.token == 700);

    source.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED;
    source.data.token = 701;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence++);

    memset(&source, 0, sizeof(source));
    source.type = NB_PRIVSEP_MESSAGE_FATAL;
    source.data.fatal.reason = NB_PRIVSEP_FATAL_DEVICE;
    source.data.fatal.system_error = 5;
    source.data.fatal.generation = 2;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, sequence, &source, wire);
    CHECK(nb_privsep_parser_feed(&parser, wire, size, &consumed, &decoded) ==
          NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == sequence);
    CHECK(decoded.data.fatal.reason == NB_PRIVSEP_FATAL_DEVICE);
    CHECK(decoded.data.fatal.system_error == 5);
    free(wire);
}

static void test_fragmentation_and_borrowing(void)
{
    struct nb_privsep_message source = {0};
    struct nb_privsep_message decoded;
    struct nb_privsep_parser parser;
    unsigned char data[257];
    unsigned char *wire = allocate_wire();
    size_t size;
    size_t split;
    size_t consumed;
    size_t offset;
    size_t amount;

    if (wire == NULL) {
        return;
    }
    for (offset = 0; offset < sizeof(data); ++offset) {
        data[offset] = (unsigned char)(offset ^ 0xa5U);
    }
    source.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    source.data.frame_data.generation = 1;
    source.data.frame_data.serial = 2;
    source.data.frame_data.offset = 3;
    source.data.frame_data.size = sizeof(data);
    source.data.frame_data.bytes = data;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, 1, &source, wire);

    for (split = 0; split <= size; ++split) {
        enum nb_privsep_parse_status status;

        nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_CORE);
        status = nb_privsep_parser_feed(&parser,
                                        wire,
                                        split,
                                        &consumed,
                                        &decoded);
        CHECK(consumed == split);
        if (split == size) {
            CHECK(status == NB_PRIVSEP_PARSE_MESSAGE);
        } else {
            CHECK(status == NB_PRIVSEP_PARSE_NEED_MORE);
            CHECK(!nb_privsep_parser_at_message_boundary(&parser) ||
                  split == 0);
            status = nb_privsep_parser_feed(&parser,
                                            wire + split,
                                            size - split,
                                            &consumed,
                                            &decoded);
            CHECK(consumed == size - split);
            CHECK(status == NB_PRIVSEP_PARSE_MESSAGE);
        }
        CHECK(decoded.data.frame_data.size == sizeof(data));
        CHECK(memcmp(decoded.data.frame_data.bytes,
                     data,
                     sizeof(data)) == 0);
    }

    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_CORE);
    offset = 0;
    amount = 1;
    while (offset < size) {
        enum nb_privsep_parse_status status;

        if (amount > size - offset) {
            amount = size - offset;
        }
        status = nb_privsep_parser_feed(&parser,
                                        wire + offset,
                                        amount,
                                        &consumed,
                                        &decoded);
        CHECK(consumed == amount);
        offset += amount;
        if (offset == size) {
            CHECK(status == NB_PRIVSEP_PARSE_MESSAGE);
        } else {
            CHECK(status == NB_PRIVSEP_PARSE_NEED_MORE);
        }
        amount = amount == 31 ? 1 : amount + 1;
    }
    CHECK(memcmp(decoded.data.frame_data.bytes, data, sizeof(data)) == 0);
    free(wire);
}

static void test_multiple_messages_and_sequence(void)
{
    struct nb_privsep_message ping = {0};
    struct nb_privsep_message decoded;
    struct nb_privsep_parser parser;
    unsigned char wire[128];
    size_t first_size = 0;
    size_t second_size = 0;
    size_t consumed;

    ping.type = NB_PRIVSEP_MESSAGE_PING;
    ping.data.token = 1;
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                    1,
                                    &ping,
                                    wire,
                                    sizeof(wire),
                                    &first_size));
    ping.data.token = 2;
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                    2,
                                    &ping,
                                    wire + first_size,
                                    sizeof(wire) - first_size,
                                    &second_size));
    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_HELPER);
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 first_size + second_size,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(consumed == first_size);
    CHECK(decoded.data.token == 1);
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire + consumed,
                                 second_size,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(consumed == second_size);
    CHECK(decoded.sequence == 2);
    CHECK(decoded.data.token == 2);

    ping.data.token = 3;
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                    2,
                                    &ping,
                                    wire,
                                    sizeof(wire),
                                    &first_size));
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 first_size,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_ERROR);
    CHECK(nb_privsep_parser_failed(&parser));
    CHECK(nb_privsep_parser_feed(&parser,
                                 NULL,
                                 0,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_ERROR);

    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_HELPER);
    parser.expected_sequence = UINT64_MAX;
    ping.data.token = 4;
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                    UINT64_MAX,
                                    &ping,
                                    wire,
                                    sizeof(wire),
                                    &first_size));
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 first_size,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(decoded.sequence == UINT64_MAX);
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                    1,
                                    &ping,
                                    wire,
                                    sizeof(wire),
                                    &first_size));
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 first_size,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_ERROR);
}

static bool parser_rejects(enum nb_privsep_endpoint sender,
                           const unsigned char *wire,
                           size_t size)
{
    struct nb_privsep_parser parser;
    struct nb_privsep_message message;
    size_t consumed;

    nb_privsep_parser_init(&parser, sender);
    return nb_privsep_parser_feed(&parser,
                                  wire,
                                  size,
                                  &consumed,
                                  &message) == NB_PRIVSEP_PARSE_ERROR &&
           nb_privsep_parser_failed(&parser);
}

static void test_header_rejections(void)
{
    struct nb_privsep_message hello = {0};
    unsigned char valid[64];
    unsigned char damaged[64];
    size_t size = 0;

    hello.type = NB_PRIVSEP_MESSAGE_CORE_HELLO;
    hello.data.credentials.process_id = 100;
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_CORE,
                                    1,
                                    &hello,
                                    valid,
                                    sizeof(valid),
                                    &size));

    memcpy(damaged, valid, size);
    damaged[0] ^= 1;
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, damaged, size));
    memcpy(damaged, valid, size);
    write_u16(damaged + 4, NB_PRIVSEP_PROTOCOL_VERSION + 1);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, damaged, size));
    memcpy(damaged, valid, size);
    write_u16(damaged + 6, NB_PRIVSEP_MESSAGE_READY);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, damaged, size));
    memcpy(damaged, valid, size);
    write_u16(damaged + 6, 0x7777);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, damaged, size));
    memcpy(damaged, valid, size);
    write_u32(damaged + 8, 31);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, damaged, size));
    memcpy(damaged, valid, size);
    write_u32(damaged + 8, NB_PRIVSEP_MAX_PAYLOAD_SIZE + 1);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE,
                         damaged,
                         NB_PRIVSEP_HEADER_SIZE));
    memcpy(damaged, valid, size);
    write_u32(damaged + 12, 1);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, damaged, size));
    memcpy(damaged, valid, size);
    write_u64(damaged + 16, 0);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, damaged, size));
    memcpy(damaged, valid, size);
    write_u64(damaged + 16, 2);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, damaged, size));
}

static void test_payload_rejections(void)
{
    struct nb_privsep_message message = {0};
    unsigned char *wire = allocate_wire();
    unsigned char byte = 7;
    size_t size;

    if (wire == NULL) {
        return;
    }
    message.type = NB_PRIVSEP_MESSAGE_CORE_HELLO;
    message.data.credentials.process_id = 1;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, 1, &message, wire);
    wire[NB_PRIVSEP_HEADER_SIZE + 31] = 1;
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, wire, size));
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, 1, &message, wire);
    memset(wire + NB_PRIVSEP_HEADER_SIZE, 0, 4);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, wire, size));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 1;
    message.data.frame_begin.serial = 2;
    message.data.frame_begin.frame_bytes = 4;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, 1, &message, wire);
    wire[NB_PRIVSEP_HEADER_SIZE + 23] = 1;
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, wire, size));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    message.data.frame_data.generation = 1;
    message.data.frame_data.serial = 2;
    message.data.frame_data.offset = 0;
    message.data.frame_data.size = 1;
    message.data.frame_data.bytes = &byte;
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, 1, &message, wire);
    write_u32(wire + NB_PRIVSEP_HEADER_SIZE + 20, 2);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, wire, size));
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, 1, &message, wire);
    write_u64(wire + NB_PRIVSEP_HEADER_SIZE, 0);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, wire, size));
    size = encode(NB_PRIVSEP_ENDPOINT_CORE, 1, &message, wire);
    write_u32(wire + NB_PRIVSEP_HEADER_SIZE + 16,
              NB_PRIVSEP_MAX_FRAME_BYTES);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_CORE, wire, size));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_READY;
    message.data.ready.generation = 1;
    message.data.ready.output = test_output;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, 1, &message, wire);
    write_u32(wire + NB_PRIVSEP_HEADER_SIZE + 8 + 20,
              test_output.stride + 1);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_HELPER, wire, size));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_POINTER_BUTTON;
    message.data.pointer_button.generation = 1;
    message.data.pointer_button.button = NB_PRIVSEP_POINTER_BUTTON_LEFT;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, 1, &message, wire);
    wire[NB_PRIVSEP_HEADER_SIZE + 28] = 2;
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_HELPER, wire, size));
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, 1, &message, wire);
    wire[NB_PRIVSEP_HEADER_SIZE + 31] = 1;
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_HELPER, wire, size));
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, 1, &message, wire);
    write_u32(wire + NB_PRIVSEP_HEADER_SIZE + 24,
              NB_PRIVSEP_POINTER_BUTTON_COUNT);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_HELPER, wire, size));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_KEY;
    message.data.key.generation = 1;
    memcpy(message.data.key.xkb_key_name, "ESC\0", 4);
    message.data.key.pressed = true;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, 1, &message, wire);
    wire[NB_PRIVSEP_HEADER_SIZE + 21] = 2;
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_HELPER, wire, size));
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, 1, &message, wire);
    wire[NB_PRIVSEP_HEADER_SIZE + 23] = 1;
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_HELPER, wire, size));
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, 1, &message, wire);
    memset(wire + NB_PRIVSEP_HEADER_SIZE + 16, 'A', 5);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_HELPER, wire, size));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FATAL;
    message.data.fatal.reason = NB_PRIVSEP_FATAL_INTERNAL;
    size = encode(NB_PRIVSEP_ENDPOINT_HELPER, 1, &message, wire);
    write_u32(wire + NB_PRIVSEP_HEADER_SIZE, 99);
    CHECK(parser_rejects(NB_PRIVSEP_ENDPOINT_HELPER, wire, size));
    free(wire);
}

static void test_maximum_frame_data(void)
{
    struct nb_privsep_message source = {0};
    struct nb_privsep_message decoded;
    struct nb_privsep_parser parser;
    unsigned char *data = malloc(NB_PRIVSEP_FRAME_DATA_CAPACITY + 1U);
    unsigned char *wire = allocate_wire();
    size_t size = 0;
    size_t consumed = 0;
    size_t index;

    CHECK(data != NULL);
    if (data == NULL || wire == NULL) {
        free(data);
        free(wire);
        return;
    }
    for (index = 0; index < NB_PRIVSEP_FRAME_DATA_CAPACITY; ++index) {
        data[index] = (unsigned char)(index * 13U);
    }
    source.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    source.data.frame_data.generation = 1;
    source.data.frame_data.serial = 1;
    source.data.frame_data.offset = 0;
    source.data.frame_data.size = NB_PRIVSEP_FRAME_DATA_CAPACITY;
    source.data.frame_data.bytes = data;
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_CORE,
                                    1,
                                    &source,
                                    wire,
                                    NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE,
                                    &size));
    CHECK(size == NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE);
    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_CORE);
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 size,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(consumed == size);
    CHECK(nb_privsep_parser_at_message_boundary(&parser));
    CHECK(decoded.data.frame_data.size == NB_PRIVSEP_FRAME_DATA_CAPACITY);
    CHECK(memcmp(decoded.data.frame_data.bytes,
                 data,
                 NB_PRIVSEP_FRAME_DATA_CAPACITY) == 0);

    source.data.frame_data.size = NB_PRIVSEP_FRAME_DATA_CAPACITY + 1U;
    size = 123;
    CHECK(!nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_CORE,
                                     1,
                                     &source,
                                     wire,
                                     NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE,
                                     &size));
    CHECK(size == 0);
    free(wire);
    free(data);
}

static void test_encode_rejections_and_parser_arguments(void)
{
    struct nb_privsep_message ping = {0};
    struct nb_privsep_message decoded;
    struct nb_privsep_parser parser;
    unsigned char wire[64];
    size_t size = 99;
    size_t consumed = 99;

    ping.type = NB_PRIVSEP_MESSAGE_PING;
    ping.data.token = 1;
    CHECK(!nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                     0,
                                     &ping,
                                     wire,
                                     sizeof(wire),
                                     &size));
    CHECK(size == 0);
    CHECK(!nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_CORE,
                                     1,
                                     &ping,
                                     wire,
                                     sizeof(wire),
                                     &size));
    CHECK(!nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                     1,
                                     &ping,
                                     wire,
                                     NB_PRIVSEP_HEADER_SIZE,
                                     &size));
    CHECK(!nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                     1,
                                     NULL,
                                     wire,
                                     sizeof(wire),
                                     &size));
    CHECK(!nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                     1,
                                     &ping,
                                     NULL,
                                     sizeof(wire),
                                     &size));
    CHECK(!nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                     1,
                                     &ping,
                                     wire,
                                     sizeof(wire),
                                     NULL));

    ping.data.token = 0;
    CHECK(!nb_privsep_message_is_valid(NB_PRIVSEP_ENDPOINT_HELPER, &ping));
    ping.data.token = 1;
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                    1,
                                    &ping,
                                    wire,
                                    sizeof(wire),
                                    &size));

    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_HELPER);
    CHECK(nb_privsep_parser_feed(NULL,
                                 wire,
                                 size,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_ERROR);
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 size,
                                 NULL,
                                 &decoded) == NB_PRIVSEP_PARSE_ERROR);
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 size,
                                 &consumed,
                                 NULL) == NB_PRIVSEP_PARSE_ERROR);
    CHECK(nb_privsep_parser_feed(&parser,
                                 NULL,
                                 1,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_ERROR);
    CHECK(!nb_privsep_parser_failed(&parser));
    CHECK(nb_privsep_parser_feed(&parser,
                                 NULL,
                                 0,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_NEED_MORE);
    CHECK(consumed == 0);
    CHECK(nb_privsep_parser_at_message_boundary(&parser));

    nb_privsep_parser_init(&parser, (enum nb_privsep_endpoint)99);
    CHECK(nb_privsep_parser_failed(&parser));
    CHECK(!nb_privsep_parser_at_message_boundary(&parser));
    CHECK(nb_privsep_parser_failed(NULL));
    CHECK(!nb_privsep_parser_at_message_boundary(NULL));
    nb_privsep_parser_init(NULL, NB_PRIVSEP_ENDPOINT_CORE);
}

static void test_partial_message_boundary(void)
{
    struct nb_privsep_message ping = {0};
    struct nb_privsep_message decoded;
    struct nb_privsep_parser parser;
    unsigned char wire[64];
    size_t size = 0;
    size_t consumed;

    ping.type = NB_PRIVSEP_MESSAGE_PING;
    ping.data.token = 9;
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                    1,
                                    &ping,
                                    wire,
                                    sizeof(wire),
                                    &size));
    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_HELPER);
    CHECK(nb_privsep_parser_at_message_boundary(&parser));
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 1,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_NEED_MORE);
    CHECK(!nb_privsep_parser_at_message_boundary(&parser));

    nb_privsep_parser_init(&parser, NB_PRIVSEP_ENDPOINT_HELPER);
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire,
                                 NB_PRIVSEP_HEADER_SIZE,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_NEED_MORE);
    CHECK(!nb_privsep_parser_at_message_boundary(&parser));
    CHECK(nb_privsep_parser_feed(&parser,
                                 wire + NB_PRIVSEP_HEADER_SIZE,
                                 size - NB_PRIVSEP_HEADER_SIZE,
                                 &consumed,
                                 &decoded) == NB_PRIVSEP_PARSE_MESSAGE);
    CHECK(nb_privsep_parser_at_message_boundary(&parser));
}

int main(void)
{
    test_output_validation();
    test_core_round_trips();
    test_helper_round_trips();
    test_fragmentation_and_borrowing();
    test_multiple_messages_and_sequence();
    test_header_rejections();
    test_payload_rejections();
    test_maximum_frame_data();
    test_encode_rejections_and_parser_arguments();
    test_partial_message_boundary();

    if (failures != 0) {
        fprintf(stderr, "privsep protocol tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("privsep protocol tests: ok");
    return 0;
}
