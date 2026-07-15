#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "wscons_input.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__NetBSD__)
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsksymdef.h>

#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#endif

struct nb_wscons_input {
    struct nb_wscons_input_reducer reducer;
    int keyboard_fd;
    int mouse_fd;
    int system_error;
    char error[NB_WSCONS_INPUT_ERROR_CAPACITY];
    bool active;
    bool prefer_mouse;
};

enum {
    NB_WSCONS_ADAPTIVE_IDLE_RESET_MILLISECONDS = 100,
    NB_WSCONS_ADAPTIVE_IDLE_RESET_NANOSECONDS = 100000000,
    NB_WSCONS_NANOSECONDS_PER_SECOND = 1000000000,
    NB_WSCONS_ADAPTIVE_PRECISION_VELOCITY = 400,
    NB_WSCONS_ADAPTIVE_LOW_VELOCITY = 750,
    NB_WSCONS_ADAPTIVE_HIGH_VELOCITY = 1500,
    NB_WSCONS_ADAPTIVE_MAX_GAIN_VELOCITY = 2500,
    NB_WSCONS_ADAPTIVE_VELOCITY_LIMIT =
        NB_WSCONS_ADAPTIVE_MAX_GAIN_VELOCITY
};

_Static_assert(NB_WSCONS_CONTROL_KEY_COUNT <= 32,
               "control-key pending releases must fit in one mask");

static bool control_key_is_valid(enum nb_wscons_control_key key)
{
    return key >= NB_WSCONS_CONTROL_KEY_ESCAPE &&
           key < NB_WSCONS_CONTROL_KEY_COUNT;
}

static const char *control_key_name(enum nb_wscons_control_key key)
{
    static const char *const names[NB_WSCONS_CONTROL_KEY_COUNT] = {
        [NB_WSCONS_CONTROL_KEY_ESCAPE] = "ESC",
        [NB_WSCONS_CONTROL_KEY_F10] = "FK10",
        [NB_WSCONS_CONTROL_KEY_UP] = "UP",
        [NB_WSCONS_CONTROL_KEY_DOWN] = "DOWN",
        [NB_WSCONS_CONTROL_KEY_LEFT] = "LEFT",
        [NB_WSCONS_CONTROL_KEY_RIGHT] = "RGHT",
        [NB_WSCONS_CONTROL_KEY_RETURN] = "RTRN",
        [NB_WSCONS_CONTROL_KEY_KP_ENTER] = "KPEN"
    };

    return control_key_is_valid(key) ? names[key] : NULL;
}

static bool control_key_state_is_valid(
    const struct nb_wscons_input_reducer *reducer)
{
    uint32_t valid_mask = 0;
    size_t configured_count = 0;
    size_t index;

    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        const struct nb_wscons_control_key_state *state =
            &reducer->control_keys[index];
        size_t other;

        if (state->keycode < -1 ||
            (state->pressed && state->keycode < 0 &&
             index != NB_WSCONS_CONTROL_KEY_ESCAPE)) {
            return false;
        }
        if (state->keycode >= 0) {
            ++configured_count;
            for (other = 0; other < index; ++other) {
                if (reducer->control_keys[other].keycode == state->keycode) {
                    return false;
                }
            }
        }
        if ((reducer->pending_key_release_mask &
             (UINT32_C(1) << (unsigned int)index)) != 0 &&
            (state->pressed ||
             (state->keycode < 0 &&
              index != NB_WSCONS_CONTROL_KEY_ESCAPE))) {
            return false;
        }
        valid_mask |= UINT32_C(1) << (unsigned int)index;
    }
    return (reducer->pending_key_release_mask & ~valid_mask) == 0 &&
           (reducer->pending_key_release_mask != 0 ||
            reducer->pending_key_release_milliseconds == 0) &&
           reducer->stats.keyboard_binding_count ==
               (uint64_t)configured_count;
}

static void reset_adaptive_motion(
    struct nb_wscons_input_reducer *reducer)
{
    reducer->adaptive_group_milliseconds = 0;
    reducer->adaptive_group_motion_nanoseconds = 0;
    reducer->adaptive_group_distance_x = 0;
    reducer->adaptive_group_distance_y = 0;
    reducer->adaptive_filtered_velocity_counts_per_second = 0;
    reducer->adaptive_gain_percent =
        NB_WSCONS_POINTER_ADAPTIVE_MIN_GAIN_PERCENT;
    reducer->adaptive_group_valid = false;
    reducer->adaptive_group_uses_native_timestamp = false;
    reducer->pointer_remainder_x = 0;
    reducer->pointer_remainder_y = 0;
    reducer->pointer_direction_x = 0;
    reducer->pointer_direction_y = 0;
}

static bool reducer_is_valid(const struct nb_wscons_input_reducer *reducer)
{
    return reducer != NULL && reducer->logical_width > 0 &&
           reducer->logical_height > 0 && reducer->pointer_x >= 0 &&
           reducer->pointer_x < reducer->logical_width &&
           reducer->pointer_y >= 0 &&
           reducer->pointer_y < reducer->logical_height &&
           control_key_state_is_valid(reducer) &&
           (reducer->pointer_profile == NB_WSCONS_POINTER_PROFILE_FLAT ||
            reducer->pointer_profile ==
                NB_WSCONS_POINTER_PROFILE_ADAPTIVE) &&
           reducer->pointer_sensitivity_percent >=
               NB_WSCONS_POINTER_SENSITIVITY_MIN_PERCENT &&
           reducer->pointer_sensitivity_percent <=
               NB_WSCONS_POINTER_SENSITIVITY_MAX_PERCENT &&
           reducer->pointer_remainder_x > -100 &&
           reducer->pointer_remainder_x < 100 &&
           reducer->pointer_remainder_y > -100 &&
           reducer->pointer_remainder_y < 100 &&
           reducer->pointer_direction_x >= -1 &&
           reducer->pointer_direction_x <= 1 &&
           reducer->pointer_direction_y >= -1 &&
           reducer->pointer_direction_y <= 1 &&
           reducer->adaptive_filtered_velocity_counts_per_second <=
               NB_WSCONS_ADAPTIVE_VELOCITY_LIMIT &&
           reducer->adaptive_gain_percent >=
               NB_WSCONS_POINTER_ADAPTIVE_MIN_GAIN_PERCENT &&
           reducer->adaptive_gain_percent <=
               NB_WSCONS_POINTER_ADAPTIVE_MAX_GAIN_PERCENT;
}

static int clamp_coordinate(int coordinate, int extent)
{
    if (coordinate < 0) {
        return 0;
    }
    return coordinate >= extent ? extent - 1 : coordinate;
}

bool nb_wscons_input_reducer_init(struct nb_wscons_input_reducer *reducer,
                                  int logical_width,
                                  int logical_height)
{
    if (reducer == NULL || logical_width <= 0 || logical_height <= 0) {
        return false;
    }
    memset(reducer, 0, sizeof(*reducer));
    reducer->logical_width = logical_width;
    reducer->logical_height = logical_height;
    reducer->pointer_x = (logical_width - 1) / 2;
    reducer->pointer_y = (logical_height - 1) / 2;
    {
        size_t index;

        for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
            reducer->control_keys[index].keycode = -1;
        }
    }
    reducer->pointer_profile = NB_WSCONS_POINTER_PROFILE_FLAT;
    reducer->pointer_sensitivity_percent =
        NB_WSCONS_POINTER_SENSITIVITY_DEFAULT_PERCENT;
    reset_adaptive_motion(reducer);
    return true;
}

