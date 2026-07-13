#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wayland_demo_ui.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

enum {
    WIDTH = 128,
    HEIGHT = 96,
    STRIDE = 133,
    PIXEL_COUNT = HEIGHT * STRIDE
};

static void test_geometry_and_click_state(void)
{
    struct nb_wayland_demo_ui ui;
    struct nb_wayland_demo_rect button;

    nb_wayland_demo_ui_init(&ui, WIDTH, HEIGHT);
    button = nb_wayland_demo_ui_button_rect(&ui);
    CHECK(button.x == 24);
    CHECK(button.y == 24);
    CHECK(button.width == 80);
    CHECK(button.height == 48);
    CHECK(!ui.hovered);
    CHECK(!ui.pressed);
    CHECK(!ui.toggled);

    CHECK(nb_wayland_demo_ui_pointer_motion(&ui,
                                             button.x + 8,
                                             button.y + 8));
    CHECK(ui.pointer_present);
    CHECK(ui.hovered);
    CHECK(nb_wayland_demo_ui_pointer_button(&ui, true));
    CHECK(ui.pressed);
    CHECK(nb_wayland_demo_ui_pointer_button(&ui, false));
    CHECK(!ui.pressed);
    CHECK(ui.toggled);
    CHECK(ui.click_count == 1);

    CHECK(nb_wayland_demo_ui_pointer_button(&ui, true));
    CHECK(nb_wayland_demo_ui_pointer_motion(&ui, 0, 0));
    CHECK(!ui.hovered);
    CHECK(ui.pressed);
    CHECK(nb_wayland_demo_ui_pointer_button(&ui, false));
    CHECK(ui.click_count == 1);
    CHECK(ui.toggled);
    CHECK(!nb_wayland_demo_ui_pointer_button(&ui, false));

    CHECK(nb_wayland_demo_ui_pointer_motion(&ui,
                                             button.x + 8,
                                             button.y + 8));
    CHECK(nb_wayland_demo_ui_pointer_button(&ui, true));
    CHECK(nb_wayland_demo_ui_pointer_leave(&ui));
    CHECK(!ui.pointer_present);
    CHECK(!ui.hovered);
    CHECK(!ui.pressed);

    nb_wayland_demo_ui_resize(&ui, 320, 200);
    button = nb_wayland_demo_ui_button_rect(&ui);
    CHECK(button.x == 50);
    CHECK(button.y == 68);
    CHECK(button.width == 220);
    CHECK(button.height == 64);

    nb_wayland_demo_ui_resize(&ui, 30, 30);
    button = nb_wayland_demo_ui_button_rect(&ui);
    CHECK(button.width == 0);
    CHECK(button.height == 0);
}

static void test_pixel_rendering(void)
{
    struct nb_wayland_demo_ui ui;
    const struct nb_wayland_demo_rect button = {24, 24, 80, 48};
    uint32_t initial[PIXEL_COUNT];
    uint32_t hovered[PIXEL_COUNT];
    uint32_t pressed[PIXEL_COUNT];
    uint32_t toggled[PIXEL_COUNT];
    size_t index;
    int y;

    for (index = 0; index < PIXEL_COUNT; ++index) {
        initial[index] = UINT32_C(0xdeadbeef);
    }
    nb_wayland_demo_ui_init(&ui, WIDTH, HEIGHT);
    CHECK(nb_wayland_demo_ui_render(&ui, initial, STRIDE));
    CHECK(initial[0] == UINT32_C(0xff526d78));
    CHECK(initial[(size_t)(button.y + 8) * STRIDE +
                  (size_t)(button.x + 8)] == UINT32_C(0xffd39a45));
    for (y = 0; y < HEIGHT; ++y) {
        int x;

        for (x = WIDTH; x < STRIDE; ++x) {
            CHECK(initial[(size_t)y * STRIDE + (size_t)x] ==
                  UINT32_C(0xdeadbeef));
        }
    }

    memcpy(hovered, initial, sizeof(initial));
    CHECK(nb_wayland_demo_ui_pointer_motion(&ui,
                                             button.x + 8,
                                             button.y + 8));
    CHECK(nb_wayland_demo_ui_render(&ui, hovered, STRIDE));
    CHECK(memcmp(initial, hovered, sizeof(initial)) != 0);

    memcpy(pressed, hovered, sizeof(hovered));
    CHECK(nb_wayland_demo_ui_pointer_button(&ui, true));
    CHECK(nb_wayland_demo_ui_render(&ui, pressed, STRIDE));
    CHECK(memcmp(hovered, pressed, sizeof(hovered)) != 0);

    memcpy(toggled, pressed, sizeof(pressed));
    CHECK(nb_wayland_demo_ui_pointer_button(&ui, false));
    CHECK(nb_wayland_demo_ui_render(&ui, toggled, STRIDE));
    CHECK(memcmp(pressed, toggled, sizeof(pressed)) != 0);
    CHECK(toggled[(size_t)(button.y + 8) * STRIDE +
                  (size_t)(button.x + 8)] == UINT32_C(0xff48a878));

    CHECK(!nb_wayland_demo_ui_render(&ui, toggled, WIDTH - 1));
    CHECK(!nb_wayland_demo_ui_render(NULL, toggled, STRIDE));
    CHECK(!nb_wayland_demo_ui_render(&ui, NULL, STRIDE));
}

