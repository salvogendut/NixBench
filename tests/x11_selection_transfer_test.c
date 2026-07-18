#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "x11_selection_transfer.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void fill_pattern(unsigned char *data, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        data[index] = (unsigned char)(index * 31U + 7U);
    }
}

static void test_outgoing_chunks_and_snapshot(void)
{
    const size_t payload_size =
        NB_X11_SELECTION_TRANSFER_CHUNK_BYTES * 2U + 19U;
    struct nb_x11_selection_transfers *transfers =
        nb_x11_selection_transfers_create();
    unsigned char *payload = malloc(payload_size);
    unsigned char *expected = malloc(payload_size);
    size_t copied = 0;
    unsigned int calls = 0;

    CHECK(transfers != NULL);
    CHECK(payload != NULL);
    CHECK(expected != NULL);
    if (transfers == NULL || payload == NULL || expected == NULL) {
        free(payload);
        free(expected);
        nb_x11_selection_transfers_destroy(transfers);
        return;
    }
    fill_pattern(payload, payload_size);
    memcpy(expected, payload, payload_size);
    CHECK(nb_x11_selection_outgoing_start(transfers,
                                          10,
                                          20,
                                          30,
                                          40,
                                          50,
                                          payload,
                                          payload_size));
    memset(payload, 0, payload_size);
    CHECK(nb_x11_selection_outgoing_count(transfers) == 1);
    for (;;) {
        uint32_t property_type = 0;
        const void *chunk = (const void *)(uintptr_t)1;
        size_t chunk_size = SIZE_MAX;
        bool complete = false;

        CHECK(nb_x11_selection_outgoing_next(transfers,
                                             10,
                                             20,
                                             &property_type,
                                             &chunk,
                                             &chunk_size,
                                             &complete));
        CHECK(property_type == 50);
        ++calls;
        if (complete) {
            CHECK(chunk == NULL);
            CHECK(chunk_size == 0);
            break;
        }
        CHECK(chunk_size <= NB_X11_SELECTION_TRANSFER_CHUNK_BYTES);
        CHECK(copied + chunk_size <= payload_size);
        CHECK(memcmp(chunk, expected + copied, chunk_size) == 0);
        copied += chunk_size;
    }
    CHECK(copied == payload_size);
    CHECK(calls == 4);
    CHECK(nb_x11_selection_outgoing_count(transfers) == 0);
    free(payload);
    free(expected);
    nb_x11_selection_transfers_destroy(transfers);
}

static void test_outgoing_limits_and_cancellation(void)
{
    struct nb_x11_selection_transfers *transfers =
        nb_x11_selection_transfers_create();
    unsigned char payload[NB_X11_SELECTION_TRANSFER_CHUNK_BYTES + 1U];
    size_t index;

    memset(payload, 'x', sizeof(payload));
    CHECK(transfers != NULL);
    if (transfers == NULL) {
        return;
    }
    CHECK(!nb_x11_selection_outgoing_start(
        transfers, 1, 2, 3, 4, 5, payload,
        NB_X11_SELECTION_TRANSFER_CHUNK_BYTES));
    CHECK(!nb_x11_selection_outgoing_start(
        transfers, 1, 2, 3, 4, 5, payload,
        NB_X11_SELECTION_TRANSFER_MAX_BYTES + 1U));
    for (index = 0; index < NB_X11_SELECTION_TRANSFER_CAPACITY; ++index) {
        CHECK(nb_x11_selection_outgoing_start(transfers,
                                              (uint32_t)(100U + index / 2U),
                                              (uint32_t)(200U + index),
                                              3,
                                              4,
                                              5,
                                              payload,
                                              sizeof(payload)));
    }
    CHECK(nb_x11_selection_outgoing_count(transfers) ==
          NB_X11_SELECTION_TRANSFER_CAPACITY);
    CHECK(!nb_x11_selection_outgoing_start(transfers,
                                           999,
                                           999,
                                           3,
                                           4,
                                           5,
                                           payload,
                                           sizeof(payload)));
    CHECK(!nb_x11_selection_outgoing_start(transfers,
                                           100,
                                           200,
                                           3,
                                           4,
                                           5,
                                           payload,
                                           sizeof(payload)));
    nb_x11_selection_outgoing_cancel_requestor(transfers, 100);
    CHECK(nb_x11_selection_outgoing_count(transfers) ==
          NB_X11_SELECTION_TRANSFER_CAPACITY - 2U);
    nb_x11_selection_outgoing_cancel(transfers, 101, 202);
    CHECK(nb_x11_selection_outgoing_count(transfers) ==
          NB_X11_SELECTION_TRANSFER_CAPACITY - 3U);
    nb_x11_selection_transfers_destroy(transfers);
}

