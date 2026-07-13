#include "application.h"

#include <string.h>

static bool copy_text(char *destination,
                      size_t capacity,
                      const char *source,
                      bool allow_empty)
{
    size_t length = 0;

    if (destination == NULL || capacity == 0 || source == NULL) {
        return false;
    }
    while (length < capacity && source[length] != '\0') {
        ++length;
    }
    if (length == capacity || (!allow_empty && length == 0)) {
        return false;
    }

    memmove(destination, source, length + 1);
    return true;
}

static bool menu_snapshot_contains_command(
    const struct nb_application_menu_snapshot *snapshot,
    size_t current_menu,
    size_t current_item,
    nb_menu_command command)
{
    size_t menu_index;

    for (menu_index = 0; menu_index <= current_menu; ++menu_index) {
        const size_t item_limit = menu_index == current_menu ?
                                  current_item :
                                  snapshot->menus[menu_index].item_count;
        size_t item_index;

        for (item_index = 0; item_index < item_limit; ++item_index) {
            const struct nb_menu_item_spec *item =
                &snapshot->items[menu_index][item_index];

            if (item->kind == NB_MENU_ITEM_COMMAND &&
                item->command == command) {
                return true;
            }
        }
    }
    return false;
}

static bool copy_menu_snapshot(
    struct nb_application_menu_snapshot *destination,
    const struct nb_menu_model *source)
{
    size_t menu_index;

    if (destination == NULL || source == NULL ||
        source->menu_count > NB_MENU_MAX_MENUS ||
        (source->menu_count > 0 && source->menus == NULL)) {
        return false;
    }

    memset(destination, 0, sizeof(*destination));
    destination->model.menu_count = source->menu_count;
    destination->model.menus = source->menu_count == 0 ?
                               NULL : destination->menus;

    for (menu_index = 0; menu_index < source->menu_count; ++menu_index) {
        const struct nb_menu_spec *source_menu =
            &source->menus[menu_index];
        struct nb_menu_spec *destination_menu =
            &destination->menus[menu_index];
        size_t item_index;

        if (!copy_text(destination->menu_labels[menu_index],
                       NB_MENU_TEXT_CAPACITY,
                       source_menu->label,
                       false) ||
            source_menu->item_count > NB_MENU_MAX_ITEMS ||
            (source_menu->item_count > 0 && source_menu->items == NULL)) {
            memset(destination, 0, sizeof(*destination));
            return false;
        }

        destination_menu->label = destination->menu_labels[menu_index];
        destination_menu->item_count = source_menu->item_count;
        destination_menu->items = source_menu->item_count == 0 ?
                                  NULL : destination->items[menu_index];

        for (item_index = 0; item_index < source_menu->item_count;
             ++item_index) {
            const struct nb_menu_item_spec *source_item =
                &source_menu->items[item_index];
            struct nb_menu_item_spec *destination_item =
                &destination->items[menu_index][item_index];

            if (source_item->kind == NB_MENU_ITEM_COMMAND) {
                if (source_item->command == NB_MENU_COMMAND_NONE ||
                    menu_snapshot_contains_command(destination,
                                                   menu_index,
                                                   item_index,
                                                   source_item->command) ||
                    !copy_text(
                        destination->item_labels[menu_index][item_index],
                        NB_MENU_TEXT_CAPACITY,
                        source_item->label,
                        false)) {
                    memset(destination, 0, sizeof(*destination));
                    return false;
                }
                destination_item->label =
                    destination->item_labels[menu_index][item_index];
            } else if (source_item->kind == NB_MENU_ITEM_SEPARATOR) {
                if (source_item->label != NULL ||
                    source_item->command != NB_MENU_COMMAND_NONE ||
                    source_item->enabled) {
                    memset(destination, 0, sizeof(*destination));
                    return false;
                }
                destination_item->label = NULL;
            } else {
                memset(destination, 0, sizeof(*destination));
                return false;
            }

            destination_item->command = source_item->command;
            destination_item->kind = source_item->kind;
            destination_item->enabled = source_item->enabled;
        }
    }

    return true;
}

bool nb_application_menu_model_is_valid(const struct nb_menu_model *model)
{
    struct nb_application_menu_snapshot snapshot;

    return copy_menu_snapshot(&snapshot, model);
}

void nb_application_request_batch_init(
    struct nb_application_request_batch *batch)
{
    if (batch == NULL) {
        return;
    }
    memset(batch, 0, sizeof(*batch));
    batch->valid = true;
}

