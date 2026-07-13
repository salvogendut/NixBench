#include "wayland_demo_ui.h"

#include <stdint.h>

enum {
    PANEL_MARGIN = 12,
    BUTTON_MARGIN = 24,
    BUTTON_MAX_WIDTH = 220,
    BUTTON_MAX_HEIGHT = 64,
    BEVEL_WIDTH = 3
};

static int minimum(int left, int right)
{
    return left < right ? left : right;
}

static int maximum(int left, int right)
{
    return left > right ? left : right;
}

static bool rect_contains(struct nb_wayland_demo_rect rect, int x, int y)
{
    return rect.width > 0 && rect.height > 0 &&
           x >= rect.x && y >= rect.y &&
           (int64_t)x < (int64_t)rect.x + rect.width &&
           (int64_t)y < (int64_t)rect.y + rect.height;
}

struct nb_wayland_demo_rect nb_wayland_demo_ui_button_rect(
    const struct nb_wayland_demo_ui *ui)
{
    struct nb_wayland_demo_rect rect = {0, 0, 0, 0};
    int available_width;
    int available_height;

    if (ui == NULL || ui->width <= 0 || ui->height <= 0) {
        return rect;
    }
    available_width = maximum(0, ui->width - (2 * BUTTON_MARGIN));
    available_height = maximum(0, ui->height - (2 * BUTTON_MARGIN));
    rect.width = minimum(BUTTON_MAX_WIDTH, available_width);
    rect.height = minimum(BUTTON_MAX_HEIGHT, available_height);
    rect.x = (ui->width - rect.width) / 2;
    rect.y = (ui->height - rect.height) / 2;
    return rect;
}

static bool update_hover(struct nb_wayland_demo_ui *ui)
{
    const bool old_hovered = ui->hovered;

    ui->hovered = ui->pointer_present &&
                  rect_contains(nb_wayland_demo_ui_button_rect(ui),
                                ui->pointer_x,
                                ui->pointer_y);
    return old_hovered != ui->hovered;
}

void nb_wayland_demo_ui_init(struct nb_wayland_demo_ui *ui,
                             int width,
                             int height)
{
    if (ui == NULL) {
        return;
    }
    ui->width = maximum(0, width);
    ui->height = maximum(0, height);
    ui->pointer_x = 0;
    ui->pointer_y = 0;
    ui->pointer_present = false;
    ui->hovered = false;
    ui->pressed = false;
    ui->toggled = false;
    ui->click_count = 0;
}

void nb_wayland_demo_ui_resize(struct nb_wayland_demo_ui *ui,
                               int width,
                               int height)
{
    if (ui == NULL) {
        return;
    }
    ui->width = maximum(0, width);
    ui->height = maximum(0, height);
    (void)update_hover(ui);
    if (!ui->hovered) {
        ui->pressed = false;
    }
}

bool nb_wayland_demo_ui_pointer_motion(struct nb_wayland_demo_ui *ui,
                                       int x,
                                       int y)
{
    bool changed;

    if (ui == NULL) {
        return false;
    }
    changed = !ui->pointer_present;
    ui->pointer_present = true;
    ui->pointer_x = x;
    ui->pointer_y = y;
    return update_hover(ui) || changed;
}

bool nb_wayland_demo_ui_pointer_leave(struct nb_wayland_demo_ui *ui)
{
    bool changed;

    if (ui == NULL) {
        return false;
    }
    changed = ui->pointer_present || ui->hovered || ui->pressed;
    ui->pointer_present = false;
    ui->hovered = false;
    ui->pressed = false;
    return changed;
}

bool nb_wayland_demo_ui_pointer_button(struct nb_wayland_demo_ui *ui,
                                       bool down)
{
    bool changed;

    if (ui == NULL) {
        return false;
    }
    if (down) {
        changed = ui->pressed != ui->hovered;
        ui->pressed = ui->hovered;
        return changed;
    }

    changed = ui->pressed;
    if (ui->pressed && ui->hovered) {
        ui->toggled = !ui->toggled;
        ++ui->click_count;
        changed = true;
    }
    ui->pressed = false;
    return changed;
}

static void fill_rect(uint32_t *pixels,
                      int width,
                      int height,
                      size_t stride,
                      struct nb_wayland_demo_rect rect,
                      uint32_t color)
{
    const int x_begin = maximum(0, rect.x);
    const int y_begin = maximum(0, rect.y);
    const int x_end = minimum(width, rect.x + maximum(0, rect.width));
    const int y_end = minimum(height, rect.y + maximum(0, rect.height));
    int y;

    for (y = y_begin; y < y_end; ++y) {
        int x;

        for (x = x_begin; x < x_end; ++x) {
            pixels[(size_t)y * stride + (size_t)x] = color;
        }
    }
}

