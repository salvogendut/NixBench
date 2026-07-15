#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wscons_input.h"

static int failures;

static const char *const control_key_names[NB_WSCONS_CONTROL_KEY_COUNT] = {
    [NB_WSCONS_CONTROL_KEY_ESCAPE] = "ESC",
    [NB_WSCONS_CONTROL_KEY_F10] = "FK10",
    [NB_WSCONS_CONTROL_KEY_UP] = "UP",
    [NB_WSCONS_CONTROL_KEY_DOWN] = "DOWN",
    [NB_WSCONS_CONTROL_KEY_LEFT] = "LEFT",
    [NB_WSCONS_CONTROL_KEY_RIGHT] = "RGHT",
    [NB_WSCONS_CONTROL_KEY_RETURN] = "RTRN",
    [NB_WSCONS_CONTROL_KEY_KP_ENTER] = "KPEN"
};

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static enum nb_wscons_reduce_result apply(
    struct nb_wscons_input_reducer *reducer,
    enum nb_wscons_raw_event_type type,
    int value,
    uint64_t milliseconds,
    struct nb_host_event *event)
{
    const struct nb_wscons_raw_event raw = {
        .type = type,
        .value = value,
        .milliseconds = milliseconds
    };

    return nb_wscons_input_reducer_apply(reducer, &raw, event);
}

static enum nb_wscons_reduce_result apply_with_motion_nanoseconds(
    struct nb_wscons_input_reducer *reducer,
    enum nb_wscons_raw_event_type type,
    int value,
    uint64_t milliseconds,
    uint64_t motion_nanoseconds,
    struct nb_host_event *event)
{
    const struct nb_wscons_raw_event raw = {
        .type = type,
        .value = value,
        .milliseconds = milliseconds,
        .motion_nanoseconds = motion_nanoseconds,
        .motion_nanoseconds_valid = true
    };

    return nb_wscons_input_reducer_apply(reducer, &raw, event);
}

static void configure_control_keys(
    struct nb_wscons_input_reducer *reducer,
    int first_keycode)
{
    size_t index;

    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        CHECK(nb_wscons_input_reducer_set_control_keycode(
            reducer,
            (enum nb_wscons_control_key)index,
            first_keycode + (int)index));
    }
}

static void test_initialization_and_bounds(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_input_stats stats;
    size_t key_index;
    int x = -1;
    int y = -1;

    CHECK(!nb_wscons_input_reducer_init(NULL, 100, 80));
    CHECK(!nb_wscons_input_reducer_init(&reducer, 0, 80));
    CHECK(!nb_wscons_input_reducer_init(&reducer, 100, -1));
    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    CHECK(reducer.pointer_profile == NB_WSCONS_POINTER_PROFILE_FLAT);
    CHECK(reducer.pointer_sensitivity_percent ==
          NB_WSCONS_POINTER_SENSITIVITY_DEFAULT_PERCENT);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(reducer.adaptive_gain_percent ==
          NB_WSCONS_POINTER_ADAPTIVE_MIN_GAIN_PERCENT);
    CHECK(!reducer.adaptive_group_valid);
    for (key_index = 0; key_index < NB_WSCONS_CONTROL_KEY_COUNT;
         ++key_index) {
        CHECK(reducer.control_keys[key_index].keycode == -1);
        CHECK(!reducer.control_keys[key_index].pressed);
    }
    CHECK(reducer.pending_key_release_mask == 0);
    CHECK(reducer.pending_key_release_milliseconds == 0);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_binding_count == 0);
    CHECK(nb_wscons_input_reducer_get_position(&reducer, &x, &y));
    CHECK(x == 49);
    CHECK(y == 39);
    CHECK(!nb_wscons_input_reducer_get_position(&reducer, NULL, &y));
    CHECK(!nb_wscons_input_reducer_get_position(&reducer, &x, NULL));
    CHECK(!nb_wscons_input_reducer_set_bounds(&reducer, 0, 80));
    CHECK(!nb_wscons_input_reducer_set_bounds(&reducer, 100, 0));

    reducer.pointer_x = 99;
    reducer.pointer_y = 79;
    CHECK(nb_wscons_input_reducer_set_bounds(&reducer, 20, 10));
    CHECK(nb_wscons_input_reducer_get_position(&reducer, &x, &y));
    CHECK(x == 19);
    CHECK(y == 9);
    CHECK(!nb_wscons_input_reducer_set_escape_keycode(&reducer, -1));
    CHECK(nb_wscons_input_reducer_set_escape_keycode(&reducer, 42));
    CHECK(reducer.control_keys[NB_WSCONS_CONTROL_KEY_ESCAPE].keycode == 42);
    CHECK(!nb_wscons_input_reducer_set_control_keycode(
        NULL,
        NB_WSCONS_CONTROL_KEY_F10,
        43));
    CHECK(!nb_wscons_input_reducer_set_control_keycode(
        &reducer,
        (enum nb_wscons_control_key)-1,
        43));
    CHECK(!nb_wscons_input_reducer_set_control_keycode(
        &reducer,
        NB_WSCONS_CONTROL_KEY_COUNT,
        43));
    CHECK(!nb_wscons_input_reducer_set_control_keycode(
        &reducer,
        NB_WSCONS_CONTROL_KEY_F10,
        -1));
    CHECK(nb_wscons_input_reducer_set_control_keycode(
        &reducer,
        NB_WSCONS_CONTROL_KEY_F10,
        43));
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_binding_count == 2);
    CHECK(!nb_wscons_input_reducer_set_control_keycode(
        &reducer,
        NB_WSCONS_CONTROL_KEY_UP,
        42));
    CHECK(reducer.control_keys[NB_WSCONS_CONTROL_KEY_UP].keycode == -1);
    CHECK(reducer.control_keys[NB_WSCONS_CONTROL_KEY_F10].keycode == 43);
    CHECK(nb_wscons_input_reducer_set_control_keycode(
        &reducer,
        NB_WSCONS_CONTROL_KEY_F10,
        44));
    CHECK(reducer.control_keys[NB_WSCONS_CONTROL_KEY_F10].keycode == 44);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_binding_count == 2);
    CHECK(!nb_wscons_input_reducer_set_pointer_sensitivity(NULL, 100));
    CHECK(!nb_wscons_input_reducer_set_pointer_sensitivity(
        &reducer,
        NB_WSCONS_POINTER_SENSITIVITY_MIN_PERCENT - 1U));
    CHECK(!nb_wscons_input_reducer_set_pointer_sensitivity(
        &reducer,
        NB_WSCONS_POINTER_SENSITIVITY_MAX_PERCENT + 1U));
    CHECK(reducer.pointer_sensitivity_percent ==
          NB_WSCONS_POINTER_SENSITIVITY_DEFAULT_PERCENT);
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(
        &reducer,
        NB_WSCONS_POINTER_SENSITIVITY_MIN_PERCENT));
    CHECK(reducer.pointer_sensitivity_percent ==
          NB_WSCONS_POINTER_SENSITIVITY_MIN_PERCENT);
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(
        &reducer,
        NB_WSCONS_POINTER_SENSITIVITY_MAX_PERCENT));
    CHECK(reducer.pointer_sensitivity_percent ==
          NB_WSCONS_POINTER_SENSITIVITY_MAX_PERCENT);
    CHECK(!nb_wscons_input_reducer_set_pointer_profile(
        NULL,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(!nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        (enum nb_wscons_pointer_profile)-1));
    CHECK(!nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        (enum nb_wscons_pointer_profile)2));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(reducer.pointer_profile == NB_WSCONS_POINTER_PROFILE_ADAPTIVE);
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_FLAT));
    CHECK(reducer.pointer_profile == NB_WSCONS_POINTER_PROFILE_FLAT);
}

static void test_relative_pointer_motion(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                10,
                100,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.type == NB_HOST_EVENT_POINTER_MOTION);
    CHECK(event.milliseconds == 100);
    CHECK(event.data.pointer_motion.x == 59);
    CHECK(event.data.pointer_motion.y == 39);
    CHECK(nb_host_event_is_valid(&event));

    /* wscons relative Y is upwards; desktop Y is downwards. */
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                5,
                101,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 59);
    CHECK(event.data.pointer_motion.y == 34);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MAX,
                102,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 99);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MAX,
                103,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(event.type == NB_HOST_EVENT_NONE);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MIN,
                104,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 0);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                INT_MIN,
                105,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.y == 79);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                INT_MAX,
                106,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.y == 0);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_ABSOLUTE_X,
                50,
                107,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_ABSOLUTE_Y,
                50,
                108,
                &event) == NB_WSCONS_REDUCE_IGNORED);
}

