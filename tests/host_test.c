#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "host.h"
#include "host_headless.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static const struct nb_host_output initial_output = {
    4, 3, 4, 3, 60000
};

static struct nb_host_event focus_event(bool focused, uint64_t milliseconds)
{
    struct nb_host_event event = {0};

    event.type = NB_HOST_EVENT_FOCUS_CHANGED;
    event.milliseconds = milliseconds;
    event.data.focus.focused = focused;
    return event;
}

static struct nb_host_event key_event(const char *name,
                                      bool pressed,
                                      bool repeat,
                                      uint64_t milliseconds)
{
    struct nb_host_event event = {0};

    event.type = NB_HOST_EVENT_KEY;
    event.milliseconds = milliseconds;
    event.data.key.pressed = pressed;
    event.data.key.repeat = repeat;
    (void)snprintf(event.data.key.xkb_key_name,
                   sizeof(event.data.key.xkb_key_name),
                   "%s",
                   name);
    return event;
}

static void test_contract_validation(void)
{
    struct nb_host_output output = initial_output;
    uint32_t pixels[12] = {0};
    struct nb_host_frame frame = {
        pixels,
        4,
        3,
        4 * sizeof(uint32_t),
        NB_HOST_PIXEL_FORMAT_XRGB8888,
        1
    };
    struct nb_host_event event = focus_event(true, 1);

    CHECK(nb_host_output_is_valid(&output));
    CHECK(!nb_host_output_is_valid(NULL));
    output.logical_width = 0;
    CHECK(!nb_host_output_is_valid(&output));
    output = initial_output;
    output.refresh_millihertz = -1;
    CHECK(!nb_host_output_is_valid(&output));

    CHECK(nb_host_frame_is_valid(&frame));
    CHECK(!nb_host_frame_is_valid(NULL));
    frame.serial = 0;
    CHECK(!nb_host_frame_is_valid(&frame));
    frame.serial = 1;
    frame.stride = 15;
    CHECK(!nb_host_frame_is_valid(&frame));
    frame.stride = 4 * sizeof(uint32_t);
    frame.format = (enum nb_host_pixel_format)99;
    CHECK(!nb_host_frame_is_valid(&frame));

    CHECK(nb_host_event_is_valid(&event));
    event.type = NB_HOST_EVENT_NONE;
    CHECK(!nb_host_event_is_valid(&event));
    event = key_event("AC01", true, false, 2);
    CHECK(nb_host_event_is_valid(&event));
    event.data.key.repeat = true;
    CHECK(nb_host_event_is_valid(&event));
    event.data.key.pressed = false;
    CHECK(!nb_host_event_is_valid(&event));
    event = key_event("", true, false, 3);
    CHECK(!nb_host_event_is_valid(&event));
    event = key_event("AC01", true, false, 4);
    memset(event.data.key.xkb_key_name,
           'A',
           sizeof(event.data.key.xkb_key_name));
    CHECK(!nb_host_event_is_valid(&event));
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_BUTTON;
    event.data.pointer_button.button =
        (enum nb_host_pointer_button)NB_HOST_POINTER_BUTTON_COUNT;
    CHECK(!nb_host_event_is_valid(&event));
}

