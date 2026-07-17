#ifndef NIXBENCH_DESKTOP_H
#define NIXBENCH_DESKTOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "window.h"

enum {
    NB_DESKTOP_MAX_WINDOWS = 16
};

typedef uint64_t nb_window_id;

#define NB_WINDOW_ID_NONE UINT64_C(0)

struct nb_desktop_slot {
    struct nb_window window;
    nb_window_id id;
    bool occupied;
};

/* The layout permits stack allocation; its fields are manager-owned state. */
struct nb_desktop {
    struct nb_desktop_slot slots[NB_DESKTOP_MAX_WINDOWS];
    /* Slot indices ordered from backmost to frontmost. */
    size_t stack[NB_DESKTOP_MAX_WINDOWS];
    size_t window_count;
    nb_window_id next_id;
    nb_window_id active_window;
    nb_window_id pointer_window;
};

struct nb_desktop_action {
    enum nb_window_action type;
    nb_window_id window;
};

void nb_desktop_init(struct nb_desktop *desktop);

nb_window_id nb_desktop_open_window(struct nb_desktop *desktop,
                                    const char *title,
                                    struct nb_rect frame);
bool nb_desktop_destroy_window(struct nb_desktop *desktop, nb_window_id id);
bool nb_desktop_raise_window(struct nb_desktop *desktop, nb_window_id id);
bool nb_desktop_activate_window(struct nb_desktop *desktop, nb_window_id id);
bool nb_desktop_set_window_title(struct nb_desktop *desktop,
                                 nb_window_id id,
                                 const char *title);
bool nb_desktop_toggle_window_minimized(struct nb_desktop *desktop,
                                        nb_window_id id);
bool nb_desktop_toggle_window_maximized(struct nb_desktop *desktop,
                                        nb_window_id id,
                                        struct nb_rect bounds);
void nb_desktop_set_window_controls(struct nb_desktop *desktop,
                                    bool minimize_gadget_visible,
                                    bool maximize_gadget_visible,
                                    enum nb_window_control_layout layout);

size_t nb_desktop_window_count(const struct nb_desktop *desktop);
nb_window_id nb_desktop_window_id_at(const struct nb_desktop *desktop,
                                     size_t stack_index);
/* Borrowed views become invalid when their ID is destroyed or on reinit. */
const struct nb_window *nb_desktop_find_window(
    const struct nb_desktop *desktop,
    nb_window_id id);
const struct nb_window *nb_desktop_window_at(const struct nb_desktop *desktop,
                                             size_t stack_index);
nb_window_id nb_desktop_active_window_id(const struct nb_desktop *desktop);

enum nb_window_hit nb_desktop_pointer_down(struct nb_desktop *desktop,
                                           int x,
                                           int y);
bool nb_desktop_pointer_move(struct nb_desktop *desktop,
                             int x,
                             int y,
                             struct nb_rect bounds);
struct nb_desktop_action nb_desktop_pointer_up(struct nb_desktop *desktop,
                                               int x,
                                               int y);
void nb_desktop_pointer_cancel(struct nb_desktop *desktop);
bool nb_desktop_has_pointer_interaction(const struct nb_desktop *desktop);

bool nb_desktop_clamp_windows(struct nb_desktop *desktop,
                              struct nb_rect bounds);

#endif