static void test_fractional_pointer_sensitivity(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;

    CHECK(nb_wscons_input_reducer_init(&reducer, 1000, 1000));
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 150));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                110,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 500);
    CHECK(reducer.pointer_remainder_x == 50);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                111,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 502);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                112,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 503);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                113,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 505);

    reducer.pointer_x = 500;
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                114,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 499);
    CHECK(reducer.pointer_remainder_x == -50);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                115,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 497);
    CHECK(reducer.pointer_remainder_x == 0);

    reducer.pointer_x = 500;
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                116,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                117,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 500);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                118,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                119,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 500);
    CHECK(reducer.pointer_remainder_x == 0);

    reducer.pointer_x = 500;
    reducer.pointer_y = 500;
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                120,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_x == 501);
    CHECK(reducer.pointer_remainder_x == 50);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                1,
                121,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_y == 499);
    CHECK(reducer.pointer_remainder_x == 50);
    CHECK(reducer.pointer_remainder_y == 50);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                122,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_x == 503);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                1,
                123,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_y == 497);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);

    reducer.pointer_x = 500;
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 150));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                124,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_remainder_x == 50);
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 175));
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                125,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 502);
    CHECK(reducer.pointer_remainder_x == 75);
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);

    reducer.pointer_x = 500;
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 25));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                126,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_remainder_x == 25);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                127,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_remainder_x == 50);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                128,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_remainder_x == 75);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                129,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 501);
    CHECK(reducer.pointer_remainder_x == 0);
}

static void test_pointer_sensitivity_edges(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;

    CHECK(nb_wscons_input_reducer_init(&reducer, 10, 10));
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 150));
    reducer.pointer_x = 9;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                130,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_x == 9);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                131,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 8);
    CHECK(reducer.pointer_remainder_x == -50);

    reducer.pointer_x = 9;
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 25));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                132,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_remainder_x == 0);
    reducer.pointer_y = 0;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                1,
                133,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_remainder_y == 0);

    reducer.pointer_x = 4;
    reducer.pointer_y = 4;
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 150));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                134,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_remainder_x == 50);
    CHECK(nb_wscons_input_reducer_set_bounds(&reducer, 8, 8));
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);

    reducer.pointer_x = 7;
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 400));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MAX,
                135,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                136,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 3);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MIN,
                137,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.x == 0);
    CHECK(nb_host_event_is_valid(&event));
    reducer.pointer_y = 4;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                INT_MIN,
                138,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.pointer_motion.y == 7);
    CHECK(nb_host_event_is_valid(&event));

    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.clamped_motion_events >= 5);
    CHECK(stats.suppressed_motion_events >= 4);
}

static void test_adaptive_pointer_velocity(void)
{
    static const struct {
        uint64_t velocity;
        uint64_t distance;
        uint64_t elapsed;
        unsigned int gain;
    } curve_knots[] = {
        {UINT64_C(400), UINT64_C(2), UINT64_C(5), 100},
        {UINT64_C(750), UINT64_C(3), UINT64_C(4), 150},
        {UINT64_C(1500), UINT64_C(3), UINT64_C(2), 200},
        {UINT64_C(2500), UINT64_C(5), UINT64_C(2), 250}
    };
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;
    uint64_t adaptive_events;
    unsigned int index;
    unsigned int paired_gain;
    uint64_t paired_velocity;

    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));

    /* Equal-timestamp axes share the preceding group's identity gain. */
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                100,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_x == 5000);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                1,
                100,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_y == 4998);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 0);

    /* One raw count every 8 ms converges below the precision threshold. */
    for (index = 1; index <= 20; ++index) {
        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                    1,
                    UINT64_C(100) + (uint64_t)index * UINT64_C(8),
                    &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(reducer.adaptive_gain_percent == 100);
        CHECK(reducer.pointer_remainder_x == 0);
    }
    CHECK(reducer.pointer_x == 5020);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 125);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_gain_100_events == 22);
    CHECK(stats.adaptive_gain_101_149_events == 0);
    CHECK(stats.adaptive_peak_filtered_velocity_counts_per_second == 125);
    CHECK(stats.adaptive_peak_gain_percent == 100);
    CHECK(stats.adaptive_native_timestamp_events == 0);
    CHECK(stats.adaptive_fallback_timestamp_events == 22);
    CHECK(stats.adaptive_motion_groups == 21);
    CHECK(stats.adaptive_same_timestamp_events == 1);
    CHECK(stats.adaptive_clock_source_resets == 0);

    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));

    /* The first group is 1:1; subsequent groups ramp through the EWMA. */
    for (index = 0; index < 32; ++index) {
        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                    40,
                    UINT64_C(200) + (uint64_t)index * UINT64_C(8),
                    &event) == NB_WSCONS_REDUCE_EVENT);
        if (index == 0) {
            CHECK(reducer.adaptive_filtered_velocity_counts_per_second ==
                  0);
            CHECK(reducer.adaptive_gain_percent == 100);
        } else if (index == 1) {
            CHECK(reducer.adaptive_filtered_velocity_counts_per_second ==
                  625);
            CHECK(reducer.adaptive_gain_percent == 132);
        } else if (index == 2) {
            CHECK(reducer.adaptive_filtered_velocity_counts_per_second ==
                  1094);
            CHECK(reducer.adaptive_gain_percent == 172);
        } else if (index == 3) {
            CHECK(reducer.adaptive_filtered_velocity_counts_per_second ==
                  1446);
            CHECK(reducer.adaptive_gain_percent == 196);
        } else if (index == 4) {
            CHECK(reducer.adaptive_filtered_velocity_counts_per_second ==
                  1710);
            CHECK(reducer.adaptive_gain_percent == 210);
            paired_gain = reducer.adaptive_gain_percent;
            paired_velocity =
                reducer.adaptive_filtered_velocity_counts_per_second;
            CHECK(apply(&reducer,
                        NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                        40,
                        UINT64_C(232),
                        &event) == NB_WSCONS_REDUCE_EVENT);
            CHECK(reducer.adaptive_gain_percent == paired_gain);
            CHECK(reducer.adaptive_filtered_velocity_counts_per_second ==
                  paired_velocity);
        }
    }
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 2500);
    CHECK(reducer.adaptive_gain_percent == 250);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    adaptive_events = stats.adaptive_gain_100_events +
                      stats.adaptive_gain_101_149_events +
                      stats.adaptive_gain_150_199_events +
                      stats.adaptive_gain_200_249_events +
                      stats.adaptive_gain_250_events;
    CHECK(adaptive_events == stats.relative_events);
    CHECK(stats.adaptive_gain_100_events != 0);
    CHECK(stats.adaptive_gain_101_149_events != 0);
    CHECK(stats.adaptive_gain_150_199_events != 0);
    CHECK(stats.adaptive_gain_200_249_events != 0);
    CHECK(stats.adaptive_gain_250_events != 0);
    CHECK(stats.adaptive_peak_filtered_velocity_counts_per_second == 2500);
    CHECK(stats.adaptive_peak_gain_percent == 250);

    for (index = 0;
         index < sizeof(curve_knots) / sizeof(curve_knots[0]);
         ++index) {
        CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
        CHECK(nb_wscons_input_reducer_set_pointer_profile(
            &reducer,
            NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
        reducer.adaptive_group_valid = true;
        reducer.adaptive_group_milliseconds = 1000;
        reducer.adaptive_group_distance_x = curve_knots[index].distance;
        reducer.adaptive_filtered_velocity_counts_per_second =
            curve_knots[index].velocity;
        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                    1,
                    UINT64_C(1000) + curve_knots[index].elapsed,
                    &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(reducer.adaptive_filtered_velocity_counts_per_second ==
              curve_knots[index].velocity);
        CHECK(reducer.adaptive_gain_percent == curve_knots[index].gain);
    }
}

static void test_adaptive_gain_boundaries(void)
{
    static const struct {
        uint64_t velocity;
        uint64_t distance;
        uint64_t elapsed;
        unsigned int gain_percent;
    } cases[] = {
        {400, 2, 5, 100},
        {750, 3, 4, 150},
        {1500, 3, 2, 200},
        {2500, 5, 2, 250}
    };
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        CHECK(nb_wscons_input_reducer_init(&reducer, 1000, 1000));
        CHECK(nb_wscons_input_reducer_set_pointer_profile(
            &reducer,
            NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
        reducer.adaptive_group_valid = true;
        reducer.adaptive_group_milliseconds = 100;
        reducer.adaptive_group_distance_x =
            cases[index].distance;
        reducer.adaptive_filtered_velocity_counts_per_second =
            cases[index].velocity;
        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                    0,
                    UINT64_C(100) + cases[index].elapsed,
                    &event) == NB_WSCONS_REDUCE_IGNORED);
        CHECK(reducer.adaptive_filtered_velocity_counts_per_second ==
              cases[index].velocity);
        CHECK(reducer.adaptive_gain_percent ==
              cases[index].gain_percent);
    }
}