static void test_output_clock_and_capture(void)
{
    struct nb_host *host = nb_host_headless_create(&initial_output);
    struct nb_host_output output;
    struct nb_host_output resized = {8, 6, 8, 6, 75000};
    struct nb_host_event event = focus_event(true, 999);

    CHECK(host != NULL);
    CHECK(nb_host_get_output(host, &output));
    CHECK(memcmp(&output, &initial_output, sizeof(output)) == 0);
    CHECK(nb_host_monotonic_milliseconds(host) == 0);
    CHECK(nb_host_wait_event(host, 25, &event) ==
          NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(event.type == NB_HOST_EVENT_NONE);
    CHECK(nb_host_monotonic_milliseconds(host) == 25);
    CHECK(nb_host_headless_advance_time(host, 75));
    CHECK(nb_host_monotonic_milliseconds(host) == 100);

    CHECK(!nb_host_headless_pointer_is_captured(host));
    CHECK(nb_host_set_pointer_capture(host, true));
    CHECK(nb_host_headless_pointer_is_captured(host));
    CHECK(nb_host_set_pointer_capture(host, false));
    CHECK(!nb_host_headless_pointer_is_captured(host));

    CHECK(nb_host_headless_set_output(host, &resized, 100));
    CHECK(nb_host_headless_pending_event_count(host) == 1);
    CHECK(nb_host_get_output(host, &output));
    CHECK(memcmp(&output, &resized, sizeof(output)) == 0);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_OUTPUT_CHANGED);
    CHECK(event.milliseconds == 100);
    CHECK(event.data.output.pixel_width == 8);
    CHECK(event.data.output.refresh_millihertz == 75000);
    CHECK(nb_host_headless_set_output(host, &resized, 101));
    CHECK(nb_host_headless_pending_event_count(host) == 0);

    resized.pixel_height = 0;
    CHECK(!nb_host_headless_set_output(host, &resized, 102));
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_EMPTY);
    nb_host_destroy(host);
}

static void test_event_fifo_and_capacity(void)
{
    struct nb_host *host = nb_host_headless_create(&initial_output);
    struct nb_host_event expected[5];
    struct nb_host_event actual;
    struct nb_host_output output;
    const struct nb_host_output resized = {5, 4, 5, 4, 60000};
    uint32_t pixels[12] = {0};
    const struct nb_host_frame frame = {
        pixels,
        4,
        3,
        4 * sizeof(uint32_t),
        NB_HOST_PIXEL_FORMAT_XRGB8888,
        1
    };
    size_t index;

    expected[0] = focus_event(true, 10);
    memset(&expected[1], 0, sizeof(expected[1]));
    expected[1].type = NB_HOST_EVENT_POINTER_MOTION;
    expected[1].milliseconds = 11;
    expected[1].data.pointer_motion.x = -4;
    expected[1].data.pointer_motion.y = 12;
    memset(&expected[2], 0, sizeof(expected[2]));
    expected[2].type = NB_HOST_EVENT_POINTER_BUTTON;
    expected[2].milliseconds = 12;
    expected[2].data.pointer_button.x = 2;
    expected[2].data.pointer_button.y = 1;
    expected[2].data.pointer_button.button = NB_HOST_POINTER_BUTTON_LEFT;
    expected[2].data.pointer_button.pressed = true;
    expected[3] = key_event("RTRN", true, false, 13);
    memset(&expected[4], 0, sizeof(expected[4]));
    expected[4].type = NB_HOST_EVENT_QUIT;
    expected[4].milliseconds = 14;

    CHECK(host != NULL);
    CHECK(!nb_host_headless_enqueue_event(host, &expected[0]));
    CHECK(nb_host_headless_advance_time(host, 14));
    for (index = 0; index < 5; ++index) {
        CHECK(nb_host_headless_enqueue_event(host, &expected[index]));
    }
    CHECK(nb_host_headless_pending_event_count(host) == 5);
    for (index = 0; index < 5; ++index) {
        CHECK(nb_host_wait_event(host, 500, &actual) ==
              NB_HOST_EVENT_STATUS_AVAILABLE);
        CHECK(memcmp(&actual, &expected[index], sizeof(actual)) == 0);
    }
    CHECK(nb_host_monotonic_milliseconds(host) == 14);

    memset(&actual, 0, sizeof(actual));
    actual.type = NB_HOST_EVENT_FRAME_COMPLETE;
    actual.data.frame_complete.frame_serial = 1;
    CHECK(!nb_host_headless_enqueue_event(host, &actual));
    actual.type = NB_HOST_EVENT_OUTPUT_CHANGED;
    actual.data.output = initial_output;
    CHECK(!nb_host_headless_enqueue_event(host, &actual));

    actual = focus_event(false, 20);
    CHECK(nb_host_headless_advance_time(host, 6));
    for (index = 0; index < NB_HOST_HEADLESS_EVENT_CAPACITY; ++index) {
        CHECK(nb_host_headless_enqueue_event(host, &actual));
    }
    CHECK(!nb_host_headless_enqueue_event(host, &actual));
    CHECK(nb_host_headless_pending_event_count(host) ==
          NB_HOST_HEADLESS_EVENT_CAPACITY);
    CHECK(!nb_host_headless_set_output(host, &resized, 20));
    CHECK(nb_host_get_output(host, &output));
    CHECK(memcmp(&output, &initial_output, sizeof(output)) == 0);
    CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_WOULD_BLOCK);
    CHECK(nb_host_headless_presentation_count(host) == 0);
    for (index = 0; index < NB_HOST_HEADLESS_EVENT_CAPACITY; ++index) {
        CHECK(nb_host_poll_event(host, &actual) ==
              NB_HOST_EVENT_STATUS_AVAILABLE);
    }
    CHECK(nb_host_poll_event(host, &actual) == NB_HOST_EVENT_STATUS_EMPTY);
    CHECK(nb_host_present(host, &frame) ==
          NB_HOST_RESULT_OK);
    CHECK(nb_host_headless_presentation_count(host) == 1);
    CHECK(nb_host_poll_event(host, &actual) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(actual.type == NB_HOST_EVENT_FRAME_COMPLETE);
    CHECK(actual.data.frame_complete.frame_serial == 1);
    nb_host_destroy(host);
}

