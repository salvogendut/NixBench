#include "x11_selection_transfer.h"

#include <stdlib.h>
#include <string.h>

struct nb_x11_selection_outgoing {
    uint32_t requestor;
    uint32_t property;
    uint32_t selection;
    uint32_t target;
    uint32_t property_type;
    unsigned char *data;
    size_t size;
    size_t offset;
    bool occupied;
};

struct nb_x11_selection_incoming {
    unsigned char *data;
    size_t size;
    size_t capacity;
    bool active;
    bool complete;
};

struct nb_x11_selection_transfers {
    struct nb_x11_selection_outgoing
        outgoing[NB_X11_SELECTION_TRANSFER_CAPACITY];
    struct nb_x11_selection_incoming incoming;
};

static void outgoing_clear(struct nb_x11_selection_outgoing *transfer)
{
    if (transfer == NULL) {
        return;
    }
    free(transfer->data);
    memset(transfer, 0, sizeof(*transfer));
}

struct nb_x11_selection_transfers *nb_x11_selection_transfers_create(void)
{
    return calloc(1, sizeof(struct nb_x11_selection_transfers));
}

void nb_x11_selection_transfers_destroy(
    struct nb_x11_selection_transfers *transfers)
{
    size_t index;

    if (transfers == NULL) {
        return;
    }
    for (index = 0; index < NB_X11_SELECTION_TRANSFER_CAPACITY; ++index) {
        outgoing_clear(&transfers->outgoing[index]);
    }
    nb_x11_selection_incoming_clear(transfers);
    free(transfers);
}

static struct nb_x11_selection_outgoing *find_outgoing(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor,
    uint32_t property)
{
    size_t index;

    if (transfers == NULL || requestor == 0 || property == 0) {
        return NULL;
    }
    for (index = 0; index < NB_X11_SELECTION_TRANSFER_CAPACITY; ++index) {
        struct nb_x11_selection_outgoing *transfer =
            &transfers->outgoing[index];

        if (transfer->occupied && transfer->requestor == requestor &&
            transfer->property == property) {
            return transfer;
        }
    }
    return NULL;
}

bool nb_x11_selection_outgoing_start(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor,
    uint32_t property,
    uint32_t selection,
    uint32_t target,
    uint32_t property_type,
    const void *data,
    size_t size)
{
    struct nb_x11_selection_outgoing *available = NULL;
    size_t index;

    if (transfers == NULL || requestor == 0 || property == 0 ||
        selection == 0 || target == 0 || property_type == 0 || data == NULL ||
        size <= NB_X11_SELECTION_TRANSFER_CHUNK_BYTES ||
        size > NB_X11_SELECTION_TRANSFER_MAX_BYTES ||
        find_outgoing(transfers, requestor, property) != NULL) {
        return false;
    }
    for (index = 0; index < NB_X11_SELECTION_TRANSFER_CAPACITY; ++index) {
        if (!transfers->outgoing[index].occupied) {
            available = &transfers->outgoing[index];
            break;
        }
    }
    if (available == NULL) {
        return false;
    }
    available->data = malloc(size);
    if (available->data == NULL) {
        return false;
    }
    memcpy(available->data, data, size);
    available->requestor = requestor;
    available->property = property;
    available->selection = selection;
    available->target = target;
    available->property_type = property_type;
    available->size = size;
    available->offset = 0;
    available->occupied = true;
    return true;
}

bool nb_x11_selection_outgoing_next(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor,
    uint32_t property,
    uint32_t *property_type,
    const void **data,
    size_t *size,
    bool *complete)
{
    struct nb_x11_selection_outgoing *transfer =
        find_outgoing(transfers, requestor, property);
    size_t amount;

    if (transfer == NULL || property_type == NULL || data == NULL ||
        size == NULL || complete == NULL) {
        return false;
    }
    *property_type = transfer->property_type;
    if (transfer->offset == transfer->size) {
        *data = NULL;
        *size = 0;
        *complete = true;
        outgoing_clear(transfer);
        return true;
    }
    amount = transfer->size - transfer->offset;
    if (amount > NB_X11_SELECTION_TRANSFER_CHUNK_BYTES) {
        amount = NB_X11_SELECTION_TRANSFER_CHUNK_BYTES;
    }
    *data = transfer->data + transfer->offset;
    *size = amount;
    *complete = false;
    transfer->offset += amount;
    return true;
}