static void test_adaptive_native_motion_timestamps(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;

    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));

    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              4,
              100,
              UINT64_C(1000000000),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 100);
    CHECK(reducer.adaptive_group_uses_native_timestamp);
    CHECK(reducer.adaptive_group_motion_nanoseconds ==
          UINT64_C(1000000000));
    CHECK(reducer.adaptive_group_milliseconds == 0);

    /* Equal native stamps group even when userspace read times differ. */
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
              4,
              112,
              UINT64_C(1000000000),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 112);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 0);
    CHECK(reducer.adaptive_group_distance_x == 4);
    CHECK(reducer.adaptive_group_distance_y == 4);

    /* Distinct native stamps split groups even in one read millisecond. */
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              4,
              112,
              UINT64_C(1008000000),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 112);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 188);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_native_timestamp_events == 3);
    CHECK(stats.adaptive_fallback_timestamp_events == 0);
    CHECK(stats.adaptive_motion_groups == 2);
    CHECK(stats.adaptive_same_timestamp_events == 1);
    CHECK(stats.adaptive_clock_source_resets == 0);

    reducer.pointer_remainder_y = 75;
    reducer.pointer_direction_y = -1;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                113,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 113);
    CHECK(!reducer.adaptive_group_uses_native_timestamp);
    CHECK(reducer.adaptive_group_milliseconds == 113);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                1,
                113,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 113);

    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              114,
              UINT64_C(1016000000),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 114);
    CHECK(reducer.adaptive_group_uses_native_timestamp);
    CHECK(reducer.adaptive_gain_percent == 100);

    /* A realtime-clock regression is distinct from a source change. */
    reducer.adaptive_gain_percent = 150;
    reducer.pointer_remainder_y = -50;
    reducer.pointer_direction_y = 1;
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              115,
              UINT64_C(1015000000),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 115);
    CHECK(reducer.adaptive_group_motion_nanoseconds ==
          UINT64_C(1015000000));
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_native_timestamp_events == 5);
    CHECK(stats.adaptive_fallback_timestamp_events == 2);
    CHECK(stats.adaptive_motion_groups == 5);
    CHECK(stats.adaptive_same_timestamp_events == 2);
    CHECK(stats.adaptive_clock_source_resets == 2);
    CHECK(stats.adaptive_timestamp_resets == 1);
}

static void test_adaptive_timestamp_velocity_equivalence(void)
{
    struct nb_wscons_input_reducer fallback;
    struct nb_wscons_input_reducer native;
    struct nb_host_event event;
    struct nb_wscons_input_stats fallback_stats;
    struct nb_wscons_input_stats native_stats;
    unsigned int index;

    CHECK(nb_wscons_input_reducer_init(&fallback, 10000, 10000));
    CHECK(nb_wscons_input_reducer_init(&native, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &fallback,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &native,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));

    for (index = 0; index < 12; ++index) {
        const uint64_t milliseconds =
            UINT64_C(200) + (uint64_t)index * UINT64_C(8);
        const uint64_t motion_nanoseconds =
            UINT64_C(5000000000) +
            (uint64_t)index * UINT64_C(8000000);

        CHECK(apply(&fallback,
                    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                    40,
                    milliseconds,
                    &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(apply_with_motion_nanoseconds(
                  &native,
                  NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                  40,
                  milliseconds,
                  motion_nanoseconds,
                  &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(event.milliseconds == milliseconds);
        CHECK(fallback.pointer_x == native.pointer_x);
        CHECK(fallback.pointer_remainder_x == native.pointer_remainder_x);
        CHECK(fallback.adaptive_filtered_velocity_counts_per_second ==
              native.adaptive_filtered_velocity_counts_per_second);
        CHECK(fallback.adaptive_gain_percent ==
              native.adaptive_gain_percent);
    }
    CHECK(nb_wscons_input_reducer_get_stats(&fallback, &fallback_stats));
    CHECK(nb_wscons_input_reducer_get_stats(&native, &native_stats));
    CHECK(fallback_stats.adaptive_fallback_timestamp_events == 12);
    CHECK(fallback_stats.adaptive_native_timestamp_events == 0);
    CHECK(native_stats.adaptive_native_timestamp_events == 12);
    CHECK(native_stats.adaptive_fallback_timestamp_events == 0);
    CHECK(fallback_stats.adaptive_motion_groups ==
          native_stats.adaptive_motion_groups);
    CHECK(fallback_stats.adaptive_same_timestamp_events == 0);
    CHECK(native_stats.adaptive_same_timestamp_events == 0);
}

static void test_adaptive_timestamp_boundaries_and_extremes(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;

    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              0,
              0,
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              99,
              UINT64_C(99999999),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 99);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 3);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_idle_resets == 0);
    CHECK(stats.adaptive_motion_groups == 2);

    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              40,
              0,
              0,
              &event) == NB_WSCONS_REDUCE_EVENT);
    reducer.adaptive_filtered_velocity_counts_per_second = 750;
    reducer.adaptive_gain_percent = 150;
    reducer.pointer_remainder_x = 50;
    reducer.pointer_remainder_y = -50;
    reducer.pointer_direction_x = 1;
    reducer.pointer_direction_y = -1;
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              100,
              UINT64_C(100000000),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 100);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 0);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 1);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_idle_resets == 1);
    CHECK(stats.adaptive_precision_carry_resets == 0);
    CHECK(stats.adaptive_motion_groups == 2);

    /* Saturated distance cannot overflow the native counts/second math. */
    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              1,
              0,
              &event) == NB_WSCONS_REDUCE_EVENT);
    reducer.adaptive_group_distance_x = UINT64_MAX;
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              2,
              UINT64_C(99999999),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 625);
    CHECK(reducer.adaptive_gain_percent == 132);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_idle_resets == 0);
    CHECK(stats.adaptive_peak_filtered_velocity_counts_per_second == 625);

    /* Subtraction remains defined at the top of the uint64 timestamp range. */
    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              4,
              10,
              UINT64_MAX - UINT64_C(8000000),
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              4,
              11,
              UINT64_MAX,
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 125);
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              12,
              0,
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.milliseconds == 12);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_timestamp_resets == 1);

    /* Every source/group timestamp diagnostic saturates without wrapping. */
    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    reducer.stats.adaptive_native_timestamp_events = UINT64_MAX;
    reducer.stats.adaptive_fallback_timestamp_events = UINT64_MAX;
    reducer.stats.adaptive_motion_groups = UINT64_MAX;
    reducer.stats.adaptive_same_timestamp_events = UINT64_MAX;
    reducer.stats.adaptive_clock_source_resets = UINT64_MAX;
    reducer.stats.adaptive_timestamp_resets = UINT64_MAX;
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
              1,
              20,
              20,
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply_with_motion_nanoseconds(
              &reducer,
              NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
              1,
              21,
              20,
              &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                30,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                29,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_native_timestamp_events == UINT64_MAX);
    CHECK(stats.adaptive_fallback_timestamp_events == UINT64_MAX);
    CHECK(stats.adaptive_motion_groups == UINT64_MAX);
    CHECK(stats.adaptive_same_timestamp_events == UINT64_MAX);
    CHECK(stats.adaptive_clock_source_resets == UINT64_MAX);
    CHECK(stats.adaptive_timestamp_resets == UINT64_MAX);
}