static void draw_bevel(uint32_t *pixels,
                       int width,
                       int height,
                       size_t stride,
                       struct nb_wayland_demo_rect rect,
                       uint32_t top_left,
                       uint32_t bottom_right)
{
    int edge;

    for (edge = 0; edge < BEVEL_WIDTH; ++edge) {
        const struct nb_wayland_demo_rect top = {
            rect.x + edge,
            rect.y + edge,
            maximum(0, rect.width - (2 * edge)),
            1
        };
        const struct nb_wayland_demo_rect left = {
            rect.x + edge,
            rect.y + edge,
            1,
            maximum(0, rect.height - (2 * edge))
        };
        const struct nb_wayland_demo_rect bottom = {
            rect.x + edge,
            rect.y + rect.height - edge - 1,
            maximum(0, rect.width - (2 * edge)),
            1
        };
        const struct nb_wayland_demo_rect right = {
            rect.x + rect.width - edge - 1,
            rect.y + edge,
            1,
            maximum(0, rect.height - (2 * edge))
        };

        fill_rect(pixels, width, height, stride, top, top_left);
        fill_rect(pixels, width, height, stride, left, top_left);
        fill_rect(pixels, width, height, stride, bottom, bottom_right);
        fill_rect(pixels, width, height, stride, right, bottom_right);
    }
}

bool nb_wayland_demo_ui_render(const struct nb_wayland_demo_ui *ui,
                               uint32_t *pixels,
                               size_t stride_pixels)
{
    const uint32_t background = UINT32_C(0xff526d78);
    const uint32_t panel = UINT32_C(0xffb8bcae);
    const uint32_t light = UINT32_C(0xfff2f0d5);
    const uint32_t dark = UINT32_C(0xff26343b);
    const uint32_t hover = UINT32_C(0xffffd36a);
    const uint32_t button_off = UINT32_C(0xffd39a45);
    const uint32_t button_on = UINT32_C(0xff48a878);
    const uint32_t button_pressed = UINT32_C(0xff8c7443);
    const uint32_t indicator_off = UINT32_C(0xff713c36);
    const uint32_t indicator_on = UINT32_C(0xffd8f5b0);
    struct nb_wayland_demo_rect panel_rect;
    struct nb_wayland_demo_rect button;
    struct nb_wayland_demo_rect indicator;
    uint32_t button_color;

    if (ui == NULL || pixels == NULL || ui->width <= 0 ||
        ui->height <= 0 || stride_pixels < (size_t)ui->width) {
        return false;
    }

    fill_rect(pixels,
              ui->width,
              ui->height,
              stride_pixels,
              (struct nb_wayland_demo_rect){0, 0,
                                             ui->width, ui->height},
              background);
    panel_rect = (struct nb_wayland_demo_rect){
        PANEL_MARGIN,
        PANEL_MARGIN,
        maximum(0, ui->width - (2 * PANEL_MARGIN)),
        maximum(0, ui->height - (2 * PANEL_MARGIN))
    };
    fill_rect(pixels, ui->width, ui->height, stride_pixels,
              panel_rect, panel);
    draw_bevel(pixels, ui->width, ui->height, stride_pixels,
               panel_rect, light, dark);

    button = nb_wayland_demo_ui_button_rect(ui);
    if (button.width <= 0 || button.height <= 0) {
        return true;
    }
    button_color = ui->pressed
                       ? button_pressed
                       : (ui->toggled ? button_on : button_off);
    fill_rect(pixels, ui->width, ui->height, stride_pixels,
              button, button_color);
    draw_bevel(pixels, ui->width, ui->height, stride_pixels,
               button,
               ui->pressed ? dark : (ui->hovered ? hover : light),
               ui->pressed ? light : dark);

    indicator.width = minimum(18, maximum(0, button.width - 12));
    indicator.height = minimum(18, maximum(0, button.height - 12));
    indicator.x = button.x + (button.width - indicator.width) / 2;
    indicator.y = button.y + (button.height - indicator.height) / 2;
    fill_rect(pixels, ui->width, ui->height, stride_pixels,
              indicator,
              ui->toggled ? indicator_on : indicator_off);
    draw_bevel(pixels, ui->width, ui->height, stride_pixels,
               indicator, dark, light);
    return true;
}