bool nb_wscons_input_reducer_set_bounds(
    struct nb_wscons_input_reducer *reducer,
    int logical_width,
    int logical_height)
{
    if (!reducer_is_valid(reducer) || logical_width <= 0 ||
        logical_height <= 0) {
        return false;
    }
    reducer->logical_width = logical_width;
    reducer->logical_height = logical_height;
    reducer->pointer_x = clamp_coordinate(reducer->pointer_x, logical_width);
    reducer->pointer_y = clamp_coordinate(reducer->pointer_y, logical_height);
    reducer->pointer_remainder_x = 0;
    reducer->pointer_remainder_y = 0;
    reset_adaptive_motion(reducer);
    return true;
}

bool nb_wscons_input_reducer_set_escape_keycode(
    struct nb_wscons_input_reducer *reducer,
    int escape_keycode)
{
    return nb_wscons_input_reducer_set_control_keycode(
        reducer,
        NB_WSCONS_CONTROL_KEY_ESCAPE,
        escape_keycode);
}

bool nb_wscons_input_reducer_set_control_keycode(
    struct nb_wscons_input_reducer *reducer,
    enum nb_wscons_control_key key,
    int keycode)
{
    size_t index;
    bool configured;

    if (!reducer_is_valid(reducer) || !control_key_is_valid(key) ||
        keycode < 0 || reducer->pending_key_release_mask != 0) {
        return false;
    }
    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        if (reducer->control_keys[index].pressed) {
            return false;
        }
        if (index != (size_t)key &&
            reducer->control_keys[index].keycode == keycode) {
            return false;
        }
    }
    configured = reducer->control_keys[(size_t)key].keycode >= 0;
    reducer->control_keys[(size_t)key].keycode = keycode;
    if (!configured) {
        ++reducer->stats.keyboard_binding_count;
    }
    return true;
}

bool nb_wscons_input_reducer_set_pointer_sensitivity(
    struct nb_wscons_input_reducer *reducer,
    unsigned int sensitivity_percent)
{
    if (!reducer_is_valid(reducer) ||
        sensitivity_percent < NB_WSCONS_POINTER_SENSITIVITY_MIN_PERCENT ||
        sensitivity_percent > NB_WSCONS_POINTER_SENSITIVITY_MAX_PERCENT) {
        return false;
    }
    reducer->pointer_sensitivity_percent = sensitivity_percent;
    reducer->pointer_remainder_x = 0;
    reducer->pointer_remainder_y = 0;
    reset_adaptive_motion(reducer);
    return true;
}

bool nb_wscons_input_reducer_set_pointer_profile(
    struct nb_wscons_input_reducer *reducer,
    enum nb_wscons_pointer_profile profile)
{
    if (!reducer_is_valid(reducer) ||
        (profile != NB_WSCONS_POINTER_PROFILE_FLAT &&
         profile != NB_WSCONS_POINTER_PROFILE_ADAPTIVE)) {
        return false;
    }
    reducer->pointer_profile = profile;
    reducer->pointer_remainder_x = 0;
    reducer->pointer_remainder_y = 0;
    reset_adaptive_motion(reducer);
    return true;
}

void nb_wscons_input_reducer_reset(
    struct nb_wscons_input_reducer *reducer)
{
    if (reducer == NULL) {
        return;
    }
    reducer->pressed_buttons = 0;
    {
        size_t index;

        for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
            reducer->control_keys[index].pressed = false;
        }
    }
    reducer->pending_key_release_mask = 0;
    reducer->pending_key_release_milliseconds = 0;
    reducer->pointer_remainder_x = 0;
    reducer->pointer_remainder_y = 0;
    reset_adaptive_motion(reducer);
}

bool nb_wscons_input_reducer_get_position(
    const struct nb_wscons_input_reducer *reducer,
    int *x,
    int *y)
{
    if (!reducer_is_valid(reducer) || x == NULL || y == NULL) {
        return false;
    }
    *x = reducer->pointer_x;
    *y = reducer->pointer_y;
    return true;
}

bool nb_wscons_input_reducer_get_stats(
    const struct nb_wscons_input_reducer *reducer,
    struct nb_wscons_input_stats *stats)
{
    if (!reducer_is_valid(reducer) || stats == NULL) {
        return false;
    }
    *stats = reducer->stats;
    return true;
}

static int moved_coordinate(int coordinate, int64_t delta, int extent)
{
    int64_t moved = (int64_t)coordinate + delta;

    if (moved < 0) {
        moved = 0;
    } else if (moved >= extent) {
        moved = extent - 1;
    }
    return (int)moved;
}

static void saturating_increment(uint64_t *value)
{
    if (*value != UINT64_MAX) {
        ++*value;
    }
}

static void saturating_add(uint64_t *value, uint64_t amount)
{
    *value = amount > UINT64_MAX - *value
                 ? UINT64_MAX
                 : *value + amount;
}

static uint64_t integer_magnitude(int value)
{
    return value < 0 ? (uint64_t)(-(int64_t)value) : (uint64_t)value;
}

static uint64_t signed_distance(int64_t value)
{
    return value < 0
               ? (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1)
               : (uint64_t)value;
}

/*
 * X and Y arrive as separate wscons events. Matching native wscons timestamps
 * form one timestamp bucket; getnanotime(9) can stamp separate injections in
 * the same kernel tick identically, so a bucket is not a physical-report
 * boundary. Portable and invalid-native events retain userspace read
 * milliseconds as a fallback. Once a later bucket starts, the completed
 * bucket's approximate vector length and elapsed time update the velocity used
 * by the new bucket. This deliberate one-bucket delay avoids axis-order bias
 * without buffering host events.
 */

static uint64_t approximate_group_distance(
    const struct nb_wscons_input_reducer *reducer)
{
    const uint64_t maximum =
        reducer->adaptive_group_distance_x >=
                reducer->adaptive_group_distance_y
            ? reducer->adaptive_group_distance_x
            : reducer->adaptive_group_distance_y;
    const uint64_t minimum =
        reducer->adaptive_group_distance_x <
                reducer->adaptive_group_distance_y
            ? reducer->adaptive_group_distance_x
            : reducer->adaptive_group_distance_y;
    const uint64_t half_minimum = minimum / UINT64_C(2);

    return half_minimum > UINT64_MAX - maximum
               ? UINT64_MAX
               : maximum + half_minimum;
}

static uint64_t adaptive_group_velocity(
    const struct nb_wscons_input_reducer *reducer,
    uint64_t elapsed,
    uint64_t ticks_per_second)
{
    const uint64_t distance = approximate_group_distance(reducer);
    uint64_t scaled_distance;
    uint64_t velocity;

    if (elapsed == 0 || ticks_per_second == 0) {
        return 0;
    }
    if (distance > UINT64_MAX / ticks_per_second) {
        return NB_WSCONS_ADAPTIVE_VELOCITY_LIMIT;
    }
    scaled_distance = distance * ticks_per_second;
    velocity = scaled_distance / elapsed;
    return velocity > NB_WSCONS_ADAPTIVE_VELOCITY_LIMIT
               ? NB_WSCONS_ADAPTIVE_VELOCITY_LIMIT
               : velocity;
}

