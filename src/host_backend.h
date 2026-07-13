#ifndef NIXBENCH_HOST_BACKEND_H
#define NIXBENCH_HOST_BACKEND_H

#include "host.h"

/* Internal adapter interface; application code should use host.h only. */
struct nb_host_backend_operations {
    bool (*get_output)(const void *context, struct nb_host_output *output);
    enum nb_host_state (*get_state)(const void *context);
    uint64_t (*monotonic_milliseconds)(const void *context);
    enum nb_host_event_status (*poll_event)(void *context,
                                            struct nb_host_event *event);
    enum nb_host_event_status (*wait_event)(
        void *context,
        uint32_t timeout_milliseconds,
        struct nb_host_event *event);
    bool (*set_pointer_capture)(void *context, bool captured);
    enum nb_host_result (*present)(
        void *context,
        const struct nb_host_frame *frame);
    enum nb_host_result (*complete_console_release)(void *context);
    enum nb_host_result (*complete_console_acquire)(void *context);
    bool (*get_last_error)(const void *context,
                           int *system_error,
                           char *message,
                           size_t message_size);
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