static void test_adaptive_pointer_carry_stabilization(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;
    int x;

    CHECK(nb_wscons_input_reducer_init(&reducer, 1000, 1000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));

    /* Entering the identity region drops sub-pixel debt on both axes. */
    reducer.adaptive_group_valid = true;
    reducer.adaptive_group_milliseconds = 100;
    reducer.adaptive_group_distance_x = 0;
    reducer.adaptive_filtered_velocity_counts_per_second = 500;
    reducer.adaptive_gain_percent = 114;
    reducer.pointer_remainder_x = 75;
    reducer.pointer_remainder_y = -25;
    reducer.pointer_direction_x = 1;
    reducer.pointer_direction_y = -1;
    x = reducer.pointer_x;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                104,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 375);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(reducer.pointer_x == x + 1);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 1);
    CHECK(reducer.pointer_direction_y == -1);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_precision_carry_resets == 2);
    CHECK(stats.adaptive_direction_carry_resets == 0);
    CHECK(stats.adaptive_nonedge_suppressed_events == 0);
    CHECK(stats.adaptive_zero_relative_events == 0);
    CHECK(stats.adaptive_native_timestamp_events == 0);
    CHECK(stats.adaptive_fallback_timestamp_events == 1);
    CHECK(stats.adaptive_motion_groups == 1);
    CHECK(stats.adaptive_same_timestamp_events == 0);
    CHECK(stats.adaptive_clock_source_resets == 0);

    CHECK(nb_wscons_input_reducer_init(&reducer, 1000, 1000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    reducer.adaptive_group_valid = true;
    reducer.adaptive_group_milliseconds = 200;
    reducer.adaptive_filtered_velocity_counts_per_second = 750;
    reducer.adaptive_gain_percent = 150;
    reducer.pointer_remainder_x = 75;
    reducer.pointer_direction_x = 1;
    x = reducer.pointer_x;

    /* Reversal moves immediately instead of consuming old-direction carry. */
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                200,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_x == x - 1);
    CHECK(reducer.pointer_remainder_x == -50);
    CHECK(reducer.pointer_direction_x == -1);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                200,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_x == x - 3);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                200,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_x == x - 2);
    CHECK(reducer.pointer_remainder_x == 50);
    CHECK(reducer.pointer_direction_x == 1);

    reducer.pointer_remainder_y = -75;
    reducer.pointer_direction_y = -1;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                1,
                200,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_remainder_y == 50);
    CHECK(reducer.pointer_direction_y == 1);

    /* Zero-valued motion is measured but never called non-edge jitter. */
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                0,
                200,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_direction_x == 1);
    CHECK(reducer.pointer_remainder_x == 50);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_direction_carry_resets == 2);
    CHECK(stats.adaptive_precision_carry_resets == 0);
    CHECK(stats.adaptive_zero_relative_events == 1);
    CHECK(stats.adaptive_nonedge_suppressed_events == 0);

    /*
     * Deliberately seed a carry/sign pairing that normal adaptive input now
     * prevents, exercising defensive non-edge suppression telemetry without
     * confusing it with zero events or clamps.
     */
    reducer.adaptive_gain_percent = 100;
    reducer.pointer_remainder_x = -99;
    reducer.pointer_direction_x = 1;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                200,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_nonedge_suppressed_events == 1);
    CHECK(stats.adaptive_zero_relative_events == 1);

    /* Reset/configuration boundaries discard direction as well as carry. */
    CHECK(nb_wscons_input_reducer_set_bounds(&reducer, 800, 800));
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);
    reducer.pointer_direction_x = -1;
    reducer.pointer_direction_y = 1;
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 175));
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);
    reducer.pointer_direction_x = 1;
    reducer.pointer_direction_y = -1;
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);
    reducer.pointer_direction_x = -1;
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_FLAT));
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);

    /* An outward edge suppression is a clamp, never non-edge jitter. */
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    reducer.pointer_x = 799;
    reducer.pointer_remainder_x = -99;
    reducer.pointer_direction_x = 1;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                250,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_edge_resets == 1);
    CHECK(stats.adaptive_nonedge_suppressed_events == 1);

    /* Every new diagnostic counter saturates instead of wrapping. */
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    reducer.pointer_x = 400;
    reducer.stats.adaptive_precision_carry_resets = UINT64_MAX;
    reducer.stats.adaptive_direction_carry_resets = UINT64_MAX;
    reducer.stats.adaptive_nonedge_suppressed_events = UINT64_MAX;
    reducer.stats.adaptive_zero_relative_events = UINT64_MAX;
    reducer.adaptive_group_valid = true;
    reducer.adaptive_group_milliseconds = 300;
    reducer.adaptive_group_distance_x = 0;
    reducer.adaptive_filtered_velocity_counts_per_second = 500;
    reducer.adaptive_gain_percent = 114;
    reducer.pointer_remainder_x = 50;
    reducer.pointer_direction_x = 1;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                304,
                &event) == NB_WSCONS_REDUCE_EVENT);
    reducer.adaptive_gain_percent = 150;
    reducer.pointer_remainder_x = 50;
    reducer.pointer_direction_x = 1;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                304,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                0,
                304,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    reducer.adaptive_gain_percent = 100;
    reducer.pointer_remainder_x = -99;
    reducer.pointer_direction_x = 1;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                304,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_precision_carry_resets == UINT64_MAX);
    CHECK(stats.adaptive_direction_carry_resets == UINT64_MAX);
    CHECK(stats.adaptive_nonedge_suppressed_events == UINT64_MAX);
    CHECK(stats.adaptive_zero_relative_events == UINT64_MAX);
}

static void test_adaptive_pointer_resets(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;

    CHECK(nb_wscons_input_reducer_init(&reducer, 10000, 10000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                0,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                8,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.adaptive_gain_percent == 132);
    CHECK(reducer.pointer_remainder_x == 80);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                108,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 0);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 1);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_idle_resets == 1);

    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                200,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                299,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_idle_resets == 1);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                399,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_idle_resets == 2);

    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                200,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                208,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_remainder_x == 80);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                207,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 1);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_timestamp_resets == 1);

    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    reducer.pointer_x = 5000;
    reducer.pointer_y = 5000;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                300,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                308,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                1,
                308,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_remainder_x == 80);
    CHECK(reducer.pointer_remainder_y == 32);
    reducer.pointer_x = 9999;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                316,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(!reducer.adaptive_group_valid);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.adaptive_edge_resets == 1);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                1,
                316,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.adaptive_gain_percent == 100);

    reducer.pointer_x = 5000;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                400,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                408,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.pointer_remainder_x != 0);
    CHECK(nb_wscons_input_reducer_set_bounds(&reducer, 10000, 10000));
    CHECK(!reducer.adaptive_group_valid);
    CHECK(reducer.adaptive_filtered_velocity_counts_per_second == 0);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                500,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                508,
                &event) == NB_WSCONS_REDUCE_EVENT);
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(!reducer.adaptive_group_valid);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                600,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                608,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_FLAT));
    CHECK(!reducer.adaptive_group_valid);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);
    CHECK(reducer.adaptive_gain_percent == 100);

    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &reducer,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                700,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                40,
                708,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(reducer.adaptive_group_valid);
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 175));
    CHECK(reducer.pointer_profile == NB_WSCONS_POINTER_PROFILE_ADAPTIVE);
    CHECK(!reducer.adaptive_group_valid);
    CHECK(reducer.adaptive_gain_percent == 100);
    CHECK(reducer.pointer_remainder_x == 0);
    CHECK(reducer.pointer_remainder_y == 0);
    CHECK(reducer.pointer_direction_x == 0);
    CHECK(reducer.pointer_direction_y == 0);
}