bool nb_application_requests_are_valid(
    const struct nb_application_request_batch *batch)
{
    size_t index;
    size_t menu_publications = 0;
    size_t exits = 0;

    if (batch == NULL || !batch->valid ||
        batch->request_count > NB_APPLICATION_REQUEST_CAPACITY) {
        return false;
    }
    for (index = 0; index < batch->request_count; ++index) {
        const struct nb_application_request *request =
            &batch->requests[index];
        char copied_title[NB_WINDOW_TITLE_CAPACITY];

        if (request->type == NB_APPLICATION_REQUEST_OPEN_WINDOW) {
            if (request->frame.width <= 0 || request->frame.height <= 0 ||
                !copy_text(copied_title,
                           sizeof(copied_title),
                           request->title,
                           false)) {
                return false;
            }
        } else if (request->type == NB_APPLICATION_REQUEST_CLOSE_WINDOW ||
                   request->type ==
                       NB_APPLICATION_REQUEST_ACTIVATE_WINDOW) {
            if (request->window == NB_WINDOW_ID_NONE) {
                return false;
            }
        } else if (request->type ==
                   NB_APPLICATION_REQUEST_PUBLISH_MENUS) {
            ++menu_publications;
        } else if (request->type == NB_APPLICATION_REQUEST_EXIT) {
            ++exits;
        } else {
            return false;
        }
    }

    if (menu_publications > 1 ||
        (menu_publications == 1 &&
         !nb_application_menu_model_is_valid(
             &batch->published_menu.model)) ||
        batch->has_published_menu != (menu_publications == 1) ||
        exits > 1 || batch->has_exit != (exits == 1) ||
        (exits == 1 && batch->request_count != 1)) {
        return false;
    }
    return true;
}

static bool invalidate_batch(struct nb_application_request_batch *batch)
{
    if (batch != NULL) {
        batch->valid = false;
    }
    return false;
}

static struct nb_application_request *append_request(
    struct nb_application_request_batch *batch,
    enum nb_application_request_type type)
{
    struct nb_application_request *request;

    if (batch == NULL || !batch->valid || batch->has_exit ||
        batch->request_count >= NB_APPLICATION_REQUEST_CAPACITY) {
        invalidate_batch(batch);
        return NULL;
    }

    request = &batch->requests[batch->request_count++];
    memset(request, 0, sizeof(*request));
    request->type = type;
    request->window = NB_WINDOW_ID_NONE;
    return request;
}

bool nb_application_request_open_window(
    struct nb_application_request_batch *batch,
    const char *title,
    struct nb_rect frame,
    uint64_t cookie)
{
    struct nb_application_request *request;

    if (frame.width <= 0 || frame.height <= 0) {
        return invalidate_batch(batch);
    }
    request = append_request(batch, NB_APPLICATION_REQUEST_OPEN_WINDOW);
    if (request == NULL) {
        return false;
    }
    if (!copy_text(request->title,
                   sizeof(request->title),
                   title,
                   false)) {
        return invalidate_batch(batch);
    }
    request->frame = frame;
    request->cookie = cookie;
    return true;
}

bool nb_application_request_close_window(
    struct nb_application_request_batch *batch,
    nb_window_id window)
{
    struct nb_application_request *request;

    if (window == NB_WINDOW_ID_NONE) {
        return invalidate_batch(batch);
    }
    request = append_request(batch, NB_APPLICATION_REQUEST_CLOSE_WINDOW);
    if (request == NULL) {
        return false;
    }
    request->window = window;
    return true;
}

bool nb_application_request_activate_window(
    struct nb_application_request_batch *batch,
    nb_window_id window)
{
    struct nb_application_request *request;

    if (window == NB_WINDOW_ID_NONE) {
        return invalidate_batch(batch);
    }
    request = append_request(batch,
                             NB_APPLICATION_REQUEST_ACTIVATE_WINDOW);
    if (request == NULL) {
        return false;
    }
    request->window = window;
    return true;
}

bool nb_application_request_publish_menus(
    struct nb_application_request_batch *batch,
    const struct nb_menu_model *menus)
{
    struct nb_application_request *request;

    if (batch == NULL || !batch->valid || batch->has_published_menu ||
        !copy_menu_snapshot(&batch->published_menu, menus)) {
        return invalidate_batch(batch);
    }
    request = append_request(batch,
                             NB_APPLICATION_REQUEST_PUBLISH_MENUS);
    if (request == NULL) {
        return false;
    }
    batch->has_published_menu = true;
    return true;
}

bool nb_application_request_exit(
    struct nb_application_request_batch *batch)
{
    struct nb_application_request *request;