static uint64_t filter_adaptive_velocity(uint64_t previous,
                                         uint64_t instantaneous)
{
    if (instantaneous > previous) {
        return previous +
               (instantaneous - previous + UINT64_C(3)) / UINT64_C(4);
    }
    if (instantaneous < previous) {
        return previous -
               (previous - instantaneous + UINT64_C(3)) / UINT64_C(4);
    }
    return previous;
}

static unsigned int interpolate_gain(uint64_t velocity,
                                     uint64_t lower_velocity,
                                     unsigned int lower_gain,
                                     uint64_t upper_velocity,
                                     unsigned int upper_gain)
{
    return lower_gain +
           (unsigned int)((velocity - lower_velocity) *
                          (uint64_t)(upper_gain - lower_gain) /
                          (upper_velocity - lower_velocity));
}

static unsigned int gain_for_velocity(uint64_t velocity)
{
    if (velocity <= NB_WSCONS_ADAPTIVE_PRECISION_VELOCITY) {
        return 100;
    }
    if (velocity <= NB_WSCONS_ADAPTIVE_LOW_VELOCITY) {
        return interpolate_gain(velocity,
                                NB_WSCONS_ADAPTIVE_PRECISION_VELOCITY,
                                100,
                                NB_WSCONS_ADAPTIVE_LOW_VELOCITY,
                                150);
    }
    if (velocity <= NB_WSCONS_ADAPTIVE_HIGH_VELOCITY) {
        return interpolate_gain(velocity,
                                NB_WSCONS_ADAPTIVE_LOW_VELOCITY,
                                150,
                                NB_WSCONS_ADAPTIVE_HIGH_VELOCITY,
                                200);
    }
    if (velocity < NB_WSCONS_ADAPTIVE_MAX_GAIN_VELOCITY) {
        return interpolate_gain(velocity,
                                NB_WSCONS_ADAPTIVE_HIGH_VELOCITY,
                                200,
                                NB_WSCONS_ADAPTIVE_MAX_GAIN_VELOCITY,
                                250);
    }
    return NB_WSCONS_POINTER_ADAPTIVE_MAX_GAIN_PERCENT;
}

static void clear_precision_carry(
    struct nb_wscons_input_reducer *reducer)
{
    if (reducer->pointer_remainder_x != 0) {
        reducer->pointer_remainder_x = 0;
        saturating_increment(
            &reducer->stats.adaptive_precision_carry_resets);
    }
    if (reducer->pointer_remainder_y != 0) {
        reducer->pointer_remainder_y = 0;
        saturating_increment(
            &reducer->stats.adaptive_precision_carry_resets);
    }
}

static void begin_adaptive_group(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event)
{
    reducer->adaptive_group_milliseconds =
        raw_event->motion_nanoseconds_valid ? 0
                                            : raw_event->milliseconds;
    reducer->adaptive_group_motion_nanoseconds =
        raw_event->motion_nanoseconds_valid
            ? raw_event->motion_nanoseconds
            : 0;
    reducer->adaptive_group_distance_x = 0;
    reducer->adaptive_group_distance_y = 0;
    reducer->adaptive_group_valid = true;
    reducer->adaptive_group_uses_native_timestamp =
        raw_event->motion_nanoseconds_valid;
    saturating_increment(&reducer->stats.adaptive_motion_groups);
}

static unsigned int prepare_adaptive_gain(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event)
{
    struct nb_wscons_input_stats *stats = &reducer->stats;
    const bool uses_native_timestamp =
        raw_event->motion_nanoseconds_valid;
    const uint64_t timestamp = uses_native_timestamp
                                   ? raw_event->motion_nanoseconds
                                   : raw_event->milliseconds;
    const uint64_t ticks_per_second = uses_native_timestamp
                                          ? NB_WSCONS_NANOSECONDS_PER_SECOND
                                          : UINT64_C(1000);
    const uint64_t idle_reset_ticks =
        uses_native_timestamp
            ? NB_WSCONS_ADAPTIVE_IDLE_RESET_NANOSECONDS
            : NB_WSCONS_ADAPTIVE_IDLE_RESET_MILLISECONDS;
    uint64_t group_timestamp;

    saturating_increment(uses_native_timestamp
                             ? &stats->adaptive_native_timestamp_events
                             : &stats->adaptive_fallback_timestamp_events);

    if (!reducer->adaptive_group_valid) {
        begin_adaptive_group(reducer, raw_event);
        return reducer->adaptive_gain_percent;
    }
    if (reducer->adaptive_group_uses_native_timestamp !=
        uses_native_timestamp) {
        saturating_increment(&stats->adaptive_clock_source_resets);
        reset_adaptive_motion(reducer);
        begin_adaptive_group(reducer, raw_event);
        return reducer->adaptive_gain_percent;
    }
    group_timestamp = uses_native_timestamp
                          ? reducer->adaptive_group_motion_nanoseconds
                          : reducer->adaptive_group_milliseconds;
    if (timestamp < group_timestamp) {
        saturating_increment(&stats->adaptive_timestamp_resets);
        reset_adaptive_motion(reducer);
        begin_adaptive_group(reducer, raw_event);
        return reducer->adaptive_gain_percent;
    }
    if (timestamp == group_timestamp) {
        saturating_increment(&stats->adaptive_same_timestamp_events);
        return reducer->adaptive_gain_percent;
    }
    {
        const uint64_t elapsed = timestamp - group_timestamp;

        if (elapsed >= idle_reset_ticks) {
            saturating_increment(&stats->adaptive_idle_resets);
            reset_adaptive_motion(reducer);
        } else {
            const uint64_t instantaneous =
                adaptive_group_velocity(reducer,
                                        elapsed,
                                        ticks_per_second);
            const unsigned int previous_gain =
                reducer->adaptive_gain_percent;

            reducer->adaptive_filtered_velocity_counts_per_second =
                filter_adaptive_velocity(
                    reducer->adaptive_filtered_velocity_counts_per_second,
                    instantaneous);
            reducer->adaptive_gain_percent = gain_for_velocity(
                reducer->adaptive_filtered_velocity_counts_per_second);
            if (previous_gain >
                    NB_WSCONS_POINTER_ADAPTIVE_MIN_GAIN_PERCENT &&
                reducer->adaptive_gain_percent ==
                    NB_WSCONS_POINTER_ADAPTIVE_MIN_GAIN_PERCENT) {
                clear_precision_carry(reducer);
            }
            if (reducer->adaptive_filtered_velocity_counts_per_second >
                stats->adaptive_peak_filtered_velocity_counts_per_second) {
                stats->adaptive_peak_filtered_velocity_counts_per_second =
                    reducer->adaptive_filtered_velocity_counts_per_second;
            }
        }
        begin_adaptive_group(reducer, raw_event);
    }
    return reducer->adaptive_gain_percent;
}

static void accumulate_adaptive_distance(
    struct nb_wscons_input_reducer *reducer,
    bool horizontal,
    uint64_t magnitude)
{
    saturating_add(horizontal
                       ? &reducer->adaptive_group_distance_x
                       : &reducer->adaptive_group_distance_y,
                   magnitude);
}

