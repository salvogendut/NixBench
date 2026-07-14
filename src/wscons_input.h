#ifndef NIXBENCH_WSCONS_INPUT_H
#define NIXBENCH_WSCONS_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host.h"

enum {
    NB_WSCONS_INPUT_ERROR_CAPACITY = 256,
    NB_WSCONS_POINTER_SENSITIVITY_MIN_PERCENT = 25,
    NB_WSCONS_POINTER_SENSITIVITY_DEFAULT_PERCENT = 100,
    NB_WSCONS_POINTER_SENSITIVITY_MAX_PERCENT = 400,
    NB_WSCONS_POINTER_ADAPTIVE_MIN_GAIN_PERCENT = 100,
    NB_WSCONS_POINTER_ADAPTIVE_MAX_GAIN_PERCENT = 250
};

enum nb_wscons_pointer_profile {
    NB_WSCONS_POINTER_PROFILE_FLAT,
    NB_WSCONS_POINTER_PROFILE_ADAPTIVE
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
    /* Monotonic userspace-read time retained for host events and latency. */
    uint64_t milliseconds;
    /* Optional native wscons clock used only for adaptive motion grouping. */
    uint64_t motion_nanoseconds;
    bool motion_nanoseconds_valid;
};

enum nb_wscons_reduce_result {
    NB_WSCONS_REDUCE_ERROR = -1,
    NB_WSCONS_REDUCE_IGNORED,
    NB_WSCONS_REDUCE_EVENT
};

/*
 * Cumulative diagnostics for raw relative pointer input. Normalized host-event
 * time is captured after userspace read with CLOCK_MONOTONIC; native motion
 * time may additionally drive adaptive grouping. Portable reducer tests may
 * supply either timestamp. Distances are unsigned path lengths rather than
 * signed displacement. Counters saturate instead of wrapping.
 */
struct nb_wscons_input_stats {
    uint64_t native_events_read;
    uint64_t untranslated_native_events;
    uint64_t relative_events;
    uint64_t unit_relative_events;
    uint64_t emitted_motion_events;
    uint64_t suppressed_motion_events;
    uint64_t clamped_motion_events;
    uint64_t raw_distance_x;
    uint64_t raw_distance_y;
    uint64_t logical_distance_x;
    uint64_t logical_distance_y;
    uint64_t first_motion_milliseconds;
    uint64_t last_motion_milliseconds;
    uint64_t maximum_motion_gap_milliseconds;
    uint64_t timestamp_regressions;
    uint64_t adaptive_gain_100_events;
    uint64_t adaptive_gain_101_149_events;
    uint64_t adaptive_gain_150_199_events;
    uint64_t adaptive_gain_200_249_events;
    uint64_t adaptive_gain_250_events;
    uint64_t adaptive_peak_filtered_velocity_counts_per_second;
    uint64_t adaptive_peak_gain_percent;
    uint64_t adaptive_idle_resets;
    uint64_t adaptive_timestamp_resets;
    uint64_t adaptive_edge_resets;
    uint64_t adaptive_precision_carry_resets;
    uint64_t adaptive_direction_carry_resets;
    uint64_t adaptive_nonedge_suppressed_events;
    uint64_t adaptive_zero_relative_events;
    uint64_t adaptive_native_timestamp_events;
    uint64_t adaptive_fallback_timestamp_events;
    uint64_t adaptive_motion_groups;
    uint64_t adaptive_same_timestamp_events;
    uint64_t adaptive_clock_source_resets;
};

/* Public so the portable reducer can be stack-allocated and tested directly. */
struct nb_wscons_input_reducer {
    int logical_width;
    int logical_height;
    int pointer_x;
    int pointer_y;
    int escape_keycode;
    uint32_t pressed_buttons;
    enum nb_wscons_pointer_profile pointer_profile;
    unsigned int pointer_sensitivity_percent;
    int pointer_remainder_x;
    int pointer_remainder_y;
    int pointer_direction_x;
    int pointer_direction_y;
    uint64_t adaptive_group_milliseconds;
    uint64_t adaptive_group_motion_nanoseconds;
    uint64_t adaptive_group_distance_x;
    uint64_t adaptive_group_distance_y;
    uint64_t adaptive_filtered_velocity_counts_per_second;
    unsigned int adaptive_gain_percent;
    struct nb_wscons_input_stats stats;
    bool escape_pressed;
    bool adaptive_group_valid;
    bool adaptive_group_uses_native_timestamp;
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
/* Configures flat raw-relative wscons gain, never hosted SDL input. */
bool nb_wscons_input_reducer_set_pointer_sensitivity(
    struct nb_wscons_input_reducer *reducer,
    unsigned int sensitivity_percent);
/* Selects flat scaling or the deterministic velocity-sensitive profile. */
bool nb_wscons_input_reducer_set_pointer_profile(
    struct nb_wscons_input_reducer *reducer,
    enum nb_wscons_pointer_profile profile);
void nb_wscons_input_reducer_reset(
    struct nb_wscons_input_reducer *reducer);
bool nb_wscons_input_reducer_get_position(
    const struct nb_wscons_input_reducer *reducer,
    int *x,
    int *y);
bool nb_wscons_input_reducer_get_stats(
    const struct nb_wscons_input_reducer *reducer,
    struct nb_wscons_input_stats *stats);
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
/* Flat-profile sensitivity may change only while the provider is inactive. */
bool nb_wscons_input_set_pointer_sensitivity(
    struct nb_wscons_input *input,
    unsigned int sensitivity_percent);
/* The profile may be changed only while the live provider is inactive. */
bool nb_wscons_input_set_pointer_profile(
    struct nb_wscons_input *input,
    enum nb_wscons_pointer_profile profile);
bool nb_wscons_input_get_position(const struct nb_wscons_input *input,
                                  int *x,
                                  int *y);
bool nb_wscons_input_get_stats(const struct nb_wscons_input *input,
                               struct nb_wscons_input_stats *stats);

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
