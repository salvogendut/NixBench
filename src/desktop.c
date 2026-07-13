#include "desktop.h"

static const size_t no_slot = NB_DESKTOP_MAX_WINDOWS;

static size_t find_slot_by_id(const struct nb_desktop *desktop,
                              nb_window_id id)
{
    size_t index;

    if (id == NB_WINDOW_ID_NONE) {
        return no_slot;
    }

    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        if (desktop->slots[index].occupied &&
            desktop->slots[index].id == id) {
            return index;
        }
    }

    return no_slot;
}

static size_t find_free_slot(const struct nb_desktop *desktop)
{
    size_t index;

    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        if (!desktop->slots[index].occupied) {
            return index;
        }
    }

    return no_slot;
}

static size_t find_stack_index(const struct nb_desktop *desktop,
                               size_t slot_index)
{
    size_t index;

    for (index = 0; index < desktop->window_count; ++index) {
        if (desktop->stack[index] == slot_index) {
            return index;
        }
    }

    return desktop->window_count;
}

static nb_window_id allocate_id(struct nb_desktop *desktop)
{
    size_t attempts;

    for (attempts = 0; attempts <= NB_DESKTOP_MAX_WINDOWS; ++attempts) {
        const nb_window_id candidate = desktop->next_id;

        ++desktop->next_id;
        if (desktop->next_id == NB_WINDOW_ID_NONE) {
            desktop->next_id = 1;
        }

        if (candidate != NB_WINDOW_ID_NONE &&
            find_slot_by_id(desktop, candidate) == no_slot) {
            return candidate;
        }
    }

    return NB_WINDOW_ID_NONE;
}

static void set_active_window(struct nb_desktop *desktop, nb_window_id id)
{
    size_t index;

    if (find_slot_by_id(desktop, id) == no_slot) {
        id = NB_WINDOW_ID_NONE;
    }

    desktop->active_window = id;
    for (index = 0; index < desktop->window_count; ++index) {
        struct nb_desktop_slot *slot =
            &desktop->slots[desktop->stack[index]];

        slot->window.active = slot->id == id;
    }
}

static size_t topmost_slot_at(const struct nb_desktop *desktop, int x, int y)
{
    size_t stack_index = desktop->window_count;

    while (stack_index > 0) {
        const size_t slot_index = desktop->stack[--stack_index];

        if (nb_window_hit_test(&desktop->slots[slot_index].window, x, y) !=
            NB_WINDOW_HIT_NONE) {
            return slot_index;
        }
    }

    return no_slot;
}

void nb_desktop_init(struct nb_desktop *desktop)
{
    size_t index;

    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        desktop->slots[index].occupied = false;
        desktop->slots[index].id = NB_WINDOW_ID_NONE;
        desktop->stack[index] = 0;
    }

    desktop->window_count = 0;
    desktop->next_id = 1;
    desktop->active_window = NB_WINDOW_ID_NONE;
    desktop->pointer_window = NB_WINDOW_ID_NONE;
}

nb_window_id nb_desktop_open_window(struct nb_desktop *desktop,
                                    const char *title,
                                    struct nb_rect frame)
{
    const size_t slot_index = find_free_slot(desktop);
    nb_window_id id;

    if (slot_index == no_slot) {
        return NB_WINDOW_ID_NONE;
    }

    id = allocate_id(desktop);
    if (id == NB_WINDOW_ID_NONE) {
        return NB_WINDOW_ID_NONE;
    }

    nb_window_init(&desktop->slots[slot_index].window, title, frame);
    desktop->slots[slot_index].id = id;
    desktop->slots[slot_index].occupied = true;
    desktop->stack[desktop->window_count++] = slot_index;
    set_active_window(desktop, id);

    return id;
}

bool nb_desktop_destroy_window(struct nb_desktop *desktop, nb_window_id id)
{
    const size_t slot_index = find_slot_by_id(desktop, id);
    size_t stack_index;
    size_t index;
    bool was_active;

    if (slot_index == no_slot) {
        return false;
    }

    stack_index = find_stack_index(desktop, slot_index);
    if (stack_index == desktop->window_count) {
        return false;
    }

    was_active = desktop->active_window == id;
    if (desktop->pointer_window == id) {
        nb_window_pointer_cancel(&desktop->slots[slot_index].window);
        desktop->pointer_window = NB_WINDOW_ID_NONE;
    }

    desktop->slots[slot_index].window.visible = false;
    desktop->slots[slot_index].window.active = false;
    desktop->slots[slot_index].occupied = false;
    desktop->slots[slot_index].id = NB_WINDOW_ID_NONE;

    for (index = stack_index + 1; index < desktop->window_count; ++index) {
        desktop->stack[index - 1] = desktop->stack[index];
    }
    --desktop->window_count;

    if (was_active) {
        if (desktop->window_count == 0) {
            set_active_window(desktop, NB_WINDOW_ID_NONE);
        } else {
            const size_t top_slot =
                desktop->stack[desktop->window_count - 1];

            set_active_window(desktop, desktop->slots[top_slot].id);
        }
    }

    return true;
}