    if (batch == NULL || !batch->valid || batch->request_count != 0) {
        return invalidate_batch(batch);
    }
    request = append_request(batch, NB_APPLICATION_REQUEST_EXIT);
    if (request == NULL) {
        return false;
    }
    batch->has_exit = true;
    return true;
}

static struct nb_application_slot *find_slot(
    struct nb_application_host *host,
    nb_application_id application)
{
    size_t index;

    if (host == NULL || application == NB_APPLICATION_ID_NONE) {
        return NULL;
    }
    for (index = 0; index < NB_APPLICATION_MAX_APPLICATIONS; ++index) {
        if (host->slots[index].occupied &&
            host->slots[index].id == application) {
            return &host->slots[index];
        }
    }
    return NULL;
}

static const struct nb_application_slot *find_slot_const(
    const struct nb_application_host *host,
    nb_application_id application)
{
    size_t index;

    if (host == NULL || application == NB_APPLICATION_ID_NONE) {
        return NULL;
    }
    for (index = 0; index < NB_APPLICATION_MAX_APPLICATIONS; ++index) {
        if (host->slots[index].occupied &&
            host->slots[index].id == application) {
            return &host->slots[index];
        }
    }
    return NULL;
}

static struct nb_application_slot *find_free_slot(
    struct nb_application_host *host)
{
    size_t index;

    for (index = 0; index < NB_APPLICATION_MAX_APPLICATIONS; ++index) {
        if (!host->slots[index].occupied) {
            return &host->slots[index];
        }
    }
    return NULL;
}

static size_t find_window_index(const struct nb_application_slot *slot,
                                nb_window_id window)
{
    size_t index;

    if (slot == NULL || window == NB_WINDOW_ID_NONE) {
        return NB_DESKTOP_MAX_WINDOWS;
    }
    for (index = 0; index < slot->window_count; ++index) {
        if (slot->windows[index].window == window) {
            return index;
        }
    }
    return NB_DESKTOP_MAX_WINDOWS;
}

static uint64_t window_cookie(const struct nb_application_slot *slot,
                              nb_window_id window)
{
    const size_t index = find_window_index(slot, window);

    return index < slot->window_count ? slot->windows[index].cookie : 0;
}

static bool menu_contains_command(const struct nb_application_slot *slot,
                                  nb_menu_command command)
{
    const struct nb_menu_model *model =
        &slot->menus[slot->active_menu].model;
    size_t menu_index;

    if (command == NB_MENU_COMMAND_NONE) {
        return false;
    }
    for (menu_index = 0; menu_index < model->menu_count; ++menu_index) {
        const struct nb_menu_spec *menu = &model->menus[menu_index];
        size_t item_index;

        for (item_index = 0; item_index < menu->item_count; ++item_index) {
            const struct nb_menu_item_spec *item = &menu->items[item_index];

            if (item->command == command &&
                nb_menu_item_is_actionable(item)) {
                return true;
            }
        }
    }
    return false;
}

static bool add_window(struct nb_application_slot *slot,
                       nb_window_id window,
                       uint64_t cookie)
{
    if (slot->window_count >= NB_DESKTOP_MAX_WINDOWS ||
        window == NB_WINDOW_ID_NONE ||
        find_window_index(slot, window) < NB_DESKTOP_MAX_WINDOWS) {
        return false;
    }

    slot->windows[slot->window_count].window = window;
    slot->windows[slot->window_count].cookie = cookie;
    ++slot->window_count;
    return true;
}

static bool shell_source_is_in_use(const struct nb_shell *shell,
                                   nb_menu_source_id source)
{
    size_t index;

    if (source == shell->desktop_menu_source) {
        return true;
    }
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        if (shell->menu_bindings[index].window != NB_WINDOW_ID_NONE &&
            shell->menu_bindings[index].menu_source == source) {
            return true;
        }
    }
    return false;
}

static bool remove_window(struct nb_application_slot *slot,
                          nb_window_id window,
                          uint64_t *cookie)
{
    const size_t found = find_window_index(slot, window);
    size_t index;

    if (found >= slot->window_count) {
        return false;
    }
    if (cookie != NULL) {
        *cookie = slot->windows[found].cookie;
    }
    for (index = found + 1; index < slot->window_count; ++index) {
        slot->windows[index - 1] = slot->windows[index];
    }
    --slot->window_count;
    slot->windows[slot->window_count].window = NB_WINDOW_ID_NONE;
    slot->windows[slot->window_count].cookie = 0;
    return true;
}

