#define _POSIX_C_SOURCE 200809L

#ifndef NIXBENCH_TEST_SESSION_CORE
#define NIXBENCH_TEST_SESSION_CORE 0
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#if NIXBENCH_TEST_SESSION_CORE
#include <sys/wait.h>
#endif
#include <unistd.h>

#include "host.h"
#include "host_privsep_client.h"
#include "privsep_protocol.h"
#if NIXBENCH_TEST_SESSION_CORE
#include "session_core.h"
#endif

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

#if NIXBENCH_TEST_SESSION_CORE
static struct nb_privsep_output runtime_output(void)
{
    const struct nb_privsep_output output = {
        .logical_width = 320,
        .logical_height = 200,
        .pixel_width = 320,
        .pixel_height = 200,
        .refresh_millihertz = 60000,
        .stride = 1280,
        .frame_bytes = 256000,
        .format = NB_PRIVSEP_PIXEL_FORMAT_XRGB8888
    };

    return output;
}
#endif

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

    /* A helper can reacquire the VT before the core has completed release. */
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SUSPEND;
    message.data.suspend.generation = 3;
    message.data.suspend.milliseconds = 40;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_RESUME;
    message.data.ready.generation = 3;
    message.data.ready.output = tiny_output();
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_POINTER_MOTION;
    message.data.pointer_motion.generation = 3;
    message.data.pointer_motion.milliseconds = 41;
    message.data.pointer_motion.x = 1;
    message.data.pointer_motion.y = 1;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_RELEASE_PENDING);
    CHECK(nb_host_complete_console_release(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACQUIRE_PENDING);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED);
    CHECK(nb_host_complete_console_acquire(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACTIVE);
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);

    /* The inverse race is deferred until the local acquire is complete. */
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SUSPEND;
    message.data.suspend.generation = 4;
    message.data.suspend.milliseconds = 50;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED);
    CHECK(nb_host_complete_console_release(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_SUSPENDED);
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_RESUME;
    message.data.ready.generation = 4;
    message.data.ready.output = tiny_output();
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SUSPEND;
    message.data.suspend.generation = 5;
    message.data.suspend.milliseconds = 51;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACQUIRE_PENDING);
    CHECK(nb_host_complete_console_acquire(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_RELEASE_PENDING);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED);
    CHECK(nb_host_complete_console_release(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_SUSPENDED);
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_RESUME;
    message.data.ready.generation = 5;
    message.data.ready.output = tiny_output();
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED);
    CHECK(nb_host_complete_console_acquire(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACTIVE);

    /* Saturated queued input must not hide a following VT release. */
    for (index = 0; index < 96; ++index) {
        memset(&message, 0, sizeof(message));
        message.type = NB_PRIVSEP_MESSAGE_POINTER_MOTION;
        message.data.pointer_motion.generation = 5;
        message.data.pointer_motion.milliseconds = 60 + index;
        message.data.pointer_motion.x = (int32_t)(index % 3);
        message.data.pointer_motion.y = (int32_t)(index % 2);
        CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    }
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SUSPEND;
    message.data.suspend.generation = 6;
    message.data.suspend.milliseconds = 200;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED);
    CHECK(nb_host_complete_console_release(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_SUSPENDED);
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_RESUME;
    message.data.ready.generation = 6;
    message.data.ready.output = tiny_output();
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED);
    CHECK(nb_host_complete_console_acquire(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACTIVE);
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);

    CHECK(nb_host_privsep_client_request_shutdown(
        host, UINT64_C(0xaabbccddeeff0011)));
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST);
    CHECK(message.data.token == UINT64_C(0xaabbccddeeff0011));
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED;
    message.data.token = UINT64_C(0xaabbccddeeff0011);
    {
        struct nb_privsep_message ping;

        memset(&ping, 0, sizeof(ping));
        ping.type = NB_PRIVSEP_MESSAGE_PING;
        ping.data.token = UINT64_C(0x9988776655443322);
        CHECK(send_helper_message(sockets[1], helper_sequence++, &ping));
    }
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_QUIT);
    {
        struct pollfd descriptor = {
            .fd = sockets[1],
            .events = POLLIN,
            .revents = 0
        };
        int poll_result;

        do {
            poll_result = poll(&descriptor, 1, 50);
        } while (poll_result < 0 && errno == EINTR);
        CHECK(poll_result == 0);
    }

    nb_host_destroy(host);
    (void)close(sockets[1]);
}

