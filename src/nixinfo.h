#ifndef NIXBENCH_NIXINFO_H
#define NIXBENCH_NIXINFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "application.h"
#include "nixinfo_system.h"

enum {
    NB_NIXINFO_MAX_WINDOWS = 8,
    NB_NIXINFO_REFRESHED_AT_CAPACITY = 20,
    NB_NIXINFO_PROJECT_ITEM_COUNT = 6,
    NB_NIXINFO_VIEW_ITEM_COUNT = 1,
    NB_NIXINFO_WINDOW_ITEM_COUNT = 2,
    NB_NIXINFO_MENU_COUNT = 3
};

enum nb_nixinfo_command {
    NB_NIXINFO_COMMAND_NEW_WINDOW = 1,
    NB_NIXINFO_COMMAND_REFRESH,
    NB_NIXINFO_COMMAND_ABOUT,
    NB_NIXINFO_COMMAND_QUIT,
    NB_NIXINFO_COMMAND_TOGGLE_VERSION,
    NB_NIXINFO_COMMAND_CLOSE_WINDOW,
    NB_NIXINFO_COMMAND_CLOSE_OTHER_WINDOWS
};

enum nb_nixinfo_window_kind {
    NB_NIXINFO_WINDOW_SYSTEM = 1,
    NB_NIXINFO_WINDOW_ABOUT
};

struct nb_nixinfo_window_state {
    nb_window_id window;
    enum nb_nixinfo_window_kind kind;
    struct nb_nixinfo_system_snapshot snapshot;
    uint64_t revision;
    bool refresh_failed;
    bool show_full_version;
    char refreshed_at[NB_NIXINFO_REFRESHED_AT_CAPACITY];
};

typedef bool (*nb_nixinfo_snapshot_provider)(
    void *context,
    struct nb_nixinfo_system_snapshot *snapshot);

/*
 * This is application-owned, self-referential state. Its address must remain
 * stable after initialization; the menu descriptors point into this object,
 * and the in-process host borrows it as callback context while registered.
 */
struct nb_nixinfo {
    nb_application_id application;
    nb_nixinfo_snapshot_provider provide_snapshot;
    void *provider_context;
    struct nb_nixinfo_window_state windows[NB_NIXINFO_MAX_WINDOWS];
    size_t window_count;
    nb_window_id active_window;
    nb_window_id about_window;
    unsigned int next_window_position;
    struct nb_menu_item_spec
        project_items[NB_NIXINFO_PROJECT_ITEM_COUNT];
    struct nb_menu_item_spec view_items[NB_NIXINFO_VIEW_ITEM_COUNT];
    struct nb_menu_item_spec window_items[NB_NIXINFO_WINDOW_ITEM_COUNT];
    struct nb_menu_spec menus[NB_NIXINFO_MENU_COUNT];
    struct nb_menu_model menu_model;
};

void nb_nixinfo_init(struct nb_nixinfo *nixinfo,
                     nb_nixinfo_snapshot_provider provide_snapshot,
                     void *provider_context);
struct nb_application_spec nb_nixinfo_application_spec(
    struct nb_nixinfo *nixinfo);

void nb_nixinfo_handle_event(
    void *context,
    const struct nb_application_event *event,
    struct nb_application_request_batch *requests);

size_t nb_nixinfo_window_count(const struct nb_nixinfo *nixinfo);
nb_window_id nb_nixinfo_active_window(const struct nb_nixinfo *nixinfo);
nb_window_id nb_nixinfo_about_window(const struct nb_nixinfo *nixinfo);
bool nb_nixinfo_owns_window(const struct nb_nixinfo *nixinfo,
                            nb_window_id window);
const struct nb_nixinfo_window_state *nb_nixinfo_find_window(
    const struct nb_nixinfo *nixinfo,
    nb_window_id window);
const struct nb_menu_model *nb_nixinfo_menu_model(
    const struct nb_nixinfo *nixinfo);

#endif
