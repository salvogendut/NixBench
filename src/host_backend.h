#ifndef NIXBENCH_HOST_BACKEND_H
#define NIXBENCH_HOST_BACKEND_H

#include "host.h"

/* Internal adapter interface; application code should use host.h only. */
struct nb_host_backend_operations {
    bool (*get_output)(const void *context, struct nb_host_output *output);
    uint64_t (*monotonic_milliseconds)(const void *context);
    enum nb_host_event_status (*poll_event)(void *context,
                                            struct nb_host_event *event);
    enum nb_host_event_status (*wait_event)(
        void *context,
        uint32_t timeout_milliseconds,
        struct nb_host_event *event);
    bool (*set_pointer_capture)(void *context, bool captured);
    enum nb_host_present_status (*present)(
        void *context,
        const struct nb_host_frame *frame);
    void (*destroy)(void *context);
};

/* Context ownership transfers only when creation succeeds. */
struct nb_host *nb_host_backend_create(
    const struct nb_host_backend_operations *operations,
    void *context);

/* Adapter helpers reject a host created by a different operations table. */
void *nb_host_backend_context(
    struct nb_host *host,
    const struct nb_host_backend_operations *operations);
const void *nb_host_backend_context_const(
    const struct nb_host *host,
    const struct nb_host_backend_operations *operations);

#endif
