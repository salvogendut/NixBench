#ifndef NIXBENCH_WSCONS_INPUT_H
#define NIXBENCH_WSCONS_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host.h"

enum {
    NB_WSCONS_INPUT_ERROR_CAPACITY = 256
};

/*
 * Portable wscons-event vocabulary used by the reducer and its device-free
 * tests. The NetBSD reader translates the native wscons_event constants into
 * these values before updating the normalized pointer and keyboard state.
 */
enum nb_wscons_raw_event_type {
    NB_WSCONS_RAW_EVENT_NONE,
    NB_WSCONS_RAW_EVENT_KEY_UP,
    NB_WSCONS_RAW_EVENT_KEY_DOWN,
    NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
    NB_WSCONS_RAW_EVENT_MOUSE_UP,
    NB_WSCONS_RAW_EVENT_MOUSE_DOWN,
    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
    NB_WSCONS_RAW_EVENT_MOUSE_ABSOLUTE_X,
    NB_WSCONS_RAW_EVENT_MOUSE_ABSOLUTE_Y,
    NB_WSCONS_RAW_EVENT_ASCII
};

struct nb_wscons_raw_event {
    enum nb_wscons_raw_event_type type;
    int value;
    uint64_t milliseconds;
};

enum nb_wscons_reduce_result {
    NB_WSCONS_REDUCE_ERROR = -1,
    NB_WSCONS_REDUCE_IGNORED,
    NB_WSCONS_REDUCE_EVENT
};

/* Public so the portable reducer can be stack-allocated and tested directly. */
struct nb_wscons_input_reducer {
    int logical_width;
    int logical_height;
    int pointer_x;
    int pointer_y;
    int escape_keycode;
    uint32_t pressed_buttons;
    bool escape_pressed;
};

bool nb_wscons_input_reducer_init(struct nb_wscons_input_reducer *reducer,
                                  int logical_width,
                                  int logical_height);
bool nb_wscons_input_reducer_set_bounds(
    struct nb_wscons_input_reducer *reducer,
    int logical_width,
    int logical_height);
bool nb_wscons_input_reducer_set_escape_keycode(
    struct nb_wscons_input_reducer *reducer,
    int escape_keycode);
void nb_wscons_input_reducer_reset(
    struct nb_wscons_input_reducer *reducer);
bool nb_wscons_input_reducer_get_position(
    const struct nb_wscons_input_reducer *reducer,
    int *x,
    int *y);
enum nb_wscons_reduce_result nb_wscons_input_reducer_apply(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event,
    struct nb_host_event *event);

/*
 * Live input owns only NetBSD's fixed /dev/wskbd and /dev/wsmouse mux aliases.
 * Creation allocates state but does not open devices. resume() opens both;
 * suspend() closes both and clears held input state. Other platforms provide a
 * clean unsupported resume() stub while retaining the portable reducer API.
 */
struct nb_wscons_input;

struct nb_wscons_input *nb_wscons_input_create(int logical_width,
                                               int logical_height);
bool nb_wscons_input_resume(struct nb_wscons_input *input);
void nb_wscons_input_suspend(struct nb_wscons_input *input);
bool nb_wscons_input_is_active(const struct nb_wscons_input *input);
bool nb_wscons_input_set_bounds(struct nb_wscons_input *input,
                                int logical_width,
                                int logical_height);
bool nb_wscons_input_get_position(const struct nb_wscons_input *input,
                                  int *x,
                                  int *y);

/* Nonblocking; returns at most one normalized event per call. */
enum nb_host_event_status nb_wscons_input_poll(
    struct nb_wscons_input *input,
    struct nb_host_event *event);

bool nb_wscons_input_get_last_error(const struct nb_wscons_input *input,
                                    int *system_error,
                                    char *message,
                                    size_t message_size);
void nb_wscons_input_destroy(struct nb_wscons_input *input);

#endif
