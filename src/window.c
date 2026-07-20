#include "window.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int maximum(int left, int right)
{
    return left > right ? left : right;
}

static int minimum(int left, int right)
{
    return left < right ? left : right;
}

static int clamp_int64_to_int(int64_t value)
{
    if (value < INT_MIN) {
        return INT_MIN;
    }
    if (value > INT_MAX) {
        return INT_MAX;
    }
    return (int)value;
}

static int clamp_drag_coordinate(int64_t desired,
                                 int64_t minimum_origin,
                                 int64_t maximum_origin)
{
    if (desired < minimum_origin) {
        desired = minimum_origin;
    }
    if (desired > maximum_origin) {
        desired = maximum_origin;
    }
    return clamp_int64_to_int(desired);
}

static bool move_window_for_drag(struct nb_window *window,
                                 int x,
                                 int y,
                                 struct nb_rect bounds)
{
    const int old_x = window->frame.x;
    const int old_y = window->frame.y;
    int visible_width;
    int visible_height;
    int64_t minimum_x;
    int64_t maximum_x;
    int64_t maximum_y;

    if (bounds.width <= 0 || bounds.height <= 0) {
        return false;
    }

    visible_width = minimum(window->frame.width,
                            minimum(bounds.width,
                                    NB_WINDOW_DRAG_VISIBLE_WIDTH));
    visible_height = minimum(window->frame.height,
                             minimum(bounds.height,
                                     NB_WINDOW_DRAG_VISIBLE_HEIGHT));

    minimum_x = (int64_t)bounds.x - window->frame.width + visible_width;
    maximum_x = (int64_t)bounds.x + bounds.width - visible_width;
    maximum_y = (int64_t)bounds.y + bounds.height - visible_height;

    window->frame.x = clamp_drag_coordinate(
        (int64_t)x - window->pointer_offset_x,
        minimum_x,
        maximum_x);
    window->frame.y = clamp_drag_coordinate(
        (int64_t)y - window->pointer_offset_y,
        bounds.y,
        maximum_y);

    return old_x != window->frame.x || old_y != window->frame.y;
}

static bool rect_contains(struct nb_rect rect, int x, int y)
{
    return rect.width > 0 && rect.height > 0 &&
           x >= rect.x && y >= rect.y &&
           (int64_t)x < (int64_t)rect.x + rect.width &&
           (int64_t)y < (int64_t)rect.y + rect.height;
}

static int resize_dimension(int pointer_coordinate,
                            int pointer_offset,
                            int frame_origin,
                            int bounds_origin,
                            int bounds_size,
                            int minimum_size)
{
    const int64_t bounds_end = (int64_t)bounds_origin +
                               maximum(0, bounds_size);
    int64_t available = bounds_end - frame_origin;
    int64_t desired = (int64_t)pointer_coordinate + pointer_offset -
                      frame_origin;

    if (available < 0) {
        available = 0;
    } else if (available > INT_MAX) {
        available = INT_MAX;
    }

    if (desired < minimum_size) {
        desired = minimum_size;
    }
    if (desired > available) {
        desired = available;
    }

    return (int)desired;
}

static bool origin_is_within_bounds(int origin,
                                    int bounds_origin,
                                    int bounds_size)
{
    const int64_t bounds_end = (int64_t)bounds_origin + bounds_size;

    return bounds_size > 0 && origin >= bounds_origin &&
           (int64_t)origin < bounds_end;
}

