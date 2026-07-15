#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "host.h"
#include "host_privsep_client.h"
#include "privsep_protocol.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

struct wire_reader {
    struct nb_privsep_parser parser;
    unsigned char bytes[4096];
    size_t used;
    size_t offset;
};

static bool write_all(int descriptor, const void *bytes, size_t size)
{
    const unsigned char *next = bytes;

    while (size != 0) {
        const ssize_t written = write(descriptor, next, size);

        if (written > 0) {
            next += (size_t)written;
            size -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool send_helper_message(int descriptor,
                                uint64_t sequence,
                                const struct nb_privsep_message *message)
{
    unsigned char wire[NB_PRIVSEP_MAX_WIRE_MESSAGE_SIZE];
    size_t wire_size;

    return nb_privsep_message_encode(NB_PRIVSEP_ENDPOINT_HELPER,
                                     sequence,
                                     message,
                                     wire,
                                     sizeof(wire),
                                     &wire_size) &&
           write_all(descriptor, wire, wire_size);
}

static bool receive_core_message(int descriptor,
                                 struct wire_reader *reader,
                                 struct nb_privsep_message *message)
{
    for (;;) {
        enum nb_privsep_parse_status status;
        size_t consumed;

        if (reader->offset == reader->used) {
            ssize_t count;

            reader->offset = 0;
            reader->used = 0;
            do {
                count = read(descriptor,
                             reader->bytes,
                             sizeof(reader->bytes));
            } while (count < 0 && errno == EINTR);
            if (count <= 0) {
                return false;
            }
            reader->used = (size_t)count;
        }
        status = nb_privsep_parser_feed(
            &reader->parser,
            reader->bytes + reader->offset,
            reader->used - reader->offset,
            &consumed,
            message);
        reader->offset += consumed;
        if (status == NB_PRIVSEP_PARSE_MESSAGE) {
            return true;
        }
        if (status == NB_PRIVSEP_PARSE_ERROR || consumed == 0) {
            return false;
        }
    }
}

static struct nb_privsep_output tiny_output(void)
{
    const struct nb_privsep_output output = {
        .logical_width = 3,
        .logical_height = 2,
        .pixel_width = 3,
        .pixel_height = 2,
        .refresh_millihertz = 60000,
        .stride = 12,
        .frame_bytes = 24,
        .format = NB_PRIVSEP_PIXEL_FORMAT_XRGB8888
    };

    return output;
}

static void test_host_proxy(void)
{
    int sockets[2] = {-1, -1};
    struct wire_reader reader;
    struct nb_privsep_message message;
    struct nb_host *host = NULL;
    struct nb_host_event event;
    struct nb_host_output output;
    unsigned char source[32];
    unsigned char expected[24];
    struct nb_host_frame frame;
    enum nb_host_event_status status;
    uint64_t helper_sequence = 1;
    int flags;
    size_t received = 0;
    size_t index;

    memset(&reader, 0, sizeof(reader));
    nb_privsep_parser_init(&reader.parser, NB_PRIVSEP_ENDPOINT_CORE);
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    if (sockets[0] < 0 || sockets[1] < 0) {
        return;
    }

    host = nb_host_privsep_client_create(sockets[0]);
    CHECK(host != NULL);
    if (host == NULL) {
        (void)close(sockets[0]);
        (void)close(sockets[1]);
        return;
    }
    flags = fcntl(sockets[0], F_GETFD);
    CHECK(flags >= 0 && (flags & FD_CLOEXEC) != 0);

    status = nb_host_poll_event(host, &event);
    CHECK(status == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_CORE_HELLO);
    CHECK(message.data.credentials.process_id == (uint32_t)getpid());

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_READY;
    message.data.ready.generation = 1;
    message.data.ready.output = tiny_output();
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    status = nb_host_wait_event(host, 1000, &event);
    CHECK(status == NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_OUTPUT_CHANGED);
    CHECK(nb_host_privsep_client_is_ready(host));
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACTIVE);
    CHECK(nb_host_get_output(host, &output));
    CHECK(output.pixel_width == 3 && output.pixel_height == 2);

    for (index = 0; index < sizeof(source); ++index) {
        source[index] = index < 16 ? (unsigned char)(index + 1) :
                                    (unsigned char)(index + 17);
    }
    memset(source + 12, 0xee, 4);
    memset(source + 28, 0xee, 4);
    memcpy(expected, source, 12);
    memcpy(expected + 12, source + 16, 12);
    memset(&frame, 0, sizeof(frame));
    frame.pixels = source;
    frame.width = 3;
    frame.height = 2;
    frame.stride = 16;
    frame.format = NB_HOST_PIXEL_FORMAT_XRGB8888;
    frame.serial = 1;
    CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_OK);
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);

    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_FRAME_BEGIN);
    CHECK(message.data.frame_begin.generation == 1);
    CHECK(message.data.frame_begin.serial == 1);
    CHECK(message.data.frame_begin.frame_bytes == sizeof(expected));
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_FRAME_DATA);
    CHECK(message.data.frame_data.offset == 0);
    CHECK(message.data.frame_data.size == sizeof(expected));
    CHECK(memcmp(message.data.frame_data.bytes,
                 expected,
                 sizeof(expected)) == 0);
    received += message.data.frame_data.size;
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_FRAME_COMMIT);
    CHECK(received == sizeof(expected));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMPLETE;
    message.data.frame_complete.generation = 1;
    message.data.frame_complete.serial = 1;
    message.data.frame_complete.milliseconds = 25;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_FRAME_COMPLETE);
    CHECK(event.data.frame_complete.frame_serial == 1);

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_PING;
    message.data.token = UINT64_C(0x1122334455667788);
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_PONG);
    CHECK(message.data.token == UINT64_C(0x1122334455667788));

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SUSPEND;
    message.data.suspend.generation = 2;
    message.data.suspend.milliseconds = 30;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED);
    CHECK(nb_host_complete_console_release(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_SUSPENDED);

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_RESUME;
    message.data.ready.generation = 2;
    message.data.ready.output = tiny_output();
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED);
    CHECK(nb_host_complete_console_acquire(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACTIVE);

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_POINTER_MOTION;
    message.data.pointer_motion.generation = 2;
    message.data.pointer_motion.milliseconds = 35;
    message.data.pointer_motion.x = 2;
    message.data.pointer_motion.y = 1;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_POINTER_MOTION);
    CHECK(event.data.pointer_motion.x == 2);
    CHECK(event.data.pointer_motion.y == 1);

    CHECK(nb_host_privsep_client_request_shutdown(
        host, UINT64_C(0xaabbccddeeff0011)));
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST);
    CHECK(message.data.token == UINT64_C(0xaabbccddeeff0011));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED;
    message.data.token = UINT64_C(0xaabbccddeeff0011);
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_QUIT);

    nb_host_destroy(host);
    (void)close(sockets[1]);
}

int main(void)
{
    CHECK(nb_host_privsep_client_create(-1) == NULL);
    CHECK(nb_host_privsep_client_creation_error()[0] != '\0');
    test_host_proxy();

    if (failures != 0) {
        fprintf(stderr, "session-core tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("session-core tests: ok");
    return 0;
}