static void test_shutdown_acknowledgement_before_eof(void)
{
    int sockets[2] = {-1, -1};
    struct wire_reader reader;
    struct nb_privsep_message message;
    struct nb_host_event event;
    struct nb_host *host;
    const uint64_t token = UINT64_C(0x123456789abcdef0);

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
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_CORE_HELLO);
    CHECK(nb_host_privsep_client_request_shutdown(host, token));
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST);
    CHECK(message.data.token == token);

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED;
    message.data.token = token;
    CHECK(send_helper_message(sockets[1], 1, &message));
    CHECK(close(sockets[1]) == 0);
    sockets[1] = -1;
    CHECK(nb_host_wait_event(host, 1000, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_QUIT);
    CHECK(nb_host_get_state(host) != NB_HOST_STATE_FAILED);

    nb_host_destroy(host);
}

#if NIXBENCH_TEST_SESSION_CORE
enum session_core_shutdown_trigger {
    SESSION_CORE_SHUTDOWN_BY_ESCAPE,
    SESSION_CORE_SHUTDOWN_BY_SIGTERM
};

static void prior_sigterm_handler(int signal_number)
{
    (void)signal_number;
}

static bool application_result_matches(const char *path,
                                       const char *expected)
{
    char buffer[32];
    const size_t expected_size = strlen(expected);
    size_t used = 0;
    int descriptor = open(path, O_RDONLY);
    bool matched;

    if (descriptor < 0) {
        return false;
    }
    while (used < sizeof(buffer)) {
        const ssize_t count = read(descriptor,
                                   buffer + used,
                                   sizeof(buffer) - used);

        if (count > 0) {
            used += (size_t)count;
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            (void)close(descriptor);
            return false;
        }
    }
    matched = close(descriptor) == 0 &&
              used == expected_size &&
              memcmp(buffer, expected, expected_size) == 0;
    return matched;
}

static bool wait_for_application_ready(const char *path)
{
    unsigned int attempt;

    for (attempt = 0; attempt < 200; ++attempt) {
        if (application_result_matches(path, "ready\n")) {
            return true;
        }
        (void)poll(NULL, 0, 10);
    }
    return false;
}

