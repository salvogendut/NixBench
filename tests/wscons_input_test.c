#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wscons_input.h"

static int failures;

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
    const struct nb_wscons_raw_event raw = {type, value, milliseconds};

    return nb_wscons_input_reducer_apply(reducer, &raw, event);
}

static void test_initialization_and_bounds(void)
{
    struct nb_wscons_input_reducer reducer;
    int x = -1;
    int y = -1;

    CHECK(!nb_wscons_input_reducer_init(NULL, 100, 80));
    CHECK(!nb_wscons_input_reducer_init(&reducer, 0, 80));
    CHECK(!nb_wscons_input_reducer_init(&reducer, 100, -1));
    CHECK(nb_wscons_input_reducer_init(&reducer, 100, 80));
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
    CHECK(reducer.escape_keycode == 42);
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

    /* wscons relative Y is positive upwards; desktop Y is positive downwards. */
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
    nb_wscons_input_reducer_reset(&reducer);
    CHECK(!reducer.escape_pressed);
}

static void test_defensive_reducer(void)
{
    struct nb_wscons_input_reducer reducer;
    struct nb_wscons_raw_event raw = {
        NB_WSCONS_RAW_EVENT_MOUSE_DELTA_X, 1, 400
    };
    struct nb_host_event event;

    CHECK(nb_wscons_input_reducer_init(&reducer, 10, 10));
    CHECK(nb_wscons_input_reducer_apply(NULL, &raw, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    CHECK(nb_wscons_input_reducer_apply(&reducer, NULL, &event) ==
          NB_WSCONS_REDUCE_ERROR);
    CHECK(nb_wscons_input_reducer_apply(&reducer, &raw, NULL) ==
          NB_WSCONS_REDUCE_ERROR);
    reducer.pointer_x = 10;
    CHECK(nb_wscons_input_reducer_apply(&reducer, &raw, &event) ==
          NB_WSCONS_REDUCE_ERROR);
}

static void test_live_provider_without_devices(void)
{
    struct nb_wscons_input *input;
    struct nb_host_event event;
    char message[NB_WSCONS_INPUT_ERROR_CAPACITY];
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

    nb_wscons_input_suspend(input);
    nb_wscons_input_destroy(input);
    nb_wscons_input_destroy(NULL);
    CHECK(nb_wscons_input_poll(NULL, &event) ==
          NB_HOST_EVENT_STATUS_ERROR);
}

int main(void)
{
    test_initialization_and_bounds();
    test_relative_pointer_motion();
    test_pointer_buttons();
    test_escape_translation();
    test_defensive_reducer();
    test_live_provider_without_devices();

    if (failures != 0) {
        fprintf(stderr, "wscons input tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("wscons input tests: ok");
    return 0;
}