void nb_x11_selection_outgoing_cancel(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor,
    uint32_t property)
{
    outgoing_clear(find_outgoing(transfers, requestor, property));
}

void nb_x11_selection_outgoing_cancel_requestor(
    struct nb_x11_selection_transfers *transfers,
    uint32_t requestor)
{
    size_t index;

    if (transfers == NULL || requestor == 0) {
        return;
    }
    for (index = 0; index < NB_X11_SELECTION_TRANSFER_CAPACITY; ++index) {
        if (transfers->outgoing[index].occupied &&
            transfers->outgoing[index].requestor == requestor) {
            outgoing_clear(&transfers->outgoing[index]);
        }
    }
}

size_t nb_x11_selection_outgoing_count(
    const struct nb_x11_selection_transfers *transfers)
{
    size_t count = 0;
    size_t index;

    if (transfers == NULL) {
        return 0;
    }
    for (index = 0; index < NB_X11_SELECTION_TRANSFER_CAPACITY; ++index) {
        if (transfers->outgoing[index].occupied) {
            ++count;
        }
    }
    return count;
}

void nb_x11_selection_incoming_clear(
    struct nb_x11_selection_transfers *transfers)
{
    if (transfers == NULL) {
        return;
    }
    free(transfers->incoming.data);
    memset(&transfers->incoming, 0, sizeof(transfers->incoming));
}

bool nb_x11_selection_incoming_begin(
    struct nb_x11_selection_transfers *transfers,
    size_t expected_size)
{
    size_t capacity;

    if (transfers == NULL || transfers->incoming.active ||
        expected_size > NB_X11_SELECTION_TRANSFER_MAX_BYTES) {
        return false;
    }
    capacity = expected_size;
    if (capacity < NB_X11_SELECTION_TRANSFER_CHUNK_BYTES) {
        capacity = NB_X11_SELECTION_TRANSFER_CHUNK_BYTES;
    }
    transfers->incoming.data = malloc(capacity);
    if (transfers->incoming.data == NULL) {
        return false;
    }
    transfers->incoming.capacity = capacity;
    transfers->incoming.active = true;
    return true;
}

enum nb_x11_selection_incoming_result nb_x11_selection_incoming_append(
    struct nb_x11_selection_transfers *transfers,
    const void *data,
    size_t size)
{
    struct nb_x11_selection_incoming *incoming;
    size_t needed;

    if (transfers == NULL) {
        return NB_X11_SELECTION_INCOMING_ERROR;
    }
    incoming = &transfers->incoming;
    if (!incoming->active || incoming->complete ||
        (size != 0 && data == NULL)) {
        return NB_X11_SELECTION_INCOMING_ERROR;
    }
    if (size == 0) {
        incoming->complete = true;
        return NB_X11_SELECTION_INCOMING_COMPLETE;
    }
    if (size > NB_X11_SELECTION_TRANSFER_MAX_BYTES - incoming->size) {
        nb_x11_selection_incoming_clear(transfers);
        return NB_X11_SELECTION_INCOMING_ERROR;
    }
    needed = incoming->size + size;
    if (needed > incoming->capacity) {
        size_t capacity = incoming->capacity;
        unsigned char *resized;

        while (capacity < needed) {
            if (capacity > NB_X11_SELECTION_TRANSFER_MAX_BYTES / 2U) {
                capacity = NB_X11_SELECTION_TRANSFER_MAX_BYTES;
            } else {
                capacity *= 2U;
            }
        }
        resized = realloc(incoming->data, capacity);
        if (resized == NULL) {
            nb_x11_selection_incoming_clear(transfers);
            return NB_X11_SELECTION_INCOMING_ERROR;
        }
        incoming->data = resized;
        incoming->capacity = capacity;
    }
    memcpy(incoming->data + incoming->size, data, size);
    incoming->size += size;
    return NB_X11_SELECTION_INCOMING_MORE;
}

bool nb_x11_selection_incoming_data(
    const struct nb_x11_selection_transfers *transfers,
    const void **data,
    size_t *size)
{
    if (transfers == NULL || data == NULL || size == NULL ||
        !transfers->incoming.active || !transfers->incoming.complete) {
        return false;
    }
    *data = transfers->incoming.data;
    *size = transfers->incoming.size;
    return true;
}