static void test_adaptive_pointer_symmetry_and_saturation(void)
{
    struct nb_wscons_input_reducer positive;
    struct nb_wscons_input_reducer negative;
    struct nb_wscons_input_reducer extreme;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;
    const int center = 49999;
    unsigned int index;

    CHECK(nb_wscons_input_reducer_init(&positive, 100000, 100000));
    CHECK(nb_wscons_input_reducer_init(&negative, 100000, 100000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &positive,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &negative,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    for (index = 0; index < 12; ++index) {
        const uint64_t milliseconds =
            UINT64_C(700) + (uint64_t)index * UINT64_C(8);

        CHECK(apply(&positive,
                    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                    13,
                    milliseconds,
                    &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(apply(&negative,
                    NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                    -13,
                    milliseconds,
                    &event) == NB_WSCONS_REDUCE_EVENT);
    }
    CHECK(positive.pointer_x - center == center - negative.pointer_x);
    CHECK(positive.pointer_remainder_x == -negative.pointer_remainder_x);
    CHECK(positive.adaptive_gain_percent == negative.adaptive_gain_percent);
    CHECK(positive.adaptive_filtered_velocity_counts_per_second ==
          negative.adaptive_filtered_velocity_counts_per_second);

    CHECK(nb_wscons_input_reducer_init(&positive, 100000, 100000));
    CHECK(nb_wscons_input_reducer_init(&negative, 100000, 100000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &positive,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &negative,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    positive.adaptive_group_valid = true;
    positive.adaptive_group_milliseconds = 750;
    positive.adaptive_filtered_velocity_counts_per_second = 750;
    positive.adaptive_gain_percent = 150;
    positive.pointer_remainder_x = 75;
    positive.pointer_direction_x = 1;
    negative.adaptive_group_valid = true;
    negative.adaptive_group_milliseconds = 750;
    negative.adaptive_filtered_velocity_counts_per_second = 750;
    negative.adaptive_gain_percent = 150;
    negative.pointer_remainder_x = -75;
    negative.pointer_direction_x = -1;
    CHECK(apply(&positive,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                -1,
                750,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&negative,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                750,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(center - positive.pointer_x == negative.pointer_x - center);
    CHECK(positive.pointer_remainder_x == -negative.pointer_remainder_x);
    CHECK(positive.stats.adaptive_direction_carry_resets == 1);
    CHECK(negative.stats.adaptive_direction_carry_resets == 1);

    CHECK(nb_wscons_input_reducer_init(&extreme, 100000, 100000));
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &extreme,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply(&extreme,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MAX,
                800,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(extreme.pointer_x == 99999);
    extreme.pointer_x = center;
    CHECK(apply(&extreme,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MIN,
                801,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(extreme.pointer_x == 0);

    positive.stats.adaptive_gain_100_events = UINT64_MAX;
    positive.stats.adaptive_gain_101_149_events = UINT64_MAX;
    positive.stats.adaptive_gain_150_199_events = UINT64_MAX;
    positive.stats.adaptive_gain_200_249_events = UINT64_MAX;
    positive.stats.adaptive_gain_250_events = UINT64_MAX;
    positive.stats.adaptive_peak_filtered_velocity_counts_per_second =
        UINT64_MAX;
    positive.stats.adaptive_peak_gain_percent = UINT64_MAX;
    positive.stats.adaptive_idle_resets = UINT64_MAX;
    positive.stats.adaptive_timestamp_resets = UINT64_MAX;
    positive.stats.adaptive_edge_resets = UINT64_MAX;
    CHECK(nb_wscons_input_reducer_set_pointer_profile(
        &positive,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(apply(&positive,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                900,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&positive,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                899,
                &event) == NB_WSCONS_REDUCE_EVENT);
    positive.pointer_x = 99999;
    CHECK(apply(&positive,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MAX,
                1000,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(nb_wscons_input_reducer_get_stats(&positive, &stats));
    CHECK(stats.adaptive_gain_100_events == UINT64_MAX);
    CHECK(stats.adaptive_gain_101_149_events == UINT64_MAX);
    CHECK(stats.adaptive_gain_150_199_events == UINT64_MAX);
    CHECK(stats.adaptive_gain_200_249_events == UINT64_MAX);
    CHECK(stats.adaptive_gain_250_events == UINT64_MAX);
    CHECK(stats.adaptive_peak_filtered_velocity_counts_per_second ==
          UINT64_MAX);
    CHECK(stats.adaptive_peak_gain_percent == UINT64_MAX);
    CHECK(stats.adaptive_idle_resets == UINT64_MAX);
    CHECK(stats.adaptive_timestamp_resets == UINT64_MAX);
    CHECK(stats.adaptive_edge_resets == UINT64_MAX);
}

static void test_pointer_statistics(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;
    struct nb_wscons_input_stats saved;

    CHECK(nb_wscons_input_reducer_init(&reducer, 1000, 1000));
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 150));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                100,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                104,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_Y,
                -2,
                110,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                0,
                109,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    reducer.pointer_x = 999;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                120,
                &event) == NB_WSCONS_REDUCE_IGNORED);

    memset(&stats, 0xa5, sizeof(stats));
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.native_events_read == 0);
    CHECK(stats.untranslated_native_events == 0);
    CHECK(stats.relative_events == 5);
    CHECK(stats.unit_relative_events == 3);
    CHECK(stats.emitted_motion_events == 3);
    CHECK(stats.suppressed_motion_events == 2);
    CHECK(stats.clamped_motion_events == 1);
    CHECK(stats.raw_distance_x == 3);
    CHECK(stats.raw_distance_y == 2);
    CHECK(stats.logical_distance_x == 3);
    CHECK(stats.logical_distance_y == 3);
    CHECK(stats.first_motion_milliseconds == 100);
    CHECK(stats.last_motion_milliseconds == 120);
    CHECK(stats.maximum_motion_gap_milliseconds == 11);
    CHECK(stats.timestamp_regressions == 1);
    CHECK(stats.adaptive_precision_carry_resets == 0);
    CHECK(stats.adaptive_direction_carry_resets == 0);
    CHECK(stats.adaptive_nonedge_suppressed_events == 0);
    CHECK(stats.adaptive_zero_relative_events == 0);
    CHECK(stats.adaptive_native_timestamp_events == 0);
    CHECK(stats.adaptive_fallback_timestamp_events == 0);
    CHECK(stats.adaptive_motion_groups == 0);
    CHECK(stats.adaptive_same_timestamp_events == 0);
    CHECK(stats.adaptive_clock_source_resets == 0);
    saved = stats;

    nb_wscons_input_reducer_reset(&reducer);
    CHECK(nb_wscons_input_reducer_set_bounds(&reducer, 800, 800));
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(memcmp(&stats, &saved, sizeof(stats)) == 0);
    CHECK(!nb_wscons_input_reducer_get_stats(NULL, &stats));
    CHECK(!nb_wscons_input_reducer_get_stats(&reducer, NULL));

    reducer.pointer_x = 400;
    reducer.stats.relative_events = UINT64_MAX;
    reducer.stats.unit_relative_events = UINT64_MAX;
    reducer.stats.emitted_motion_events = UINT64_MAX;
    reducer.stats.raw_distance_x = UINT64_MAX - UINT64_C(1);
    reducer.stats.logical_distance_x = UINT64_MAX - UINT64_C(1);
    CHECK(nb_wscons_input_reducer_set_pointer_sensitivity(&reducer, 400));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                INT_MAX,
                121,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.relative_events == UINT64_MAX);
    CHECK(stats.unit_relative_events == UINT64_MAX);
    CHECK(stats.emitted_motion_events == UINT64_MAX);
    CHECK(stats.raw_distance_x == UINT64_MAX);
    CHECK(stats.logical_distance_x == UINT64_MAX);
}

static void test_pointer_buttons(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;
    int button;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    for (button = NB_HOST_POINTER_BUTTON_LEFT;
         button < NB_HOST_POINTER_BUTTON_COUNT;
         ++button) {
        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_MOUSE_DOWN,
                    button,
                    (uint64_t)(200 + button),
                    &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(event.type == NB_HOST_EVENT_POINTER_BUTTON);
        CHECK(event.data.pointer_button.x == 49);
        CHECK(event.data.pointer_button.y == 39);
        CHECK(event.data.pointer_button.button ==
              (enum nb_host_pointer_button)button);
        CHECK(event.data.pointer_button.pressed);
        CHECK(nb_host_event_is_valid(&event));
        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_MOUSE_DOWN,
                    button,
                    210,
                    &event) == NB_WSCONS_REDUCE_IGNORED);
    }
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DOWN,
                NB_HOST_POINTER_BUTTON_COUNT,
                220,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_UP,
                -1,
                221,
                &event) == NB_WSCONS_REDUCE_IGNORED);

    nb_wscons_input_reducer_reset(&reducer);
    CHECK(reducer.pressed_buttons == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_UP,
                NB_HOST_POINTER_BUTTON_LEFT,
                222,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DOWN,
                NB_HOST_POINTER_BUTTON_LEFT,
                223,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_UP,
                NB_HOST_POINTER_BUTTON_LEFT,
                224,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(!event.data.pointer_button.pressed);
}

static void test_escape_translation(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_host_event event;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    CHECK(nb_wscons_input_reducer_set_escape_keycode(&reducer, 42));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                41,
                300,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                42,
                301,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.type == NB_HOST_EVENT_KEY);
    CHECK(strcmp(event.data.key.xkb_key_name, "ESC") == 0);
    CHECK(event.data.key.pressed);
    CHECK(!event.data.key.repeat);
    CHECK(nb_host_event_is_valid(&event));

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                42,
                302,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.key.pressed);
    CHECK(event.data.key.repeat);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                42,
                303,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(!event.data.key.pressed);
    CHECK(!event.data.key.repeat);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                42,
                304,
                &event) == NB_WSCONS_REDUCE_IGNORED);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                42,
                305,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                306,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(!event.data.key.pressed);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                307,
                &event) == NB_WSCONS_REDUCE_IGNORED);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ASCII,
                'x',
                308,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ASCII,
                0x1b,
                309,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.key.pressed);
    CHECK(strcmp(event.data.key.xkb_key_name, "ESC") == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ASCII,
                0x1b,
                310,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.key.repeat);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                311,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(!event.data.key.pressed);
    CHECK(!event.data.key.repeat);
    CHECK(strcmp(event.data.key.xkb_key_name, "ESC") == 0);
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(!reducer.control_keys[NB_WSCONS_CONTROL_KEY_ESCAPE].pressed);
}