static void test_session_core_with_fake_helper(
    const char *application_path,
    enum session_core_shutdown_trigger trigger,
    bool verify_application_cleanup)
{
    char application_result[] =
        "/tmp/nixbench-session-app-result-XXXXXX";
    char runtime_directory[] =
        "/tmp/nixbench-session-runtime-XXXXXX";
    int sockets[2] = {-1, -1};
    struct wire_reader reader;
    struct nb_privsep_message message;
    const struct nb_privsep_output output = runtime_output();
    uint64_t helper_sequence = 1;
    uint64_t frame_serial = 0;
    uint64_t shutdown_token = 0;
    uint32_t received = 0;
    pid_t child;
    int child_status = 0;
    bool committed = false;
    bool shutdown_requested = false;
    int temporary;

    if (geteuid() == 0) {
        puts("session-core runtime integration skipped for uid 0");
        return;
    }
    if (verify_application_cleanup) {
        temporary = mkstemp(application_result);
        CHECK(temporary >= 0);
        if (temporary < 0) {
            return;
        }
        CHECK(close(temporary) == 0);
        if (mkdtemp(runtime_directory) == NULL) {
            CHECK(false);
            (void)unlink(application_result);
            return;
        }
    }
    memset(&reader, 0, sizeof(reader));
    nb_privsep_parser_init(&reader.parser, NB_PRIVSEP_ENDPOINT_CORE);
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    if (sockets[0] < 0 || sockets[1] < 0) {
        if (verify_application_cleanup) {
            (void)unlink(application_result);
            (void)rmdir(runtime_directory);
        }
        return;
    }
    child = fork();
    CHECK(child >= 0);
    if (child < 0) {
        (void)close(sockets[0]);
        (void)close(sockets[1]);
        if (verify_application_cleanup) {
            (void)unlink(application_result);
            (void)rmdir(runtime_directory);
        }
        return;
    }
    if (child == 0) {
        struct sigaction prior_action;
        int result;

        (void)close(sockets[1]);
        if (verify_application_cleanup &&
            setenv("NIXBENCH_TEST_APPLICATION_RESULT",
                   application_result,
                   1) != 0) {
            _exit(123);
        }
        if (trigger == SESSION_CORE_SHUTDOWN_BY_SIGTERM) {
            memset(&prior_action, 0, sizeof(prior_action));
            prior_action.sa_handler = prior_sigterm_handler;
            if (sigemptyset(&prior_action.sa_mask) != 0 ||
                sigaddset(&prior_action.sa_mask, SIGUSR1) != 0) {
                _exit(120);
            }
            prior_action.sa_flags = SA_RESTART;
            if (sigaction(SIGTERM, &prior_action, NULL) != 0) {
                _exit(121);
            }
        }
        result = nb_session_core_run(
            sockets[0],
            application_path,
            verify_application_cleanup ? runtime_directory : NULL);
        if (trigger == SESSION_CORE_SHUTDOWN_BY_SIGTERM) {
            struct sigaction restored_action;

            if (sigaction(SIGTERM, NULL, &restored_action) != 0 ||
                restored_action.sa_handler != prior_sigterm_handler ||
                (restored_action.sa_flags & SA_RESTART) == 0 ||
                sigismember(&restored_action.sa_mask, SIGUSR1) != 1) {
                _exit(122);
            }
        }
        _exit(result);
    }
    (void)close(sockets[0]);
    sockets[0] = -1;

    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_CORE_HELLO);
    CHECK(message.data.credentials.process_id == (uint32_t)child);

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_READY;
    message.data.ready.generation = 1;
    message.data.ready.output = output;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));

    while (!committed) {
        CHECK(receive_core_message(sockets[1], &reader, &message));
        if (message.type == NB_PRIVSEP_MESSAGE_FRAME_BEGIN) {
            CHECK(frame_serial == 0);
            CHECK(message.data.frame_begin.generation == 1);
            CHECK(message.data.frame_begin.frame_bytes == output.frame_bytes);
            frame_serial = message.data.frame_begin.serial;
            received = 0;
        } else if (message.type == NB_PRIVSEP_MESSAGE_FRAME_DATA) {
            CHECK(frame_serial != 0);
            CHECK(message.data.frame_data.generation == 1);
            CHECK(message.data.frame_data.serial == frame_serial);
            CHECK(message.data.frame_data.offset == received);
            received += message.data.frame_data.size;
            CHECK(received <= output.frame_bytes);
        } else if (message.type == NB_PRIVSEP_MESSAGE_FRAME_COMMIT) {
            CHECK(message.data.frame_reference.generation == 1);
            CHECK(message.data.frame_reference.serial == frame_serial);
            CHECK(received == output.frame_bytes);
            committed = true;
        } else {
            CHECK(false);
            break;
        }
    }

    if (verify_application_cleanup) {
        CHECK(wait_for_application_ready(application_result));
    }

    if (trigger == SESSION_CORE_SHUTDOWN_BY_SIGTERM) {
        CHECK(kill(child, SIGTERM) == 0);
    } else {
        memset(&message, 0, sizeof(message));
        message.type = NB_PRIVSEP_MESSAGE_KEY;
        message.data.key.generation = 1;
        message.data.key.milliseconds = 101;
        (void)snprintf(message.data.key.xkb_key_name,
                       sizeof(message.data.key.xkb_key_name),
                       "ESC");
        message.data.key.pressed = true;
        CHECK(send_helper_message(sockets[1], helper_sequence++, &message));
    }

    {
        struct pollfd descriptor = {
            .fd = sockets[1],
            .events = POLLIN,
            .revents = 0
        };
        int poll_result;

        do {
            poll_result = poll(&descriptor, 1, 100);
        } while (poll_result < 0 && errno == EINTR);
        /* Neither shutdown trigger may overtake the incomplete frame. */
        CHECK(poll_result == 0);
    }

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_FRAME_COMPLETE;
    message.data.frame_complete.generation = 1;
    message.data.frame_complete.serial = frame_serial;
    message.data.frame_complete.milliseconds = 102;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));

    while (!shutdown_requested) {
        CHECK(receive_core_message(sockets[1], &reader, &message));
        if (message.type == NB_PRIVSEP_MESSAGE_SHUTDOWN_REQUEST) {
            CHECK(message.data.token != 0);
            shutdown_token = message.data.token;
            shutdown_requested = true;
        } else {
            CHECK(message.type == NB_PRIVSEP_MESSAGE_PONG);
        }
    }
    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_SHUTDOWN_ACCEPTED;
    message.data.token = shutdown_token;
    CHECK(send_helper_message(sockets[1], helper_sequence++, &message));

    while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
    }
    (void)close(sockets[1]);
    CHECK(WIFEXITED(child_status));
    CHECK(WEXITSTATUS(child_status) == 0);
    if (verify_application_cleanup) {
        CHECK(application_result_matches(application_result,
                                         "socket-alive\n"));
        (void)unlink(application_result);
        CHECK(rmdir(runtime_directory) == 0);
    }
}