static void record_adaptive_gain(struct nb_wscons_input_reducer *reducer,
                                 unsigned int gain_percent)
{
    struct nb_wscons_input_stats *stats = &reducer->stats;
    uint64_t *bucket;

    if (gain_percent == 100) {
        bucket = &stats->adaptive_gain_100_events;
    } else if (gain_percent < 150) {
        bucket = &stats->adaptive_gain_101_149_events;
    } else if (gain_percent < 200) {
        bucket = &stats->adaptive_gain_150_199_events;
    } else if (gain_percent < 250) {
        bucket = &stats->adaptive_gain_200_249_events;
    } else {
        bucket = &stats->adaptive_gain_250_events;
    }
    saturating_increment(bucket);
    if ((uint64_t)gain_percent > stats->adaptive_peak_gain_percent) {
        stats->adaptive_peak_gain_percent = gain_percent;
    }
}

static void record_relative_event(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event,
    bool horizontal)
{
    struct nb_wscons_input_stats *stats = &reducer->stats;
    const uint64_t magnitude = integer_magnitude(raw_event->value);

    if (stats->relative_events == 0) {
        stats->first_motion_milliseconds = raw_event->milliseconds;
    } else if (raw_event->milliseconds < stats->last_motion_milliseconds) {
        saturating_increment(&stats->timestamp_regressions);
    } else {
        const uint64_t gap =
            raw_event->milliseconds - stats->last_motion_milliseconds;

        if (gap > stats->maximum_motion_gap_milliseconds) {
            stats->maximum_motion_gap_milliseconds = gap;
        }
    }
    stats->last_motion_milliseconds = raw_event->milliseconds;
    saturating_increment(&stats->relative_events);
    if (reducer->pointer_profile ==
            NB_WSCONS_POINTER_PROFILE_ADAPTIVE &&
        raw_event->value == 0) {
        saturating_increment(&stats->adaptive_zero_relative_events);
    }
    if (magnitude == 1) {
        saturating_increment(&stats->unit_relative_events);
    }
    saturating_add(horizontal ? &stats->raw_distance_x
                              : &stats->raw_distance_y,
                   magnitude);
}

static int64_t scaled_pointer_delta(int raw_delta,
                                    unsigned int sensitivity_percent,
                                    int *remainder)
{
    const int64_t numerator =
        (int64_t)raw_delta * (int64_t)sensitivity_percent +
        (int64_t)*remainder;
    const int64_t scaled = numerator / INT64_C(100);

    *remainder = (int)(numerator % INT64_C(100));
    return scaled;
}

static void prepare_adaptive_direction(
    struct nb_wscons_input_reducer *reducer,
    int raw_delta,
    int *remainder,
    int *direction)
{
    const int current_direction = raw_delta < 0 ? -1
                                  : raw_delta > 0 ? 1
                                                  : 0;

    if (current_direction == 0) {
        return;
    }
    if (*direction != 0 && *direction != current_direction &&
        *remainder != 0) {
        *remainder = 0;
        saturating_increment(
            &reducer->stats.adaptive_direction_carry_resets);
    }
    *direction = current_direction;
}

static void set_pointer_motion_event(
    const struct nb_wscons_input_reducer *reducer,
    uint64_t milliseconds,
    struct nb_host_event *event)
{
    event->type = NB_HOST_EVENT_POINTER_MOTION;
    event->milliseconds = milliseconds;
    event->data.pointer_motion.x = reducer->pointer_x;
    event->data.pointer_motion.y = reducer->pointer_y;
}

static void set_escape_event(bool pressed,
                             bool repeat,
                             uint64_t milliseconds,
                             struct nb_host_event *event)
{
    const char *const name =
        control_key_name(NB_WSCONS_CONTROL_KEY_ESCAPE);

    event->type = NB_HOST_EVENT_KEY;
    event->milliseconds = milliseconds;
    (void)snprintf(event->data.key.xkb_key_name,
                   sizeof(event->data.key.xkb_key_name),
                   "%s",
                   name);
    event->data.key.pressed = pressed;
    event->data.key.repeat = repeat;
}

static void set_control_key_event(enum nb_wscons_control_key key,
                                  bool pressed,
                                  bool repeat,
                                  uint64_t milliseconds,
                                  struct nb_host_event *event)
{
    const char *const name = control_key_name(key);

    event->type = NB_HOST_EVENT_KEY;
    event->milliseconds = milliseconds;
    (void)snprintf(event->data.key.xkb_key_name,
                   sizeof(event->data.key.xkb_key_name),
                   "%s",
                   name);
    event->data.key.pressed = pressed;
    event->data.key.repeat = repeat;
}

static void record_emitted_key(struct nb_wscons_input_reducer *reducer,
                               bool repeat)
{
    saturating_increment(&reducer->stats.keyboard_emitted_events);
    if (repeat) {
        saturating_increment(&reducer->stats.keyboard_repeat_events);
    }
}

enum nb_wscons_reduce_result nb_wscons_input_reducer_poll_pending(
    struct nb_wscons_input_reducer *reducer,
    struct nb_host_event *event)
{
    size_t index;

    if (event != NULL) {
        memset(event, 0, sizeof(*event));
    }
    if (!reducer_is_valid(reducer) || event == NULL) {
        return NB_WSCONS_REDUCE_ERROR;
    }
    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        const uint32_t mask = UINT32_C(1) << (unsigned int)index;

        if ((reducer->pending_key_release_mask & mask) == 0) {
            continue;
        }
        reducer->pending_key_release_mask &= ~mask;
        set_control_key_event((enum nb_wscons_control_key)index,
                              false,
                              false,
                              reducer->pending_key_release_milliseconds,
                              event);
        record_emitted_key(reducer, false);
        saturating_increment(
            &reducer->stats.keyboard_synthesized_release_events);
        if (reducer->pending_key_release_mask == 0) {
            reducer->pending_key_release_milliseconds = 0;
        }
        return NB_WSCONS_REDUCE_EVENT;
    }
    return NB_WSCONS_REDUCE_IGNORED;
}

static enum nb_wscons_reduce_result reduce_all_keys_up(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event,
    struct nb_host_event *event)
{
    size_t index;

    saturating_increment(&reducer->stats.keyboard_all_keys_up_events);
    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        struct nb_wscons_control_key_state *state =
            &reducer->control_keys[index];

        if (state->pressed) {
            reducer->pending_key_release_mask |=
                UINT32_C(1) << (unsigned int)index;
            state->pressed = false;
        }
    }
    if (reducer->pending_key_release_mask == 0) {
        saturating_increment(&reducer->stats.keyboard_ignored_events);
        return NB_WSCONS_REDUCE_IGNORED;
    }
    reducer->pending_key_release_milliseconds = raw_event->milliseconds;
    return nb_wscons_input_reducer_poll_pending(reducer, event);
}

static enum nb_wscons_control_key control_key_for_keycode(
    const struct nb_wscons_input_reducer *reducer,
    int keycode)
{
    size_t index;

    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        if (reducer->control_keys[index].keycode >= 0 &&
            reducer->control_keys[index].keycode == keycode) {
            return (enum nb_wscons_control_key)index;
        }
    }
    return NB_WSCONS_CONTROL_KEY_COUNT;
}