nb_application_id nb_application_host_window_owner(
    const struct nb_application_host *host,
    nb_window_id window)
{
    size_t index;

    if (host == NULL || window == NB_WINDOW_ID_NONE) {
        return NB_APPLICATION_ID_NONE;
    }
    for (index = 0; index < NB_APPLICATION_MAX_APPLICATIONS; ++index) {
        const struct nb_application_slot *slot = &host->slots[index];

        if (slot->occupied &&
            find_window_index(slot, window) < slot->window_count) {
            return slot->id;
        }
    }
    return NB_APPLICATION_ID_NONE;
}

bool nb_application_host_owns_window(
    const struct nb_application_host *host,
    nb_application_id application,
    nb_window_id window)
{
    return application != NB_APPLICATION_ID_NONE &&
           nb_application_host_window_owner(host, window) == application;
}

static nb_application_id allocate_application_id(
    struct nb_application_host *host)
{
    size_t attempt;

    for (attempt = 0;
         attempt < NB_DESKTOP_MAX_WINDOWS +
                       NB_APPLICATION_MAX_APPLICATIONS + 2;
         ++attempt) {
        const nb_application_id candidate = host->next_id;

        ++host->next_id;
        if (host->next_id == NB_APPLICATION_ID_NONE) {
            host->next_id = 1;
        }

        if (candidate != NB_APPLICATION_ID_NONE &&
            !shell_source_is_in_use(host->shell, candidate) &&
            find_slot(host, candidate) == NULL) {
            return candidate;
        }
    }
    return NB_APPLICATION_ID_NONE;
}

static struct nb_application_event make_event(
    enum nb_application_event_type type,
    nb_application_id application)
{
    const struct nb_application_event event = {
        type,
        application,
        NB_WINDOW_ID_NONE,
        NB_WINDOW_ID_NONE,
        NB_MENU_COMMAND_NONE,
        0,
        true
    };

    return event;
}

static bool queue_event(struct nb_application_host *host,
                        struct nb_application_event event)
{
    size_t tail;

    if (host->event_count >= NB_APPLICATION_EVENT_QUEUE_CAPACITY) {
        return false;
    }
    tail = (host->event_head + host->event_count) %
           NB_APPLICATION_EVENT_QUEUE_CAPACITY;
    host->pending_events[tail] = event;
    ++host->event_count;
    return true;
}

static bool event_queue_has_room(const struct nb_application_host *host,
                                 size_t needed)
{
    return needed <= NB_APPLICATION_EVENT_QUEUE_CAPACITY -
                     host->event_count;
}

static bool pop_event(struct nb_application_host *host,
                      struct nb_application_event *event)
{
    if (host->event_count == 0) {
        return false;
    }
    *event = host->pending_events[host->event_head];
    host->event_head = (host->event_head + 1) %
                       NB_APPLICATION_EVENT_QUEUE_CAPACITY;
    --host->event_count;
    return true;
}

static bool queue_focus_change(struct nb_application_host *host)
{
    const nb_window_id desktop_window =
        nb_desktop_active_window_id(&host->shell->desktop);
    const nb_application_id new_application =
        nb_application_host_window_owner(host, desktop_window);
    const nb_window_id new_window =
        new_application == NB_APPLICATION_ID_NONE ?
        NB_WINDOW_ID_NONE : desktop_window;
    const nb_application_id old_application = host->focused_application;
    const nb_window_id old_window = host->focused_window;
    size_t event_slots_needed;

    if (new_application == old_application && new_window == old_window) {
        return true;
    }

    if (old_application == new_application) {
        event_slots_needed = new_application == NB_APPLICATION_ID_NONE ?
                             0 : 1;
    } else {
        event_slots_needed =
            (old_application == NB_APPLICATION_ID_NONE ? 0U : 1U) +
            (new_application == NB_APPLICATION_ID_NONE ? 0U : 1U);
    }
    if (!event_queue_has_room(host, event_slots_needed)) {
        return false;
    }

    host->focused_application = new_application;
    host->focused_window = new_window;

    if (old_application == new_application) {
        struct nb_application_event event;

        if (new_application == NB_APPLICATION_ID_NONE) {
            return true;
        }
        event = make_event(NB_APPLICATION_EVENT_FOCUS_CHANGED,
                           new_application);
        event.window = new_window;
        event.previous_window = old_window;
        return queue_event(host, event);
    }

    if (old_application != NB_APPLICATION_ID_NONE) {
        struct nb_application_event event =
            make_event(NB_APPLICATION_EVENT_FOCUS_CHANGED,
                       old_application);

        event.previous_window = old_window;
        if (!queue_event(host, event)) {
            return false;
        }
    }
    if (new_application != NB_APPLICATION_ID_NONE) {
        struct nb_application_event event =
            make_event(NB_APPLICATION_EVENT_FOCUS_CHANGED,
                       new_application);

        event.window = new_window;
        if (!queue_event(host, event)) {
            return false;
        }
    }
    return true;
}

