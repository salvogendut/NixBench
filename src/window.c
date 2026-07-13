#include "window.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static int maximum(int left, int right)
{
    return left > right ? left : right;
}

static int minimum(int left, int right)
{
    return left < right ? left : right;
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
    size_t title_index = 0;

    window->frame = frame;
    window->frame.width = maximum(window->frame.width, NB_WINDOW_MIN_WIDTH);
    window->frame.height = maximum(window->frame.height,
                                   NB_WINDOW_MIN_HEIGHT);
    if (title != NULL) {
        while (title_index + 1 < NB_WINDOW_TITLE_CAPACITY &&
               title[title_index] != '\0') {
            window->title[title_index] = title[title_index];
            ++title_index;
        }
    }
    window->title[title_index] = '\0';
    window->visible = true;
    window->active = false;
    window->pointer_mode = NB_WINDOW_POINTER_IDLE;
    window->close_pressed = false;
    window->pointer_offset_x = 0;
    window->pointer_offset_y = 0;
}

struct nb_rect nb_window_title_rect(const struct nb_window *window)
{
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
    const struct nb_rect title = nb_window_title_rect(window);
    const int bottom = window->frame.y + window->frame.height -
                       NB_WINDOW_BORDER_WIDTH - NB_WINDOW_FOOTER_HEIGHT;
    struct nb_rect content = {
        title.x,
        title.y + title.height,
        title.width,
        maximum(0, bottom - (title.y + title.height))
    };

    return content;
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

struct nb_rect nb_window_close_rect(const struct nb_window *window)
{
    const struct nb_rect title = nb_window_title_rect(window);
    const int available_width = maximum(0, title.width -
                                           (2 * NB_WINDOW_GADGET_MARGIN));
    const int available_height = maximum(0, title.height -
                                            (2 * NB_WINDOW_GADGET_MARGIN));
    const int size = minimum(NB_WINDOW_CLOSE_SIZE,
                             minimum(available_width, available_height));
    struct nb_rect close = {
        title.x + NB_WINDOW_GADGET_MARGIN,
        title.y + ((title.height - size) / 2),
        size,
        size
    };

    return close;
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
    if (!window->visible || !rect_contains(window->frame, x, y)) {
        return NB_WINDOW_HIT_NONE;
    }

    if (rect_contains(nb_window_close_rect(window), x, y)) {
        return NB_WINDOW_HIT_CLOSE;
    }

    if (rect_contains(nb_window_resize_rect(window), x, y)) {
        return NB_WINDOW_HIT_RESIZE;
    }

    if (rect_contains(nb_window_title_rect(window), x, y)) {
        return NB_WINDOW_HIT_TITLE;
    }

    if (rect_contains(nb_window_content_rect(window), x, y)) {
        return NB_WINDOW_HIT_CONTENT;
    }

    return NB_WINDOW_HIT_FRAME;
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
        window->pointer_mode = NB_WINDOW_POINTER_DRAG;
        window->pointer_offset_x = x - window->frame.x;
        window->pointer_offset_y = y - window->frame.y;
    } else if (hit == NB_WINDOW_HIT_CLOSE) {
        window->pointer_mode = NB_WINDOW_POINTER_CLOSE;
        window->close_pressed = true;
    } else if (hit == NB_WINDOW_HIT_RESIZE) {
        window->pointer_mode = NB_WINDOW_POINTER_RESIZE;
        window->pointer_offset_x = (int)((int64_t)window->frame.x +
                                         window->frame.width - x);
        window->pointer_offset_y = (int)((int64_t)window->frame.y +
                                         window->frame.height - y);
    }

    return hit;
}

bool nb_window_pointer_move(struct nb_window *window,
                            int x,
                            int y,
                            struct nb_rect bounds)
{
    if (window->pointer_mode == NB_WINDOW_POINTER_DRAG) {
        const int old_x = window->frame.x;
        const int old_y = window->frame.y;

        window->frame.x = x - window->pointer_offset_x;
        window->frame.y = y - window->pointer_offset_y;
        nb_window_clamp_to(window, bounds);
        return old_x != window->frame.x || old_y != window->frame.y;
    }

    if (window->pointer_mode == NB_WINDOW_POINTER_CLOSE) {
        const bool was_pressed = window->close_pressed;

        window->close_pressed =
            nb_window_hit_test(window, x, y) == NB_WINDOW_HIT_CLOSE;
        return was_pressed != window->close_pressed;
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
    }

    nb_window_pointer_cancel(window);
    return action;
}

void nb_window_pointer_cancel(struct nb_window *window)
{
    window->pointer_mode = NB_WINDOW_POINTER_IDLE;
    window->close_pressed = false;
    window->pointer_offset_x = 0;
    window->pointer_offset_y = 0;
}

bool nb_window_clamp_to(struct nb_window *window, struct nb_rect bounds)
{
    const int old_x = window->frame.x;
    const int old_y = window->frame.y;
    const int maximum_x = bounds.x + maximum(0, bounds.width -
                                                 window->frame.width);
    const int maximum_y = bounds.y + maximum(0, bounds.height -
                                                 window->frame.height);

    window->frame.x = maximum(bounds.x, minimum(window->frame.x, maximum_x));
    window->frame.y = maximum(bounds.y, minimum(window->frame.y, maximum_y));

    return old_x != window->frame.x || old_y != window->frame.y;
}