static void test_control_key_translation_and_diagnostics(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_input_stats stats;
    struct nb_host_event event;
    const int first_keycode = 100;
    size_t index;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    configure_control_keys(&reducer, first_keycode);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_binding_count == NB_WSCONS_CONTROL_KEY_COUNT);

    for (index = 0; index < NB_WSCONS_CONTROL_KEY_COUNT; ++index) {
        const int keycode = first_keycode + (int)index;

        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_KEY_DOWN,
                    keycode,
                    UINT64_C(400) + index * UINT64_C(10),
                    &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(event.type == NB_HOST_EVENT_KEY);
        CHECK(strcmp(event.data.key.xkb_key_name,
                     control_key_names[index]) == 0);
        CHECK(event.data.key.pressed);
        CHECK(!event.data.key.repeat);
        CHECK(nb_host_event_is_valid(&event));
        CHECK(reducer.control_keys[index].pressed);

        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_KEY_DOWN,
                    keycode,
                    UINT64_C(401) + index * UINT64_C(10),
                    &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(event.data.key.pressed);
        CHECK(event.data.key.repeat);
        CHECK(nb_host_event_is_valid(&event));

        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_KEY_UP,
                    keycode,
                    UINT64_C(402) + index * UINT64_C(10),
                    &event) == NB_WSCONS_REDUCE_EVENT);
        CHECK(strcmp(event.data.key.xkb_key_name,
                     control_key_names[index]) == 0);
        CHECK(!event.data.key.pressed);
        CHECK(!event.data.key.repeat);
        CHECK(nb_host_event_is_valid(&event));
        CHECK(!reducer.control_keys[index].pressed);

        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_KEY_UP,
                    keycode,
                    UINT64_C(403) + index * UINT64_C(10),
                    &event) == NB_WSCONS_REDUCE_IGNORED);
        CHECK(event.type == NB_HOST_EVENT_NONE);
    }

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                first_keycode - 1,
                500,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(event.type == NB_HOST_EVENT_NONE);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                -1,
                501,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ASCII,
                'x',
                502,
                &event) == NB_WSCONS_REDUCE_IGNORED);

    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_events ==
          (uint64_t)NB_WSCONS_CONTROL_KEY_COUNT * UINT64_C(4) +
              UINT64_C(3));
    CHECK(stats.keyboard_emitted_events ==
          (uint64_t)NB_WSCONS_CONTROL_KEY_COUNT * UINT64_C(3));
    CHECK(stats.keyboard_repeat_events ==
          (uint64_t)NB_WSCONS_CONTROL_KEY_COUNT);
    CHECK(stats.keyboard_ignored_events ==
          (uint64_t)NB_WSCONS_CONTROL_KEY_COUNT + UINT64_C(3));
    CHECK(stats.keyboard_all_keys_up_events == 0);
    CHECK(stats.keyboard_synthesized_release_events == 0);
    CHECK(stats.keyboard_binding_count == NB_WSCONS_CONTROL_KEY_COUNT);
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_events ==
          (uint64_t)NB_WSCONS_CONTROL_KEY_COUNT * UINT64_C(4) +
              UINT64_C(3));
    CHECK(stats.keyboard_binding_count == NB_WSCONS_CONTROL_KEY_COUNT);
}

static void test_all_keys_up_pending_releases(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_input_stats stats;
    struct nb_host_event event;
    const int first_keycode = 200;
    int pointer_x;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    configure_control_keys(&reducer, first_keycode);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                first_keycode + NB_WSCONS_CONTROL_KEY_RIGHT,
                600,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                first_keycode + NB_WSCONS_CONTROL_KEY_ESCAPE,
                601,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                first_keycode + NB_WSCONS_CONTROL_KEY_F10,
                602,
                &event) == NB_WSCONS_REDUCE_EVENT);
    pointer_x = reducer.pointer_x;

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                603,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "ESC") == 0);
    CHECK(!event.data.key.pressed);
    CHECK(event.milliseconds == 603);
    CHECK(reducer.pending_key_release_mask != 0);
    CHECK(!reducer.control_keys[NB_WSCONS_CONTROL_KEY_ESCAPE].pressed);
    CHECK(!reducer.control_keys[NB_WSCONS_CONTROL_KEY_F10].pressed);
    CHECK(!reducer.control_keys[NB_WSCONS_CONTROL_KEY_RIGHT].pressed);

    CHECK(!nb_wscons_input_reducer_set_control_keycode(
        &reducer,
        NB_WSCONS_CONTROL_KEY_UP,
        999));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                10,
                604,
                &event) == NB_WSCONS_REDUCE_ERROR);
    CHECK(event.type == NB_HOST_EVENT_NONE);
    CHECK(reducer.pointer_x == pointer_x);

    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "FK10") == 0);
    CHECK(!event.data.key.pressed);
    CHECK(event.milliseconds == 603);
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "RGHT") == 0);
    CHECK(!event.data.key.pressed);
    CHECK(event.milliseconds == 603);
    CHECK(reducer.pending_key_release_mask == 0);
    CHECK(reducer.pending_key_release_milliseconds == 0);
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_IGNORED);
    CHECK(event.type == NB_HOST_EVENT_NONE);

    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_events == 4);
    CHECK(stats.keyboard_emitted_events == 6);
    CHECK(stats.keyboard_repeat_events == 0);
    CHECK(stats.keyboard_ignored_events == 0);
    CHECK(stats.keyboard_all_keys_up_events == 1);
    CHECK(stats.keyboard_synthesized_release_events == 3);
    CHECK(stats.keyboard_binding_count == NB_WSCONS_CONTROL_KEY_COUNT);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                605,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_events == 5);
    CHECK(stats.keyboard_ignored_events == 1);
    CHECK(stats.keyboard_all_keys_up_events == 2);
    CHECK(stats.keyboard_synthesized_release_events == 3);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                first_keycode + NB_WSCONS_CONTROL_KEY_UP,
                606,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                first_keycode + NB_WSCONS_CONTROL_KEY_DOWN,
                607,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                608,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "UP") == 0);
    CHECK(reducer.pending_key_release_mask != 0);
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(reducer.pending_key_release_mask == 0);
    CHECK(reducer.pending_key_release_milliseconds == 0);
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_IGNORED);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
                1,
                609,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.type == NB_HOST_EVENT_POINTER_MOTION);
}