static bool apply_request_batch(
    struct nb_application_host *host,
    nb_application_id application,
    const struct nb_application_request_batch *batch);

static bool event_has_live_target(
    const struct nb_application_slot *slot,
    const struct nb_application_event *event)
{
    if (event->type == NB_APPLICATION_EVENT_MENU_COMMAND) {
        return find_window_index(slot, event->window) < slot->window_count &&
               menu_contains_command(slot, event->menu_command);
    }
    if (event->type == NB_APPLICATION_EVENT_WINDOW_CLOSE_REQUESTED) {
        return find_window_index(slot, event->window) < slot->window_count;
    }
    return true;
}

static bool process_event(struct nb_application_host *host,
                          const struct nb_application_event *event)
{
    struct nb_application_slot *slot = find_slot(host, event->application);
    struct nb_application_request_batch requests;

    if (slot == NULL) {
        return true;
    }
    if (event->type == NB_APPLICATION_EVENT_STOP) {
        if (!slot->stopping) {
            return true;
        }
        nb_application_request_batch_init(&requests);
        slot->handle_event(slot->context, event, &requests);
        slot->running = false;
        slot->stopping = false;
        return requests.valid && requests.request_count == 0;
    }
    if (!slot->running ||
        (slot->stopping &&
         event->type != NB_APPLICATION_EVENT_WINDOW_CLOSED &&
         event->type != NB_APPLICATION_EVENT_FOCUS_CHANGED) ||
        !event_has_live_target(slot, event)) {
        return true;
    }

    nb_application_request_batch_init(&requests);
    slot->handle_event(slot->context, event, &requests);
    /* Closure/focus notifications during teardown are informational only. */
    if (slot->stopping) {
        return true;
    }
    if (!requests.valid) {
        if (event->type == NB_APPLICATION_EVENT_START &&
            slot->window_count == 0) {
            slot->running = false;
        }
        return false;
    }
    if (!apply_request_batch(host, event->application, &requests)) {
        if (event->type == NB_APPLICATION_EVENT_START &&
            slot->window_count == 0) {
            slot->running = false;
        }
        return false;
    }
    return true;
}

static bool pump_events(struct nb_application_host *host)
{
    struct nb_application_event event;
    size_t processed = 0;
    bool succeeded = true;

    if (host->pumping_events) {
        return true;
    }
    host->pumping_events = true;
    while (pop_event(host, &event)) {
        ++processed;
        if (processed > NB_APPLICATION_EVENT_PUMP_LIMIT ||
            !process_event(host, &event)) {
            succeeded = false;
            break;
        }
    }
    if (!succeeded) {
        host->event_head = 0;
        host->event_count = 0;
    }
    host->pumping_events = false;
    return succeeded;
}

static bool finish_queued_events(struct nb_application_host *host)
{
    return host->pumping_events || pump_events(host);
}

static bool apply_open_window(struct nb_application_host *host,
                              struct nb_application_slot *slot,
                              const struct nb_application_request *request)
{
    struct nb_application_event event =
        make_event(NB_APPLICATION_EVENT_WINDOW_OPENED, slot->id);
    const struct nb_menu_model *menus =
        &slot->menus[slot->active_menu].model;
    nb_window_id window;

    if (!event_queue_has_room(host, 3)) {
        return false;
    }
    window = nb_shell_open_window(host->shell,
                                  request->title,
                                  request->frame,
                                  slot->id,
                                  menus);

    event.cookie = request->cookie;
    if (window == NB_WINDOW_ID_NONE) {
        event.succeeded = false;
        return queue_event(host, event);
    }
    if (!add_window(slot, window, request->cookie)) {
        nb_shell_destroy_window(host->shell, window);
        return false;
    }

    event.window = window;
    if (!queue_event(host, event)) {
        return false;
    }
    return queue_focus_change(host);
}

static bool apply_close_window(struct nb_application_host *host,
                               struct nb_application_slot *slot,
                               nb_window_id window)
{
    struct nb_application_event event;
    uint64_t cookie;

    if (!event_queue_has_room(host, 3) ||
        find_window_index(slot, window) >= slot->window_count ||
        !nb_shell_destroy_window(host->shell, window) ||
        !remove_window(slot, window, &cookie)) {
        return false;
    }

    event = make_event(NB_APPLICATION_EVENT_WINDOW_CLOSED, slot->id);
    event.window = window;
    event.cookie = cookie;
    if (!queue_event(host, event)) {
        return false;
    }
    return queue_focus_change(host);
}

