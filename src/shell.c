#include "shell.h"

#include <stddef.h>
#include <string.h>

enum {
    MINIMIZED_BUTTON_GAP = 4,
    MINIMIZED_BUTTON_MIN_WIDTH = 48,
    MINIMIZED_BUTTON_MAX_WIDTH = 144,
    MINIMIZED_BUTTON_INSET_Y = 2
};

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

static void set_visible_menu_model(struct nb_shell *shell,
                                   const struct nb_menu_model *base)
{
    const struct nb_menu_model *overlay = shell->menu_overlay_model;
    const struct nb_menu_model *visible = base;
    size_t base_count;
    size_t overlay_count;
    size_t index;

    shell->active_base_menu_model = base;
    if (base == NULL || overlay == NULL || overlay->menus == NULL ||
        overlay->menu_count == 0) {
        nb_menu_set_model(&shell->menu, visible);
        return;
    }
    base_count = base->menu_count < NB_MENU_MAX_MENUS
                     ? base->menu_count
                     : NB_MENU_MAX_MENUS;
    overlay_count = overlay->menu_count;
    if (overlay_count > NB_MENU_MAX_MENUS - base_count) {
        overlay_count = NB_MENU_MAX_MENUS - base_count;
    }
    for (index = 0; index < base_count; ++index) {
        shell->composed_menus[index] = base->menus[index];
    }
    for (index = 0; index < overlay_count; ++index) {
        shell->composed_menus[base_count + index] = overlay->menus[index];
    }
    if (overlay_count != 0) {
        shell->composed_menu_model.menus = shell->composed_menus;
        shell->composed_menu_model.menu_count = base_count + overlay_count;
        visible = &shell->composed_menu_model;
    }
    nb_menu_set_model(&shell->menu, visible);
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

    if (source != shell->active_menu_source ||
        shell->active_base_menu_model != model ||
        context_window != shell->active_menu_window) {
        set_visible_menu_model(shell, model);
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
    shell->menu_overlay_model = NULL;
    shell->active_base_menu_model = NULL;
    shell->composed_menu_model.menus = shell->composed_menus;
    shell->composed_menu_model.menu_count = 0;
    shell->active_menu_source = NB_MENU_SOURCE_NONE;
    shell->active_menu_window = NB_WINDOW_ID_NONE;
    shell->pointer_owner = NB_SHELL_POINTER_NONE;
    shell->minimized_pointer_window = NB_WINDOW_ID_NONE;
    shell->minimized_pointer_pressed = false;
    shell->minimize_gadget_visible = true;
    shell->maximize_gadget_visible = true;
    shell->window_control_layout = NB_WINDOW_CONTROLS_RIGHT;
    sync_active_menu(shell);
}

void nb_shell_set_menu_overlay(struct nb_shell *shell,
                               const struct nb_menu_model *menu_overlay_model)
{
    if (shell == NULL || shell->menu_overlay_model == menu_overlay_model) {
        return;
    }
    shell->menu_overlay_model = menu_overlay_model;
    set_visible_menu_model(shell, shell->active_base_menu_model);
    if (shell->pointer_owner == NB_SHELL_POINTER_MENU) {
        shell->pointer_owner = NB_SHELL_POINTER_NONE;
    }
}

void nb_shell_set_window_controls(struct nb_shell *shell,
                                  bool minimize_gadget_visible,
                                  bool maximize_gadget_visible,
                                  enum nb_window_control_layout layout)
{
    if (shell == NULL || layout < NB_WINDOW_CONTROLS_SPLIT ||
        layout > NB_WINDOW_CONTROLS_RIGHT) {
        return;
    }
    shell->minimize_gadget_visible = minimize_gadget_visible;
    shell->maximize_gadget_visible = maximize_gadget_visible;
    shell->window_control_layout = layout;
    nb_desktop_set_window_controls(&shell->desktop,
                                   minimize_gadget_visible,
                                   maximize_gadget_visible,
                                   layout);
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
    nb_desktop_set_window_controls(&shell->desktop,
                                   shell->minimize_gadget_visible,
                                   shell->maximize_gadget_visible,
                                   shell->window_control_layout);

    shell->menu_bindings[binding_index].window = window;
    shell->menu_bindings[binding_index].menu_source = menu_source;
    shell->menu_bindings[binding_index].menu_model = menu_model;
    sync_active_menu(shell);
    return window;
}

bool nb_shell_destroy_window(struct nb_shell *shell, nb_window_id id)
{
    const size_t binding_index = find_binding(shell, id);

    if (shell->pointer_owner == NB_SHELL_POINTER_MINIMIZED_WINDOW &&
        shell->minimized_pointer_window == id) {
        nb_shell_pointer_cancel(shell);
    }
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

bool nb_shell_toggle_window_minimized(struct nb_shell *shell,
                                      nb_window_id id)
{
    if (shell == NULL ||
        !nb_desktop_toggle_window_minimized(&shell->desktop, id)) {
        return false;
    }
    sync_active_menu(shell);
    return true;
}

static size_t minimized_window_count(const struct nb_shell *shell)
{
    size_t index;
    size_t count = 0;

    for (index = 0;
         index < nb_desktop_window_count(&shell->desktop);
         ++index) {
        const struct nb_window *window =
            nb_desktop_window_at(&shell->desktop, index);

        if (window != NULL && window->visible && window->minimized) {
            ++count;
        }
    }
    return count;
}

struct nb_rect nb_shell_minimized_window_rect(
    const struct nb_shell *shell,
    struct nb_rect viewport,
    nb_window_id window_id)
{
    const struct nb_rect bar = nb_menu_bar_rect(viewport);
    const struct nb_rect clock = nb_menu_clock_rect(viewport);
    size_t count;
    size_t menu_count = 0;
    size_t index;
    size_t minimized_index = 0;
    int menu_right = bar.x + MINIMIZED_BUTTON_GAP;
    int left;
    int right = clock.x - MINIMIZED_BUTTON_GAP;
    int available;
    int width;

    if (shell == NULL || window_id == NB_WINDOW_ID_NONE) {
        return (struct nb_rect){bar.x, bar.y, 0, 0};
    }
    count = minimized_window_count(shell);
    if (count == 0) {
        return (struct nb_rect){bar.x, bar.y, 0, 0};
    }
    if (shell->menu.model != NULL) {
        menu_count = shell->menu.model->menu_count < NB_MENU_MAX_MENUS
                         ? shell->menu.model->menu_count
                         : NB_MENU_MAX_MENUS;
    }
    for (index = 0; index < menu_count; ++index) {
        const struct nb_rect label =
            nb_menu_label_rect(&shell->menu, viewport, index);
        const int label_right = label.x + label.width;

        if (label_right > menu_right) {
            menu_right = label_right;
        }
    }
    left = menu_right + MINIMIZED_BUTTON_GAP;
    available = right - left - ((int)count - 1) * MINIMIZED_BUTTON_GAP;
    width = available / (int)count;
    if (width < MINIMIZED_BUTTON_MIN_WIDTH) {
        left = bar.x + MINIMIZED_BUTTON_GAP;
        available = right - left -
                    ((int)count - 1) * MINIMIZED_BUTTON_GAP;
        width = available / (int)count;
    }
    if (width > MINIMIZED_BUTTON_MAX_WIDTH) {
        width = MINIMIZED_BUTTON_MAX_WIDTH;
    }
    if (width <= 0) {
        return (struct nb_rect){bar.x, bar.y, 0, 0};
    }

    for (index = 0;
         index < nb_desktop_window_count(&shell->desktop);
         ++index) {
        const nb_window_id current_id =
            nb_desktop_window_id_at(&shell->desktop, index);
        const struct nb_window *window =
            nb_desktop_window_at(&shell->desktop, index);

        if (window == NULL || !window->visible || !window->minimized) {
            continue;
        }
        if (current_id == window_id) {
            return (struct nb_rect){
                left + (int)minimized_index *
                           (width + MINIMIZED_BUTTON_GAP),
                bar.y + MINIMIZED_BUTTON_INSET_Y,
                width,
                bar.height - (2 * MINIMIZED_BUTTON_INSET_Y)
            };
        }
        ++minimized_index;
    }
    return (struct nb_rect){bar.x, bar.y, 0, 0};
}

static bool rect_contains(struct nb_rect rect, int x, int y)
{
    return rect.width > 0 && rect.height > 0 &&
           x >= rect.x && y >= rect.y &&
           x < rect.x + rect.width && y < rect.y + rect.height;
}

static nb_window_id minimized_window_at(const struct nb_shell *shell,
                                        int x,
                                        int y,
                                        struct nb_rect viewport)
{
    size_t index;

    for (index = 0;
         index < nb_desktop_window_count(&shell->desktop);
         ++index) {
        const nb_window_id id =
            nb_desktop_window_id_at(&shell->desktop, index);

        if (rect_contains(nb_shell_minimized_window_rect(shell,
                                                         viewport,
                                                         id),
                          x,
                          y)) {
            return id;
        }
    }
    return NB_WINDOW_ID_NONE;
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
        set_visible_menu_model(shell, menu_model);
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
        set_visible_menu_model(shell, menu_model);
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
    nb_window_id minimized_window;

    if (shell->pointer_owner != NB_SHELL_POINTER_NONE) {
        return true;
    }

    minimized_window = nb_menu_is_open(&shell->menu)
                           ? NB_WINDOW_ID_NONE
                           : minimized_window_at(shell, x, y, viewport);
    if (minimized_window != NB_WINDOW_ID_NONE) {
        shell->pointer_owner = NB_SHELL_POINTER_MINIMIZED_WINDOW;
        shell->minimized_pointer_window = minimized_window;
        shell->minimized_pointer_pressed = true;
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
    if (shell->pointer_owner == NB_SHELL_POINTER_MINIMIZED_WINDOW) {
        const bool was_pressed = shell->minimized_pointer_pressed;

        shell->minimized_pointer_pressed = rect_contains(
            nb_shell_minimized_window_rect(
                shell,
                viewport,
                shell->minimized_pointer_window),
            x,
            y);
        return was_pressed != shell->minimized_pointer_pressed;
    }
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

    if (shell->pointer_owner == NB_SHELL_POINTER_MINIMIZED_WINDOW) {
        if (shell->minimized_pointer_pressed &&
            rect_contains(nb_shell_minimized_window_rect(
                              shell,
                              viewport,
                              shell->minimized_pointer_window),
                          x,
                          y)) {
            action.type = NB_SHELL_ACTION_WINDOW_MINIMIZE_TOGGLED;
            action.window = shell->minimized_pointer_window;
        }
        shell->pointer_owner = NB_SHELL_POINTER_NONE;
        shell->minimized_pointer_window = NB_WINDOW_ID_NONE;
        shell->minimized_pointer_pressed = false;
    } else if (shell->pointer_owner == NB_SHELL_POINTER_MENU) {
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
        } else if (desktop_action.type ==
                   NB_WINDOW_ACTION_MINIMIZE_TOGGLED) {
            action.type = NB_SHELL_ACTION_WINDOW_MINIMIZE_TOGGLED;
            action.window = desktop_action.window;
        } else if (desktop_action.type ==
                   NB_WINDOW_ACTION_MAXIMIZE_TOGGLED) {
            action.type = NB_SHELL_ACTION_WINDOW_MAXIMIZE_TOGGLED;
            action.window = desktop_action.window;
        } else if (desktop_action.type == NB_WINDOW_ACTION_RESIZED) {
            action.type = NB_SHELL_ACTION_WINDOW_RESIZED;
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

    if (shell->pointer_owner == NB_SHELL_POINTER_WINDOW ||
        shell->pointer_owner == NB_SHELL_POINTER_MINIMIZED_WINDOW) {
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
    shell->minimized_pointer_window = NB_WINDOW_ID_NONE;
    shell->minimized_pointer_pressed = false;
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