void nb_window_init(struct nb_window *window,
                    const char *title,
                    struct nb_rect frame)
{
    window->frame = frame;
    window->frame.width = maximum(window->frame.width, NB_WINDOW_MIN_WIDTH);
    window->frame.height = maximum(window->frame.height,
                                   NB_WINDOW_MIN_HEIGHT);
    nb_window_set_title(window, title);
    window->visible = true;
    window->active = false;
    window->pointer_mode = NB_WINDOW_POINTER_IDLE;
    window->close_pressed = false;
    window->minimize_pressed = false;
    window->maximize_pressed = false;
    window->pointer_offset_x = 0;
    window->pointer_offset_y = 0;
    window->resize_origin_width = 0;
    window->resize_origin_height = 0;
    window->restore_frame = window->frame;
    window->restore_frame_valid = false;
    window->fullscreen_restore_frame = window->frame;
    window->fullscreen_restore_maximized = false;
    window->minimized = false;
    window->maximized = false;
    window->fullscreen = false;
    window->minimize_gadget_visible = true;
    window->maximize_gadget_visible = true;
    window->control_layout = NB_WINDOW_CONTROLS_RIGHT;
    window->decoration_menu_height = 0;
    window->decoration_content_insets =
        (struct nb_window_decoration_insets){0, 0, 0, 0};
    window->decoration_controls =
        (struct nb_window_decoration_controls){0, 0, 0, 0, 0};
    memset(&window->decoration_pixel_profile,
           0,
           sizeof(window->decoration_pixel_profile));
    window->decoration_pixel_profile_enabled = false;
    window->decoration_frame_draggable = false;
}

void nb_window_set_title(struct nb_window *window, const char *title)
{
    size_t title_index = 0;

    if (window == NULL) {
        return;
    }
    if (title != NULL) {
        while (title_index + 1 < NB_WINDOW_TITLE_CAPACITY &&
               title[title_index] != '\0') {
            window->title[title_index] = title[title_index];
            ++title_index;
        }
    }
    window->title[title_index] = '\0';
}

void nb_window_set_controls(struct nb_window *window,
                            bool minimize_gadget_visible,
                            bool maximize_gadget_visible,
                            enum nb_window_control_layout layout)
{
    if (window == NULL || layout < NB_WINDOW_CONTROLS_SPLIT ||
        layout > NB_WINDOW_CONTROLS_RIGHT) {
        return;
    }
    if ((!minimize_gadget_visible &&
         window->pointer_mode == NB_WINDOW_POINTER_MINIMIZE) ||
        (!maximize_gadget_visible &&
         window->pointer_mode == NB_WINDOW_POINTER_MAXIMIZE)) {
        nb_window_pointer_cancel(window);
    }
    window->minimize_gadget_visible = minimize_gadget_visible;
    window->maximize_gadget_visible = maximize_gadget_visible;
    window->control_layout = layout;
}

void nb_window_set_decoration_menu_height(struct nb_window *window,
                                          int height)
{
    if (window != NULL) {
        window->decoration_menu_height =
            height > 0
                ? minimum(height, NB_WINDOW_DECORATION_INSET_MAX)
                : 0;
    }
}

bool nb_window_decoration_insets_are_valid(
    struct nb_window_decoration_insets insets)
{
    return insets.left >= 0 && insets.top >= 0 && insets.right >= 0 &&
           insets.bottom >= 0 &&
           insets.left + insets.right < NB_WINDOW_DECORATION_INSET_SCALE &&
           insets.top + insets.bottom < NB_WINDOW_DECORATION_INSET_SCALE;
}

void nb_window_set_decoration_content_insets(
    struct nb_window *window,
    struct nb_window_decoration_insets insets)
{
    if (window != NULL && nb_window_decoration_insets_are_valid(insets)) {
        window->decoration_content_insets = insets;
    }
}

static int scaled_inset(int size, int fraction)
{
    return (int)(((int64_t)maximum(0, size) * fraction) /
                 NB_WINDOW_DECORATION_INSET_SCALE);
}

static bool has_decoration_content_insets(const struct nb_window *window)
{
    const struct nb_window_decoration_insets insets =
        window->decoration_content_insets;

    return window->decoration_pixel_profile_enabled || insets.left != 0 ||
           insets.top != 0 || insets.right != 0 || insets.bottom != 0;
}

