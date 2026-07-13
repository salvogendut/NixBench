#include "nixinfo.h"

#include <string.h>
#include <time.h>

enum {
    PROJECT_NEW_INDEX = 0,
    PROJECT_REFRESH_INDEX = 1,
    PROJECT_ABOUT_INDEX = 3,
    PROJECT_QUIT_INDEX = 5,
    VIEW_VERSION_INDEX = 0,
    WINDOW_CLOSE_INDEX = 0,
    WINDOW_CLOSE_OTHERS_INDEX = 1,
    NIXINFO_WINDOW_X = 96,
    NIXINFO_WINDOW_Y = 60,
    NIXINFO_WINDOW_WIDTH = 620,
    NIXINFO_WINDOW_HEIGHT = 360,
    NIXINFO_WINDOW_CASCADE = 26,
    NIXINFO_WINDOW_CASCADE_COUNT = 7,
    NIXINFO_ABOUT_X = 300,
    NIXINFO_ABOUT_Y = 150,
    NIXINFO_ABOUT_WIDTH = 430,
    NIXINFO_ABOUT_HEIGHT = 230
};

static const uint64_t system_window_cookie = UINT64_C(1);
static const uint64_t about_window_cookie = UINT64_C(2);

static bool native_snapshot_provider(
    void *context,
    struct nb_nixinfo_system_snapshot *snapshot)
{
    (void)context;
    nb_nixinfo_system_collect(snapshot);
    return snapshot->available != 0;
}

static struct nb_nixinfo_window_state *find_window_mutable(
    struct nb_nixinfo *nixinfo,
    nb_window_id window)
{
    size_t index;

    if (window == NB_WINDOW_ID_NONE) {
        return NULL;
    }
    for (index = 0; index < NB_NIXINFO_MAX_WINDOWS; ++index) {
        if (nixinfo->windows[index].window == window) {
            return &nixinfo->windows[index];
        }
    }
    return NULL;
}

static struct nb_nixinfo_window_state *find_free_window(
    struct nb_nixinfo *nixinfo)
{
    size_t index;

    for (index = 0; index < NB_NIXINFO_MAX_WINDOWS; ++index) {
        if (nixinfo->windows[index].window == NB_WINDOW_ID_NONE) {
            return &nixinfo->windows[index];
        }
    }
    return NULL;
}

const struct nb_nixinfo_window_state *nb_nixinfo_find_window(
    const struct nb_nixinfo *nixinfo,
    nb_window_id window)
{
    size_t index;

    if (window == NB_WINDOW_ID_NONE) {
        return NULL;
    }
    for (index = 0; index < NB_NIXINFO_MAX_WINDOWS; ++index) {
        if (nixinfo->windows[index].window == window) {
            return &nixinfo->windows[index];
        }
    }
    return NULL;
}

static void set_refreshed_at(
    char refreshed_at[NB_NIXINFO_REFRESHED_AT_CAPACITY])
{
    const time_t now = time(NULL);
    const struct tm *local_time = localtime(&now);

    if (local_time == NULL ||
        strftime(refreshed_at,
                 NB_NIXINFO_REFRESHED_AT_CAPACITY,
                 "%Y-%m-%d %H:%M:%S",
                 local_time) == 0) {
        (void)memcpy(refreshed_at,
                     "Time unavailable",
                     sizeof("Time unavailable"));
    }
}

static bool refresh_window(struct nb_nixinfo *nixinfo,
                           struct nb_nixinfo_window_state *window)
{
    struct nb_nixinfo_system_snapshot snapshot;

    (void)memset(&snapshot, 0, sizeof(snapshot));
    if (!nixinfo->provide_snapshot(nixinfo->provider_context, &snapshot)) {
        window->refresh_failed = true;
        return false;
    }

    window->snapshot = snapshot;
    ++window->revision;
    if (window->revision == 0) {
        window->revision = 1;
    }
    window->refresh_failed = false;
    set_refreshed_at(window->refreshed_at);
    return true;
}

