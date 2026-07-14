#ifndef NIXBENCH_HOST_FD_WAIT_H
#define NIXBENCH_HOST_FD_WAIT_H

#include "host.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_HOST_FD_WAIT_MAX_EXTERNAL = 2
};

/*
 * Poll one queued lifecycle event. The callback must not block. It is called
 * before waiting and again after every descriptor wake, timeout, or EINTR.
 */
typedef enum nb_host_event_status (*nb_host_fd_wait_poll_callback)(
    void *context,
    struct nb_host_event *event);

struct nb_host_fd_wait_result {
    bool primary_ready;
    bool external_ready[NB_HOST_FD_WAIT_MAX_EXTERNAL];
    bool timed_out;
    uint32_t interruptions;

    /* Zero unless the wait helper, rather than its callback, failed. */
    int system_error;
};

/*
 * Wait for a lifecycle event or for one of at most two external owners to
 * need service. External descriptors are wake-only: readiness is reported in
 * result while the function returns NB_HOST_EVENT_STATUS_EMPTY unless the
 * lifecycle callback produced an event or an error.
 */
enum nb_host_event_status nb_host_fd_wait_event(
    int primary_fd,
    const int *external_fds,
    size_t external_fd_count,
    uint32_t timeout_milliseconds,
    nb_host_fd_wait_poll_callback callback,
    void *callback_context,
    struct nb_host_event *event,
    struct nb_host_fd_wait_result *result);

#endif