static void test_present_copy_and_completion(void)
{
    struct nb_host *host = nb_host_headless_create(&initial_output);
    unsigned char source[3][20];
    struct nb_host_frame frame = {
        source,
        4,
        3,
        sizeof(source[0]),
        NB_HOST_PIXEL_FORMAT_ARGB8888_PREMULTIPLIED,
        7
    };
    struct nb_host_frame presented;
    struct nb_host_event event;
    unsigned char expected[3][16];
    size_t row;
    size_t byte;

    CHECK(host != NULL);
    CHECK(!nb_host_headless_last_presented_frame(host, &presented));
    for (row = 0; row < 3; ++row) {
        for (byte = 0; byte < sizeof(source[row]); ++byte) {
            source[row][byte] = (unsigned char)((row * 32) + byte);
        }
        memcpy(expected[row], source[row], sizeof(expected[row]));
    }
    CHECK(nb_host_headless_advance_time(host, 123));
    CHECK(nb_host_present(host, &frame) ==
          NB_HOST_RESULT_OK);
    CHECK(nb_host_headless_presentation_count(host) == 1);
    memset(source, 0xff, sizeof(source));

    CHECK(nb_host_headless_last_presented_frame(host, &presented));
    CHECK(presented.pixels != frame.pixels);
    CHECK(presented.width == 4);
    CHECK(presented.height == 3);
    CHECK(presented.stride == sizeof(expected[0]));
    CHECK(presented.format == NB_HOST_PIXEL_FORMAT_ARGB8888_PREMULTIPLIED);
    CHECK(presented.serial == 7);
    for (row = 0; row < 3; ++row) {
        CHECK(memcmp((const unsigned char *)presented.pixels +
                         (row * presented.stride),
                     expected[row],
                     sizeof(expected[row])) == 0);
    }

    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_FRAME_COMPLETE);
    CHECK(event.milliseconds == 123);
    CHECK(event.data.frame_complete.frame_serial == 7);
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);

    frame.serial = 7;
    CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_INVALID_STATE);
    frame.serial = 6;
    CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_INVALID_STATE);
    CHECK(nb_host_headless_presentation_count(host) == 1);

    frame.serial = 8;
    frame.width = 3;
    CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_INVALID_ARGUMENT);
    frame.width = 4;
    frame.height = 2;
    CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_INVALID_ARGUMENT);
    CHECK(nb_host_headless_presentation_count(host) == 1);
    nb_host_destroy(host);
}

