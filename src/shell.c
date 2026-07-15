#include "shell.h"

#include <stddef.h>

static struct nb_shell_action no_action(void)
{
    const struct nb_shell_action action = {
        NB_SHELL_ACTION_NONE,
        NB_WINDOW_ID_NONE,
        NB_MENU_SOURCE_NONE,
        NB_MENU_COMMAND_NONE
    };

    return action;
}

static size_t find_binding(const struct nb_shell *shell, nb_window_id window)
{
    size_t index;

    if (window == NB_WINDOW_ID_NONE) {
        return NB_DESKTOP_MAX_WINDOWS;
    }

    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        if (shell->menu_bindings[index].window == window) {
            return index;
        }
    }
    return NB_DESKTOP_MAX_WINDOWS;
}

static size_t find_free_binding(const struct nb_shell *shell)
{
    size_t index;

    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        if (shell->menu_bindings[index].window == NB_WINDOW_ID_NONE) {
            return index;
        }
    }
    return NB_DESKTOP_MAX_WINDOWS;
}

static bool source_model_is_consistent(
    const struct nb_shell *shell,
    nb_menu_source_id source,
    const struct nb_menu_model *model,
    nb_window_id ignored_window)
{
    size_t index;

    if (source == shell->desktop_menu_source) {
        return false;
    }
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        const struct nb_shell_menu_binding *binding =
            &shell->menu_bindings[index];

        if (binding->window != NB_WINDOW_ID_NONE &&
            binding->window != ignored_window &&
            binding->menu_source == source &&
            binding->menu_model != model) {
            return false;
        }
    }
    return true;
}

static void sync_active_menu(struct nb_shell *shell)
{
    const nb_window_id active =
        nb_desktop_active_window_id(&shell->desktop);
    const size_t binding_index = find_binding(shell, active);
    nb_menu_source_id source = shell->desktop_menu_source;
    const struct nb_menu_model *model = shell->desktop_menu_model;
    nb_window_id context_window = NB_WINDOW_ID_NONE;

    if (binding_index < NB_DESKTOP_MAX_WINDOWS) {
        source = shell->menu_bindings[binding_index].menu_source;
        model = shell->menu_bindings[binding_index].menu_model;
        context_window = active;
    }

    if (source != shell->active_menu_source || shell->menu.model != model ||
        context_window != shell->active_menu_window) {
        nb_menu_set_model(&shell->menu, model);
        shell->active_menu_source = source;
        shell->active_menu_window = context_window;
        if (shell->pointer_owner == NB_SHELL_POINTER_MENU) {
            shell->pointer_owner = NB_SHELL_POINTER_NONE;
        }
    }
}

void nb_shell_init(struct nb_shell *shell,
                   nb_menu_source_id desktop_menu_source,
                   const struct nb_menu_model *desktop_menu_model)
{
    size_t index;

    nb_desktop_init(&shell->desktop);
    nb_menu_init(&shell->menu);
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        shell->menu_bindings[index].window = NB_WINDOW_ID_NONE;
        shell->menu_bindings[index].menu_source = NB_MENU_SOURCE_NONE;
        shell->menu_bindings[index].menu_model = NULL;
    }
    shell->desktop_menu_source = desktop_menu_source;
    shell->desktop_menu_model = desktop_menu_model;
    shell->active_menu_source = NB_MENU_SOURCE_NONE;
    shell->active_menu_window = NB_WINDOW_ID_NONE;
    shell->pointer_owner = NB_SHELL_POINTER_NONE;
    sync_active_menu(shell);
}

nb_window_id nb_shell_open_window(struct nb_shell *shell,
                                  const char *title,
                                  struct nb_rect frame,
                                  nb_menu_source_id menu_source,
                                  const struct nb_menu_model *menu_model)
{
    const size_t binding_index = find_free_binding(shell);
    nb_window_id window;

    if (binding_index == NB_DESKTOP_MAX_WINDOWS ||
        menu_source == NB_MENU_SOURCE_NONE || menu_model == NULL ||
        !source_model_is_consistent(shell,
                                    menu_source,
                                    menu_model,
                                    NB_WINDOW_ID_NONE)) {
        return NB_WINDOW_ID_NONE;
    }

    if (nb_shell_has_pointer_interaction(shell)) {
        nb_shell_pointer_cancel(shell);
    }
    window = nb_desktop_open_window(&shell->desktop, title, frame);
    if (window == NB_WINDOW_ID_NONE) {
        return NB_WINDOW_ID_NONE;
    }

    shell->menu_bindings[binding_index].window = window;
    shell->menu_bindings[binding_index].menu_source = menu_source;
    shell->menu_bindings[binding_index].menu_model = menu_model;
    sync_active_menu(shell);
    return window;
}

