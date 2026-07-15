#ifndef NIXBENCH_HOST_PRIVSEP_CLIENT_H
#define NIXBENCH_HOST_PRIVSEP_CLIENT_H

#include "host.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Unprivileged side of the standalone helper/core boundary.  Creation takes
 * ownership of descriptor only on success and marks it close-on-exec before
 * doing any other setup.  The descriptor must be one endpoint of the private
 * local byte stream inherited from the privileged helper.
 */
struct nb_host *nb_host_privsep_client_create(int descriptor);

/* Queue one orderly session shutdown request.  token must be nonzero. */
bool nb_host_privsep_client_request_shutdown(struct nb_host *host,
                                             uint64_t token);

/* True after a validated helper READY message supplied the initial output. */
bool nb_host_privsep_client_is_ready(const struct nb_host *host);

/* Borrowed creation error text, invalidated by the next create attempt. */
const char *nb_host_privsep_client_creation_error(void);
int nb_host_privsep_client_creation_system_error(void);

#endif