bool nb_window_decoration_controls_are_valid(
    struct nb_window_decoration_controls controls)
{
    const int cluster_width = (3 * controls.width) + (2 * controls.gap);

    return controls.top >= 0 && controls.right >= 0 && controls.width >= 0 &&
           controls.height >= 0 && controls.gap >= 0 &&
           controls.top + controls.height <=
               NB_WINDOW_DECORATION_INSET_SCALE &&
           controls.right + cluster_width <=
               NB_WINDOW_DECORATION_INSET_SCALE;
}

void nb_window_set_decoration_controls(
    struct nb_window *window,
    struct nb_window_decoration_controls controls)
{
    if (window != NULL &&
        nb_window_decoration_controls_are_valid(controls)) {
        window->decoration_controls = controls;
    }
}

static bool pixel_insets_are_valid(
    struct nb_window_decoration_insets insets)
{
    return insets.left >= 0 && insets.top >= 0 && insets.right >= 0 &&
           insets.bottom >= 0 &&
           (int64_t)insets.left + insets.right <= INT_MAX &&
           (int64_t)insets.top + insets.bottom <= INT_MAX;
}

static bool pixel_controls_are_valid(
    struct nb_window_decoration_controls controls)
{
    const int64_t cluster_width =
        (3 * (int64_t)controls.width) + (2 * (int64_t)controls.gap);

    return controls.top >= 0 && controls.right >= 0 && controls.width >= 0 &&
           controls.height >= 0 && controls.gap >= 0 &&
           (int64_t)controls.top + controls.height <= INT_MAX &&
           (int64_t)controls.right + cluster_width <= INT_MAX;
}

bool nb_window_decoration_pixel_profile_is_valid(
    struct nb_window_decoration_pixel_profile profile)
{
    return profile.compact_width >= NB_WINDOW_MIN_WIDTH &&
           profile.compact_height >= NB_WINDOW_MIN_HEIGHT &&
           pixel_insets_are_valid(profile.regular_insets) &&
           pixel_insets_are_valid(profile.compact_insets) &&
           pixel_controls_are_valid(profile.regular_controls) &&
           pixel_controls_are_valid(profile.compact_controls);
}

void nb_window_set_decoration_pixel_profile(
    struct nb_window *window,
    struct nb_window_decoration_pixel_profile profile)
{
    if (window != NULL &&
        nb_window_decoration_pixel_profile_is_valid(profile)) {
        window->decoration_pixel_profile = profile;
        window->decoration_pixel_profile_enabled = true;
    }
}

void nb_window_set_decoration_frame_draggable(struct nb_window *window,
                                               bool draggable)
{
    if (window != NULL) {
        window->decoration_frame_draggable = draggable;
    }
}

static bool has_custom_decoration_controls(const struct nb_window *window)
{
    if (window->decoration_pixel_profile_enabled) {
        return true;
    }
    return window->decoration_controls.width > 0 &&
           window->decoration_controls.height > 0;
}

static bool uses_compact_pixel_profile(const struct nb_window *window)
{
    const struct nb_window_decoration_pixel_profile *profile =
        &window->decoration_pixel_profile;

    return window->decoration_pixel_profile_enabled &&
           (window->frame.width <= profile->compact_width ||
            window->frame.height <= profile->compact_height);
}

static struct nb_window_decoration_insets pixel_profile_insets(
    const struct nb_window *window)
{
    return uses_compact_pixel_profile(window)
               ? window->decoration_pixel_profile.compact_insets
               : window->decoration_pixel_profile.regular_insets;
}

static struct nb_window_decoration_controls pixel_profile_controls(
    const struct nb_window *window)
{
    return uses_compact_pixel_profile(window)
               ? window->decoration_pixel_profile.compact_controls
               : window->decoration_pixel_profile.regular_controls;
}

