#define _POSIX_C_SOURCE 200809L

#include "host_fd_wait.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

struct callback_context {
    int primary_fd;
    unsigned int calls;
    unsigned int queued_events;
    unsigned int read_primary_from_call;
    uint32_t arm_alarm_microseconds;
    bool timer_failed;
    bool fail;
};

static volatile sig_atomic_t alarm_received;

static void alarm_handler(int signal_number)
{
    (void)signal_number;
    alarm_received = 1;
}

static bool make_pipe(int descriptors[2])
{
    int flags;

    if (pipe(descriptors) != 0) {
        perror("pipe");
        ++failures;
        return false;
    }
    flags = fcntl(descriptors[0], F_GETFL);
    if (flags < 0 || fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) != 0) {
        perror("fcntl");
        ++failures;
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        return false;
    }
    return true;
}

static void close_pipe(int descriptors[2])
{
    if (descriptors[0] >= 0) {
        CHECK(close(descriptors[0]) == 0);
        descriptors[0] = -1;
    }
    if (descriptors[1] >= 0) {
        CHECK(close(descriptors[1]) == 0);
        descriptors[1] = -1;
    }
}

static bool write_wake(int descriptor)
{
    const unsigned char byte = 1;

    if (write(descriptor, &byte, sizeof(byte)) != (ssize_t)sizeof(byte)) {
        perror("write");
        ++failures;
        return false;
    }
    return true;
}

static void make_event(struct nb_host_event *event)
{
    memset(event, 0, sizeof(*event));
    event->type = NB_HOST_EVENT_QUIT;
    event->milliseconds = 1;
}

static enum nb_host_event_status callback(void *opaque,
                                          struct nb_host_event *event)
{
    struct callback_context *context = opaque;

    ++context->calls;
    memset(event, 0, sizeof(*event));
    if (context->calls == 1U && context->arm_alarm_microseconds != 0U) {
        struct itimerval timer;

        memset(&timer, 0, sizeof(timer));
        timer.it_value.tv_sec =
            (time_t)(context->arm_alarm_microseconds / UINT32_C(1000000));
        timer.it_value.tv_usec =
            (suseconds_t)(context->arm_alarm_microseconds %
                          UINT32_C(1000000));
        if (setitimer(ITIMER_REAL, &timer, NULL) != 0) {
            context->timer_failed = true;
            return NB_HOST_EVENT_STATUS_ERROR;
        }
    }
    if (context->fail) {
        return NB_HOST_EVENT_STATUS_ERROR;
    }
    if (context->queued_events != 0U) {
        --context->queued_events;
        make_event(event);
        return NB_HOST_EVENT_STATUS_AVAILABLE;
    }
    if (context->primary_fd >= 0 &&
        context->read_primary_from_call != 0U &&
        context->calls >= context->read_primary_from_call) {
        unsigned char byte;
        const ssize_t count = read(context->primary_fd, &byte, sizeof(byte));

        if (count == (ssize_t)sizeof(byte)) {
            make_event(event);
            return NB_HOST_EVENT_STATUS_AVAILABLE;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return NB_HOST_EVENT_STATUS_EMPTY;
        }
        if (count < 0) {
            return NB_HOST_EVENT_STATUS_ERROR;
        }
    }
    return NB_HOST_EVENT_STATUS_EMPTY;
}

static struct callback_context empty_context(int primary_fd)
{
    const struct callback_context context = {
        .primary_fd = primary_fd
    };

    return context;
}

static void test_queued_host_event_precedes_external(void)
{
    int primary[2] = {-1, -1};
    int external[2] = {-1, -1};
    int external_fd;
    unsigned char byte = 0;
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary) || !make_pipe(external)) {
        close_pipe(primary);
        close_pipe(external);
        return;
    }
    CHECK(write_wake(external[1]));
    external_fd = external[0];
    context = empty_context(primary[0]);
    context.queued_events = 1;

    CHECK(nb_host_fd_wait_event(primary[0],
                                &external_fd,
                                1,
                                100,
                                callback,
                                &context,
                                &event,
                                &result) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_QUIT);
    CHECK(context.calls == 1U);
    CHECK(!result.primary_ready);
    CHECK(!result.external_ready[0]);
    CHECK(!result.timed_out);
    CHECK(read(external[0], &byte, sizeof(byte)) == (ssize_t)sizeof(byte));
    close_pipe(primary);
    close_pipe(external);
}