static void update_menu_state(struct nb_nixinfo *nixinfo)
{
    const struct nb_nixinfo_window_state *active =
        nb_nixinfo_find_window(nixinfo, nixinfo->active_window);
    const bool has_active = active != NULL;
    const bool system_active =
        active != NULL && active->kind == NB_NIXINFO_WINDOW_SYSTEM;

    nixinfo->project_items[PROJECT_NEW_INDEX].enabled =
        nixinfo->window_count < NB_NIXINFO_MAX_WINDOWS;
    nixinfo->project_items[PROJECT_REFRESH_INDEX].enabled = system_active;
    nixinfo->project_items[PROJECT_ABOUT_INDEX].enabled =
        nixinfo->about_window != NB_WINDOW_ID_NONE ||
        nixinfo->window_count < NB_NIXINFO_MAX_WINDOWS;
    nixinfo->view_items[VIEW_VERSION_INDEX].enabled = system_active;
    nixinfo->view_items[VIEW_VERSION_INDEX].label =
        system_active && active->show_full_version
            ? "Hide Kernel Version"
            : "Show Kernel Version";
    nixinfo->window_items[WINDOW_CLOSE_INDEX].enabled = has_active;
    nixinfo->window_items[WINDOW_CLOSE_OTHERS_INDEX].enabled =
        has_active && nixinfo->window_count > 1;
}

static void publish_menu(struct nb_nixinfo *nixinfo,
                         struct nb_application_request_batch *requests)
{
    update_menu_state(nixinfo);
    (void)nb_application_request_publish_menus(requests,
                                                &nixinfo->menu_model);
}

static bool request_system_window(
    struct nb_nixinfo *nixinfo,
    struct nb_application_request_batch *requests)
{
    const int cascade =
        (int)(nixinfo->next_window_position %
              (unsigned int)NIXINFO_WINDOW_CASCADE_COUNT) *
        NIXINFO_WINDOW_CASCADE;
    const struct nb_rect frame = {
        NIXINFO_WINDOW_X + cascade,
        NIXINFO_WINDOW_Y + cascade,
        NIXINFO_WINDOW_WIDTH,
        NIXINFO_WINDOW_HEIGHT
    };

    if (nixinfo->window_count >= NB_NIXINFO_MAX_WINDOWS ||
        !nb_application_request_open_window(requests,
                                            "NixInfo",
                                            frame,
                                            system_window_cookie)) {
        return false;
    }
    ++nixinfo->next_window_position;
    return true;
}

static void request_about_window(
    struct nb_nixinfo *nixinfo,
    struct nb_application_request_batch *requests)
{
    const struct nb_rect frame = {
        NIXINFO_ABOUT_X,
        NIXINFO_ABOUT_Y,
        NIXINFO_ABOUT_WIDTH,
        NIXINFO_ABOUT_HEIGHT
    };

    if (nixinfo->about_window != NB_WINDOW_ID_NONE) {
        (void)nb_application_request_activate_window(
            requests,
            nixinfo->about_window);
    } else if (nixinfo->window_count < NB_NIXINFO_MAX_WINDOWS) {
        (void)nb_application_request_open_window(requests,
                                                 "About NixInfo",
                                                 frame,
                                                 about_window_cookie);
    }
}

static void add_opened_window(
    struct nb_nixinfo *nixinfo,
    const struct nb_application_event *event,
    struct nb_application_request_batch *requests)
{
    struct nb_nixinfo_window_state *window;

    if (!event->succeeded || event->window == NB_WINDOW_ID_NONE ||
        (event->cookie != system_window_cookie &&
         event->cookie != about_window_cookie)) {
        return;
    }

    window = find_free_window(nixinfo);
    if (window == NULL) {
        (void)nb_application_request_close_window(requests, event->window);
        return;
    }

    (void)memset(window, 0, sizeof(*window));
    window->window = event->window;
    window->kind = event->cookie == about_window_cookie
                       ? NB_NIXINFO_WINDOW_ABOUT
                       : NB_NIXINFO_WINDOW_SYSTEM;
    window->show_full_version = false;
    ++nixinfo->window_count;
    nixinfo->active_window = event->window;
    if (window->kind == NB_NIXINFO_WINDOW_ABOUT) {
        nixinfo->about_window = event->window;
    } else {
        (void)refresh_window(nixinfo, window);
    }
    publish_menu(nixinfo, requests);
}

static void remove_closed_window(
    struct nb_nixinfo *nixinfo,
    nb_window_id closed,
    struct nb_application_request_batch *requests)
{
    struct nb_nixinfo_window_state *window =
        find_window_mutable(nixinfo, closed);

    if (window == NULL) {
        return;
    }
    if (nixinfo->about_window == closed) {
        nixinfo->about_window = NB_WINDOW_ID_NONE;
    }
    if (nixinfo->active_window == closed) {
        nixinfo->active_window = NB_WINDOW_ID_NONE;
    }
    (void)memset(window, 0, sizeof(*window));
    if (nixinfo->window_count > 0) {
        --nixinfo->window_count;
    }
    publish_menu(nixinfo, requests);
}