static enum nb_wscons_reduce_result reduce_key_event(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event,
    struct nb_host_event *event)
{
    enum nb_wscons_control_key key;
    struct nb_wscons_control_key_state *state;
    bool repeat;

    saturating_increment(&reducer->stats.keyboard_events);
    if (raw_event->type == NB_WSCONS_RAW_EVENT_ASCII) {
        if (raw_event->value != 0x1b) {
            saturating_increment(&reducer->stats.keyboard_ignored_events);
            return NB_WSCONS_REDUCE_IGNORED;
        }
        state = &reducer->control_keys[
            (size_t)NB_WSCONS_CONTROL_KEY_ESCAPE];
        repeat = state->pressed;
        state->pressed = true;
        set_escape_event(true, repeat, raw_event->milliseconds, event);
        record_emitted_key(reducer, repeat);
        return NB_WSCONS_REDUCE_EVENT;
    }
    if (raw_event->type == NB_WSCONS_RAW_EVENT_ALL_KEYS_UP) {
        return reduce_all_keys_up(reducer, raw_event, event);
    }
    key = control_key_for_keycode(reducer, raw_event->value);
    if (!control_key_is_valid(key)) {
        saturating_increment(&reducer->stats.keyboard_ignored_events);
        return NB_WSCONS_REDUCE_IGNORED;
    }
    state = &reducer->control_keys[(size_t)key];
    if (raw_event->type == NB_WSCONS_RAW_EVENT_KEY_DOWN) {
        repeat = state->pressed;
        state->pressed = true;
        set_control_key_event(key,
                              true,
                              repeat,
                              raw_event->milliseconds,
                              event);
        record_emitted_key(reducer, repeat);
        return NB_WSCONS_REDUCE_EVENT;
    }
    if (raw_event->type == NB_WSCONS_RAW_EVENT_KEY_UP) {
        if (!state->pressed) {
            saturating_increment(&reducer->stats.keyboard_ignored_events);
            return NB_WSCONS_REDUCE_IGNORED;
        }
        state->pressed = false;
        set_control_key_event(key,
                              false,
                              false,
                              raw_event->milliseconds,
                              event);
        record_emitted_key(reducer, false);
        return NB_WSCONS_REDUCE_EVENT;
    }
    saturating_increment(&reducer->stats.keyboard_ignored_events);
    return NB_WSCONS_REDUCE_IGNORED;
}

static enum nb_wscons_reduce_result reduce_button_event(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event,
    struct nb_host_event *event)
{
    uint32_t mask;
    bool pressed;

    if (raw_event->value < 0 ||
        raw_event->value >= NB_HOST_POINTER_BUTTON_COUNT) {
        return NB_WSCONS_REDUCE_IGNORED;
    }
    mask = UINT32_C(1) << (unsigned int)raw_event->value;
    pressed = raw_event->type == NB_WSCONS_RAW_EVENT_MOUSE_DOWN;
    if (pressed == ((reducer->pressed_buttons & mask) != 0)) {
        return NB_WSCONS_REDUCE_IGNORED;
    }
    if (pressed) {
        reducer->pressed_buttons |= mask;
    } else {
        reducer->pressed_buttons &= ~mask;
    }

    event->type = NB_HOST_EVENT_POINTER_BUTTON;
    event->milliseconds = raw_event->milliseconds;
    event->data.pointer_button.x = reducer->pointer_x;
    event->data.pointer_button.y = reducer->pointer_y;
    event->data.pointer_button.button =
        (enum nb_host_pointer_button)raw_event->value;
    event->data.pointer_button.pressed = pressed;
    return NB_WSCONS_REDUCE_EVENT;
}

static enum nb_wscons_reduce_result reduce_relative_event(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event,
    bool horizontal,
    struct nb_host_event *event)
{
    struct nb_wscons_input_stats *stats = &reducer->stats;
    int *coordinate = horizontal ? &reducer->pointer_x
                                 : &reducer->pointer_y;
    int *remainder = horizontal ? &reducer->pointer_remainder_x
                                : &reducer->pointer_remainder_y;
    const int extent = horizontal ? reducer->logical_width
                                  : reducer->logical_height;
    int64_t delta;
    int64_t applied;
    int moved;
    bool raw_points_outward;
    bool clamped = false;
    unsigned int gain_percent;
    const uint64_t magnitude = integer_magnitude(raw_event->value);

    record_relative_event(reducer, raw_event, horizontal);
    if (reducer->pointer_profile == NB_WSCONS_POINTER_PROFILE_ADAPTIVE) {
        gain_percent = prepare_adaptive_gain(reducer, raw_event);
        accumulate_adaptive_distance(reducer, horizontal, magnitude);
        record_adaptive_gain(reducer, gain_percent);
        prepare_adaptive_direction(
            reducer,
            raw_event->value,
            remainder,
            horizontal ? &reducer->pointer_direction_x
                       : &reducer->pointer_direction_y);
    } else {
        gain_percent = reducer->pointer_sensitivity_percent;
    }
    delta = scaled_pointer_delta(raw_event->value,
                                 gain_percent,
                                 remainder);
    if (!horizontal) {
        /* wscons relative Y is positive upwards; desktop Y is positive down. */
        delta = -delta;
    }
    raw_points_outward = horizontal
        ? ((raw_event->value < 0 && *coordinate == 0) ||
           (raw_event->value > 0 && *coordinate == extent - 1))
        : ((raw_event->value > 0 && *coordinate == 0) ||
           (raw_event->value < 0 && *coordinate == extent - 1));
    moved = moved_coordinate(*coordinate, delta, extent);
    applied = (int64_t)moved - (int64_t)*coordinate;
    if (applied != delta || raw_points_outward) {
        clamped = true;
        saturating_increment(&stats->clamped_motion_events);
        /* Never retain fractional movement after hitting an output edge. */
        *remainder = 0;
        if (reducer->pointer_profile ==
            NB_WSCONS_POINTER_PROFILE_ADAPTIVE) {
            saturating_increment(&stats->adaptive_edge_resets);
            reset_adaptive_motion(reducer);
        }
    }
    if (moved == *coordinate) {
        saturating_increment(&stats->suppressed_motion_events);
        if (reducer->pointer_profile ==
                NB_WSCONS_POINTER_PROFILE_ADAPTIVE &&
            raw_event->value != 0 && !clamped) {
            saturating_increment(
                &stats->adaptive_nonedge_suppressed_events);
        }
        return NB_WSCONS_REDUCE_IGNORED;
    }
    *coordinate = moved;
    saturating_increment(&stats->emitted_motion_events);
    saturating_add(horizontal ? &stats->logical_distance_x
                              : &stats->logical_distance_y,
                   signed_distance(applied));
    set_pointer_motion_event(reducer, raw_event->milliseconds, event);
    return NB_WSCONS_REDUCE_EVENT;
}

enum nb_wscons_reduce_result nb_wscons_input_reducer_apply(
    struct nb_wscons_input_reducer *reducer,
    const struct nb_wscons_raw_event *raw_event,
    struct nb_host_event *event)
{
    if (event != NULL) {
        memset(event, 0, sizeof(*event));
    }
    if (!reducer_is_valid(reducer) || raw_event == NULL || event == NULL) {
        return NB_WSCONS_REDUCE_ERROR;
    }
    /* The caller must preserve ALL_KEYS_UP release ordering. */
    if (reducer->pending_key_release_mask != 0) {
        return NB_WSCONS_REDUCE_ERROR;
    }

