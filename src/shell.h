#ifndef NIXBENCH_SHELL_H
#define NIXBENCH_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include "desktop.h"
#include "menu.h"

typedef uint64_t nb_menu_source_id;

#define NB_MENU_SOURCE_NONE UINT64_C(0)

/*
 * A nonzero source identifies one application instance within a shell.
 * Every window sharing a source shares its current menu model; callers keep
 * source IDs unique and descriptor storage alive until all bindings are gone.
 */

enum nb_shell_pointer_owner {
    NB_SHELL_POINTER_NONE,
    NB_SHELL_POINTER_MENU,
    NB_SHELL_POINTER_WINDOW
};

enum nb_shell_action_type {
    NB_SHELL_ACTION_NONE,
    NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED,
    NB_SHELL_ACTION_MENU_COMMAND
};

struct nb_shell_action {
    enum nb_shell_action_type type;
    nb_window_id window;
    nb_menu_source_id menu_source;
    nb_menu_command menu_command;
};

struct nb_shell_pointer_target {
    nb_window_id window;
    enum nb_window_hit hit;
};

struct nb_shell_menu_binding {
    nb_window_id window;
    nb_menu_source_id menu_source;
    const struct nb_menu_model *menu_model;
};

/* The layout permits stack allocation; its fields are shell-owned state. */
struct nb_shell {
    struct nb_desktop desktop;
    struct nb_menu menu;
    struct nb_shell_menu_binding menu_bindings[NB_DESKTOP_MAX_WINDOWS];
    nb_menu_source_id desktop_menu_source;
    const struct nb_menu_model *desktop_menu_model;
    nb_menu_source_id active_menu_source;
    nb_window_id active_menu_window;
    enum nb_shell_pointer_owner pointer_owner;
};

void nb_shell_init(struct nb_shell *shell,
                   nb_menu_source_id desktop_menu_source,
                   const struct nb_menu_model *desktop_menu_model);

nb_window_id nb_shell_open_window(struct nb_shell *shell,
                                  const char *title,
                                  struct nb_rect frame,
                                  nb_menu_source_id menu_source,
                                  const struct nb_menu_model *menu_model);
/* State-changing calls may end a grab; adapters recheck this afterwards. */
bool nb_shell_destroy_window(struct nb_shell *shell, nb_window_id id);
bool nb_shell_activate_window(struct nb_shell *shell, nb_window_id id);
bool nb_shell_update_menu_source(struct nb_shell *shell,
                                 nb_menu_source_id menu_source,
                                 const struct nb_menu_model *menu_model);
/* Replace one window's source/model binding without affecting its siblings. */
bool nb_shell_update_window_menu(struct nb_shell *shell,
                                 nb_window_id window,
                                 nb_menu_source_id menu_source,
                                 const struct nb_menu_model *menu_model);

/*
 * Return the frontmost shell window beneath a point without changing focus,
 * stacking, menus, or pointer state. Menus and the menu bar mask windows.
 */
struct nb_shell_pointer_target nb_shell_pointer_target_at(
    const struct nb_shell *shell,
    int x,
    int y,
    struct nb_rect viewport);
bool nb_shell_pointer_down(struct nb_shell *shell,
                           int x,
                           int y,
                           struct nb_rect viewport);
bool nb_shell_pointer_move(struct nb_shell *shell,
                           int x,
                           int y,
                           struct nb_rect viewport);
struct nb_shell_action nb_shell_pointer_up(struct nb_shell *shell,
                                           int x,
                                           int y,
                                           struct nb_rect viewport);
struct nb_shell_action nb_shell_menu_key_press(struct nb_shell *shell,
                                               enum nb_menu_key key);
void nb_shell_pointer_cancel(struct nb_shell *shell);
bool nb_shell_has_pointer_interaction(const struct nb_shell *shell);
bool nb_shell_wants_pointer_motion(const struct nb_shell *shell);

bool nb_shell_clamp_windows(struct nb_shell *shell,
                            struct nb_rect viewport);

#endif