static struct nb_rect custom_control_rect(const struct nb_window *window,
                                          int slot_from_right)
{
    const struct nb_window_decoration_controls controls =
        window->decoration_pixel_profile_enabled
            ? pixel_profile_controls(window)
            : window->decoration_controls;
    const int width = window->decoration_pixel_profile_enabled
                          ? controls.width
                          : scaled_inset(window->frame.width, controls.width);
    const int height = window->decoration_pixel_profile_enabled
                           ? controls.height
                           : scaled_inset(window->frame.height,
                                          controls.height);
    const int right = window->decoration_pixel_profile_enabled
                          ? controls.right
                          : scaled_inset(window->frame.width, controls.right);
    const int top = window->decoration_pixel_profile_enabled
                        ? controls.top
                        : scaled_inset(window->frame.height, controls.top);
    const int gap = window->decoration_pixel_profile_enabled
                        ? controls.gap
                        : scaled_inset(window->frame.width, controls.gap);

    return (struct nb_rect){
        window->frame.x + window->frame.width - right - width -
            (slot_from_right * (width + gap)),
        window->frame.y + top,
        width,
        height
    };
}

struct nb_rect nb_window_title_rect(const struct nb_window *window)
{
    if (window->fullscreen) {
        return (struct nb_rect){window->frame.x,
                                window->frame.y,
                                0,
                                0};
    }
    const int inner_width = maximum(0, window->frame.width -
                                       (2 * NB_WINDOW_BORDER_WIDTH));
    const int inner_height = maximum(0, window->frame.height -
                                        (2 * NB_WINDOW_BORDER_WIDTH));
    struct nb_rect title = {
        window->frame.x + NB_WINDOW_BORDER_WIDTH,
        window->frame.y + NB_WINDOW_BORDER_WIDTH,
        inner_width,
        minimum(NB_WINDOW_TITLE_HEIGHT, inner_height)
    };

    return title;
}

struct nb_rect nb_window_content_rect(const struct nb_window *window)
{
    if (window->fullscreen) {
        return window->frame;
    }
    if (has_decoration_content_insets(window)) {
        const struct nb_window_decoration_insets insets =
            window->decoration_pixel_profile_enabled
                ? pixel_profile_insets(window)
                : window->decoration_content_insets;
        const int left = window->decoration_pixel_profile_enabled
                             ? minimum(window->frame.width, insets.left)
                             : scaled_inset(window->frame.width, insets.left);
        const int top = window->decoration_pixel_profile_enabled
                            ? minimum(window->frame.height, insets.top)
                            : scaled_inset(window->frame.height, insets.top);
        const int right_available = maximum(0, window->frame.width - left);
        const int bottom_available = maximum(0, window->frame.height - top);
        const int right = window->decoration_pixel_profile_enabled
                              ? minimum(right_available, insets.right)
                              : scaled_inset(window->frame.width, insets.right);
        const int bottom = window->decoration_pixel_profile_enabled
                               ? minimum(bottom_available, insets.bottom)
                               : scaled_inset(window->frame.height,
                                              insets.bottom);

        return (struct nb_rect){
            window->frame.x + left,
            window->frame.y + top,
            maximum(0, window->frame.width - left - right),
            maximum(0, window->frame.height - top - bottom)
        };
    }
    const struct nb_rect title = nb_window_title_rect(window);
    const int bottom = window->frame.y + window->frame.height -
                       NB_WINDOW_BORDER_WIDTH - NB_WINDOW_FOOTER_HEIGHT;
    struct nb_rect content = {
        title.x,
        title.y + title.height + window->decoration_menu_height,
        title.width,
        maximum(0, bottom - (title.y + title.height +
                             window->decoration_menu_height))
    };

    return content;
}

struct nb_rect nb_window_menu_rect(const struct nb_window *window)
{
    if (has_decoration_content_insets(window)) {
        return (struct nb_rect){0, 0, 0, 0};
    }
    const struct nb_rect title = nb_window_title_rect(window);
    const int available = maximum(0,
        window->frame.y + window->frame.height -
        NB_WINDOW_BORDER_WIDTH - NB_WINDOW_FOOTER_HEIGHT -
        (title.y + title.height));
    const int height = window->fullscreen
                           ? 0
                           : minimum(window->decoration_menu_height,
                                     available);