    switch (raw_event->type) {
    case NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X:
        return reduce_relative_event(reducer, raw_event, true, event);
    case NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y:
        return reduce_relative_event(reducer, raw_event, false, event);
    case NB_WSCONS_RAW_EVENT_MOUSE_DOWN:
    case NB_WSCONS_RAW_EVENT_MOUSE_UP:
        return reduce_button_event(reducer, raw_event, event);
    case NB_WSCONS_RAW_EVENT_KEY_DOWN:
    case NB_WSCONS_RAW_EVENT_KEY_UP:
    case NB_WSCONS_RAW_EVENT_ALL_KEYS_UP:
    case NB_WSCONS_RAW_EVENT_ASCII:
        return reduce_key_event(reducer, raw_event, event);
    case NB_WSCONS_RAW_EVENT_MOUSE_ABSOLUTE_X:
    case NB_WSCONS_RAW_EVENT_MOUSE_ABSOLUTE_Y:
    case NB_WSCONS_RAW_EVENT_NONE:
    default:
        return NB_WSCONS_REDUCE_IGNORED;
    }
}

static void remember_error(struct nb_wscons_input *input,
                           const char *operation,
                           int system_error)
{
    input->system_error = system_error;
    (void)snprintf(input->error, sizeof(input->error), "%s", operation);
}

struct nb_wscons_input *nb_wscons_input_create(int logical_width,
                                               int logical_height)
{
    struct nb_wscons_input *input = calloc(1, sizeof(*input));

    if (input == NULL) {
        return NULL;
    }
    input->keyboard_fd = -1;
    input->mouse_fd = -1;
    if (!nb_wscons_input_reducer_init(&input->reducer,
                                      logical_width,
                                      logical_height)) {
        free(input);
        return NULL;
    }
    return input;
}

#if defined(__NetBSD__)

static void clear_control_keycodes(
    struct nb_wscons_input_reducer *reducer)
{
    size_t index;

    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        reducer->control_keys[index].keycode = -1;
        reducer->control_keys[index].pressed = false;
    }
    reducer->pending_key_release_mask = 0;
    reducer->pending_key_release_milliseconds = 0;
    reducer->stats.keyboard_binding_count = 0;
}

enum {
    /* The caller's batch limit must also bound discarded native events. */
    NB_WSCONS_POLL_DISCARD_LIMIT = 1
};

static const char keyboard_path[] = "/dev/wskbd";
static const char mouse_path[] = "/dev/wsmouse";

static void clear_error(struct nb_wscons_input *input)
{
    input->system_error = 0;
    input->error[0] = '\0';
}

static void close_descriptor(int *descriptor)
{
    if (*descriptor >= 0) {
        (void)close(*descriptor);
        *descriptor = -1;
    }
}

static int open_input_device(const char *path,
                             bool exclusive,
                             unsigned long version_request,
                             int version,
                             int *system_error)
{
    struct stat status;
    int flags = O_RDONLY | O_NONBLOCK | O_NOCTTY;
    int descriptor;

    if (exclusive) {
        flags |= O_EXCL;
    }
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    do {
        descriptor = open(path, flags);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        *system_error = errno;
        return -1;
    }
    if (fstat(descriptor, &status) != 0) {
        *system_error = errno;
        (void)close(descriptor);
        return -1;
    }
    if (!S_ISCHR(status.st_mode)) {
        *system_error = ENOTTY;
        (void)close(descriptor);
        return -1;
    }
    if (ioctl(descriptor, version_request, &version) != 0) {
        *system_error = errno;
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static bool keymap_entry_has_primary_keysym(
    const struct wscons_keymap *entry,
    keysym_t keysym)
{
    /*
     * Raw events provide no modifier state. Matching shifted or alternate
     * groups would make an ordinary key act like its NEO/AltGr-layer control.
     */
    return entry->group1[0] == keysym;
}

static bool keycode_is_already_bound(
    const int keycodes[NB_WSCONS_CONTROL_KEY_COUNT],
    int keycode)
{
    size_t index;

    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        if (keycodes[index] == keycode) {
            return true;
        }
    }
    return false;
}

static int find_keycode_for_keysym(
    const struct wscons_keymap *map,
    u_int map_length,
    keysym_t first,
    keysym_t second,
    const int keycodes[NB_WSCONS_CONTROL_KEY_COUNT])
{
    u_int index;

    for (index = 0; index < map_length; ++index) {
        if (!keycode_is_already_bound(keycodes, (int)index) &&
            (keymap_entry_has_primary_keysym(&map[index], first) ||
             (second != first &&
              keymap_entry_has_primary_keysym(&map[index], second)))) {
            return (int)index;
        }
    }
    return -1;
}

static int find_control_keycode(
    const struct wscons_keymap *map,
    u_int map_length,
    enum nb_wscons_control_key key,
    const int keycodes[NB_WSCONS_CONTROL_KEY_COUNT])
{
    switch (key) {
    case NB_WSCONS_CONTROL_KEY_ESCAPE:
        return find_keycode_for_keysym(map,
                                       map_length,
                                       KS_Escape,
                                       KS_Escape,
                                       keycodes);
    case NB_WSCONS_CONTROL_KEY_F10:
        return find_keycode_for_keysym(map,
                                       map_length,
                                       KS_f10,
                                       KS_F10,
                                       keycodes);
    case NB_WSCONS_CONTROL_KEY_UP:
        return find_keycode_for_keysym(map,
                                       map_length,
                                       KS_Up,
                                       KS_Up,
                                       keycodes);
    case NB_WSCONS_CONTROL_KEY_DOWN:
        return find_keycode_for_keysym(map,
                                       map_length,
                                       KS_Down,
                                       KS_Down,
                                       keycodes);
    case NB_WSCONS_CONTROL_KEY_LEFT:
        return find_keycode_for_keysym(map,
                                       map_length,
                                       KS_Left,
                                       KS_Left,
                                       keycodes);
    case NB_WSCONS_CONTROL_KEY_RIGHT:
        return find_keycode_for_keysym(map,
                                       map_length,
                                       KS_Right,
                                       KS_Right,
                                       keycodes);
    case NB_WSCONS_CONTROL_KEY_RETURN:
        return find_keycode_for_keysym(map,
                                       map_length,
                                       KS_Return,
                                       KS_Return,
                                       keycodes);
    case NB_WSCONS_CONTROL_KEY_KP_ENTER:
        return find_keycode_for_keysym(map,
                                       map_length,
                                       KS_KP_Enter,
                                       KS_KP_Enter,
                                       keycodes);
    case NB_WSCONS_CONTROL_KEY_COUNT:
    default:
        return -1;
    }
}

static bool query_control_keycodes(
    int descriptor,
    int keycodes[NB_WSCONS_CONTROL_KEY_COUNT],
    int *system_error)
{
    struct wskbd_map_data map_data;
    struct wscons_keymap *map;
    size_t index;

    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        keycodes[index] = -1;
    }
    map = calloc(WSKBDIO_MAXMAPLEN, sizeof(*map));
    if (map == NULL) {
        *system_error = ENOMEM;
        return false;
    }
    map_data.maplen = WSKBDIO_MAXMAPLEN;
    map_data.map = map;
    if (ioctl(descriptor, WSKBDIO_GETMAP, &map_data) != 0) {
        *system_error = errno;
    } else if (map_data.maplen > WSKBDIO_MAXMAPLEN) {
        *system_error = EOVERFLOW;
    } else {
        /* Enum order gives deterministic priority for pathological maps. */
        for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
            keycodes[index] = find_control_keycode(
                map,
                map_data.maplen,
                (enum nb_wscons_control_key)index,
                keycodes);
        }
        if (keycodes[NB_WSCONS_CONTROL_KEY_ESCAPE] < 0) {
            *system_error = ENOENT;
        }
    }
    free(map);
    return keycodes[NB_WSCONS_CONTROL_KEY_ESCAPE] >= 0;
}

