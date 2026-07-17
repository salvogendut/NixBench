#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "privsep_helper.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static const struct nb_privsep_credentials credentials = {
    1234, 1000, 1000, 1000, 100, 100, 100
};

static const struct nb_privsep_output output = {
    4, 2, 4, 2, 60000, 16, 32, NB_PRIVSEP_PIXEL_FORMAT_XRGB8888
};

struct presentation_capture {
    unsigned char pixels[64];
    size_t bytes;
    uint64_t generation;
    uint64_t serial;
    int width;
    int height;
    size_t stride;
    int damage_x;
    int damage_y;
    int damage_width;
    int damage_height;
    size_t damage_count;
    unsigned int calls;
    bool succeed;
};

struct test_session {
    struct nb_privsep_helper *helper;
    struct nb_privsep_parser outbound_parser;
    struct presentation_capture presentation;
    uint64_t next_core_sequence;
};

static bool capture_present(void *data,
                            uint64_t generation,
                            const struct nb_host_frame *frame)
{
    struct presentation_capture *capture = data;
    const size_t bytes = frame->stride * (size_t)frame->height;

    ++capture->calls;
    capture->generation = generation;
    capture->serial = frame->serial;
    capture->width = frame->width;
    capture->height = frame->height;
    capture->stride = frame->stride;
    capture->damage_x = frame->damage_x;
    capture->damage_y = frame->damage_y;
    capture->damage_width = frame->damage_width;
    capture->damage_height = frame->damage_height;
    capture->damage_count = frame->damage_count;
    capture->bytes = bytes;
    if (bytes <= sizeof(capture->pixels)) {
        memcpy(capture->pixels, frame->pixels, bytes);
    }
    return capture->succeed;
}

static bool session_create(struct test_session *session)
{
    struct nb_privsep_helper_options options;

    memset(session, 0, sizeof(*session));
    nb_privsep_helper_options_init(&options);
    options.expected_credentials = credentials;
    options.output = output;
    options.present = capture_present;
    options.present_data = &session->presentation;
    session->presentation.succeed = true;
    session->helper = nb_privsep_helper_create(&options);
    nb_privsep_parser_init(&session->outbound_parser,
                           NB_PRIVSEP_ENDPOINT_HELPER);
    session->next_core_sequence = 1;
    CHECK(session->helper != NULL);
    return session->helper != NULL;
}

static void session_destroy(struct test_session *session)
{
    nb_privsep_helper_destroy(session->helper);
    session->helper = NULL;
}

static bool send_core(struct test_session *session,
                      const struct nb_privsep_message *message,
                      size_t fragment)
{
    unsigned char *wire = malloc(NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE);
    size_t wire_size = 0;
    size_t offset = 0;
    bool success = true;

    CHECK(wire != NULL);
    if (wire == NULL) {
        return false;
    }
    CHECK(nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_CORE,
                                    session->next_core_sequence,
                                    message,
                                    wire,
                                    NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE,
                                    &wire_size));
    ++session->next_core_sequence;
    while (offset < wire_size) {
        size_t amount = fragment == 0 ? wire_size - offset : fragment;
        size_t consumed = 0;

        if (amount > wire_size - offset) {
            amount = wire_size - offset;
        }
        success = nb_privsep_helper_feed(session->helper,
                                         wire + offset,
                                         amount,
                                         &consumed);
        CHECK(consumed == amount);
        offset += consumed;
        if (!success) {
            break;
        }
    }
    free(wire);
    return success;
}

static bool send_hello(struct test_session *session, size_t fragment)
{
    struct nb_privsep_message hello = {0};

    hello.type = NB_PRIVSEP_MESSAGE_CORE_HELLO;
    hello.data.credentials = credentials;
    return send_core(session, &hello, fragment);
}

static bool pop_outbound(struct test_session *session,
                         struct nb_privsep_message *message)
{
    for (;;) {
        const unsigned char *bytes;
        size_t size;
        size_t consumed = 0;
        enum nb_privsep_parse_status status;

        CHECK(nb_privsep_helper_peek_outbound(session->helper,
                                              &bytes,
                                              &size));
        if (size == 0) {
            return false;
        }
        status = nb_privsep_parser_feed(&session->outbound_parser,
                                        bytes,
                                        size,
                                        &consumed,
                                        message);
        CHECK(status != NB_PRIVSEP_PARSE_ERROR);
        CHECK(consumed != 0);
        CHECK(nb_privsep_helper_consume_outbound(session->helper,
                                                 consumed));
        if (status == NB_PRIVSEP_PARSE_MESSAGE) {
            return true;
        }
    }
}