static bool apply_activate_window(struct nb_application_host *host,
                                  const struct nb_application_slot *slot,
                                  nb_window_id window)
{
    if (!event_queue_has_room(host, 2) ||
        find_window_index(slot, window) >= slot->window_count ||
        !nb_shell_activate_window(host->shell, window)) {
        return false;
    }
    return queue_focus_change(host);
}

static bool apply_publish_menus(
    struct nb_application_host *host,
    struct nb_application_slot *slot,
    const struct nb_application_menu_snapshot *published_menu)
{
    const unsigned int inactive = slot->active_menu == 0U ? 1U : 0U;
    struct nb_application_menu_snapshot *destination =
        &slot->menus[inactive];

    if (!copy_menu_snapshot(destination, &published_menu->model)) {
        return false;
    }
    if (slot->window_count > 0 &&
        !nb_shell_update_menu_source(host->shell,
                                     slot->id,
                                     &destination->model)) {
        return false;
    }
    slot->active_menu = inactive;
    return true;
}

static bool begin_stop(struct nb_application_host *host,
                       struct nb_application_slot *slot)
{
    bool succeeded = true;

    if (!slot->running || slot->stopping) {
        return false;
    }
    if (!event_queue_has_room(host, slot->window_count + 3)) {
        return false;
    }
    slot->stopping = true;

    while (slot->window_count > 0) {
        const nb_window_id window =
            slot->windows[slot->window_count - 1].window;
        const uint64_t cookie =
            slot->windows[slot->window_count - 1].cookie;
        struct nb_application_event event =
            make_event(NB_APPLICATION_EVENT_WINDOW_CLOSED, slot->id);

        if (!nb_shell_destroy_window(host->shell, window) ||
            !remove_window(slot, window, NULL)) {
            succeeded = false;
            break;
        }
        event.window = window;
        event.cookie = cookie;
        if (!queue_event(host, event)) {
            succeeded = false;
            break;
        }
    }

    if (succeeded && !queue_focus_change(host)) {
        succeeded = false;
    }
    if (succeeded &&
        !queue_event(host,
                     make_event(NB_APPLICATION_EVENT_STOP, slot->id))) {
        succeeded = false;
    }
    return succeeded;
}

static bool request_targets_are_valid(
    const struct nb_application_slot *slot,
    const struct nb_application_request_batch *batch)
{
    bool closed[NB_DESKTOP_MAX_WINDOWS] = {false};
    size_t projected_count = slot->window_count;
    size_t request_index;

    for (request_index = 0; request_index < batch->request_count;
         ++request_index) {
        const struct nb_application_request *request =
            &batch->requests[request_index];

        if (request->type == NB_APPLICATION_REQUEST_OPEN_WINDOW) {
            if (projected_count >= NB_DESKTOP_MAX_WINDOWS) {
                return false;
            }
            ++projected_count;
        } else if (request->type == NB_APPLICATION_REQUEST_CLOSE_WINDOW ||
                   request->type ==
                       NB_APPLICATION_REQUEST_ACTIVATE_WINDOW) {
            const size_t window_index =
                find_window_index(slot, request->window);

            if (window_index >= slot->window_count ||
                closed[window_index]) {
                return false;
            }
            if (request->type == NB_APPLICATION_REQUEST_CLOSE_WINDOW) {
                closed[window_index] = true;
                --projected_count;
            }
        }
    }
    return true;
}

static bool request_events_have_capacity(
    const struct nb_application_host *host,
    const struct nb_application_slot *slot,
    const struct nb_application_request_batch *batch)
{
    size_t needed = 0;
    size_t request_index;

    for (request_index = 0; request_index < batch->request_count;
         ++request_index) {
        const struct nb_application_request *request =
            &batch->requests[request_index];
        size_t request_events = 0;

        if (request->type == NB_APPLICATION_REQUEST_OPEN_WINDOW ||
            request->type == NB_APPLICATION_REQUEST_CLOSE_WINDOW) {
            /* Lifecycle event plus focus loss and focus gain. */
            request_events = 3;
        } else if (request->type ==
                   NB_APPLICATION_REQUEST_ACTIVATE_WINDOW) {
            request_events = 2;
        } else if (request->type == NB_APPLICATION_REQUEST_EXIT) {
            /* One close per window, two focus events, and STOP. */
            request_events = slot->window_count + 3;
        }

        if (request_events > NB_APPLICATION_EVENT_QUEUE_CAPACITY - needed) {
            return false;
        }
        needed += request_events;
    }

    return event_queue_has_room(host, needed);
}