bool nb_wscons_input_resume(struct nb_wscons_input *input)
{
    int system_error = 0;
    int keycodes[NB_WSCONS_CONTROL_KEY_COUNT];
    size_t index;

    if (input == NULL || !reducer_is_valid(&input->reducer)) {
        return false;
    }
    if (input->active) {
        return true;
    }
    clear_error(input);
    nb_wscons_input_reducer_reset(&input->reducer);
    clear_control_keycodes(&input->reducer);
    input->mouse_fd = open_input_device(mouse_path,
                                        false,
                                        WSMOUSEIO_SETVERSION,
                                        WSMOUSE_EVENT_VERSION,
                                        &system_error);
    if (input->mouse_fd < 0) {
        remember_error(input, "Could not open /dev/wsmouse", system_error);
        return false;
    }
    input->keyboard_fd = open_input_device(keyboard_path,
                                           true,
                                           WSKBDIO_SETVERSION,
                                           WSKBDIO_EVENT_VERSION,
                                           &system_error);
    if (input->keyboard_fd < 0) {
        remember_error(input, "Could not open /dev/wskbd", system_error);
        close_descriptor(&input->mouse_fd);
        return false;
    }
    if (!query_control_keycodes(input->keyboard_fd,
                                keycodes,
                                &system_error)) {
        remember_error(input,
                       "Could not find Escape in the wscons keymap",
                       system_error);
        close_descriptor(&input->keyboard_fd);
        close_descriptor(&input->mouse_fd);
        return false;
    }
    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        if (keycodes[index] >= 0 &&
            !nb_wscons_input_reducer_set_control_keycode(
                &input->reducer,
                (enum nb_wscons_control_key)index,
                keycodes[index])) {
            remember_error(input,
                           "Could not configure the wscons control keymap",
                           EINVAL);
            close_descriptor(&input->keyboard_fd);
            close_descriptor(&input->mouse_fd);
            clear_control_keycodes(&input->reducer);
            return false;
        }
    }
    input->prefer_mouse = true;
    input->active = true;
    return true;
}

void nb_wscons_input_suspend(struct nb_wscons_input *input)
{
    if (input == NULL) {
        return;
    }
    close_descriptor(&input->keyboard_fd);
    close_descriptor(&input->mouse_fd);
    input->active = false;
    nb_wscons_input_reducer_reset(&input->reducer);
}

static bool monotonic_milliseconds(uint64_t *value, int *system_error)
{
    struct timespec now;
    uint64_t seconds;
    uint64_t milliseconds;
    uint64_t fraction;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        *system_error = errno != 0 ? errno : EIO;
        return false;
    }
    if (now.tv_sec < 0 || now.tv_nsec < 0 ||
        now.tv_nsec >= 1000000000L) {
        *system_error = EINVAL;
        return false;
    }
    seconds = (uint64_t)now.tv_sec;
    if (seconds > UINT64_MAX / UINT64_C(1000)) {
        *value = UINT64_MAX;
        return true;
    }
    milliseconds = seconds * UINT64_C(1000);
    fraction = (uint64_t)now.tv_nsec / UINT64_C(1000000);
    *value = fraction > UINT64_MAX - milliseconds
                 ? UINT64_MAX
                 : milliseconds + fraction;
    return true;
}

static bool wscons_motion_nanoseconds(const struct timespec *timestamp,
                                      uint64_t *value)
{
    uint64_t seconds;
    uint64_t nanoseconds;

    if (timestamp == NULL || value == NULL || timestamp->tv_sec < 0 ||
        timestamp->tv_nsec < 0 ||
        timestamp->tv_nsec >= NB_WSCONS_NANOSECONDS_PER_SECOND) {
        return false;
    }
    seconds = (uint64_t)timestamp->tv_sec;
    nanoseconds = (uint64_t)timestamp->tv_nsec;
    if (seconds >
        (UINT64_MAX - nanoseconds) /
            (uint64_t)NB_WSCONS_NANOSECONDS_PER_SECOND) {
        return false;
    }
    *value = seconds * (uint64_t)NB_WSCONS_NANOSECONDS_PER_SECOND +
             nanoseconds;
    return true;
}

static bool translate_native_event(const struct wscons_event *native,
                                   uint64_t read_milliseconds,
                                   struct nb_wscons_raw_event *raw)
{
    memset(raw, 0, sizeof(*raw));
    raw->value = native->value;
    raw->milliseconds = read_milliseconds;
    raw->motion_nanoseconds_valid =
        wscons_motion_nanoseconds(&native->time,
                                  &raw->motion_nanoseconds);
    switch (native->type) {
    case WSCONS_EVENT_KEY_UP:
        raw->type = NB_WSCONS_RAW_EVENT_KEY_UP;
        break;
    case WSCONS_EVENT_KEY_DOWN:
        raw->type = NB_WSCONS_RAW_EVENT_KEY_DOWN;
        break;
    case WSCONS_EVENT_ALL_KEYS_UP:
        raw->type = NB_WSCONS_RAW_EVENT_ALL_KEYS_UP;
        break;
    case WSCONS_EVENT_MOUSE_UP:
        raw->type = NB_WSCONS_RAW_EVENT_MOUSE_UP;
        break;
    case WSCONS_EVENT_MOUSE_DOWN:
        raw->type = NB_WSCONS_RAW_EVENT_MOUSE_DOWN;
        break;
    case WSCONS_EVENT_MOUSE_DELTA_X:
        raw->type = NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X;
        break;
    case WSCONS_EVENT_MOUSE_DELTA_Y:
        raw->type = NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y;
        break;
    case WSCONS_EVENT_MOUSE_ABSOLUTE_X:
        raw->type = NB_WSCONS_RAW_EVENT_MOUSE_ABSOLUTE_X;
        break;
    case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:
        raw->type = NB_WSCONS_RAW_EVENT_MOUSE_ABSOLUTE_Y;
        break;
    case WSCONS_EVENT_ASCII:
        raw->type = NB_WSCONS_RAW_EVENT_ASCII;
        break;
    default:
        return false;
    }
    return true;
}

static enum nb_host_event_status fail_poll(struct nb_wscons_input *input,
                                           const char *operation,
                                           int system_error,
                                           struct nb_host_event *event)
{
    remember_error(input, operation, system_error);
    nb_wscons_input_suspend(input);
    memset(event, 0, sizeof(*event));
    return NB_HOST_EVENT_STATUS_ERROR;
}