static void test_create_and_handshake(void)
{
    struct nb_privsep_helper_options options;
    struct test_session session;
    struct nb_privsep_message message;

    nb_privsep_helper_options_init(&options);
    CHECK(nb_privsep_helper_create(NULL) == NULL);
    CHECK(nb_privsep_helper_create(&options) == NULL);
    options.expected_credentials = credentials;
    options.output = output;
    options.present = capture_present;
    options.expected_credentials.process_id = 0;
    CHECK(nb_privsep_helper_create(&options) == NULL);

    CHECK(session_create(&session));
    CHECK(!nb_privsep_helper_is_ready(session.helper));
    CHECK(nb_privsep_helper_generation(session.helper) == 1);
    CHECK(send_hello(&session, 1));
    CHECK(nb_privsep_helper_is_ready(session.helper));
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_READY);
    CHECK(message.data.ready.generation == 1);
    CHECK(message.data.ready.output.frame_bytes == 32);
    CHECK(nb_privsep_helper_outbound_size(session.helper) == 0);
    session_destroy(&session);
}

static void test_bad_credentials_and_order(void)
{
    struct test_session session;
    struct nb_privsep_message message = {0};
    enum nb_privsep_helper_error error;
    uint32_t system_error;
    char text[NB_PRIVSEP_HELPER_ERROR_CAPACITY];

    CHECK(session_create(&session));
    message.type = NB_PRIVSEP_MESSAGE_CORE_HELLO;
    message.data.credentials = credentials;
    ++message.data.credentials.effective_user_id;
    CHECK(!send_core(&session, &message, 3));
    CHECK(nb_privsep_helper_failed(session.helper));
    CHECK(nb_privsep_helper_get_last_error(session.helper,
                                           &error,
                                           &system_error,
                                           text,
                                           sizeof(text)));
    CHECK(error == NB_PRIVSEP_HELPER_ERROR_CREDENTIALS);
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_FATAL);
    session_destroy(&session);

    CHECK(session_create(&session));
    message.type = NB_PRIVSEP_MESSAGE_PONG;
    message.data.token = 1;
    CHECK(!send_core(&session, &message, 0));
    CHECK(nb_privsep_helper_failed(session.helper));
    session_destroy(&session);
}

static void test_frame_and_completion(void)
{
    struct test_session session;
    struct nb_privsep_message message = {0};
    struct nb_privsep_message outbound;
    unsigned char pixels[32];
    size_t index;

    for (index = 0; index < sizeof(pixels); ++index) {
        pixels[index] = (unsigned char)(index * 9U);
    }
    CHECK(session_create(&session));
    CHECK(send_hello(&session, 2));
    CHECK(pop_outbound(&session, &outbound));

    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 1;
    message.data.frame_begin.serial = 10;
    message.data.frame_begin.frame_bytes = sizeof(pixels);
    CHECK(send_core(&session, &message, 1));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    message.data.frame_data.generation = 1;
    message.data.frame_data.serial = 10;
    message.data.frame_data.offset = 0;
    message.data.frame_data.size = 13;
    message.data.frame_data.bytes = pixels;
    CHECK(send_core(&session, &message, 5));
    message.data.frame_data.offset = 13;
    message.data.frame_data.size = sizeof(pixels) - 13;
    message.data.frame_data.bytes = pixels + 13;
    CHECK(send_core(&session, &message, 7));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMMIT;
    message.data.frame_reference.generation = 1;
    message.data.frame_reference.serial = 10;
    CHECK(send_core(&session, &message, 1));
    CHECK(session.presentation.calls == 1);
    CHECK(session.presentation.generation == 1);
    CHECK(session.presentation.serial == 10);
    CHECK(session.presentation.width == 4);
    CHECK(session.presentation.height == 2);
    CHECK(session.presentation.stride == 16);
    CHECK(session.presentation.bytes == sizeof(pixels));
    CHECK(memcmp(session.presentation.pixels,
                 pixels,
                 sizeof(pixels)) == 0);
    CHECK(nb_privsep_helper_presentation_pending(session.helper));

    CHECK(nb_privsep_helper_complete_frame(session.helper, 1, 10, 500));
    CHECK(!nb_privsep_helper_presentation_pending(session.helper));
    CHECK(pop_outbound(&session, &outbound));
    CHECK(outbound.type == NB_PRIVSEP_MESSAGE_FRAME_COMPLETE);
    CHECK(outbound.data.frame_complete.serial == 10);
    CHECK(outbound.data.frame_complete.milliseconds == 500);
    session_destroy(&session);
}

