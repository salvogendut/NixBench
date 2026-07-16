#ifndef NIXBENCH_WINDOW_H
#define NIXBENCH_WINDOW_H

#include <stdbool.h>

#include "preferences.h"

enum {
    NB_WINDOW_BORDER_WIDTH = 3,
    NB_WINDOW_TITLE_HEIGHT = 24,
    NB_WINDOW_FOOTER_HEIGHT = 20,
    NB_WINDOW_GADGET_MARGIN = 4,
    NB_WINDOW_CLOSE_SIZE = 16,
    NB_WINDOW_MAXIMIZE_SIZE = 16,
    NB_WINDOW_RESIZE_SIZE = 20,
    NB_WINDOW_MIN_WIDTH = 160,
    NB_WINDOW_MIN_HEIGHT = 100,
    NB_WINDOW_TITLE_CAPACITY = 64
};

struct nb_rect {
    int x;
    int y;
    int width;
    int height;
};

enum nb_window_hit {
    NB_WINDOW_HIT_NONE,
    NB_WINDOW_HIT_FRAME,
    NB_WINDOW_HIT_CONTENT,
    NB_WINDOW_HIT_TITLE,
    NB_WINDOW_HIT_CLOSE,
    NB_WINDOW_HIT_MAXIMIZE,
    NB_WINDOW_HIT_RESIZE
};

enum nb_window_pointer_mode {
    NB_WINDOW_POINTER_IDLE,
    NB_WINDOW_POINTER_DRAG,
    NB_WINDOW_POINTER_CLOSE,
    NB_WINDOW_POINTER_MAXIMIZE,
    NB_WINDOW_POINTER_RESIZE
};

enum nb_window_action {
    NB_WINDOW_ACTION_NONE,
    NB_WINDOW_ACTION_CLOSE_REQUESTED,
    NB_WINDOW_ACTION_MAXIMIZE_TOGGLED,
    NB_WINDOW_ACTION_RESIZED
};

struct nb_window {
    struct nb_rect frame;
    char title[NB_WINDOW_TITLE_CAPACITY];
    bool visible;
    bool active;
    enum nb_window_pointer_mode pointer_mode;
    bool close_pressed;
    bool maximize_pressed;
    int pointer_offset_x;
    int pointer_offset_y;
    int resize_origin_width;
    int resize_origin_height;
    struct nb_rect restore_frame;
    bool restore_frame_valid;
    bool maximized;
    bool maximize_gadget_visible;
    enum nb_window_control_layout control_layout;
};

void nb_window_init(struct nb_window *window,
                    const char *title,
                    struct nb_rect frame);
void nb_window_set_controls(struct nb_window *window,
                            bool maximize_gadget_visible,
                            enum nb_window_control_layout layout);

struct nb_rect nb_window_title_rect(const struct nb_window *window);
struct nb_rect nb_window_content_rect(const struct nb_window *window);
struct nb_rect nb_window_footer_rect(const struct nb_window *window);
struct nb_rect nb_window_close_rect(const struct nb_window *window);
struct nb_rect nb_window_maximize_rect(const struct nb_window *window);
struct nb_rect nb_window_resize_rect(const struct nb_window *window);

enum nb_window_hit nb_window_hit_test(const struct nb_window *window,
                                      int x,
                                      int y);
enum nb_window_hit nb_window_pointer_down(struct nb_window *window,
                                          int x,
                                          int y);
bool nb_window_pointer_move(struct nb_window *window,
                            int x,
                            int y,
                            struct nb_rect bounds);
enum nb_window_action nb_window_pointer_up(struct nb_window *window,
                                           int x,
                                           int y);
void nb_window_pointer_cancel(struct nb_window *window);
bool nb_window_toggle_maximized(struct nb_window *window,
                               struct nb_rect bounds);
bool nb_window_clamp_to(struct nb_window *window, struct nb_rect bounds);

#endif