bool nb_desktop_raise_window(struct nb_desktop *desktop, nb_window_id id)
{
    const size_t slot_index = find_slot_by_id(desktop, id);
    size_t stack_index;
    size_t index;

    if (slot_index == no_slot) {
        return false;
    }

    stack_index = find_stack_index(desktop, slot_index);
    if (stack_index == desktop->window_count) {
        return false;
    }

    for (index = stack_index + 1; index < desktop->window_count; ++index) {
        desktop->stack[index - 1] = desktop->stack[index];
    }
    desktop->stack[desktop->window_count - 1] = slot_index;
    return true;
}

bool nb_desktop_activate_window(struct nb_desktop *desktop, nb_window_id id)
{
    if (!nb_desktop_raise_window(desktop, id)) {
        return false;
    }

    set_active_window(desktop, id);
    return true;
}

size_t nb_desktop_window_count(const struct nb_desktop *desktop)
{
    return desktop->window_count;
}

nb_window_id nb_desktop_window_id_at(const struct nb_desktop *desktop,
                                     size_t stack_index)
{
    if (stack_index >= desktop->window_count) {
        return NB_WINDOW_ID_NONE;
    }

    return desktop->slots[desktop->stack[stack_index]].id;
}

static struct nb_window *find_window_mutable(struct nb_desktop *desktop,
                                             nb_window_id id)
{
    const size_t slot_index = find_slot_by_id(desktop, id);

    return slot_index == no_slot ? NULL : &desktop->slots[slot_index].window;
}

const struct nb_window *nb_desktop_find_window(
    const struct nb_desktop *desktop,
    nb_window_id id)
{
    const size_t slot_index = find_slot_by_id(desktop, id);

    return slot_index == no_slot ? NULL : &desktop->slots[slot_index].window;
}

const struct nb_window *nb_desktop_window_at(const struct nb_desktop *desktop,
                                             size_t stack_index)
{
    if (stack_index >= desktop->window_count) {
        return NULL;
    }

    return &desktop->slots[desktop->stack[stack_index]].window;
}

nb_window_id nb_desktop_active_window_id(const struct nb_desktop *desktop)
{
    return desktop->active_window;
}

enum nb_window_hit nb_desktop_pointer_down(struct nb_desktop *desktop,
                                           int x,
                                           int y)
{
    const size_t slot_index = topmost_slot_at(desktop, x, y);
    struct nb_desktop_slot *slot;
    enum nb_window_hit hit;

    if (desktop->pointer_window != NB_WINDOW_ID_NONE) {
        return NB_WINDOW_HIT_NONE;
    }

    if (slot_index == no_slot) {
        set_active_window(desktop, NB_WINDOW_ID_NONE);
        return NB_WINDOW_HIT_NONE;
    }

    slot = &desktop->slots[slot_index];
    nb_desktop_activate_window(desktop, slot->id);
    hit = nb_window_pointer_down(&slot->window, x, y);
    if (slot->window.pointer_mode != NB_WINDOW_POINTER_IDLE) {
        desktop->pointer_window = slot->id;
    }

    return hit;
}

bool nb_desktop_pointer_move(struct nb_desktop *desktop,
                             int x,
                             int y,
                             struct nb_rect bounds)
{
    struct nb_window *window =
        find_window_mutable(desktop, desktop->pointer_window);

    if (window == NULL) {
        desktop->pointer_window = NB_WINDOW_ID_NONE;
        return false;
    }

    return nb_window_pointer_move(window, x, y, bounds);
}

struct nb_desktop_action nb_desktop_pointer_up(struct nb_desktop *desktop,
                                               int x,
                                               int y)
{
    const nb_window_id id = desktop->pointer_window;
    struct nb_window *window = find_window_mutable(desktop, id);
    struct nb_desktop_action action = {
        NB_WINDOW_ACTION_NONE,
        NB_WINDOW_ID_NONE
    };

    if (window == NULL) {
        desktop->pointer_window = NB_WINDOW_ID_NONE;
        return action;
    }

    action.type = nb_window_pointer_up(window, x, y);
    desktop->pointer_window = NB_WINDOW_ID_NONE;
    if (action.type != NB_WINDOW_ACTION_NONE) {
        action.window = id;
    }

    return action;
}

void nb_desktop_pointer_cancel(struct nb_desktop *desktop)
{
    struct nb_window *window =
        find_window_mutable(desktop, desktop->pointer_window);

    if (window != NULL) {
        nb_window_pointer_cancel(window);
    }
    desktop->pointer_window = NB_WINDOW_ID_NONE;
}

bool nb_desktop_has_pointer_interaction(const struct nb_desktop *desktop)
{
    return desktop->pointer_window != NB_WINDOW_ID_NONE;
}

bool nb_desktop_clamp_windows(struct nb_desktop *desktop,
                              struct nb_rect bounds)
{
    size_t index;
    bool changed = false;

    for (index = 0; index < desktop->window_count; ++index) {
        struct nb_window *window =
            &desktop->slots[desktop->stack[index]].window;

        if (nb_window_clamp_to(window, bounds)) {
            changed = true;
        }
    }

    return changed;
}