bool nb_shell_destroy_window(struct nb_shell *shell, nb_window_id id)
{
    const size_t binding_index = find_binding(shell, id);

    if (binding_index == NB_DESKTOP_MAX_WINDOWS ||
        !nb_desktop_destroy_window(&shell->desktop, id)) {
        return false;
    }

    shell->menu_bindings[binding_index].window = NB_WINDOW_ID_NONE;
    shell->menu_bindings[binding_index].menu_source = NB_MENU_SOURCE_NONE;
    shell->menu_bindings[binding_index].menu_model = NULL;
    if (shell->pointer_owner == NB_SHELL_POINTER_WINDOW &&
        !nb_desktop_has_pointer_interaction(&shell->desktop)) {
        shell->pointer_owner = NB_SHELL_POINTER_NONE;
    }
    sync_active_menu(shell);
    return true;
}

bool nb_shell_activate_window(struct nb_shell *shell, nb_window_id id)
{
    if (nb_shell_has_pointer_interaction(shell)) {
        nb_shell_pointer_cancel(shell);
    }
    if (!nb_desktop_activate_window(&shell->desktop, id)) {
        return false;
    }
    sync_active_menu(shell);
    return true;
}

bool nb_shell_update_menu_source(struct nb_shell *shell,
                                 nb_menu_source_id menu_source,
                                 const struct nb_menu_model *menu_model)
{
    size_t index;
    bool found = false;

    if (menu_source == NB_MENU_SOURCE_NONE || menu_model == NULL) {
        return false;
    }

    if (shell->desktop_menu_source == menu_source) {
        shell->desktop_menu_model = menu_model;
        found = true;
    }
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        if (shell->menu_bindings[index].window != NB_WINDOW_ID_NONE &&
            shell->menu_bindings[index].menu_source == menu_source) {
            shell->menu_bindings[index].menu_model = menu_model;
            found = true;
        }
    }

    if (found && shell->active_menu_source == menu_source) {
        nb_menu_set_model(&shell->menu, menu_model);
        if (shell->pointer_owner == NB_SHELL_POINTER_MENU) {
            shell->pointer_owner = NB_SHELL_POINTER_NONE;
        }
    }
    return found;
}

bool nb_shell_update_window_menu(struct nb_shell *shell,
                                 nb_window_id window,
                                 nb_menu_source_id menu_source,
                                 const struct nb_menu_model *menu_model)
{
    const size_t binding_index = find_binding(shell, window);
    struct nb_shell_menu_binding *binding;

    if (binding_index == NB_DESKTOP_MAX_WINDOWS ||
        menu_source == NB_MENU_SOURCE_NONE || menu_model == NULL ||
        !source_model_is_consistent(shell,
                                    menu_source,
                                    menu_model,
                                    window)) {
        return false;
    }

    binding = &shell->menu_bindings[binding_index];
    binding->menu_source = menu_source;
    binding->menu_model = menu_model;
    if (nb_desktop_active_window_id(&shell->desktop) == window) {
        nb_menu_set_model(&shell->menu, menu_model);
        shell->active_menu_source = menu_source;
        shell->active_menu_window = window;
        if (shell->pointer_owner == NB_SHELL_POINTER_MENU) {
            shell->pointer_owner = NB_SHELL_POINTER_NONE;
        }
    } else {
        sync_active_menu(shell);
    }
    return true;
}

struct nb_shell_pointer_target nb_shell_pointer_target_at(
    const struct nb_shell *shell,
    int x,
    int y,
    struct nb_rect viewport)
{
    struct nb_shell_pointer_target target = {
        NB_WINDOW_ID_NONE,
        NB_WINDOW_HIT_NONE
    };
    size_t stack_index;

    if (nb_menu_is_open(&shell->menu) ||
        nb_menu_hit_test(&shell->menu, x, y, viewport).kind !=
            NB_MENU_HIT_NONE) {
        return target;
    }

    stack_index = nb_desktop_window_count(&shell->desktop);
    while (stack_index > 0) {
        const size_t current = --stack_index;
        const struct nb_window *window =
            nb_desktop_window_at(&shell->desktop, current);
        const enum nb_window_hit hit =
            nb_window_hit_test(window, x, y);

        if (hit != NB_WINDOW_HIT_NONE) {
            target.window = nb_desktop_window_id_at(&shell->desktop,
                                                    current);
            target.hit = hit;
            return target;
        }
    }
    return target;
}

bool nb_shell_pointer_down(struct nb_shell *shell,
                           int x,
                           int y,
                           struct nb_rect viewport)
{
    enum nb_window_hit window_hit;

