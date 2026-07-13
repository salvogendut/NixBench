#ifndef NIXBENCH_WAYLAND_DEMO_UI_H
#define NIXBENCH_WAYLAND_DEMO_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nb_wayland_demo_rect {
    int x;
    int y;
    int width;
    int height;
};

struct nb_wayland_demo_ui {
    int width;
    int height;
    int pointer_x;
    int pointer_y;
    bool pointer_present;
    bool hovered;
    bool pressed;
    bool keyboard_focused;
    bool keyboard_pressed;
    uint32_t keyboard_active_key;
    bool toggled;
    unsigned int click_count;
};

/* Semantic keys translated from the compositor-provided XKB keymap. */
enum nb_wayland_demo_key {
    NB_WAYLAND_DEMO_KEY_UNKNOWN,
    NB_WAYLAND_DEMO_KEY_ESCAPE,
    NB_WAYLAND_DEMO_KEY_ENTER,
    NB_WAYLAND_DEMO_KEY_SPACE,
    NB_WAYLAND_DEMO_KEY_KEYPAD_ENTER
};

enum nb_wayland_demo_key_result {
    NB_WAYLAND_DEMO_KEY_IGNORED,
    NB_WAYLAND_DEMO_KEY_REDRAW,
    NB_WAYLAND_DEMO_KEY_CLOSE
};

void nb_wayland_demo_ui_init(struct nb_wayland_demo_ui *ui,
                             int width,
                             int height);
void nb_wayland_demo_ui_resize(struct nb_wayland_demo_ui *ui,
                               int width,
                               int height);
struct nb_wayland_demo_rect nb_wayland_demo_ui_button_rect(
    const struct nb_wayland_demo_ui *ui);

/* These return true when the visible state changed. */
bool nb_wayland_demo_ui_pointer_motion(struct nb_wayland_demo_ui *ui,
                                       int x,
                                       int y);
bool nb_wayland_demo_ui_pointer_leave(struct nb_wayland_demo_ui *ui);
bool nb_wayland_demo_ui_pointer_button(struct nb_wayland_demo_ui *ui,
                                       bool down);

bool nb_wayland_demo_ui_keyboard_enter(struct nb_wayland_demo_ui *ui);
bool nb_wayland_demo_ui_keyboard_leave(struct nb_wayland_demo_ui *ui);
enum nb_wayland_demo_key_result nb_wayland_demo_ui_keyboard_key(
    struct nb_wayland_demo_ui *ui,
    uint32_t key,
    bool down);

/* Pixels are opaque XRGB8888; stride is measured in uint32_t pixels. */
bool nb_wayland_demo_ui_render(const struct nb_wayland_demo_ui *ui,
                               uint32_t *pixels,
                               size_t stride_pixels);

#endif