static void close_other_windows(
    const struct nb_nixinfo *nixinfo,
    nb_window_id keep,
    struct nb_application_request_batch *requests)
{
    size_t index;

    for (index = 0; index < NB_NIXINFO_MAX_WINDOWS; ++index) {
        const nb_window_id window = nixinfo->windows[index].window;

        if (window != NB_WINDOW_ID_NONE && window != keep) {
            (void)nb_application_request_close_window(requests, window);
        }
    }
}

static void handle_menu_command(
    struct nb_nixinfo *nixinfo,
    const struct nb_application_event *event,
    struct nb_application_request_batch *requests)
{
    struct nb_nixinfo_window_state *context =
        find_window_mutable(nixinfo, event->window);

    if (context == NULL) {
        return;
    }

    switch (event->menu_command) {
    case NB_NIXINFO_COMMAND_NEW_WINDOW:
        (void)request_system_window(nixinfo, requests);
        break;
    case NB_NIXINFO_COMMAND_REFRESH:
        if (context->kind == NB_NIXINFO_WINDOW_SYSTEM) {
            (void)refresh_window(nixinfo, context);
        }
        break;
    case NB_NIXINFO_COMMAND_ABOUT:
        request_about_window(nixinfo, requests);
        break;
    case NB_NIXINFO_COMMAND_QUIT:
        (void)nb_application_request_exit(requests);
        break;
    case NB_NIXINFO_COMMAND_TOGGLE_VERSION:
        if (context->kind == NB_NIXINFO_WINDOW_SYSTEM) {
            context->show_full_version = !context->show_full_version;
            publish_menu(nixinfo, requests);
        }
        break;
    case NB_NIXINFO_COMMAND_CLOSE_WINDOW:
        (void)nb_application_request_close_window(requests, event->window);
        break;
    case NB_NIXINFO_COMMAND_CLOSE_OTHER_WINDOWS:
        if (nixinfo->window_count > 1) {
            close_other_windows(nixinfo, event->window, requests);
        }
        break;
    default:
        break;
    }
}

void nb_nixinfo_handle_event(
    void *context,
    const struct nb_application_event *event,
    struct nb_application_request_batch *requests)
{
    struct nb_nixinfo *nixinfo = context;

    if (nixinfo == NULL || event == NULL || requests == NULL) {
        return;
    }
    if (nixinfo->application == NB_APPLICATION_ID_NONE) {
        nixinfo->application = event->application;
    }
    if (event->application != nixinfo->application) {
        return;
    }

    switch (event->type) {
    case NB_APPLICATION_EVENT_START:
        if (nixinfo->window_count == 0) {
            (void)request_system_window(nixinfo, requests);
        } else if (nixinfo->active_window != NB_WINDOW_ID_NONE) {
            (void)nb_application_request_activate_window(
                requests,
                nixinfo->active_window);
        }
        break;
    case NB_APPLICATION_EVENT_MENU_COMMAND:
        handle_menu_command(nixinfo, event, requests);
        break;
    case NB_APPLICATION_EVENT_WINDOW_CLOSE_REQUESTED:
        if (nb_nixinfo_owns_window(nixinfo, event->window)) {
            (void)nb_application_request_close_window(requests,
                                                       event->window);
        }
        break;
    case NB_APPLICATION_EVENT_WINDOW_OPENED:
        add_opened_window(nixinfo, event, requests);
        break;
    case NB_APPLICATION_EVENT_WINDOW_CLOSED:
        remove_closed_window(nixinfo, event->window, requests);
        break;
    case NB_APPLICATION_EVENT_FOCUS_CHANGED:
        nixinfo->active_window =
            nb_nixinfo_owns_window(nixinfo, event->window)
                ? event->window
                : NB_WINDOW_ID_NONE;
        publish_menu(nixinfo, requests);
        break;
    case NB_APPLICATION_EVENT_STOP:
        nixinfo->active_window = NB_WINDOW_ID_NONE;
        break;
    }
}