    return (struct nb_rect){title.x,
                            title.y + title.height,
                            title.width,
                            height};
}

struct nb_rect nb_window_footer_rect(const struct nb_window *window)
{
    const struct nb_rect title = nb_window_title_rect(window);
    const int inner_width = maximum(0, window->frame.width -
                                       (2 * NB_WINDOW_BORDER_WIDTH));
    const int inner_height = maximum(0, window->frame.height -
                                        (2 * NB_WINDOW_BORDER_WIDTH));
    const int available_height = maximum(0, inner_height - title.height);
    const int height = minimum(NB_WINDOW_FOOTER_HEIGHT, available_height);
    struct nb_rect footer = {
        window->frame.x + NB_WINDOW_BORDER_WIDTH,
        window->frame.y + window->frame.height -
            NB_WINDOW_BORDER_WIDTH - height,
        inner_width,
        height
    };

    return footer;
}

static int control_gadget_size(const struct nb_window *window)
{
    const struct nb_rect title = nb_window_title_rect(window);
    const int count = 1 + (window->minimize_gadget_visible ? 1 : 0) +
                      (window->maximize_gadget_visible ? 1 : 0);
    const int available_width = maximum(
        0,
        title.width - ((count + 1) * NB_WINDOW_GADGET_MARGIN));
    const int available_height = maximum(0, title.height -
                                            (2 * NB_WINDOW_GADGET_MARGIN));
    const int per_gadget = count > 0 ? available_width / count : 0;

    return minimum(NB_WINDOW_CLOSE_SIZE,
                   minimum(per_gadget, available_height));
}

struct nb_rect nb_window_close_rect(const struct nb_window *window)
{
    if (!window->fullscreen && has_custom_decoration_controls(window)) {
        return custom_control_rect(window, 0);
    }
    const struct nb_rect title = nb_window_title_rect(window);
    const int size = control_gadget_size(window);
    int x = title.x + NB_WINDOW_GADGET_MARGIN;
    struct nb_rect close;

    if (window->control_layout != NB_WINDOW_CONTROLS_LEFT) {
        x = title.x + title.width - NB_WINDOW_GADGET_MARGIN - size;
    }
    close = (struct nb_rect){
        x,
        title.y + ((title.height - size) / 2),
        size,
        size
    };

    return close;
}

struct nb_rect nb_window_minimize_rect(const struct nb_window *window)
{
    if (!window->fullscreen && has_custom_decoration_controls(window)) {
        if (!window->minimize_gadget_visible) {
            return (struct nb_rect){0, 0, 0, 0};
        }
        return custom_control_rect(
            window,
            window->maximize_gadget_visible ? 2 : 1);
    }
    const struct nb_rect title = nb_window_title_rect(window);
    const struct nb_rect close = nb_window_close_rect(window);
    const int size = control_gadget_size(window);
    int x;
    struct nb_rect minimize;

    if (!window->minimize_gadget_visible) {
        return (struct nb_rect){0, 0, 0, 0};
    }
    if (window->control_layout == NB_WINDOW_CONTROLS_SPLIT) {
        x = title.x + NB_WINDOW_GADGET_MARGIN;
    } else if (window->control_layout == NB_WINDOW_CONTROLS_LEFT) {
        x = close.x + close.width + NB_WINDOW_GADGET_MARGIN;
    } else {
        x = close.x - NB_WINDOW_GADGET_MARGIN - size;
        if (window->maximize_gadget_visible) {
            x -= size + NB_WINDOW_GADGET_MARGIN;
        }
    }
    minimize = (struct nb_rect){
        x,
        title.y + ((title.height - size) / 2),
        size,
        size
    };

    return minimize;
}

