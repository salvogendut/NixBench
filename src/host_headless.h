#ifndef NIXBENCH_HOST_HEADLESS_H
#define NIXBENCH_HOST_HEADLESS_H

#include "host.h"

enum {
    NB_HOST_HEADLESS_EVENT_CAPACITY = 64
};

/* A deterministic, in-memory host intended for runtime and backend tests. */
struct nb_host *nb_host_headless_create(
    const struct nb_host_output *output);

/*
 * Enqueue input events; output, console lifecycle, completion, and failure
 * events are host-owned. An event cannot be timestamped later than the
 * headless monotonic clock.
 */
bool nb_host_headless_enqueue_event(struct nb_host *host,
                                    const struct nb_host_event *event);

/* Atomically update the current output and enqueue OUTPUT_CHANGED if needed. */
bool nb_host_headless_set_output(struct nb_host *host,
                                 const struct nb_host_output *output,
                                 uint64_t milliseconds);

/* Queue prioritized console lifecycle requests at the current time. */
bool nb_host_headless_request_console_release(struct nb_host *host,
                                              uint64_t milliseconds);
bool nb_host_headless_request_console_acquire(struct nb_host *host,
                                              uint64_t milliseconds);

/* The headless wait operation advances this clock when its queue is empty. */
bool nb_host_headless_advance_time(struct nb_host *host,
                                   uint64_t milliseconds);

size_t nb_host_headless_pending_event_count(const struct nb_host *host);
size_t nb_host_headless_presentation_count(const struct nb_host *host);
bool nb_host_headless_pointer_is_captured(const struct nb_host *host);

/* Borrowed pixels remain valid until the next successful present or destroy. */
bool nb_host_headless_last_presented_frame(
    const struct nb_host *host,
    struct nb_host_frame *frame);

#endif