static bool apply_request_batch(
    struct nb_application_host *host,
    nb_application_id application,
    const struct nb_application_request_batch *batch)
{
    struct nb_application_slot *slot = find_slot(host, application);
    size_t index;

    if (!nb_application_requests_are_valid(batch) || slot == NULL ||
        !slot->running || slot->stopping ||
        !request_targets_are_valid(slot, batch) ||
        !request_events_have_capacity(host, slot, batch)) {
        return false;
    }
    for (index = 0; index < batch->request_count; ++index) {
        const struct nb_application_request *request =
            &batch->requests[index];

        if (!slot->running || slot->stopping) {
            return false;
        }

        if (request->type == NB_APPLICATION_REQUEST_OPEN_WINDOW) {
            if (!apply_open_window(host, slot, request)) {
                return false;
            }
        } else if (request->type == NB_APPLICATION_REQUEST_CLOSE_WINDOW) {
            if (!apply_close_window(host, slot, request->window)) {
                return false;
            }
        } else if (request->type ==
                   NB_APPLICATION_REQUEST_ACTIVATE_WINDOW) {
            if (!apply_activate_window(host, slot, request->window)) {
                return false;
            }
        } else if (request->type ==
                   NB_APPLICATION_REQUEST_PUBLISH_MENUS) {
            if (!batch->has_published_menu ||
                !apply_publish_menus(host,
                                     slot,
                                     &batch->published_menu)) {
                return false;
            }
        } else if (request->type == NB_APPLICATION_REQUEST_EXIT) {
            if (!batch->has_exit || batch->request_count != 1 ||
                !begin_stop(host, slot)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool nb_application_host_init(struct nb_application_host *host,
                              struct nb_shell *shell)
{
    if (host == NULL || shell == NULL ||
        shell->desktop_menu_source == NB_MENU_SOURCE_NONE ||
        shell->desktop_menu_model == NULL) {
        return false;
    }

    memset(host, 0, sizeof(*host));
    host->shell = shell;
    host->next_id = 1;
    return true;
}

nb_application_id nb_application_host_register(
    struct nb_application_host *host,
    const struct nb_application_spec *spec)
{
    struct nb_application_slot *slot;
    nb_application_id application;

    if (host == NULL || host->shell == NULL || host->pumping_events ||
        spec == NULL || spec->handle_event == NULL ||
        spec->initial_menus == NULL) {
        return NB_APPLICATION_ID_NONE;
    }
    slot = find_free_slot(host);
    if (slot == NULL) {
        return NB_APPLICATION_ID_NONE;
    }

    memset(slot, 0, sizeof(*slot));
    if (!copy_text(slot->name,
                   sizeof(slot->name),
                   spec->name,
                   false) ||
        !copy_menu_snapshot(&slot->menus[0], spec->initial_menus)) {
        memset(slot, 0, sizeof(*slot));
        return NB_APPLICATION_ID_NONE;
    }
    application = allocate_application_id(host);
    if (application == NB_APPLICATION_ID_NONE) {
        memset(slot, 0, sizeof(*slot));
        return NB_APPLICATION_ID_NONE;
    }

    slot->occupied = true;
    slot->id = application;
    slot->handle_event = spec->handle_event;
    slot->context = spec->context;
    slot->active_menu = 0;
    return application;
}

bool nb_application_host_unregister(struct nb_application_host *host,
                                    nb_application_id application)
{
    struct nb_application_slot *slot = find_slot(host, application);

    if (slot == NULL || host->pumping_events || slot->running ||
        slot->stopping || slot->window_count != 0) {
        return false;
    }
    memset(slot, 0, sizeof(*slot));
    return true;
}

bool nb_application_host_start(struct nb_application_host *host,
                               nb_application_id application)
{
    struct nb_application_slot *slot = find_slot(host, application);
    bool started;

    if (slot == NULL || host->pumping_events || slot->running ||
        slot->stopping || slot->window_count != 0) {
        return false;
    }
    slot->running = true;
    if (!queue_event(host,
                     make_event(NB_APPLICATION_EVENT_START, application))) {
        slot->running = false;
        return false;
    }
    started = finish_queued_events(host);
    if (started || !slot->running) {
        return started;
    }

    /* A descendant lifecycle callback failed after START mutated the shell. */
    if (!slot->stopping && begin_stop(host, slot)) {
        (void)finish_queued_events(host);
    }
    return false;
}

bool nb_application_host_stop(struct nb_application_host *host,
                              nb_application_id application)
{
    struct nb_application_slot *slot = find_slot(host, application);
    bool queued;

    if (slot == NULL || host->pumping_events) {
        return false;
    }
    queued = begin_stop(host, slot);
    return finish_queued_events(host) && queued;
}

bool nb_application_host_restart(struct nb_application_host *host,
                                 nb_application_id application)
{
    struct nb_application_slot *slot = find_slot(host, application);

    if (slot == NULL || host->pumping_events || slot->stopping) {
        return false;
    }
    if (slot->running && !nb_application_host_stop(host, application)) {
        return false;
    }
    return nb_application_host_start(host, application);
}

bool nb_application_host_sync_focus(struct nb_application_host *host)
{
    if (host == NULL || host->shell == NULL || !queue_focus_change(host)) {
        return false;
    }
    return finish_queued_events(host);
}

enum nb_application_dispatch_result
nb_application_host_dispatch_shell_action(
    struct nb_application_host *host,
    struct nb_shell_action action)
{
    enum nb_application_dispatch_result result =
        NB_APPLICATION_DISPATCH_UNHANDLED;

    if (host == NULL || host->shell == NULL ||
        !queue_focus_change(host)) {
        return NB_APPLICATION_DISPATCH_ERROR;
    }

    if (action.type == NB_SHELL_ACTION_NONE) {
        result = NB_APPLICATION_DISPATCH_UNHANDLED;
    } else if (action.type == NB_SHELL_ACTION_MENU_COMMAND) {
        struct nb_application_slot *slot =
            find_slot(host, action.menu_source);

        if (slot != NULL) {
            struct nb_application_event event;

            if (!slot->running || slot->stopping ||
                !menu_contains_command(slot, action.menu_command) ||
                find_window_index(slot, action.window) >=
                    slot->window_count) {
                (void)finish_queued_events(host);
                return NB_APPLICATION_DISPATCH_ERROR;
            }
            event = make_event(NB_APPLICATION_EVENT_MENU_COMMAND,
                               slot->id);
            event.window = action.window;
            event.menu_command = action.menu_command;
            event.cookie = window_cookie(slot, action.window);
            if (!queue_event(host, event)) {
                (void)finish_queued_events(host);
                return NB_APPLICATION_DISPATCH_ERROR;
            }
            result = NB_APPLICATION_DISPATCH_HANDLED;
        }
    } else if (action.type == NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED) {
        const nb_application_id owner =
            nb_application_host_window_owner(host, action.window);
        struct nb_application_slot *slot = find_slot(host, owner);

        if (slot != NULL) {
            struct nb_application_event event;

            if (!slot->running || slot->stopping) {
                (void)finish_queued_events(host);
                return NB_APPLICATION_DISPATCH_ERROR;
            }
            event = make_event(
                NB_APPLICATION_EVENT_WINDOW_CLOSE_REQUESTED,
                slot->id);
            event.window = action.window;
            event.cookie = window_cookie(slot, action.window);
            if (!queue_event(host, event)) {
                (void)finish_queued_events(host);
                return NB_APPLICATION_DISPATCH_ERROR;
            }
            result = NB_APPLICATION_DISPATCH_HANDLED;
        }
    } else {
        (void)finish_queued_events(host);
        return NB_APPLICATION_DISPATCH_ERROR;
    }

    if (!finish_queued_events(host)) {
        return NB_APPLICATION_DISPATCH_ERROR;
    }
    return result;
}

bool nb_application_host_is_running(
    const struct nb_application_host *host,
    nb_application_id application)
{
    const struct nb_application_slot *slot =
        find_slot_const(host, application);

    return slot != NULL && slot->running && !slot->stopping;
}

size_t nb_application_host_window_count(
    const struct nb_application_host *host,
    nb_application_id application)
{
    const struct nb_application_slot *slot =
        find_slot_const(host, application);

    return slot == NULL ? 0 : slot->window_count;
}

nb_window_id nb_application_host_window_at(
    const struct nb_application_host *host,
    nb_application_id application,
    size_t index)
{
    const struct nb_application_slot *slot =
        find_slot_const(host, application);

    if (slot == NULL || index >= slot->window_count) {
        return NB_WINDOW_ID_NONE;
    }
    return slot->windows[index].window;
}

void *nb_application_host_context(
    const struct nb_application_host *host,
    nb_application_id application)
{
    const struct nb_application_slot *slot =
        find_slot_const(host, application);

    return slot == NULL ? NULL : slot->context;
}

const struct nb_menu_model *nb_application_host_menu_model(
    const struct nb_application_host *host,
    nb_application_id application)
{
    const struct nb_application_slot *slot =
        find_slot_const(host, application);

    return slot == NULL ? NULL : &slot->menus[slot->active_menu].model;
}