struct nb_rect nb_window_maximize_rect(const struct nb_window *window)
{
    if (!window->fullscreen && has_custom_decoration_controls(window)) {
        if (!window->maximize_gadget_visible) {
            return (struct nb_rect){0, 0, 0, 0};
        }
        return custom_control_rect(window, 1);
    }
    const struct nb_rect title = nb_window_title_rect(window);
    const struct nb_rect close = nb_window_close_rect(window);
    const struct nb_rect minimize = nb_window_minimize_rect(window);
    const int size = control_gadget_size(window);
    int x;
    struct nb_rect maximize;

    if (!window->maximize_gadget_visible) {
        return (struct nb_rect){0, 0, 0, 0};
    }
    if (window->control_layout == NB_WINDOW_CONTROLS_LEFT) {
        x = close.x + close.width + NB_WINDOW_GADGET_MARGIN;
        if (minimize.width > 0) {
            x = minimize.x + minimize.width + NB_WINDOW_GADGET_MARGIN;
        }
    } else {
        x = close.x - NB_WINDOW_GADGET_MARGIN - size;
    }
    maximize = (struct nb_rect){
        x,
        title.y + ((title.height - size) / 2),
        size,
        size
    };

    return maximize;
}

struct nb_rect nb_window_resize_rect(const struct nb_window *window)
{
    const struct nb_rect footer = nb_window_footer_rect(window);
    const int size = minimum(NB_WINDOW_RESIZE_SIZE,
                             minimum(footer.width, footer.height));
    struct nb_rect resize = {
        footer.x + footer.width - size,
        footer.y + footer.height - size,
        size,
        size
    };

    return resize;
}

enum nb_window_hit nb_window_hit_test(const struct nb_window *window,
                                      int x,
                                      int y)
{
    if (!window->visible || window->minimized ||
        !rect_contains(window->frame, x, y)) {
        return NB_WINDOW_HIT_NONE;
    }

    if (window->fullscreen) {
        return NB_WINDOW_HIT_CONTENT;
    }

    if (rect_contains(nb_window_close_rect(window), x, y)) {
        return NB_WINDOW_HIT_CLOSE;
    }

    if (rect_contains(nb_window_minimize_rect(window), x, y)) {
        return NB_WINDOW_HIT_MINIMIZE;
    }

    if (rect_contains(nb_window_maximize_rect(window), x, y)) {
        return NB_WINDOW_HIT_MAXIMIZE;
    }

    if (!window->maximized &&
        rect_contains(nb_window_resize_rect(window), x, y)) {
        return NB_WINDOW_HIT_RESIZE;
    }

    if (rect_contains(nb_window_title_rect(window), x, y)) {
        return NB_WINDOW_HIT_TITLE;
    }

    if (rect_contains(nb_window_content_rect(window), x, y)) {
        return NB_WINDOW_HIT_CONTENT;
    }

    return window->decoration_frame_draggable ? NB_WINDOW_HIT_TITLE
                                              : NB_WINDOW_HIT_FRAME;
}

enum nb_window_hit nb_window_pointer_down(struct nb_window *window,
                                          int x,
                                          int y)
{
    enum nb_window_hit hit;

    if (window->pointer_mode != NB_WINDOW_POINTER_IDLE) {
        return NB_WINDOW_HIT_NONE;
    }

    hit = nb_window_hit_test(window, x, y);
    if (hit == NB_WINDOW_HIT_TITLE) {
        if (window->maximized && window->restore_frame_valid) {
            window->frame = window->restore_frame;
            window->maximized = false;
        }
        window->pointer_mode = NB_WINDOW_POINTER_DRAG;
        window->pointer_offset_x = x - window->frame.x;
        window->pointer_offset_y = y - window->frame.y;
    } else if (hit == NB_WINDOW_HIT_CLOSE) {
        window->pointer_mode = NB_WINDOW_POINTER_CLOSE;
        window->close_pressed = true;
    } else if (hit == NB_WINDOW_HIT_MINIMIZE) {
        window->pointer_mode = NB_WINDOW_POINTER_MINIMIZE;
        window->minimize_pressed = true;
    } else if (hit == NB_WINDOW_HIT_MAXIMIZE) {
        window->pointer_mode = NB_WINDOW_POINTER_MAXIMIZE;
        window->maximize_pressed = true;
    } else if (hit == NB_WINDOW_HIT_RESIZE) {
        window->pointer_mode = NB_WINDOW_POINTER_RESIZE;
        window->pointer_offset_x = (int)((int64_t)window->frame.x +
                                         window->frame.width - x);
        window->pointer_offset_y = (int)((int64_t)window->frame.y +
                                         window->frame.height - y);
        window->resize_origin_width = window->frame.width;
        window->resize_origin_height = window->frame.height;
    }

    return hit;
}