static void test_session_core_rejects_unlaunchable_application(void)
{
    char missing_path[] = "/tmp/nixbench-missing-application-XXXXXX";
    int temporary;
    int sockets[2] = {-1, -1};
    struct wire_reader reader;
    struct nb_privsep_message message;
    const struct nb_privsep_output output = runtime_output();
    pid_t child;
    int child_status = 0;

    if (geteuid() == 0) {
        return;
    }
    temporary = mkstemp(missing_path);
    CHECK(temporary >= 0);
    if (temporary < 0) {
        return;
    }
    CHECK(close(temporary) == 0);
    CHECK(unlink(missing_path) == 0);

    memset(&reader, 0, sizeof(reader));
    nb_privsep_parser_init(&reader.parser, NB_PRIVSEP_ENDPOINT_CORE);
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    if (sockets[0] < 0 || sockets[1] < 0) {
        return;
    }
    child = fork();
    CHECK(child >= 0);
    if (child < 0) {
        (void)close(sockets[0]);
        (void)close(sockets[1]);
        return;
    }
    if (child == 0) {
        const int result = nb_session_core_run(sockets[0],
                                               missing_path,
                                               NULL);

        _exit(result);
    }
    (void)close(sockets[0]);
    sockets[0] = -1;
    CHECK(receive_core_message(sockets[1], &reader, &message));
    CHECK(message.type == NB_PRIVSEP_MESSAGE_CORE_HELLO);

    memset(&message, 0, sizeof(message));
    message.type = NB_PRIVSEP_MESSAGE_READY;
    message.data.ready.generation = 1;
    message.data.ready.output = output;
    CHECK(send_helper_message(sockets[1], 1, &message));

    while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
    }
    (void)close(sockets[1]);
    CHECK(WIFEXITED(child_status));
    CHECK(WEXITSTATUS(child_status) == 1);
}
#endif

int main(int argc, char *argv[])
{
#if NIXBENCH_TEST_SESSION_CORE
    const char *application_path = argc > 1 ? argv[1] : "/usr/bin/true";
    const bool verify_application_cleanup = argc > 1;
#else
    (void)argc;
    (void)argv;
#endif

    CHECK(nb_host_privsep_client_create(-1) == NULL);
    CHECK(nb_host_privsep_client_creation_error()[0] != '\0');
    test_host_proxy();
    test_shutdown_acknowledgement_before_eof();
#if NIXBENCH_TEST_SESSION_CORE
    test_session_core_with_fake_helper(
        application_path,
        SESSION_CORE_SHUTDOWN_BY_ESCAPE,
        verify_application_cleanup);
    test_session_core_with_fake_helper(
        application_path,
        SESSION_CORE_SHUTDOWN_BY_SIGTERM,
        verify_application_cleanup);
    test_session_core_rejects_unlaunchable_application();
#endif

    if (failures != 0) {
        fprintf(stderr, "session-core tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("session-core tests: ok");
    return 0;
}