enum nb_host_event_status nb_wscons_input_poll(
    struct nb_wscons_input *input,
    struct nb_host_event *event)
{
    unsigned int discarded;
    enum nb_wscons_reduce_result pending;

    if (input == NULL || event == NULL) {
        return NB_HOST_EVENT_STATUS_ERROR;
    }
    memset(event, 0, sizeof(*event));
    if (!input->active) {
        return NB_HOST_EVENT_STATUS_EMPTY;
    }
    pending = nb_wscons_input_reducer_poll_pending(&input->reducer, event);
    if (pending == NB_WSCONS_REDUCE_EVENT) {
        return NB_HOST_EVENT_STATUS_AVAILABLE;
    }
    if (pending == NB_WSCONS_REDUCE_ERROR) {
        return fail_poll(input,
                         "Could not drain pending wscons key releases",
                         EINVAL,
                         event);
    }

    for (discarded = 0; discarded < NB_WSCONS_POLL_DISCARD_LIMIT;
         ++discarded) {
        struct pollfd descriptors[2];
        struct wscons_event native;
        struct nb_wscons_raw_event raw;
        enum nb_wscons_reduce_result reduced;
        uint64_t read_milliseconds;
        int selected;
        int result;
        int timestamp_error = 0;
        ssize_t count;

        descriptors[0].fd = input->keyboard_fd;
        descriptors[0].events = POLLIN;
        descriptors[0].revents = 0;
        descriptors[1].fd = input->mouse_fd;
        descriptors[1].events = POLLIN;
        descriptors[1].revents = 0;
        do {
            result = poll(descriptors, 2, 0);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            return fail_poll(input,
                             "Could not poll wscons input",
                             errno,
                             event);
        }
        if (result == 0) {
            return NB_HOST_EVENT_STATUS_EMPTY;
        }
        if ((descriptors[0].revents | descriptors[1].revents) &
            (POLLERR | POLLHUP | POLLNVAL)) {
            return fail_poll(input,
                             "A wscons input device became unavailable",
                             EIO,
                             event);
        }
        if ((descriptors[0].revents & POLLIN) != 0 &&
            (descriptors[1].revents & POLLIN) != 0) {
            selected = input->prefer_mouse ? 1 : 0;
            input->prefer_mouse = !input->prefer_mouse;
        } else if ((descriptors[1].revents & POLLIN) != 0) {
            selected = 1;
            input->prefer_mouse = false;
        } else if ((descriptors[0].revents & POLLIN) != 0) {
            selected = 0;
            input->prefer_mouse = true;
        } else {
            return fail_poll(input,
                             "wscons poll returned no readable device",
                             EIO,
                             event);
        }

        do {
            count = read(descriptors[selected].fd, &native, sizeof(native));
        } while (count < 0 && errno == EINTR);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (count != (ssize_t)sizeof(native)) {
            const int system_error = count < 0 ? errno : EIO;

            return fail_poll(input,
                             "Could not read a complete wscons event",
                             system_error,
                             event);
        }
        saturating_increment(&input->reducer.stats.native_events_read);
        if (!monotonic_milliseconds(&read_milliseconds,
                                    &timestamp_error)) {
            return fail_poll(input,
                             "Could not timestamp wscons input",
                             timestamp_error,
                             event);
        }
        if (!translate_native_event(&native, read_milliseconds, &raw)) {
            saturating_increment(
                &input->reducer.stats.untranslated_native_events);
            continue;
        }
        reduced = nb_wscons_input_reducer_apply(&input->reducer,
                                                &raw,
                                                event);
        if (reduced == NB_WSCONS_REDUCE_EVENT) {
            return NB_HOST_EVENT_STATUS_AVAILABLE;
        }
        if (reduced == NB_WSCONS_REDUCE_ERROR) {
            return fail_poll(input,
                             "Could not translate a wscons event",
                             EINVAL,
                             event);
        }
    }
    return NB_HOST_EVENT_STATUS_EMPTY;
}

#else

bool nb_wscons_input_resume(struct nb_wscons_input *input)
{
    if (input == NULL) {
        return false;
    }
    remember_error(input,
                   "NetBSD wscons input is unavailable on this platform",
                   ENOTSUP);
    return false;
}

void nb_wscons_input_suspend(struct nb_wscons_input *input)
{
    if (input == NULL) {
        return;
    }
    input->active = false;
    nb_wscons_input_reducer_reset(&input->reducer);
}

enum nb_host_event_status nb_wscons_input_poll(
    struct nb_wscons_input *input,
    struct nb_host_event *event)
{
    if (input == NULL || event == NULL) {
        return NB_HOST_EVENT_STATUS_ERROR;
    }
    memset(event, 0, sizeof(*event));
    return NB_HOST_EVENT_STATUS_EMPTY;
}

#endif

bool nb_wscons_input_is_active(const struct nb_wscons_input *input)
{
    return input != NULL && input->active;
}

bool nb_wscons_input_get_wait_descriptors(
    const struct nb_wscons_input *input,
    int descriptors[NB_WSCONS_INPUT_WAIT_DESCRIPTOR_COUNT])
{
    if (descriptors == NULL) {
        return false;
    }
    descriptors[0] = -1;
    descriptors[1] = -1;
    if (input == NULL || !input->active || input->keyboard_fd < 0 ||
        input->mouse_fd < 0) {
        return false;
    }
    descriptors[0] = input->keyboard_fd;
    descriptors[1] = input->mouse_fd;
    return true;
}

bool nb_wscons_input_set_bounds(struct nb_wscons_input *input,
                                int logical_width,
                                int logical_height)
{
    return input != NULL &&
           nb_wscons_input_reducer_set_bounds(&input->reducer,
                                              logical_width,
                                              logical_height);
}

bool nb_wscons_input_set_pointer_sensitivity(
    struct nb_wscons_input *input,
    unsigned int sensitivity_percent)
{
    return input != NULL && !input->active &&
           nb_wscons_input_reducer_set_pointer_sensitivity(
               &input->reducer,
               sensitivity_percent);
}

bool nb_wscons_input_set_pointer_profile(
    struct nb_wscons_input *input,
    enum nb_wscons_pointer_profile profile)
{
    return input != NULL && !input->active &&
           nb_wscons_input_reducer_set_pointer_profile(&input->reducer,
                                                       profile);
}

bool nb_wscons_input_get_position(const struct nb_wscons_input *input,
                                  int *x,
                                  int *y)
{
    return input != NULL &&
           nb_wscons_input_reducer_get_position(&input->reducer, x, y);
}

bool nb_wscons_input_get_stats(const struct nb_wscons_input *input,
                               struct nb_wscons_input_stats *stats)
{
    return input != NULL &&
           nb_wscons_input_reducer_get_stats(&input->reducer, stats);
}

bool nb_wscons_input_get_last_error(const struct nb_wscons_input *input,
                                    int *system_error,
                                    char *message,
                                    size_t message_size)
{
    if (input == NULL || system_error == NULL || message == NULL ||
        message_size == 0 || input->error[0] == '\0') {
        return false;
    }
    *system_error = input->system_error;
    (void)snprintf(message, message_size, "%s", input->error);
    return true;
}

void nb_wscons_input_destroy(struct nb_wscons_input *input)
{
    if (input == NULL) {
        return;
    }
    nb_wscons_input_suspend(input);
    free(input);
}
