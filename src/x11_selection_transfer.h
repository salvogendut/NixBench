#ifndef NIXBENCH_X11_SELECTION_TRANSFER_H
#define NIXBENCH_X11_SELECTION_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_X11_SELECTION_TRANSFER_CAPACITY = 16,
    NB_X11_SELECTION_TRANSFER_CHUNK_BYTES = 64 * 1024,
    NB_X11_SELECTION_TRANSFER_MAX_BYTES = 1024 * 1024
};

struct nb_x11_selection_transfers;

enum nb_x11_selection_incoming_result {
    NB_X11_SELECTION_INCOMING_ERROR = 0,
    NB_X11_SELECTION_INCOMING_MORE,
    NB_X11_SELECTION_INCOMING_COMPLETE
};

struct nb_x11_selection_transfers *nb_x11_selection_transfers_create(void);
void nb_x11_selection_transfers_destroy(
    struct nb_x11_selection_transfers *transfers);

/*
 * Snapshot one outgoing payload.  The X11 identifiers are intentionally plain
 * integers so this bounded state machine remains testable without XCB.
 */
bool nb_x11_selection_outgoing_start(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor,
    uint32_t property,
    uint32_t selection,
    uint32_t target,
    uint32_t property_type,
    const void *data,
    size_t size);

/*
 * Return the next chunk after the requestor deletes its property.  A final
 * successful call returns size zero with complete set and releases the saved
 * transfer.  Returned non-empty data is borrowed until the next mutation.
 */
bool nb_x11_selection_outgoing_next(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor,
    uint32_t property,
    uint32_t *property_type,
    const void **data,
    size_t *size,
    bool *complete);
void nb_x11_selection_outgoing_cancel(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor,
    uint32_t property);
void nb_x11_selection_outgoing_cancel_requestor(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor);
size_t nb_x11_selection_outgoing_count(
    const struct nb_x11_selection_transfers *transfers);

/* The INCR length is a lower bound; the accumulated result remains capped. */
bool nb_x11_selection_incoming_begin(
    struct nb_x11_selection_transfers *transfers,
    size_t expected_size);
enum nb_x11_selection_incoming_result nb_x11_selection_incoming_append(
    struct nb_x11_selection_transfers *transfers,
    const void *data,
    size_t size);
bool nb_x11_selection_incoming_data(
    const struct nb_x11_selection_transfers *transfers,
    const void **data,
    size_t *size);
void nb_x11_selection_incoming_clear(
    struct nb_x11_selection_transfers *transfers);

#endif
