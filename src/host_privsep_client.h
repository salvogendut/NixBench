#ifndef NIXBENCH_HOST_PRIVSEP_CLIENT_H
#define NIXBENCH_HOST_PRIVSEP_CLIENT_H

#include "host.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Unprivileged side of the standalone helper/core boundary.  Creation takes
 * ownership of descriptor only on success and marks it close-on-exec before
 * doing any other setup.  The descriptor must be one endpoint of the private
 * local byte stream inherited from the privileged helper.
 */
struct nb_host *nb_host_privsep_client_create(int descriptor);

/*
 * Wait for helper traffic or one caller-owned wake descriptor. Readiness on
 * wake_descriptor returns EMPTY after servicing helper traffic; the caller
 * retains and drains that descriptor. This keeps the standalone compositor
 * event-driven without exposing the private helper descriptor.
 */
enum nb_host_event_status
nb_host_privsep_client_wait_event_with_descriptor(
    struct nb_host *host,
    int wake_descriptor,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event);

/* Same contract for up to four independent compositor-service descriptors. */
enum nb_host_event_status
nb_host_privsep_client_wait_event_with_descriptors(
    struct nb_host *host,
    const int *wake_descriptors,
    size_t wake_descriptor_count,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event);

/* Queue one orderly session shutdown request.  token must be nonzero. */
bool nb_host_privsep_client_request_shutdown(struct nb_host *host,
                                             uint64_t token);

/* True after a validated helper READY message supplied the initial output. */
bool nb_host_privsep_client_is_ready(const struct nb_host *host);

/* Borrowed creation error text, invalidated by the next create attempt. */
const char *nb_host_privsep_client_creation_error(void);
int nb_host_privsep_client_creation_system_error(void);

#endif
