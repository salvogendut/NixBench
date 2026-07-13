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
    bool toggled;
    unsigned int click_count;
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

/* Pixels are opaque XRGB8888; stride is measured in uint32_t pixels. */
bool nb_wayland_demo_ui_render(const struct nb_wayland_demo_ui *ui,
                               uint32_t *pixels,
                               size_t stride_pixels);

#endif