bool nb_window_pointer_move(struct nb_window *window,
                            int x,
                            int y,
                            struct nb_rect bounds)
{
    if (window->pointer_mode == NB_WINDOW_POINTER_DRAG) {
        return move_window_for_drag(window, x, y, bounds);
    }

    if (window->pointer_mode == NB_WINDOW_POINTER_CLOSE) {
        const bool was_pressed = window->close_pressed;

        window->close_pressed =
            nb_window_hit_test(window, x, y) == NB_WINDOW_HIT_CLOSE;
        return was_pressed != window->close_pressed;
    }

    if (window->pointer_mode == NB_WINDOW_POINTER_MINIMIZE) {
        const bool was_pressed = window->minimize_pressed;

        window->minimize_pressed =
            nb_window_hit_test(window, x, y) == NB_WINDOW_HIT_MINIMIZE;
        return was_pressed != window->minimize_pressed;
    }

    if (window->pointer_mode == NB_WINDOW_POINTER_MAXIMIZE) {
        const bool was_pressed = window->maximize_pressed;

        window->maximize_pressed =
            nb_window_hit_test(window, x, y) == NB_WINDOW_HIT_MAXIMIZE;
        return was_pressed != window->maximize_pressed;
    }

    if (window->pointer_mode == NB_WINDOW_POINTER_RESIZE) {
        const int old_width = window->frame.width;
        const int old_height = window->frame.height;

        if (!origin_is_within_bounds(window->frame.x,
                                     bounds.x,
                                     bounds.width) ||
            !origin_is_within_bounds(window->frame.y,
                                     bounds.y,
                                     bounds.height)) {
            return false;
        }

        window->frame.width = resize_dimension(x,
                                               window->pointer_offset_x,
                                               window->frame.x,
                                               bounds.x,
                                               bounds.width,
                                               NB_WINDOW_MIN_WIDTH);
        window->frame.height = resize_dimension(y,
                                                window->pointer_offset_y,
                                                window->frame.y,
                                                bounds.y,
                                                bounds.height,
                                                NB_WINDOW_MIN_HEIGHT);
        return old_width != window->frame.width ||
               old_height != window->frame.height;
    }

    return false;
}

enum nb_window_action nb_window_pointer_up(struct nb_window *window,
                                           int x,
                                           int y)
{
    enum nb_window_action action = NB_WINDOW_ACTION_NONE;

    if (window->pointer_mode == NB_WINDOW_POINTER_CLOSE &&
        nb_window_hit_test(window, x, y) == NB_WINDOW_HIT_CLOSE) {
        action = NB_WINDOW_ACTION_CLOSE_REQUESTED;
    } else if (window->pointer_mode == NB_WINDOW_POINTER_MINIMIZE &&
               nb_window_hit_test(window, x, y) ==
                   NB_WINDOW_HIT_MINIMIZE) {
        action = NB_WINDOW_ACTION_MINIMIZE_TOGGLED;
    } else if (window->pointer_mode == NB_WINDOW_POINTER_MAXIMIZE &&
               nb_window_hit_test(window, x, y) ==
                   NB_WINDOW_HIT_MAXIMIZE) {
        action = NB_WINDOW_ACTION_MAXIMIZE_TOGGLED;
    } else if (window->pointer_mode == NB_WINDOW_POINTER_RESIZE &&
               (window->frame.width != window->resize_origin_width ||
                window->frame.height != window->resize_origin_height)) {
        action = NB_WINDOW_ACTION_RESIZED;
    }

    nb_window_pointer_cancel(window);
    return action;
}