static void test_external_only_wakes_without_host_event(void)
{
    int primary[2] = {-1, -1};
    int external[2] = {-1, -1};
    int external_fd;
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary) || !make_pipe(external)) {
        close_pipe(primary);
        close_pipe(external);
        return;
    }
    CHECK(write_wake(external[1]));
    external_fd = external[0];
    context = empty_context(primary[0]);

    CHECK(nb_host_fd_wait_event(primary[0],
                                &external_fd,
                                1,
                                100,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(event.type == NB_HOST_EVENT_NONE);
    CHECK(context.calls == 2U);
    CHECK(!result.primary_ready);
    CHECK(result.external_ready[0]);
    CHECK(!result.external_ready[1]);
    CHECK(!result.timed_out);
    CHECK(result.interruptions == 0U);
    CHECK(result.system_error == 0);
    close_pipe(primary);
    close_pipe(external);
}

static void test_maximum_external_descriptors(void)
{
    int primary[2] = {-1, -1};
    int external[NB_HOST_FD_WAIT_MAX_EXTERNAL][2];
    int external_fds[NB_HOST_FD_WAIT_MAX_EXTERNAL];
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;
    size_t index;

    if (!make_pipe(primary)) {
        return;
    }
    for (index = 0;
         index < (size_t)NB_HOST_FD_WAIT_MAX_EXTERNAL;
         ++index) {
        external[index][0] = -1;
        external[index][1] = -1;
        if (!make_pipe(external[index])) {
            while (index != 0U) {
                --index;
                close_pipe(external[index]);
            }
            close_pipe(primary);
            return;
        }
        external_fds[index] = external[index][0];
    }
    CHECK(write_wake(external[NB_HOST_FD_WAIT_MAX_EXTERNAL - 1][1]));
    context = empty_context(primary[0]);

    CHECK(nb_host_fd_wait_event(primary[0],
                                external_fds,
                                NB_HOST_FD_WAIT_MAX_EXTERNAL,
                                100,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(!result.primary_ready);
    CHECK(!result.external_ready[0]);
    CHECK(!result.external_ready[1]);
    CHECK(!result.external_ready[2]);
    CHECK(result.external_ready[3]);
    CHECK(!result.timed_out);

    for (index = 0;
         index < (size_t)NB_HOST_FD_WAIT_MAX_EXTERNAL;
         ++index) {
        close_pipe(external[index]);
    }
    close_pipe(primary);
}

static void test_zero_timeout_checks_descriptors_once(void)
{
    int primary[2] = {-1, -1};
    int external[2] = {-1, -1};
    int external_fd;
    unsigned char byte = 0;
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary) || !make_pipe(external)) {
        close_pipe(primary);
        close_pipe(external);
        return;
    }
    external_fd = external[0];
    context = empty_context(primary[0]);
    CHECK(write_wake(external[1]));
    CHECK(nb_host_fd_wait_event(primary[0],
                                &external_fd,
                                1,
                                0,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(result.external_ready[0]);
    CHECK(!result.timed_out);
    CHECK(read(external[0], &byte, sizeof(byte)) == (ssize_t)sizeof(byte));

    context = empty_context(primary[0]);
    CHECK(nb_host_fd_wait_event(primary[0],
                                &external_fd,
                                1,
                                0,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(!result.external_ready[0]);
    CHECK(result.timed_out);
    close_pipe(primary);
    close_pipe(external);
}

static void test_primary_wake_becomes_host_event(void)
{
    int primary[2] = {-1, -1};
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary)) {
        return;
    }
    CHECK(write_wake(primary[1]));
    context = empty_context(primary[0]);
    context.read_primary_from_call = 2;

    CHECK(nb_host_fd_wait_event(primary[0],
                                NULL,
                                0,
                                100,
                                callback,
                                &context,
                                &event,
                                &result) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_QUIT);
    CHECK(context.calls == 2U);
    CHECK(result.primary_ready);
    CHECK(!result.timed_out);
    CHECK(result.system_error == 0);
    close_pipe(primary);
}

static void test_lifecycle_event_has_simultaneous_priority(void)
{
    int primary[2] = {-1, -1};
    int external[2] = {-1, -1};
    int external_fd;
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary) || !make_pipe(external)) {
        close_pipe(primary);
        close_pipe(external);
        return;
    }
    CHECK(write_wake(primary[1]));
    CHECK(write_wake(external[1]));
    external_fd = external[0];
    context = empty_context(primary[0]);
    context.read_primary_from_call = 2;

    CHECK(nb_host_fd_wait_event(primary[0],
                                &external_fd,
                                1,
                                100,
                                callback,
                                &context,
                                &event,
                                &result) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_QUIT);
    CHECK(result.primary_ready);
    CHECK(result.external_ready[0]);
    CHECK(!result.timed_out);
    close_pipe(primary);
    close_pipe(external);
}

static void test_timeout(void)
{
    int primary[2] = {-1, -1};
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary)) {
        return;
    }
    context = empty_context(primary[0]);
    CHECK(nb_host_fd_wait_event(primary[0],
                                NULL,
                                0,
                                10,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(event.type == NB_HOST_EVENT_NONE);
    CHECK(context.calls >= 2U);
    CHECK(!result.primary_ready);
    CHECK(result.timed_out);
    CHECK(result.interruptions == 0U);
    CHECK(result.system_error == 0);
    close_pipe(primary);
}

static void test_callback_error_precedes_wait(void)
{
    int primary[2] = {-1, -1};
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary)) {
        return;
    }
    context = empty_context(primary[0]);
    context.fail = true;
    CHECK(nb_host_fd_wait_event(primary[0],
                                NULL,
                                0,
                                100,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_ERROR);
    CHECK(context.calls == 1U);
    CHECK(!result.primary_ready);
    CHECK(!result.timed_out);
    CHECK(result.system_error == 0);
    close_pipe(primary);
}