static void test_pc_xt_keyboard_profile(void)
{
    static const struct {
        int keycode;
        const char *xkb_key_name;
    } address_probe_keys[] = {
        {29, "LCTL"}, {38, "AC09"},
        {32, "AC03"}, {30, "AC01"}, {20, "AD05"},
        {39, "AC10"}, {18, "AD03"}, {45, "AB02"},
        {53, "AB10"}, {25, "AD10"}, {23, "AD08"},
        {49, "AB06"}, {51, "AB08"}, {48, "AB05"},
        {46, "AB03"}, {35, "AC06"}, {12, "AE11"},
        {37, "AC08"}, {21, "AD06"}, {24, "AD09"},
        {19, "AD04"}, {14, "BKSP"}, {203, "LEFT"},
        {205, "RGHT"}, {28, "RTRN"}
    };
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_input_stats stats;
    struct nb_host_event event;
    size_t index;
    size_t mapped = 0;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    CHECK(!nb_wscons_input_reducer_set_keycode(&reducer, -1, "AC01"));
    CHECK(!nb_wscons_input_reducer_set_keycode(
        &reducer, NB_WSCONS_KEYCODE_CAPACITY, "AC01"));
    CHECK(!nb_wscons_input_reducer_set_keycode(&reducer, 30, ""));
    CHECK(!nb_wscons_input_reducer_set_keycode(&reducer, 30, "ABCDE"));
    CHECK(nb_wscons_input_reducer_set_pc_xt_keycodes(&reducer));
    CHECK(!nb_wscons_input_reducer_set_pc_xt_keycodes(&reducer));

    for (index = 0; index < NB_WSCONS_KEYCODE_CAPACITY; ++index) {
        if (reducer.keyboard_keys[index].xkb_key_name[0] != '\0') {
            ++mapped;
        }
    }
    CHECK(mapped >= 100);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_binding_count == mapped);
    CHECK(strcmp(reducer.keyboard_keys[1].xkb_key_name, "ESC") == 0);
    CHECK(strcmp(reducer.keyboard_keys[14].xkb_key_name, "BKSP") == 0);
    CHECK(strcmp(reducer.keyboard_keys[30].xkb_key_name, "AC01") == 0);
    CHECK(strcmp(reducer.keyboard_keys[42].xkb_key_name, "LFSH") == 0);
    CHECK(strcmp(reducer.keyboard_keys[53].xkb_key_name, "AB10") == 0);
    CHECK(strcmp(reducer.keyboard_keys[57].xkb_key_name, "SPCE") == 0);
    CHECK(strcmp(reducer.keyboard_keys[156].xkb_key_name, "KPEN") == 0);
    CHECK(strcmp(reducer.keyboard_keys[184].xkb_key_name, "RALT") == 0);
    CHECK(strcmp(reducer.keyboard_keys[200].xkb_key_name, "UP") == 0);
    CHECK(strcmp(reducer.keyboard_keys[211].xkb_key_name, "DELE") == 0);
    CHECK(strcmp(reducer.keyboard_keys[221].xkb_key_name, "MENU") == 0);
    for (index = 0;
         index < sizeof(address_probe_keys) / sizeof(address_probe_keys[0]);
         ++index) {
        if (strcmp(reducer.keyboard_keys[
                       (size_t)address_probe_keys[index].keycode]
                       .xkb_key_name,
                   address_probe_keys[index].xkb_key_name) != 0) {
            fprintf(stderr,
                    "PC-XT code %d: expected %s, received %s\n",
                    address_probe_keys[index].keycode,
                    address_probe_keys[index].xkb_key_name,
                    reducer.keyboard_keys[
                        (size_t)address_probe_keys[index].keycode]
                        .xkb_key_name);
        }
        CHECK(strcmp(reducer.keyboard_keys[
                         (size_t)address_probe_keys[index].keycode]
                         .xkb_key_name,
                     address_probe_keys[index].xkb_key_name) == 0);
    }

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                42,
                900,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "LFSH") == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                30,
                901,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "AC01") == 0);
    CHECK(event.data.key.pressed && !event.data.key.repeat);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                30,
                902,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.key.repeat);
    CHECK(!nb_wscons_input_reducer_set_keycode(&reducer, 31, "AC02"));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                30,
                903,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                42,
                904,
                &event) == NB_WSCONS_REDUCE_EVENT);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                29,
                905,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "LCTL") == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                38,
                906,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "AC09") == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                38,
                907,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                29,
                908,
                &event) == NB_WSCONS_REDUCE_EVENT);

    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                14,
                909,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "BKSP") == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                14,
                910,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                211,
                911,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "DELE") == 0);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                211,
                912,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                NB_WSCONS_KEYCODE_CAPACITY,
                913,
                &event) == NB_WSCONS_REDUCE_IGNORED);
}

static void test_keyboard_mux_inventory(void)
{
    const enum nb_wscons_mux_device_type keyboard[] = {
        NB_WSCONS_MUX_DEVICE_KEYBOARD
    };
    const enum nb_wscons_mux_device_type two_keyboards[] = {
        NB_WSCONS_MUX_DEVICE_KEYBOARD,
        NB_WSCONS_MUX_DEVICE_KEYBOARD
    };
    const enum nb_wscons_mux_device_type nested_mux[] = {
        NB_WSCONS_MUX_DEVICE_KEYBOARD,
        NB_WSCONS_MUX_DEVICE_MUX
    };
    const enum nb_wscons_mux_device_type keyboard_and_mouse[] = {
        NB_WSCONS_MUX_DEVICE_KEYBOARD,
        NB_WSCONS_MUX_DEVICE_MOUSE
    };
    const enum nb_wscons_mux_device_type unknown[] = {
        NB_WSCONS_MUX_DEVICE_UNKNOWN
    };

    CHECK(!nb_wscons_input_mux_is_single_keyboard(NULL, 0));
    CHECK(!nb_wscons_input_mux_is_single_keyboard(keyboard, 0));
    CHECK(nb_wscons_input_mux_is_single_keyboard(keyboard, 1));
    CHECK(!nb_wscons_input_mux_is_single_keyboard(two_keyboards, 2));
    CHECK(!nb_wscons_input_mux_is_single_keyboard(nested_mux, 2));
    CHECK(!nb_wscons_input_mux_is_single_keyboard(
        keyboard_and_mouse,
        2));
    CHECK(!nb_wscons_input_mux_is_single_keyboard(unknown, 1));
}

static void test_pc_xt_all_keys_up(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_input_stats stats;
    struct nb_host_event event;
    size_t released = 0;
    int keycode;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    CHECK(nb_wscons_input_reducer_set_pc_xt_keycodes(&reducer));
    for (keycode = 1; keycode <= 50; ++keycode) {
        CHECK(apply(&reducer,
                    NB_WSCONS_RAW_EVENT_KEY_DOWN,
                    keycode,
                    UINT64_C(1000) + (uint64_t)keycode,
                    &event) == NB_WSCONS_REDUCE_EVENT);
    }
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                1100,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(!event.data.key.pressed);
    CHECK(strcmp(event.data.key.xkb_key_name, "ESC") == 0);
    ++released;
    while (nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
           NB_WSCONS_REDUCE_EVENT) {
        CHECK(!event.data.key.pressed);
        CHECK(event.milliseconds == 1100);
        ++released;
    }
    CHECK(released == 50);
    CHECK(reducer.pending_key_release_count == 0);
    CHECK(reducer.pending_key_release_mask == 0);
    CHECK(reducer.pending_key_release_cursor == 0);
    for (keycode = 1; keycode <= 50; ++keycode) {
        CHECK(!reducer.keyboard_keys[(size_t)keycode].pressed);
        CHECK(!reducer.keyboard_keys[(size_t)keycode].release_pending);
    }
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                30,
                1101,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_synthesized_release_events == 50);
}

static void test_ascii_escape_without_keycode(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_input_stats stats;
    struct nb_host_event event;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ASCII,
                0x1b,
                700,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "ESC") == 0);
    CHECK(event.data.key.pressed);
    CHECK(!event.data.key.repeat);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ASCII,
                0x1b,
                701,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(event.data.key.repeat);
    CHECK(!nb_wscons_input_reducer_set_escape_keycode(&reducer, 42));
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                702,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(strcmp(event.data.key.xkb_key_name, "ESC") == 0);
    CHECK(!event.data.key.pressed);
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_IGNORED);
    CHECK(nb_wscons_input_reducer_set_escape_keycode(&reducer, 42));

    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_events == 3);
    CHECK(stats.keyboard_emitted_events == 3);
    CHECK(stats.keyboard_repeat_events == 1);
    CHECK(stats.keyboard_ignored_events == 0);
    CHECK(stats.keyboard_all_keys_up_events == 1);
    CHECK(stats.keyboard_synthesized_release_events == 1);
    CHECK(stats.keyboard_binding_count == 1);
}

static void test_keyboard_counter_saturation(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_input_stats stats;
    struct nb_host_event event;

    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
    CHECK(nb_wscons_input_reducer_set_escape_keycode(&reducer, 42));
    reducer.stats.keyboard_events = UINT64_MAX;
    reducer.stats.keyboard_emitted_events = UINT64_MAX;
    reducer.stats.keyboard_repeat_events = UINT64_MAX;
    reducer.stats.keyboard_ignored_events = UINT64_MAX;
    reducer.stats.keyboard_all_keys_up_events = UINT64_MAX;
    reducer.stats.keyboard_synthesized_release_events = UINT64_MAX;
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                42,
                800,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_DOWN,
                42,
                801,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_ALL_KEYS_UP,
                0,
                802,
                &event) == NB_WSCONS_REDUCE_EVENT);
    CHECK(apply(&reducer,
                NB_WSCONS_RAW_EVENT_KEY_UP,
                42,
                803,
                &event) == NB_WSCONS_REDUCE_IGNORED);
    CHECK(nb_wscons_input_reducer_get_stats(&reducer, &stats));
    CHECK(stats.keyboard_events == UINT64_MAX);
    CHECK(stats.keyboard_emitted_events == UINT64_MAX);
    CHECK(stats.keyboard_repeat_events == UINT64_MAX);
    CHECK(stats.keyboard_ignored_events == UINT64_MAX);
    CHECK(stats.keyboard_all_keys_up_events == UINT64_MAX);
    CHECK(stats.keyboard_synthesized_release_events == UINT64_MAX);
    CHECK(stats.keyboard_binding_count == 1);
}