    if (shell->pointer_owner != NB_SHELL_POINTER_NONE) {
        return true;
    }

    if (nb_menu_is_open(&shell->menu)) {
        nb_menu_pointer_down(&shell->menu, x, y, viewport);
        if (nb_menu_is_tracking(&shell->menu)) {
            shell->pointer_owner = NB_SHELL_POINTER_MENU;
        }
        return true;
    }

    if (nb_menu_pointer_down(&shell->menu, x, y, viewport)) {
        if (nb_menu_is_tracking(&shell->menu)) {
            shell->pointer_owner = NB_SHELL_POINTER_MENU;
        }
        return true;
    }

    window_hit = nb_desktop_pointer_down(&shell->desktop, x, y);
    if (nb_desktop_has_pointer_interaction(&shell->desktop)) {
        shell->pointer_owner = NB_SHELL_POINTER_WINDOW;
    }
    sync_active_menu(shell);
    return window_hit != NB_WINDOW_HIT_NONE;
}

bool nb_shell_pointer_move(struct nb_shell *shell,
                           int x,
                           int y,
                           struct nb_rect viewport)
{
    if (shell->pointer_owner == NB_SHELL_POINTER_MENU ||
        (shell->pointer_owner == NB_SHELL_POINTER_NONE &&
         nb_menu_is_open(&shell->menu))) {
        return nb_menu_pointer_move(&shell->menu, x, y, viewport);
    }
    if (shell->pointer_owner == NB_SHELL_POINTER_WINDOW) {
        return nb_desktop_pointer_move(&shell->desktop,
                                       x,
                                       y,
                                       nb_menu_work_area(viewport));
    }
    return false;
}

struct nb_shell_action nb_shell_pointer_up(struct nb_shell *shell,
                                           int x,
                                           int y,
                                           struct nb_rect viewport)
{
    struct nb_shell_action action = no_action();

    if (shell->pointer_owner == NB_SHELL_POINTER_MENU) {
        const nb_menu_command command =
            nb_menu_pointer_up(&shell->menu, x, y, viewport);

        shell->pointer_owner = NB_SHELL_POINTER_NONE;
        if (command != NB_MENU_COMMAND_NONE) {
            action.type = NB_SHELL_ACTION_MENU_COMMAND;
            action.window = shell->active_menu_window;
            action.menu_source = shell->active_menu_source;
            action.menu_command = command;
        }
    } else if (shell->pointer_owner == NB_SHELL_POINTER_WINDOW) {
        const struct nb_desktop_action desktop_action =
            nb_desktop_pointer_up(&shell->desktop, x, y);

        shell->pointer_owner = NB_SHELL_POINTER_NONE;
        if (desktop_action.type == NB_WINDOW_ACTION_CLOSE_REQUESTED) {
            action.type = NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED;
            action.window = desktop_action.window;
        }
    }
    return action;
}

struct nb_shell_action nb_shell_menu_key_press(struct nb_shell *shell,
                                               enum nb_menu_key key)
{
    struct nb_shell_action action = no_action();
    nb_menu_command command;

    if (shell->pointer_owner == NB_SHELL_POINTER_WINDOW) {
        return action;
    }

    command = nb_menu_key_press(&shell->menu, key);
    if (shell->pointer_owner == NB_SHELL_POINTER_MENU &&
        !nb_menu_is_tracking(&shell->menu)) {
        shell->pointer_owner = NB_SHELL_POINTER_NONE;
    }
    if (command != NB_MENU_COMMAND_NONE) {
        action.type = NB_SHELL_ACTION_MENU_COMMAND;
        action.window = shell->active_menu_window;
        action.menu_source = shell->active_menu_source;
        action.menu_command = command;
    }
    return action;
}

void nb_shell_pointer_cancel(struct nb_shell *shell)
{
    nb_menu_cancel(&shell->menu);
    nb_desktop_pointer_cancel(&shell->desktop);
    shell->pointer_owner = NB_SHELL_POINTER_NONE;
}

bool nb_shell_has_pointer_interaction(const struct nb_shell *shell)
{
    return shell->pointer_owner != NB_SHELL_POINTER_NONE;
}

bool nb_shell_wants_pointer_motion(const struct nb_shell *shell)
{
    return nb_shell_has_pointer_interaction(shell) ||
           nb_menu_is_open(&shell->menu);
}

bool nb_shell_clamp_windows(struct nb_shell *shell,
                            struct nb_rect viewport)
{
    return nb_desktop_clamp_windows(&shell->desktop,
                                    nb_menu_work_area(viewport));
}