static void test_keyboard_state(void)
{
    struct nb_wayland_demo_ui ui;
    const unsigned int starting_clicks = 0;
    uint32_t unfocused[PIXEL_COUNT];
    uint32_t focused[PIXEL_COUNT];
    uint32_t key_pressed[PIXEL_COUNT];

    nb_wayland_demo_ui_init(&ui, WIDTH, HEIGHT);
    memset(unfocused, 0, sizeof(unfocused));
    memset(focused, 0, sizeof(focused));
    memset(key_pressed, 0, sizeof(key_pressed));
    CHECK(nb_wayland_demo_ui_render(&ui, unfocused, STRIDE));
    CHECK(!ui.keyboard_focused);
    CHECK(!ui.keyboard_pressed);
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_SPACE, true) ==
          NB_WAYLAND_DEMO_KEY_IGNORED);

    CHECK(nb_wayland_demo_ui_keyboard_enter(&ui));
    CHECK(nb_wayland_demo_ui_render(&ui, focused, STRIDE));
    CHECK(memcmp(unfocused, focused, sizeof(unfocused)) != 0);
    CHECK(ui.keyboard_focused);
    CHECK(!nb_wayland_demo_ui_keyboard_enter(&ui));
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_SPACE, true) ==
          NB_WAYLAND_DEMO_KEY_REDRAW);
    CHECK(nb_wayland_demo_ui_render(&ui, key_pressed, STRIDE));
    CHECK(memcmp(focused, key_pressed, sizeof(focused)) != 0);
    CHECK(ui.keyboard_pressed);
    CHECK(ui.keyboard_active_key == NB_WAYLAND_DEMO_KEY_SPACE);
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_SPACE, true) ==
          NB_WAYLAND_DEMO_KEY_IGNORED);
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_ENTER, false) ==
          NB_WAYLAND_DEMO_KEY_IGNORED);
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_SPACE, false) ==
          NB_WAYLAND_DEMO_KEY_REDRAW);
    CHECK(!ui.keyboard_pressed);
    CHECK(ui.toggled);
    CHECK(ui.click_count == starting_clicks + 1);

    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_ENTER, true) ==
          NB_WAYLAND_DEMO_KEY_REDRAW);
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_ENTER, false) ==
          NB_WAYLAND_DEMO_KEY_REDRAW);
    CHECK(!ui.toggled);
    CHECK(ui.click_count == starting_clicks + 2);

    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_KEYPAD_ENTER, true) ==
          NB_WAYLAND_DEMO_KEY_REDRAW);
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_KEYPAD_ENTER, false) ==
          NB_WAYLAND_DEMO_KEY_REDRAW);
    CHECK(ui.toggled);
    CHECK(ui.click_count == starting_clicks + 3);

    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_ESCAPE, false) ==
          NB_WAYLAND_DEMO_KEY_IGNORED);
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_ESCAPE, true) ==
          NB_WAYLAND_DEMO_KEY_CLOSE);
    CHECK(nb_wayland_demo_ui_keyboard_key(&ui, 30, true) ==
          NB_WAYLAND_DEMO_KEY_IGNORED);

    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_SPACE, true) ==
          NB_WAYLAND_DEMO_KEY_REDRAW);
    CHECK(nb_wayland_demo_ui_keyboard_leave(&ui));
    CHECK(!ui.keyboard_focused);
    CHECK(!ui.keyboard_pressed);
    CHECK(ui.keyboard_active_key == 0);
    CHECK(!nb_wayland_demo_ui_keyboard_leave(&ui));
    CHECK(nb_wayland_demo_ui_keyboard_key(
              &ui, NB_WAYLAND_DEMO_KEY_SPACE, false) ==
          NB_WAYLAND_DEMO_KEY_IGNORED);
    CHECK(ui.click_count == starting_clicks + 3);

    CHECK(!nb_wayland_demo_ui_keyboard_enter(NULL));
    CHECK(!nb_wayland_demo_ui_keyboard_leave(NULL));
    CHECK(nb_wayland_demo_ui_keyboard_key(
              NULL, NB_WAYLAND_DEMO_KEY_SPACE, true) ==
          NB_WAYLAND_DEMO_KEY_IGNORED);
}

int main(void)
{
    test_geometry_and_click_state();
    test_pixel_rendering();
    test_keyboard_state();

    if (failures != 0) {
        fprintf(stderr, "Wayland demo UI tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("Wayland demo UI tests: ok");
    return 0;
}
