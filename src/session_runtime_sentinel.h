#ifndef NIXBENCH_SESSION_RUNTIME_SENTINEL_H
#define NIXBENCH_SESSION_RUNTIME_SENTINEL_H

#include <stdbool.h>
#include <stdint.h>

enum {
    NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY = 512,
    NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY = 256
};

/*
 * Run the ordinary-user runtime-directory sentinel on one end of a connected
 * local stream socket. The sentinel creates and exclusively owns a private
 * /tmp/nixbench-runtime-UID-* directory, reports its path, and then waits for
 * either an explicit cleanup request or controller EOF.
 *
 * Cleanup is deliberately performed only by this unprivileged process. The
 * controller must never use the reported path for privileged filesystem
 * operations. The function returns zero only after the directory has been
 * removed and its former parent entry has been verified absent.
 */
int nb_session_runtime_sentinel_run(int controller_fd);

/*
 * Controller-side fixed-protocol helpers. Each operation is bounded by the
 * supplied monotonic timeout and preserves the descriptor's blocking mode.
 */
bool nb_session_runtime_sentinel_wait_ready(
    int controller_fd,
    uint32_t timeout_ms,
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY],
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY]);

bool nb_session_runtime_sentinel_request_cleanup(
    int controller_fd,
    uint32_t timeout_ms,
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY]);

#endif