static void test_console_release_acquire_and_retry(void)
{
    struct nb_host *host = nb_host_headless_create(&initial_output);
    uint32_t pixels[12] = {0};
    const struct nb_host_frame frame = {
        pixels,
        4,
        3,
        4 * sizeof(uint32_t),
        NB_HOST_PIXEL_FORMAT_XRGB8888,
        1
    };
    struct nb_host_event event;
    const struct nb_host_event queued_input = focus_event(false, 50);

    CHECK(host != NULL);
    CHECK(nb_host_headless_advance_time(host, 50));
    CHECK(nb_host_set_pointer_capture(host, true));
    CHECK(nb_host_headless_enqueue_event(host, &queued_input));
    CHECK(nb_host_headless_request_console_release(host, 50));
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_RELEASE_PENDING);
    CHECK(nb_host_headless_pointer_is_captured(host));
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED);
    CHECK(event.milliseconds == 50);
    CHECK(nb_host_poll_event(host, &event) == NB_HOST_EVENT_STATUS_EMPTY);

    CHECK(nb_host_present(host, &frame) ==
          NB_HOST_RESULT_SUSPENDED);
    CHECK(nb_host_headless_presentation_count(host) == 0);
    CHECK(nb_host_complete_console_release(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_SUSPENDED);
    CHECK(!nb_host_headless_pointer_is_captured(host));
    CHECK(!nb_host_headless_request_console_release(host, 50));
    CHECK(nb_host_complete_console_release(host) ==
          NB_HOST_RESULT_INVALID_STATE);
    CHECK(!nb_host_headless_enqueue_event(host, &queued_input));
    CHECK(nb_host_headless_pending_event_count(host) == 0);

    CHECK(nb_host_headless_advance_time(host, 25));
    CHECK(nb_host_headless_request_console_acquire(host, 75));
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACQUIRE_PENDING);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED);
    CHECK(event.milliseconds == 75);
    CHECK(nb_host_complete_console_acquire(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACTIVE);
    CHECK(nb_host_complete_console_acquire(host) ==
          NB_HOST_RESULT_INVALID_STATE);

    CHECK(nb_host_present(host, &frame) ==
          NB_HOST_RESULT_OK);
    CHECK(nb_host_headless_presentation_count(host) == 1);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_FRAME_COMPLETE);
    CHECK(event.data.frame_complete.frame_serial == frame.serial);
    nb_host_destroy(host);
}

static void test_lifecycle_priority_preserves_critical_events(void)
{
    struct nb_host *host = nb_host_headless_create(&initial_output);
    struct nb_host_event input = focus_event(true, 10);
    struct nb_host_event quit = {0};
    struct nb_host_event event;
    uint32_t pixels[12] = {0};
    const struct nb_host_frame frame = {
        pixels,
        4,
        3,
        4 * sizeof(uint32_t),
        NB_HOST_PIXEL_FORMAT_XRGB8888,
        9
    };
    size_t index;

    CHECK(host != NULL);
    CHECK(nb_host_headless_advance_time(host, 10));
    for (index = 0; index < NB_HOST_HEADLESS_EVENT_CAPACITY - 2;
         ++index) {
        CHECK(nb_host_headless_enqueue_event(host, &input));
    }
    quit.type = NB_HOST_EVENT_QUIT;
    quit.milliseconds = 10;
    CHECK(nb_host_headless_enqueue_event(host, &quit));
    CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_OK);
    CHECK(nb_host_headless_pending_event_count(host) ==
          NB_HOST_HEADLESS_EVENT_CAPACITY);

    CHECK(nb_host_headless_request_console_release(host, 10));
    CHECK(nb_host_headless_pending_event_count(host) == 3);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_QUIT);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_FRAME_COMPLETE);
    CHECK(event.data.frame_complete.frame_serial == frame.serial);
    CHECK(nb_host_complete_console_release(host) == NB_HOST_RESULT_OK);
    nb_host_destroy(host);
}