static void test_incoming_chunks(void)
{
    struct nb_x11_selection_transfers *transfers =
        nb_x11_selection_transfers_create();
    unsigned char first[NB_X11_SELECTION_TRANSFER_CHUNK_BYTES];
    unsigned char second[NB_X11_SELECTION_TRANSFER_CHUNK_BYTES + 23U];
    const void *data = NULL;
    size_t size = 0;

    fill_pattern(first, sizeof(first));
    fill_pattern(second, sizeof(second));
    CHECK(transfers != NULL);
    if (transfers == NULL) {
        return;
    }
    CHECK(nb_x11_selection_incoming_begin(transfers, 12));
    CHECK(!nb_x11_selection_incoming_begin(transfers, 12));
    CHECK(nb_x11_selection_incoming_append(transfers,
                                           first,
                                           sizeof(first)) ==
          NB_X11_SELECTION_INCOMING_MORE);
    CHECK(nb_x11_selection_incoming_append(transfers,
                                           second,
                                           sizeof(second)) ==
          NB_X11_SELECTION_INCOMING_MORE);
    CHECK(!nb_x11_selection_incoming_data(transfers, &data, &size));
    CHECK(nb_x11_selection_incoming_append(transfers, NULL, 0) ==
          NB_X11_SELECTION_INCOMING_COMPLETE);
    CHECK(nb_x11_selection_incoming_data(transfers, &data, &size));
    CHECK(size == sizeof(first) + sizeof(second));
    CHECK(memcmp(data, first, sizeof(first)) == 0);
    CHECK(memcmp((const unsigned char *)data + sizeof(first),
                 second,
                 sizeof(second)) == 0);
    CHECK(nb_x11_selection_incoming_append(transfers, NULL, 0) ==
          NB_X11_SELECTION_INCOMING_ERROR);
    nb_x11_selection_incoming_clear(transfers);
    CHECK(!nb_x11_selection_incoming_data(transfers, &data, &size));
    nb_x11_selection_transfers_destroy(transfers);
}

static void test_incoming_limit_and_recovery(void)
{
    struct nb_x11_selection_transfers *transfers =
        nb_x11_selection_transfers_create();
    unsigned char *maximum =
        malloc(NB_X11_SELECTION_TRANSFER_MAX_BYTES);
    const unsigned char extra = 1;

    CHECK(transfers != NULL);
    CHECK(maximum != NULL);
    if (transfers == NULL || maximum == NULL) {
        free(maximum);
        nb_x11_selection_transfers_destroy(transfers);
        return;
    }
    memset(maximum, 'm', NB_X11_SELECTION_TRANSFER_MAX_BYTES);
    CHECK(!nb_x11_selection_incoming_begin(
        transfers, NB_X11_SELECTION_TRANSFER_MAX_BYTES + 1U));
    CHECK(nb_x11_selection_incoming_begin(transfers, 0));
    CHECK(nb_x11_selection_incoming_append(
              transfers,
              maximum,
              NB_X11_SELECTION_TRANSFER_MAX_BYTES) ==
          NB_X11_SELECTION_INCOMING_MORE);
    CHECK(nb_x11_selection_incoming_append(transfers, &extra, 1) ==
          NB_X11_SELECTION_INCOMING_ERROR);
    CHECK(nb_x11_selection_incoming_begin(transfers, 1));
    nb_x11_selection_incoming_clear(transfers);
    free(maximum);
    nb_x11_selection_transfers_destroy(transfers);
}

static void test_defensive_arguments(void)
{
    const unsigned char payload[NB_X11_SELECTION_TRANSFER_CHUNK_BYTES + 1U] =
        {0};
    const void *data;
    size_t size;

    CHECK(nb_x11_selection_outgoing_count(NULL) == 0);
    CHECK(!nb_x11_selection_outgoing_start(NULL,
                                           1,
                                           2,
                                           3,
                                           4,
                                           5,
                                           payload,
                                           sizeof(payload)));
    CHECK(!nb_x11_selection_incoming_begin(NULL, 0));
    CHECK(nb_x11_selection_incoming_append(NULL, NULL, 0) ==
          NB_X11_SELECTION_INCOMING_ERROR);
    CHECK(!nb_x11_selection_incoming_data(NULL, &data, &size));
    nb_x11_selection_outgoing_cancel(NULL, 1, 2);
    nb_x11_selection_outgoing_cancel_requestor(NULL, 1);
    nb_x11_selection_incoming_clear(NULL);
    nb_x11_selection_transfers_destroy(NULL);
}

int main(void)
{
    test_outgoing_chunks_and_snapshot();
    test_outgoing_limits_and_cancellation();
    test_incoming_chunks();
    test_incoming_limit_and_recovery();
    test_defensive_arguments();

    if (failures != 0) {
        fprintf(stderr, "X11 selection transfer tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("X11 selection transfer tests: ok");
    return 0;
}
