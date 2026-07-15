#ifndef NIXBENCH_PRIVSEP_HELPER_H
#define NIXBENCH_PRIVSEP_HELPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host.h"
#include "privsep_protocol.h"

enum {
    NB_PRIVSEP_HELPER_OUTBOUND_CAPACITY = 4096,
    NB_PRIVSEP_HELPER_CONTROL_RESERVE = 256,
    NB_PRIVSEP_HELPER_ERROR_CAPACITY = 192
};

enum nb_privsep_helper_error {
    NB_PRIVSEP_HELPER_ERROR_NONE,
    NB_PRIVSEP_HELPER_ERROR_PROTOCOL,
    NB_PRIVSEP_HELPER_ERROR_CREDENTIALS,
    NB_PRIVSEP_HELPER_ERROR_OUTBOUND_FULL,
    NB_PRIVSEP_HELPER_ERROR_PRESENTATION,
    NB_PRIVSEP_HELPER_ERROR_ALLOCATION,
    NB_PRIVSEP_HELPER_ERROR_INVALID_STATE
};

/* Pixels are borrowed only for the duration of the callback. */
typedef bool (*nb_privsep_helper_present_fn)(
    void *data,
    uint64_t generation,
    const struct nb_host_frame *frame);

struct nb_privsep_helper_options {
    struct nb_privsep_credentials expected_credentials;
    struct nb_privsep_output output;
    nb_privsep_helper_present_fn present;
    void *present_data;
};

struct nb_privsep_helper;

void nb_privsep_helper_options_init(
    struct nb_privsep_helper_options *options);
struct nb_privsep_helper *nb_privsep_helper_create(
    const struct nb_privsep_helper_options *options);
void nb_privsep_helper_destroy(struct nb_privsep_helper *helper);

/* Consumes as many complete or partial core messages as possible. */
bool nb_privsep_helper_feed(struct nb_privsep_helper *helper,
                            const void *bytes,
                            size_t size,
                            size_t *consumed);

/* Borrowed contiguous ring-buffer span. Empty queues return NULL and zero. */
bool nb_privsep_helper_peek_outbound(
    const struct nb_privsep_helper *helper,
    const unsigned char **bytes,
    size_t *size);
bool nb_privsep_helper_consume_outbound(
    struct nb_privsep_helper *helper,
    size_t size);
size_t nb_privsep_helper_outbound_size(
    const struct nb_privsep_helper *helper);

/* These lifecycle calls perform no I/O and suspend never allocates or waits. */
bool nb_privsep_helper_suspend(struct nb_privsep_helper *helper,
                               uint64_t milliseconds);
bool nb_privsep_helper_resume(struct nb_privsep_helper *helper,
                              const struct nb_privsep_output *output);

bool nb_privsep_helper_send_input(struct nb_privsep_helper *helper,
                                  const struct nb_host_event *event);
bool nb_privsep_helper_complete_frame(struct nb_privsep_helper *helper,
                                      uint64_t generation,
                                      uint64_t serial,
                                      uint64_t milliseconds);
bool nb_privsep_helper_send_ping(struct nb_privsep_helper *helper,
                                uint64_t token);
bool nb_privsep_helper_report_fatal(
    struct nb_privsep_helper *helper,
    enum nb_privsep_fatal_reason reason,
    uint32_t system_error,
    const char *message);

bool nb_privsep_helper_is_ready(const struct nb_privsep_helper *helper);
bool nb_privsep_helper_is_suspended(const struct nb_privsep_helper *helper);
bool nb_privsep_helper_presentation_pending(
    const struct nb_privsep_helper *helper);
uint64_t nb_privsep_helper_generation(
    const struct nb_privsep_helper *helper);

bool nb_privsep_helper_ping_outstanding(
    const struct nb_privsep_helper *helper);
bool nb_privsep_helper_take_pong(struct nb_privsep_helper *helper,
                                uint64_t *token);
bool nb_privsep_helper_shutdown_requested(
    const struct nb_privsep_helper *helper,
    uint64_t *request_id);

bool nb_privsep_helper_failed(const struct nb_privsep_helper *helper);
bool nb_privsep_helper_get_last_error(
    const struct nb_privsep_helper *helper,
    enum nb_privsep_helper_error *error,
    uint32_t *system_error,
    char *message,
    size_t message_size);

#endif