void nb_window_pointer_cancel(struct nb_window *window)
{
    window->pointer_mode = NB_WINDOW_POINTER_IDLE;
    window->close_pressed = false;
    window->minimize_pressed = false;
    window->maximize_pressed = false;
    window->pointer_offset_x = 0;
    window->pointer_offset_y = 0;
    window->resize_origin_width = 0;
    window->resize_origin_height = 0;
}

bool nb_window_toggle_minimized(struct nb_window *window)
{
    if (window == NULL || !window->visible) {
        return false;
    }
    nb_window_pointer_cancel(window);
    window->minimized = !window->minimized;
    return true;
}

bool nb_window_toggle_maximized(struct nb_window *window, struct nb_rect bounds)
{
    struct nb_rect restored;

    if (window == NULL || window->fullscreen || bounds.width <= 0 ||
        bounds.height <= 0) {
        return false;
    }

    if (!window->maximized) {
        window->restore_frame = window->frame;
        window->restore_frame_valid = true;
        window->maximized = true;
        window->frame = bounds;
        return true;
    }

    restored = window->restore_frame_valid ? window->restore_frame
                                           : window->frame;
    window->maximized = false;
    window->frame = restored;
    if (window->frame.width > bounds.width) {
        window->frame.width = bounds.width;
    }
    if (window->frame.height > bounds.height) {
        window->frame.height = bounds.height;
    }
    (void)nb_window_clamp_to(window, bounds);
    return true;
}

bool nb_window_set_fullscreen(struct nb_window *window,
                              bool fullscreen,
                              struct nb_rect bounds)
{
    if (window == NULL || bounds.width <= 0 || bounds.height <= 0) {
        return false;
    }
    if (window->fullscreen == fullscreen) {
        return true;
    }

    nb_window_pointer_cancel(window);
    if (fullscreen) {
        window->fullscreen_restore_frame = window->frame;
        window->fullscreen_restore_maximized = window->maximized;
        window->fullscreen = true;
        window->maximized = false;
        window->minimized = false;
        window->frame = bounds;
        return true;
    }

    window->fullscreen = false;
    window->maximized = window->fullscreen_restore_maximized;
    window->frame = window->fullscreen_restore_frame;
    return true;
}

bool nb_window_clamp_to(struct nb_window *window, struct nb_rect bounds)
{
    const int old_x = window->frame.x;
    const int old_y = window->frame.y;
    const int old_width = window->frame.width;
    const int old_height = window->frame.height;

    if (window->maximized || window->fullscreen) {
        if (window->frame.x != bounds.x || window->frame.y != bounds.y ||
            window->frame.width != bounds.width ||
            window->frame.height != bounds.height) {
            window->frame = bounds;
            return true;
        }
        return false;
    }

    if (window->frame.width > bounds.width) {
        window->frame.width = bounds.width;
    }
    if (window->frame.height > bounds.height) {
        window->frame.height = bounds.height;
    }

    const int maximum_x = bounds.x + maximum(0, bounds.width -
                                                 window->frame.width);
    const int maximum_y = bounds.y + maximum(0, bounds.height -
                                                 window->frame.height);

    window->frame.x = maximum(bounds.x, minimum(window->frame.x, maximum_x));
    window->frame.y = maximum(bounds.y, minimum(window->frame.y, maximum_y));

    return old_x != window->frame.x || old_y != window->frame.y ||
           old_width != window->frame.width ||
           old_height != window->frame.height;
}