static void test_frame_order_rejections(void)
{
    struct test_session session;
    struct nb_privsep_message message = {0};
    struct nb_privsep_message outbound;
    unsigned char byte = 1;

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &outbound));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 1;
    message.data.frame_begin.serial = 1;
    message.data.frame_begin.frame_bytes = 31;
    CHECK(!send_core(&session, &message, 0));
    CHECK(nb_privsep_helper_failed(session.helper));
    session_destroy(&session);

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &outbound));
    message.data.frame_begin.frame_bytes = 32;
    CHECK(send_core(&session, &message, 0));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    message.data.frame_data.generation = 1;
    message.data.frame_data.serial = 1;
    message.data.frame_data.offset = 1;
    message.data.frame_data.size = 1;
    message.data.frame_data.bytes = &byte;
    CHECK(!send_core(&session, &message, 0));
    CHECK(nb_privsep_helper_failed(session.helper));
    session_destroy(&session);
}

static void test_partial_frame_updates_staging(void)
{
    struct test_session session;
    struct nb_privsep_message message = {0};
    struct nb_privsep_message outbound;
    unsigned char full[32];
    unsigned char damage[16];
    unsigned char expected[32];
    size_t index;

    for (index = 0; index < sizeof(full); ++index) {
        full[index] = (unsigned char)index;
    }
    for (index = 0; index < sizeof(damage); ++index) {
        damage[index] = (unsigned char)(200U + index);
    }
    memcpy(expected, full, sizeof(expected));
    memcpy(expected + 4, damage, 8);
    memcpy(expected + 20, damage + 8, 8);

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &outbound));

    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 1;
    message.data.frame_begin.serial = 1;
    message.data.frame_begin.frame_bytes = sizeof(full);
    CHECK(send_core(&session, &message, 0));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    message.data.frame_data.generation = 1;
    message.data.frame_data.serial = 1;
    message.data.frame_data.size = sizeof(full);
    message.data.frame_data.bytes = full;
    CHECK(send_core(&session, &message, 0));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMMIT;
    message.data.frame_reference.generation = 1;
    message.data.frame_reference.serial = 1;
    CHECK(send_core(&session, &message, 0));
    CHECK(nb_privsep_helper_complete_frame(session.helper, 1, 1, 1));
    CHECK(pop_outbound(&session, &outbound));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 1;
    message.data.frame_begin.serial = 2;
    message.data.frame_begin.frame_bytes = sizeof(damage);
    message.data.frame_begin.damage_count = 2;
    message.data.frame_begin.damage_rects[0].x = 1;
    message.data.frame_begin.damage_rects[0].y = 0;
    message.data.frame_begin.damage_rects[0].width = 2;
    message.data.frame_begin.damage_rects[0].height = 1;
    message.data.frame_begin.damage_rects[1].x = 1;
    message.data.frame_begin.damage_rects[1].y = 1;
    message.data.frame_begin.damage_rects[1].width = 2;
    message.data.frame_begin.damage_rects[1].height = 1;
    CHECK(send_core(&session, &message, 3));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    message.data.frame_data.generation = 1;
    message.data.frame_data.serial = 2;
    message.data.frame_data.size = sizeof(damage);
    message.data.frame_data.bytes = damage;
    CHECK(send_core(&session, &message, 5));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMMIT;
    message.data.frame_reference.generation = 1;
    message.data.frame_reference.serial = 2;
    CHECK(send_core(&session, &message, 0));
    CHECK(session.presentation.calls == 2);
    CHECK(session.presentation.damage_x == 1);
    CHECK(session.presentation.damage_y == 0);
    CHECK(session.presentation.damage_width == 2);
    CHECK(session.presentation.damage_height == 1);
    CHECK(session.presentation.damage_count == 2);
    CHECK(memcmp(session.presentation.pixels,
                 expected,
                 sizeof(expected)) == 0);
    session_destroy(&session);
}