static void test_defensive_reducer(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_raw_event raw = {
        .type = NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X,
        .value = 1,
        .milliseconds = 400
    };
    struct nb_host_event event;

    CHECK(nb_wscons_input_reducer_init(&reducer, 10, 10));
    CHECK(nb_wscons_input_reducer_poll_pending(NULL, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, NULL) ==
          NB_WSCONS_REDUCE_ERROR);
    CHECK(nb_wscons_input_reducer_apply(NULL, &raw, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    CHECK(nb_wscons_input_reducer_apply(&reducer, NULL, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    CHECK(nb_wscons_input_reducer_apply(&reducer, &raw, NULL) ==
          NB_WSCONS_REDUCE_ERROR);
    reducer.pointer_x = 10;
    CHECK(nb_wscons_input_reducer_apply(&reducer, &raw, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    reducer.pointer_x = 4;
    reducer.pointer_direction_x = 2;
    CHECK(nb_wscons_input_reducer_apply(&reducer, &raw, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    reducer.pointer_direction_x = 0;
    reducer.pointer_direction_y = -2;
    CHECK(nb_wscons_input_reducer_apply(&reducer, &raw, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    reducer.pointer_direction_y = 0;
    reducer.control_keys[NB_WSCONS_CONTROL_KEY_ESCAPE].keycode = 42;
    reducer.control_keys[NB_WSCONS_CONTROL_KEY_F10].keycode = 42;
    CHECK(nb_wscons_input_reducer_apply(&reducer, &raw, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    reducer.control_keys[NB_WSCONS_CONTROL_KEY_F10].keycode = -1;
    reducer.pending_key_release_mask =
        UINT32_C(1) << (unsigned int)NB_WSCONS_CONTROL_KEY_COUNT;
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    reducer.pending_key_release_mask = 0;
    reducer.pending_key_release_milliseconds = 1;
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_ERROR);

    CHECK(nb_wscons_input_reducer_init(&reducer, 10, 10));
    CHECK(nb_wscons_input_reducer_set_escape_keycode(&reducer, 42));
    reducer.stats.keyboard_binding_count = 0;
    CHECK(nb_wscons_input_reducer_apply(&reducer, &raw, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    reducer.stats.keyboard_binding_count = 1;
    reducer.control_keys[NB_WSCONS_CONTROL_KEY_ESCAPE].pressed = true;
    reducer.pending_key_release_mask =
        UINT32_C(1) << (unsigned int)NB_WSCONS_CONTROL_KEY_ESCAPE;
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_ERROR);

    CHECK(nb_wscons_input_reducer_init(&reducer, 10, 10));
    CHECK(nb_wscons_input_reducer_set_keycode(&reducer, 30, "AC01"));
    reducer.keyboard_keys[30].release_pending = true;
    reducer.pending_key_release_count = 1;
    reducer.pending_key_release_cursor = 31;
    reducer.pending_key_release_milliseconds = 2;
    CHECK(nb_wscons_input_reducer_poll_pending(&reducer, &event) ==
          NB_WSCONS_REDUCE_ERROR);
}

static void test_live_provider_without_devices(void)
{
    struct nb_wscons_input *input;
    struct nb_host_event event;
    struct nb_wscons_input_stats stats;
    char message[NB_WSCONS_INPUT_ERROR_CAPACITY];
    int wait_descriptors[NB_WSCONS_INPUT_WAIT_DESCRIPTOR_COUNT] = {7, 8};
    int system_error = 0;
    int x = -1;
    int y = -1;

    CHECK(nb_wscons_input_create(0, 10) == NULL);
    input = nb_wscons_input_create(20, 10);
    CHECK(input != NULL);
    if (input == NULL) {
        return;
    }
    CHECK(!nb_wscons_input_is_active(input));
    CHECK(!nb_wscons_input_get_wait_descriptors(NULL, wait_descriptors));
    CHECK(wait_descriptors[0] == -1);
    CHECK(wait_descriptors[1] == -1);
    wait_descriptors[0] = 7;
    wait_descriptors[1] = 8;
    CHECK(!nb_wscons_input_get_wait_descriptors(input, wait_descriptors));
    CHECK(wait_descriptors[0] == -1);
    CHECK(wait_descriptors[1] == -1);
    CHECK(!nb_wscons_input_get_wait_descriptors(input, NULL));
    CHECK(!nb_wscons_input_set_pointer_sensitivity(
        NULL,
        NB_WSCONS_POINTER_SENSITIVITY_DEFAULT_PERCENT));
    CHECK(!nb_wscons_input_set_pointer_profile(
        NULL,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(!nb_wscons_input_set_pointer_sensitivity(
        input,
        NB_WSCONS_POINTER_SENSITIVITY_MIN_PERCENT - 1U));
    CHECK(!nb_wscons_input_set_pointer_sensitivity(
        input,
        NB_WSCONS_POINTER_SENSITIVITY_MAX_PERCENT + 1U));
    CHECK(nb_wscons_input_set_pointer_sensitivity(input, 150));
    CHECK(nb_wscons_input_set_pointer_profile(
        input,
        NB_WSCONS_POINTER_PROFILE_ADAPTIVE));
    CHECK(nb_wscons_input_set_pointer_profile(
        input,
        NB_WSCONS_POINTER_PROFILE_FLAT));
    CHECK(!nb_wscons_input_set_pointer_profile(
        input,
        (enum nb_wscons_pointer_profile)2));
    CHECK(nb_wscons_input_get_position(input, &x, &y));
    CHECK(x == 9);
    CHECK(y == 4);
    CHECK(nb_wscons_input_poll(input, &event) ==
          NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(event.type == NB_HOST_EVENT_NONE);
    CHECK(!nb_wscons_input_get_last_error(input,
                                          &system_error,
                                          message,
                                          sizeof(message)));
    CHECK(nb_wscons_input_set_bounds(input, 5, 4));
    CHECK(nb_wscons_input_get_position(input, &x, &y));
    CHECK(x == 4);
    CHECK(y == 3);

#if !defined(__NetBSD__)
    CHECK(!nb_wscons_input_resume(input));
    CHECK(!nb_wscons_input_is_active(input));
    CHECK(nb_wscons_input_get_last_error(input,
                                         &system_error,
                                         message,
                                         sizeof(message)));
    CHECK(system_error == ENOTSUP);
    CHECK(message[0] != '\0');
#endif

    memset(&stats, 0xa5, sizeof(stats));
    CHECK(nb_wscons_input_get_stats(input, &stats));
    CHECK(stats.native_events_read == 0);
    CHECK(stats.untranslated_native_events == 0);
    CHECK(stats.relative_events == 0);
    CHECK(!nb_wscons_input_get_stats(NULL, &stats));
    CHECK(!nb_wscons_input_get_stats(input, NULL));
    nb_wscons_input_suspend(input);
    CHECK(nb_wscons_input_set_pointer_sensitivity(input, 175));
    CHECK(nb_wscons_input_get_stats(input, &stats));
    CHECK(stats.relative_events == 0);
    nb_wscons_input_destroy(input);
    nb_wscons_input_destroy(NULL);
    CHECK(nb_wscons_input_poll(NULL, &event) ==
          NB_HOST_EVENT_STATUS_ERROR);
}

int main(void)
{
    test_initialization_and_bounds();
    test_relative_pointer_motion();
    test_fractional_pointer_sensitivity();
    test_pointer_sensitivity_edges();
    test_adaptive_pointer_velocity();
    test_adaptive_gain_boundaries();
    test_adaptive_native_motion_timestamps();
    test_adaptive_timestamp_velocity_equivalence();
    test_adaptive_timestamp_boundaries_and_extremes();
    test_adaptive_pointer_carry_stabilization();
    test_adaptive_pointer_resets();
    test_adaptive_pointer_symmetry_and_saturation();
    test_pointer_statistics();
    test_pointer_buttons();
    test_escape_translation();
    test_control_key_translation_and_diagnostics();
    test_all_keys_up_pending_releases();
    test_pc_xt_keyboard_profile();
    test_pc_xt_all_keys_up();
    test_keyboard_mux_inventory();
    test_ascii_escape_without_keycode();
    test_keyboard_counter_saturation();
    test_defensive_reducer();
    test_live_provider_without_devices();

    if (failures != 0) {
        fprintf(stderr, "wscons input tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("wscons input tests: ok");
    return 0;
}
