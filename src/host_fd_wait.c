#define _POSIX_C_SOURCE 200809L

#include "host_fd_wait.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

enum {
    NB_NANOSECONDS_PER_MILLISECOND = 1000000,
    NB_NANOSECONDS_PER_SECOND = 1000000000
};

static enum nb_host_event_status fail_wait(
    struct nb_host_fd_wait_result *result,
    int system_error)
{
    result->system_error = system_error;
    errno = system_error;
    return NB_HOST_EVENT_STATUS_ERROR;
}

static bool arguments_are_valid(
    int primary_fd,
    const int *external_fds,
    size_t external_fd_count,
    nb_host_fd_wait_poll_callback callback,
    const struct nb_host_event *event,
    const struct nb_host_fd_wait_result *result)
{
    size_t first;
    size_t second;

    if (primary_fd < 0 ||
        external_fd_count > (size_t)NB_HOST_FD_WAIT_MAX_EXTERNAL ||
        (external_fd_count != 0U && external_fds == NULL) ||
        callback == NULL || event == NULL || result == NULL) {
        return false;
    }
    for (first = 0; first < external_fd_count; ++first) {
        if (external_fds[first] < 0 || external_fds[first] == primary_fd) {
            return false;
        }
        for (second = first + 1U;
             second < external_fd_count;
             ++second) {
            if (external_fds[first] == external_fds[second]) {
                return false;
            }
        }
    }
    return true;
}

static bool monotonic_now(struct timespec *now, int *system_error)
{
    if (clock_gettime(CLOCK_MONOTONIC, now) != 0) {
        *system_error = errno;
        return false;
    }
    return true;
}

static uint64_t elapsed_nanoseconds(const struct timespec *start,
                                    const struct timespec *now)
{
    uint64_t seconds;
    uint64_t nanoseconds;

    if (now->tv_sec < start->tv_sec ||
        (now->tv_sec == start->tv_sec && now->tv_nsec < start->tv_nsec)) {
        return 0;
    }
    seconds = (uint64_t)now->tv_sec - (uint64_t)start->tv_sec;
    if (now->tv_nsec < start->tv_nsec) {
        --seconds;
        nanoseconds = (uint64_t)(NB_NANOSECONDS_PER_SECOND +
                                 now->tv_nsec - start->tv_nsec);
    } else {
        nanoseconds = (uint64_t)(now->tv_nsec - start->tv_nsec);
    }
    if (seconds >
        (UINT64_MAX - nanoseconds) /
            (uint64_t)NB_NANOSECONDS_PER_SECOND) {
        return UINT64_MAX;
    }
    return seconds * (uint64_t)NB_NANOSECONDS_PER_SECOND + nanoseconds;
}

static int remaining_poll_timeout(uint64_t timeout_nanoseconds,
                                  uint64_t elapsed)
{
    const uint64_t remaining = timeout_nanoseconds - elapsed;
    const uint64_t rounded_milliseconds =
        (remaining + (uint64_t)NB_NANOSECONDS_PER_MILLISECOND - UINT64_C(1)) /
        (uint64_t)NB_NANOSECONDS_PER_MILLISECOND;

    return rounded_milliseconds > (uint64_t)INT_MAX
               ? INT_MAX
               : (int)rounded_milliseconds;
}

static enum nb_host_event_status poll_callback(
    nb_host_fd_wait_poll_callback callback,
    void *callback_context,
    struct nb_host_event *event)
{
    return callback(callback_context, event);
}

enum nb_host_event_status nb_host_fd_wait_event(
    int primary_fd,
    const int *external_fds,
    size_t external_fd_count,
    uint32_t timeout_milliseconds,
    nb_host_fd_wait_poll_callback callback,
    void *callback_context,
    struct nb_host_event *event,
    struct nb_host_fd_wait_result *result)
{
    struct pollfd descriptors[1 + NB_HOST_FD_WAIT_MAX_EXTERNAL];
    struct timespec started;
    const uint64_t timeout_nanoseconds =
        (uint64_t)timeout_milliseconds *
        (uint64_t)NB_NANOSECONDS_PER_MILLISECOND;
    enum nb_host_event_status status;
    size_t index;
    int system_error = 0;
    bool zero_timeout_poll_pending = timeout_milliseconds == 0;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (!arguments_are_valid(primary_fd,
                             external_fds,
                             external_fd_count,
                             callback,
                             event,
                             result)) {
        if (result != NULL) {
            return fail_wait(result, EINVAL);
        }
        errno = EINVAL;
        return NB_HOST_EVENT_STATUS_ERROR;
    }

    status = poll_callback(callback, callback_context, event);
    if (status != NB_HOST_EVENT_STATUS_EMPTY) {
        return status;
    }
    if (!monotonic_now(&started, &system_error)) {
        return fail_wait(result, system_error);
    }

    descriptors[0].fd = primary_fd;
    descriptors[0].events = POLLIN;
    descriptors[0].revents = 0;
    for (index = 0; index < external_fd_count; ++index) {
        descriptors[index + 1U].fd = external_fds[index];
        descriptors[index + 1U].events = POLLIN;
        descriptors[index + 1U].revents = 0;
    }

    for (;;) {
        struct timespec now;
        uint64_t elapsed;
        int poll_timeout;
        int poll_result;

        if (!monotonic_now(&now, &system_error)) {
            return fail_wait(result, system_error);
        }
        elapsed = elapsed_nanoseconds(&started, &now);
        if (elapsed >= timeout_nanoseconds &&
            !zero_timeout_poll_pending) {
            status = poll_callback(callback, callback_context, event);
            if (status != NB_HOST_EVENT_STATUS_EMPTY) {
                return status;
            }
            result->timed_out = true;
            return NB_HOST_EVENT_STATUS_EMPTY;
        }
        poll_timeout = elapsed >= timeout_nanoseconds
                           ? 0
                           : remaining_poll_timeout(timeout_nanoseconds,
                                                    elapsed);
        descriptors[0].revents = 0;
        for (index = 0; index < external_fd_count; ++index) {
            descriptors[index + 1U].revents = 0;
        }
        poll_result = poll(descriptors,
                           (nfds_t)(external_fd_count + 1U),
                           poll_timeout);
        if (poll_result > 0) {
            zero_timeout_poll_pending = false;
            result->primary_ready = descriptors[0].revents != 0;
            for (index = 0; index < external_fd_count; ++index) {
                result->external_ready[index] =
                    descriptors[index + 1U].revents != 0;
            }
            status = poll_callback(callback, callback_context, event);
            if (status != NB_HOST_EVENT_STATUS_EMPTY) {
                return status;
            }
            if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) !=
                0) {
                return fail_wait(result, EIO);
            }
            return NB_HOST_EVENT_STATUS_EMPTY;
        }
        if (poll_result == 0) {
            zero_timeout_poll_pending = false;
            status = poll_callback(callback, callback_context, event);
            if (status != NB_HOST_EVENT_STATUS_EMPTY) {
                return status;
            }
            if (timeout_nanoseconds == 0) {
                result->timed_out = true;
                return NB_HOST_EVENT_STATUS_EMPTY;
            }
            continue;
        }
        if (errno != EINTR) {
            system_error = errno;
            return fail_wait(result, system_error);
        }
        if (result->interruptions != UINT32_MAX) {
            ++result->interruptions;
        }
        status = poll_callback(callback, callback_context, event);
        if (status != NB_HOST_EVENT_STATUS_EMPTY) {
            return status;
        }
    }
}