static void test_suspend_stale_drain_and_resume(void)
{
    struct test_session session;
    struct nb_privsep_message message = {0};
    struct nb_privsep_message outbound;
    unsigned char pixels[32];

    memset(pixels, 0x5a, sizeof(pixels));
    CHECK(session_create(&session));
    CHECK(send_hello(&session, 1));
    CHECK(pop_outbound(&session, &outbound));

    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 1;
    message.data.frame_begin.serial = 4;
    message.data.frame_begin.frame_bytes = 32;
    CHECK(send_core(&session, &message, 0));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    message.data.frame_data.generation = 1;
    message.data.frame_data.serial = 4;
    message.data.frame_data.offset = 0;
    message.data.frame_data.size = 8;
    message.data.frame_data.bytes = pixels;
    CHECK(send_core(&session, &message, 0));

    CHECK(nb_privsep_helper_suspend(session.helper, 1000));
    CHECK(nb_privsep_helper_is_suspended(session.helper));
    CHECK(nb_privsep_helper_generation(session.helper) == 2);
    CHECK(pop_outbound(&session, &outbound));
    CHECK(outbound.type == NB_PRIVSEP_MESSAGE_SUSPEND);
    CHECK(outbound.data.suspend.generation == 2);
    CHECK(outbound.data.suspend.abandoned_frame_serial == 4);

    message.data.frame_data.offset = 8;
    message.data.frame_data.size = 24;
    message.data.frame_data.bytes = pixels + 8;
    CHECK(send_core(&session, &message, 1));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMMIT;
    message.data.frame_reference.generation = 1;
    message.data.frame_reference.serial = 4;
    CHECK(send_core(&session, &message, 0));
    CHECK(session.presentation.calls == 0);

    CHECK(nb_privsep_helper_resume(session.helper, &output));
    CHECK(!nb_privsep_helper_is_suspended(session.helper));
    CHECK(pop_outbound(&session, &outbound));
    CHECK(outbound.type == NB_PRIVSEP_MESSAGE_RESUME);
    CHECK(outbound.data.ready.generation == 2);

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 2;
    message.data.frame_begin.serial = 5;
    message.data.frame_begin.frame_bytes = 32;
    CHECK(send_core(&session, &message, 0));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    message.data.frame_data.generation = 2;
    message.data.frame_data.serial = 5;
    message.data.frame_data.offset = 0;
    message.data.frame_data.size = 32;
    message.data.frame_data.bytes = pixels;
    CHECK(send_core(&session, &message, 0));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMMIT;
    message.data.frame_reference.generation = 2;
    message.data.frame_reference.serial = 5;
    CHECK(send_core(&session, &message, 0));
    CHECK(session.presentation.calls == 1);
    session_destroy(&session);
}

static void test_input_heartbeat_and_shutdown(void)
{
    struct test_session session;
    struct nb_host_event event = {0};
    struct nb_privsep_message message;
    uint64_t token;

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &message));

    event.type = NB_HOST_EVENT_POINTER_MOTION;
    event.milliseconds = 20;
    event.data.pointer_motion.x = 3;
    event.data.pointer_motion.y = 1;
    CHECK(nb_privsep_helper_send_input(session.helper, &event));
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_POINTER_MOTION);
    CHECK(message.data.pointer_motion.x == 3);

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_KEY;
    event.milliseconds = 21;
    memcpy(event.data.key.xkb_key_name, "ESC\0", 4);
    event.data.key.pressed = true;
    CHECK(nb_privsep_helper_send_input(session.helper, &event));
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_KEY);

    CHECK(nb_privsep_helper_send_ping(session.helper, 90));
    CHECK(nb_privsep_helper_ping_outstanding(session.helper));
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_PING);
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_PONG;
    message.data.token = 90;
    CHECK(send_core(&session, &message, 1));
    CHECK(!nb_privsep_helper_ping_outstanding(session.helper));
    CHECK(!nb_privsep_helper_send_ping(session.helper, 91));
    CHECK(!nb_privsep_helper_failed(session.helper));
    CHECK(nb_privsep_helper_take_pong(session.helper, &token));
    CHECK(token == 90);
    CHECK(!nb_privsep_helper_take_pong(session.helper, &token));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST;
    message.data.token = 91;
    CHECK(send_core(&session, &message, 0));
    CHECK(nb_privsep_helper_shutdown_requested(session.helper, &token));
    CHECK(token == 91);
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED);
    CHECK(message.data.token == 91);
    session_destroy(&session);
}

