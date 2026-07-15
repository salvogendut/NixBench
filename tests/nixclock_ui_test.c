#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nixclock_ui.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                               \
    do {                                                                      \
        const double check_actual = (actual);                                 \
        const double check_expected = (expected);                             \
        if (fabs(check_actual - check_expected) > (tolerance)) {              \
            fprintf(stderr,                                                   \
                    "%s:%d: check failed: %s (%g) near %s (%g)\n",           \
                    __FILE__, __LINE__, #actual, check_actual,                \
                    #expected, check_expected);                               \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void test_hand_geometry(void)
{
    struct nb_nixclock_hand_geometry hands;
    struct nb_nixclock_local_time time = {0U, 0U, 0U, 0U};

    CHECK(nb_nixclock_hand_geometry(&time, false, &hands));
    CHECK_NEAR(hands.hour_tip.x, 0.0, 1.0e-12);
    CHECK_NEAR(hands.hour_tip.y, -0.48, 1.0e-12);
    CHECK_NEAR(hands.minute_tip.x, 0.0, 1.0e-12);
    CHECK_NEAR(hands.minute_tip.y, -0.70, 1.0e-12);
    CHECK(!hands.second_visible);
    CHECK_NEAR(hands.second_tip.x, 0.0, 1.0e-12);
    CHECK_NEAR(hands.second_tip.y, 0.0, 1.0e-12);

    time = (struct nb_nixclock_local_time){3U, 15U, 0U, 0U};
    CHECK(nb_nixclock_hand_geometry(&time, false, &hands));
    CHECK_NEAR(hands.hour_tip.x, 0.475893, 1.0e-6);
    CHECK_NEAR(hands.hour_tip.y, 0.062653, 1.0e-6);
    CHECK_NEAR(hands.minute_tip.x, 0.70, 1.0e-12);
    CHECK_NEAR(hands.minute_tip.y, 0.0, 1.0e-12);

    time = (struct nb_nixclock_local_time){6U, 30U, 0U, 0U};
    CHECK(nb_nixclock_hand_geometry(&time, false, &hands));
    CHECK_NEAR(hands.hour_tip.x, -0.124233, 1.0e-6);
    CHECK_NEAR(hands.hour_tip.y, 0.463644, 1.0e-6);
    CHECK_NEAR(hands.minute_tip.x, 0.0, 1.0e-12);
    CHECK_NEAR(hands.minute_tip.y, 0.70, 1.0e-12);

    time = (struct nb_nixclock_local_time){12U, 0U, 15U, 0U};
    CHECK(nb_nixclock_hand_geometry(&time, true, &hands));
    CHECK(hands.second_visible);
    CHECK_NEAR(hands.second_tip.x, 0.78, 1.0e-12);
    CHECK_NEAR(hands.second_tip.y, 0.0, 1.0e-12);
    CHECK(hands.minute_tip.x > 0.0);
    CHECK(hands.hour_tip.x > 0.0);

    time = (struct nb_nixclock_local_time){3U, 15U, 30U, 0U};
    CHECK(nb_nixclock_hand_geometry(&time, false, &hands));
    CHECK(hands.minute_tip.x < 0.70);
    CHECK(hands.minute_tip.y > 0.0);

    time.hour = 24U;
    CHECK(!nb_nixclock_hand_geometry(&time, false, &hands));
    CHECK(!nb_nixclock_hand_geometry(NULL, false, &hands));
    CHECK(!nb_nixclock_hand_geometry(&time, false, NULL));
}

static void test_update_alignment(void)
{
    struct nb_nixclock_local_time time = {8U, 12U, 0U, 0U};
    uint32_t delay = 0U;

    CHECK(nb_nixclock_next_update_delay_ms(&time, true, &delay));
    CHECK(delay == UINT32_C(1000));
    CHECK(nb_nixclock_next_update_delay_ms(&time, false, &delay));
    CHECK(delay == UINT32_C(60000));

    time.second = 59U;
    time.millisecond = 999U;
    CHECK(nb_nixclock_next_update_delay_ms(&time, true, &delay));
    CHECK(delay == UINT32_C(1));
    CHECK(nb_nixclock_next_update_delay_ms(&time, false, &delay));
    CHECK(delay == UINT32_C(1));

    time.second = 37U;
    time.millisecond = 250U;
    CHECK(nb_nixclock_next_update_delay_ms(&time, true, &delay));
    CHECK(delay == UINT32_C(750));
    CHECK(nb_nixclock_next_update_delay_ms(&time, false, &delay));
    CHECK(delay == UINT32_C(22750));

    time.millisecond = 1000U;
    CHECK(!nb_nixclock_next_update_delay_ms(&time, true, &delay));
    CHECK(!nb_nixclock_next_update_delay_ms(NULL, true, &delay));
    CHECK(!nb_nixclock_next_update_delay_ms(&time, true, NULL));
}

enum {
    WIDTH = 97,
    HEIGHT = 61,
    STRIDE = 103,
    PIXEL_COUNT = HEIGHT * STRIDE
};

static size_t count_color(const uint32_t *pixels,
                          uint32_t color,
                          int width,
                          int height,
                          size_t stride)
{
    size_t count = 0U;
    int y;

    for (y = 0; y < height; ++y) {
        int x;

        for (x = 0; x < width; ++x) {
            if (pixels[(size_t)y * stride + (size_t)x] == color) {
                ++count;
            }
        }
    }
    return count;
}

static void test_rendering(void)
{
    const uint32_t sentinel = UINT32_C(0x55aa33cc);
    const struct nb_nixclock_palette *palette = nb_nixclock_ui_palette();
    const struct nb_nixclock_local_time time = {10U, 8U, 15U, 500U};
    struct nb_nixclock_ui ui;
    uint32_t without_seconds[PIXEL_COUNT];
    uint32_t with_seconds[PIXEL_COUNT];
    size_t index;
    int y;

    nb_nixclock_ui_init(&ui, WIDTH, HEIGHT);
    CHECK(ui.width == WIDTH);
    CHECK(ui.height == HEIGHT);
    CHECK(!ui.show_seconds);
    CHECK(nb_nixclock_ui_set_local_time(&ui, &time));

    for (index = 0U; index < PIXEL_COUNT; ++index) {
        without_seconds[index] = sentinel;
        with_seconds[index] = sentinel;
    }
    CHECK(nb_nixclock_ui_render(&ui, without_seconds, STRIDE));
    CHECK(count_color(without_seconds, palette->minor_tick,
                      WIDTH, HEIGHT, STRIDE) > 0U);
    CHECK(count_color(without_seconds, palette->hour_tick,
                      WIDTH, HEIGHT, STRIDE) > 0U);
    CHECK(count_color(without_seconds, palette->second_hand,
                      WIDTH, HEIGHT, STRIDE) == 0U);
    CHECK(without_seconds[0] == palette->desktop);

    for (y = 0; y < HEIGHT; ++y) {
        int x;

        for (x = 0; x < WIDTH; ++x) {
            const uint32_t pixel =
                without_seconds[(size_t)y * STRIDE + (size_t)x];

            CHECK((pixel & UINT32_C(0xff000000)) == UINT32_C(0xff000000));
        }
        for (x = WIDTH; x < STRIDE; ++x) {
            CHECK(without_seconds[(size_t)y * STRIDE + (size_t)x] ==
                  sentinel);
        }
    }

    CHECK(nb_nixclock_ui_set_show_seconds(&ui, true));
    CHECK(!nb_nixclock_ui_set_show_seconds(&ui, true));
    CHECK(nb_nixclock_ui_render(&ui, with_seconds, STRIDE));
    CHECK(count_color(with_seconds, palette->second_hand,
                      WIDTH, HEIGHT, STRIDE) > 0U);
    CHECK(memcmp(without_seconds, with_seconds,
                 sizeof(without_seconds)) != 0);

    CHECK(!nb_nixclock_ui_render(&ui, with_seconds, WIDTH - 1U));
    CHECK(!nb_nixclock_ui_render(NULL, with_seconds, STRIDE));
    CHECK(!nb_nixclock_ui_render(&ui, NULL, STRIDE));
}

static void test_resize_and_tiny_surfaces(void)
{
    const uint32_t sentinel = UINT32_C(0x12345678);
    struct nb_nixclock_ui ui;
    uint32_t pixels[21];
    size_t index;

    nb_nixclock_ui_init(&ui, -3, -7);
    CHECK(ui.width == 0);
    CHECK(ui.height == 0);
    CHECK(!nb_nixclock_ui_render(&ui, pixels, 3U));

    for (index = 0U; index < 21U; ++index) {
        pixels[index] = sentinel;
    }
    nb_nixclock_ui_resize(&ui, 1, 1);
    CHECK(nb_nixclock_ui_render(&ui, pixels, 3U));
    CHECK((pixels[0] & UINT32_C(0xff000000)) == UINT32_C(0xff000000));
    CHECK(pixels[1] == sentinel);
    CHECK(pixels[2] == sentinel);

    nb_nixclock_ui_resize(&ui, 2, 7);
    CHECK(nb_nixclock_ui_render(&ui, pixels, 3U));
    for (index = 0U; index < 7U; ++index) {
        CHECK((pixels[index * 3U] & UINT32_C(0xff000000)) ==
              UINT32_C(0xff000000));
        CHECK((pixels[index * 3U + 1U] & UINT32_C(0xff000000)) ==
              UINT32_C(0xff000000));
        CHECK(pixels[index * 3U + 2U] == sentinel);
    }

    nb_nixclock_ui_resize(NULL, 2, 2);
    nb_nixclock_ui_init(NULL, 2, 2);
    CHECK(!nb_nixclock_ui_set_local_time(NULL, &ui.local_time));
    CHECK(!nb_nixclock_ui_set_show_seconds(NULL, true));
}

static void test_palette_is_opaque_and_distinct(void)
{
    const struct nb_nixclock_palette *palette = nb_nixclock_ui_palette();
    const uint32_t colors[] = {
        palette->desktop,
        palette->rim_shadow,
        palette->rim_highlight,
        palette->face,
        palette->minor_tick,
        palette->hour_tick,
        palette->hour_hand,
        palette->minute_hand,
        palette->second_hand,
        palette->centre_pin
    };
    size_t index;

    for (index = 0U; index < sizeof(colors) / sizeof(colors[0]); ++index) {
        CHECK((colors[index] & UINT32_C(0xff000000)) ==
              UINT32_C(0xff000000));
    }
    CHECK(palette->second_hand != palette->hour_hand);
    CHECK(palette->second_hand != palette->minute_hand);
    CHECK(palette->hour_tick != palette->minor_tick);
}

int main(void)
{
    test_hand_geometry();
    test_update_alignment();
    test_rendering();
    test_resize_and_tiny_surfaces();
    test_palette_is_opaque_and_distinct();

    if (failures != 0) {
        fprintf(stderr, "NixClock UI tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("NixClock UI tests: ok");
    return 0;
}
