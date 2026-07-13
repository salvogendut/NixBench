#ifndef NIXBENCH_WINDOW_H
#define NIXBENCH_WINDOW_H

#include <stdbool.h>

enum {
    NB_WINDOW_BORDER_WIDTH = 3,
    NB_WINDOW_TITLE_HEIGHT = 24,
    NB_WINDOW_GADGET_MARGIN = 4,
    NB_WINDOW_CLOSE_SIZE = 16
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
    NB_WINDOW_HIT_CLOSE
};

enum nb_window_pointer_mode {
    NB_WINDOW_POINTER_IDLE,
    NB_WINDOW_POINTER_DRAG,
    NB_WINDOW_POINTER_CLOSE
};

enum nb_window_action {
    NB_WINDOW_ACTION_NONE,
    NB_WINDOW_ACTION_CLOSE_REQUESTED
};

struct nb_window {
    struct nb_rect frame;
    const char *title;
    bool visible;
    bool active;
    enum nb_window_pointer_mode pointer_mode;
    bool close_pressed;
    int grab_x;
    int grab_y;
};

void nb_window_init(struct nb_window *window,
                    const char *title,
                    struct nb_rect frame);

struct nb_rect nb_window_title_rect(const struct nb_window *window);
struct nb_rect nb_window_content_rect(const struct nb_window *window);
struct nb_rect nb_window_close_rect(const struct nb_window *window);

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
bool nb_window_clamp_to(struct nb_window *window, struct nb_rect bounds);

#endif