void nb_nixinfo_init(struct nb_nixinfo *nixinfo,
                     nb_nixinfo_snapshot_provider provide_snapshot,
                     void *provider_context)
{
    size_t index;

    (void)memset(nixinfo, 0, sizeof(*nixinfo));
    nixinfo->provide_snapshot = provide_snapshot != NULL
                                    ? provide_snapshot
                                    : native_snapshot_provider;
    nixinfo->provider_context = provider_context;

    nixinfo->project_items[PROJECT_NEW_INDEX] =
        (struct nb_menu_item_spec){"New Window",
                                   NB_NIXINFO_COMMAND_NEW_WINDOW,
                                   NB_MENU_ITEM_COMMAND,
                                   true};
    nixinfo->project_items[PROJECT_REFRESH_INDEX] =
        (struct nb_menu_item_spec){"Refresh",
                                   NB_NIXINFO_COMMAND_REFRESH,
                                   NB_MENU_ITEM_COMMAND,
                                   false};
    nixinfo->project_items[2] =
        (struct nb_menu_item_spec){NULL,
                                   NB_MENU_COMMAND_NONE,
                                   NB_MENU_ITEM_SEPARATOR,
                                   false};
    nixinfo->project_items[PROJECT_ABOUT_INDEX] =
        (struct nb_menu_item_spec){"About NixInfo",
                                   NB_NIXINFO_COMMAND_ABOUT,
                                   NB_MENU_ITEM_COMMAND,
                                   true};
    nixinfo->project_items[4] =
        (struct nb_menu_item_spec){NULL,
                                   NB_MENU_COMMAND_NONE,
                                   NB_MENU_ITEM_SEPARATOR,
                                   false};
    nixinfo->project_items[PROJECT_QUIT_INDEX] =
        (struct nb_menu_item_spec){"Quit NixInfo",
                                   NB_NIXINFO_COMMAND_QUIT,
                                   NB_MENU_ITEM_COMMAND,
                                   true};
    nixinfo->view_items[VIEW_VERSION_INDEX] =
        (struct nb_menu_item_spec){"Show Kernel Version",
                                   NB_NIXINFO_COMMAND_TOGGLE_VERSION,
                                   NB_MENU_ITEM_COMMAND,
                                   false};
    nixinfo->window_items[WINDOW_CLOSE_INDEX] =
        (struct nb_menu_item_spec){"Close Window",
                                   NB_NIXINFO_COMMAND_CLOSE_WINDOW,
                                   NB_MENU_ITEM_COMMAND,
                                   false};
    nixinfo->window_items[WINDOW_CLOSE_OTHERS_INDEX] =
        (struct nb_menu_item_spec){"Close Other Windows",
                                   NB_NIXINFO_COMMAND_CLOSE_OTHER_WINDOWS,
                                   NB_MENU_ITEM_COMMAND,
                                   false};

    nixinfo->menus[0] =
        (struct nb_menu_spec){"Project",
                              nixinfo->project_items,
                              NB_NIXINFO_PROJECT_ITEM_COUNT};
    nixinfo->menus[1] =
        (struct nb_menu_spec){"View",
                              nixinfo->view_items,
                              NB_NIXINFO_VIEW_ITEM_COUNT};
    nixinfo->menus[2] =
        (struct nb_menu_spec){"Window",
                              nixinfo->window_items,
                              NB_NIXINFO_WINDOW_ITEM_COUNT};
    nixinfo->menu_model.menus = nixinfo->menus;
    nixinfo->menu_model.menu_count = NB_NIXINFO_MENU_COUNT;

    for (index = 0; index < NB_NIXINFO_MAX_WINDOWS; ++index) {
        nixinfo->windows[index].window = NB_WINDOW_ID_NONE;
    }
    update_menu_state(nixinfo);
}

struct nb_application_spec nb_nixinfo_application_spec(
    struct nb_nixinfo *nixinfo)
{
    const struct nb_application_spec spec = {
        "NixInfo",
        &nixinfo->menu_model,
        nb_nixinfo_handle_event,
        nixinfo
    };

    return spec;
}

size_t nb_nixinfo_window_count(const struct nb_nixinfo *nixinfo)
{
    return nixinfo->window_count;
}

nb_window_id nb_nixinfo_active_window(const struct nb_nixinfo *nixinfo)
{
    return nixinfo->active_window;
}

nb_window_id nb_nixinfo_about_window(const struct nb_nixinfo *nixinfo)
{
    return nixinfo->about_window;
}

bool nb_nixinfo_owns_window(const struct nb_nixinfo *nixinfo,
                            nb_window_id window)
{
    return nb_nixinfo_find_window(nixinfo, window) != NULL;
}

const struct nb_menu_model *nb_nixinfo_menu_model(
    const struct nb_nixinfo *nixinfo)
{
    return &nixinfo->menu_model;
}