static void test_output_change_during_suspend(void)
{
    const struct nb_host_output resized = {5, 4, 5, 4, 60000};
    struct nb_host *host = nb_host_headless_create(&initial_output);
    struct nb_host_event event;
    uint32_t pixels[20] = {0};
    const struct nb_host_frame frame = {
        pixels,
        5,
        4,
        5 * sizeof(uint32_t),
        NB_HOST_PIXEL_FORMAT_XRGB8888,
        1
    };

    CHECK(host != NULL);
    CHECK(nb_host_headless_request_console_release(host, 0));
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(nb_host_complete_console_release(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_headless_set_output(host, &resized, 0));
    CHECK(nb_host_headless_request_console_acquire(host, 0));

    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED);
    CHECK(nb_host_complete_console_acquire(host) == NB_HOST_RESULT_OK);
    CHECK(nb_host_poll_event(host, &event) ==
          NB_HOST_EVENT_STATUS_AVAILABLE);
    CHECK(event.type == NB_HOST_EVENT_OUTPUT_CHANGED);
    CHECK(event.data.output.pixel_width == resized.pixel_width);
    CHECK(event.data.output.pixel_height == resized.pixel_height);
    CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_OK);
    nb_host_destroy(host);
}

static void test_defensive_api(void)
{
    struct nb_host_event event = {0};
    struct nb_host_output output = {0};
    struct nb_host_frame frame = {0};
    int system_error;
    char message[16];

    CHECK(nb_host_headless_create(NULL) == NULL);
    CHECK(!nb_host_get_output(NULL, &output));
    CHECK(!nb_host_get_output(NULL, NULL));
    CHECK(nb_host_get_state(NULL) == NB_HOST_STATE_FAILED);
    CHECK(nb_host_monotonic_milliseconds(NULL) == 0);
    CHECK(nb_host_poll_event(NULL, &event) == NB_HOST_EVENT_STATUS_ERROR);
    CHECK(nb_host_wait_event(NULL, 0, &event) == NB_HOST_EVENT_STATUS_ERROR);
    CHECK(!nb_host_set_pointer_capture(NULL, true));
    CHECK(nb_host_present(NULL, &frame) == NB_HOST_RESULT_INVALID_ARGUMENT);
    CHECK(nb_host_complete_console_release(NULL) ==
          NB_HOST_RESULT_INVALID_ARGUMENT);
    CHECK(nb_host_complete_console_acquire(NULL) ==
          NB_HOST_RESULT_INVALID_ARGUMENT);
    CHECK(!nb_host_get_last_error(NULL,
                                  &system_error,
                                  message,
                                  sizeof(message)));
    CHECK(!nb_host_headless_enqueue_event(NULL, &event));
    CHECK(!nb_host_headless_advance_time(NULL, 1));
    CHECK(nb_host_headless_pending_event_count(NULL) == 0);
    CHECK(nb_host_headless_presentation_count(NULL) == 0);
    CHECK(!nb_host_headless_pointer_is_captured(NULL));
    CHECK(!nb_host_headless_last_presented_frame(NULL, &frame));
    nb_host_destroy(NULL);
}

int main(void)
{
    test_contract_validation();
    test_output_clock_and_capture();
    test_event_fifo_and_capacity();
    test_present_copy_and_completion();
    test_console_release_acquire_and_retry();
    test_lifecycle_priority_preserves_critical_events();
    test_output_change_during_suspend();
    test_defensive_api();

    if (failures != 0) {
        fprintf(stderr, "host tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("host tests: ok");
    return 0;
}