static void test_primary_hangup_is_an_error(void)
{
    int primary[2] = {-1, -1};
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary)) {
        return;
    }
    CHECK(close(primary[1]) == 0);
    primary[1] = -1;
    context = empty_context(primary[0]);
    CHECK(nb_host_fd_wait_event(primary[0],
                                NULL,
                                0,
                                100,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_ERROR);
    CHECK(context.calls == 2U);
    CHECK(result.primary_ready);
    CHECK(!result.timed_out);
    CHECK(result.system_error == EIO);
    close_pipe(primary);
}

static void test_external_hangup_and_invalid_are_wakes(void)
{
    int primary[2] = {-1, -1};
    int hung_up[2] = {-1, -1};
    int invalid[2] = {-1, -1};
    int external_fds[2];
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary) || !make_pipe(hung_up) || !make_pipe(invalid)) {
        close_pipe(primary);
        close_pipe(hung_up);
        close_pipe(invalid);
        return;
    }
    CHECK(close(hung_up[1]) == 0);
    hung_up[1] = -1;
    external_fds[0] = hung_up[0];
    external_fds[1] = invalid[0];
    CHECK(close(invalid[0]) == 0);
    invalid[0] = -1;
    context = empty_context(primary[0]);

    CHECK(nb_host_fd_wait_event(primary[0],
                                external_fds,
                                2,
                                100,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(context.calls == 2U);
    CHECK(!result.primary_ready);
    CHECK(result.external_ready[0]);
    CHECK(result.external_ready[1]);
    CHECK(!result.timed_out);
    CHECK(result.system_error == 0);
    close_pipe(primary);
    close_pipe(hung_up);
    close_pipe(invalid);
}

static void test_interrupt_preserves_deadline(void)
{
    int primary[2] = {-1, -1};
    struct callback_context context;
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;
    struct sigaction action;
    struct sigaction saved_action;
    struct itimerval timer;

    if (!make_pipe(primary)) {
        return;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = alarm_handler;
    CHECK(sigemptyset(&action.sa_mask) == 0);
    CHECK(sigaction(SIGALRM, &action, &saved_action) == 0);
    alarm_received = 0;

    context = empty_context(primary[0]);
    context.arm_alarm_microseconds = UINT32_C(10000);
    CHECK(nb_host_fd_wait_event(primary[0],
                                NULL,
                                0,
                                40,
                                callback,
                                &context,
                                &event,
                                &result) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(alarm_received != 0);
    CHECK(!context.timer_failed);
    CHECK(result.interruptions >= 1U);
    CHECK(result.timed_out);
    CHECK(context.calls >= 3U);

    memset(&timer, 0, sizeof(timer));
    CHECK(setitimer(ITIMER_REAL, &timer, NULL) == 0);
    CHECK(sigaction(SIGALRM, &saved_action, NULL) == 0);
    close_pipe(primary);
}

static void check_invalid_call(int primary_fd,
                               const int *external_fds,
                               size_t external_fd_count,
                               nb_host_fd_wait_poll_callback wait_callback,
                               struct nb_host_event *event,
                               struct nb_host_fd_wait_result *result)
{
    struct callback_context context = empty_context(primary_fd);

    errno = 0;
    CHECK(nb_host_fd_wait_event(primary_fd,
                                external_fds,
                                external_fd_count,
                                0,
                                wait_callback,
                                &context,
                                event,
                                result) == NB_HOST_EVENT_STATUS_ERROR);
    CHECK(errno == EINVAL);
    if (result != NULL) {
        CHECK(result->system_error == EINVAL);
    }
}

static void test_invalid_arguments(void)
{
    int primary[2] = {-1, -1};
    int other[2] = {-1, -1};
    int external_fds[NB_HOST_FD_WAIT_MAX_EXTERNAL + 1];
    struct nb_host_event event;
    struct nb_host_fd_wait_result result;

    if (!make_pipe(primary) || !make_pipe(other)) {
        close_pipe(primary);
        close_pipe(other);
        return;
    }
    external_fds[0] = other[0];
    external_fds[1] = other[1];
    external_fds[2] = primary[1];
    external_fds[3] = other[1];

    check_invalid_call(-1, NULL, 0, callback, &event, &result);
    check_invalid_call(primary[0], NULL, 1, callback, &event, &result);
    check_invalid_call(primary[0],
                       external_fds,
                       NB_HOST_FD_WAIT_MAX_EXTERNAL + 1,
                       callback,
                       &event,
                       &result);
    external_fds[0] = -1;
    check_invalid_call(primary[0], external_fds, 1, callback, &event, &result);
    external_fds[0] = primary[0];
    check_invalid_call(primary[0], external_fds, 1, callback, &event, &result);
    external_fds[0] = other[0];
    external_fds[1] = other[0];
    check_invalid_call(primary[0], external_fds, 2, callback, &event, &result);
    check_invalid_call(primary[0], NULL, 0, NULL, &event, &result);
    check_invalid_call(primary[0], NULL, 0, callback, NULL, &result);
    check_invalid_call(primary[0], NULL, 0, callback, &event, NULL);
    close_pipe(primary);
    close_pipe(other);
}

int main(void)
{
    test_queued_host_event_precedes_external();
    test_external_only_wakes_without_host_event();
    test_maximum_external_descriptors();
    test_zero_timeout_checks_descriptors_once();
    test_primary_wake_becomes_host_event();
    test_lifecycle_event_has_simultaneous_priority();
    test_timeout();
    test_callback_error_precedes_wait();
    test_primary_hangup_is_an_error();
    test_external_hangup_and_invalid_are_wakes();
    test_interrupt_preserves_deadline();
    test_invalid_arguments();

    if (failures != 0) {
        fprintf(stderr, "%d host fd wait test(s) failed\n", failures);
        return 1;
    }
    puts("host fd wait tests passed");
    return 0;
}