static void test_suspend_prioritizes_control(void)
{
    struct test_session session;
    struct nb_host_event event = {0};
    struct nb_privsep_message message;
    const unsigned char *bytes;
    size_t size;
    size_t consumed;
    enum nb_privsep_parse_status status;

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &message));
    event.type = NB_HOST_EVENT_POINTER_MOTION;
    event.data.pointer_motion.x = 1;
    event.data.pointer_motion.y = 1;
    CHECK(nb_privsep_helper_send_input(session.helper, &event));
    ++event.milliseconds;
    CHECK(nb_privsep_helper_send_input(session.helper, &event));
    CHECK(nb_privsep_helper_send_ping(session.helper, 77));
    ++event.milliseconds;
    CHECK(nb_privsep_helper_send_input(session.helper, &event));
    CHECK(nb_privsep_helper_suspend(session.helper, 100));
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_PING);
    CHECK(message.data.token == 77);
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_SUSPEND);
    CHECK(nb_privsep_helper_outbound_size(session.helper) == 0);
    session_destroy(&session);

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &message));
    CHECK(nb_privsep_helper_send_input(session.helper, &event));
    ++event.milliseconds;
    CHECK(nb_privsep_helper_send_input(session.helper, &event));
    CHECK(nb_privsep_helper_peek_outbound(session.helper, &bytes, &size));
    CHECK(size > 7);
    consumed = 0;
    status = nb_privsep_parser_feed(&session.outbound_parser,
                                    bytes,
                                    7,
                                    &consumed,
                                    &message);
    CHECK(status == NB_PRIVSEP_PARSE_NEED_MORE);
    CHECK(consumed == 7);
    CHECK(nb_privsep_helper_consume_outbound(session.helper, consumed));
    CHECK(nb_privsep_helper_suspend(session.helper, 101));
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_POINTER_MOTION);
    CHECK(pop_outbound(&session, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_SUSPEND);
    CHECK(nb_privsep_helper_outbound_size(session.helper) == 0);
    session_destroy(&session);
}

static void test_fresh_stale_frame_is_rejected(void)
{
    struct test_session session;
    struct nb_privsep_message message = {0};
    struct nb_privsep_message outbound;

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &outbound));
    CHECK(nb_privsep_helper_suspend(session.helper, 1));
    CHECK(pop_outbound(&session, &outbound));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 1;
    message.data.frame_begin.serial = 1;
    message.data.frame_begin.frame_bytes = output.frame_bytes;
    CHECK(!send_core(&session, &message, 0));
    CHECK(nb_privsep_helper_failed(session.helper));
    session_destroy(&session);
}

static void test_queue_saturation(void)
{
    struct test_session session;
    struct nb_host_event event = {0};
    struct nb_privsep_message message;
    size_t count = 0;

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &message));
    event.type = NB_HOST_EVENT_POINTER_MOTION;
    while (nb_privsep_helper_send_input(session.helper, &event)) {
        ++count;
        ++event.milliseconds;
    }
    CHECK(count != 0);
    CHECK(nb_privsep_helper_failed(session.helper));
    CHECK(nb_privsep_helper_outbound_size(session.helper) <=
          NB_PRIVSEP_HELPER_OUTBOUND_CAPACITY);
    session_destroy(&session);
}

static void test_suspend_before_hello_and_late_completion(void)
{
    struct test_session session;
    struct nb_privsep_message message = {0};
    struct nb_privsep_message outbound;
    unsigned char pixels[32] = {0};

    CHECK(session_create(&session));
    CHECK(nb_privsep_helper_suspend(session.helper, 1));
    CHECK(nb_privsep_helper_outbound_size(session.helper) == 0);
    CHECK(send_hello(&session, 1));
    CHECK(!nb_privsep_helper_is_ready(session.helper));
    CHECK(nb_privsep_helper_resume(session.helper, &output));
    CHECK(pop_outbound(&session, &outbound));
    CHECK(outbound.type == NB_PRIVSEP_MESSAGE_READY);
    CHECK(outbound.data.ready.generation == 2);
    session_destroy(&session);

    CHECK(session_create(&session));
    CHECK(send_hello(&session, 0));
    CHECK(pop_outbound(&session, &outbound));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_BEGIN;
    message.data.frame_begin.generation = 1;
    message.data.frame_begin.serial = 1;
    message.data.frame_begin.frame_bytes = 32;
    CHECK(send_core(&session, &message, 0));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_DATA;
    message.data.frame_data.generation = 1;
    message.data.frame_data.serial = 1;
    message.data.frame_data.size = 32;
    message.data.frame_data.bytes = pixels;
    CHECK(send_core(&session, &message, 0));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMMIT;
    message.data.frame_reference.generation = 1;
    message.data.frame_reference.serial = 1;
    CHECK(send_core(&session, &message, 0));
    CHECK(nb_privsep_helper_suspend(session.helper, 2));
    CHECK(nb_privsep_helper_complete_frame(session.helper, 1, 1, 3));
    session_destroy(&session);
}

int main(void)
{
    test_create_and_handshake();
    test_bad_credentials_and_order();
    test_frame_and_completion();
    test_frame_order_rejections();
    test_partial_frame_updates_staging();
    test_suspend_stale_drain_and_resume();
    test_input_heartbeat_and_shutdown();
    test_suspend_prioritizes_control();
    test_fresh_stale_frame_is_rejected();
    test_queue_saturation();
    test_suspend_before_hello_and_late_completion();

    if (failures != 0) {
        fprintf(stderr, "privsep helper tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("privsep helper tests: ok");
    return 0;
}
