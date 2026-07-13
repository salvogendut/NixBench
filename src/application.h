#ifndef NIXBENCH_APPLICATION_H
#define NIXBENCH_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shell.h"

enum {
    NB_APPLICATION_MAX_APPLICATIONS = 8,
    NB_APPLICATION_NAME_CAPACITY = 64,
    NB_APPLICATION_REQUEST_CAPACITY = 32,
    NB_APPLICATION_EVENT_QUEUE_CAPACITY = 64,
    NB_APPLICATION_EVENT_PUMP_LIMIT = 256
};

typedef nb_menu_source_id nb_application_id;

#define NB_APPLICATION_ID_NONE NB_MENU_SOURCE_NONE

enum nb_application_event_type {
    NB_APPLICATION_EVENT_START,
    NB_APPLICATION_EVENT_MENU_COMMAND,
    NB_APPLICATION_EVENT_WINDOW_CLOSE_REQUESTED,
    NB_APPLICATION_EVENT_WINDOW_OPENED,
    NB_APPLICATION_EVENT_WINDOW_CLOSED,
    NB_APPLICATION_EVENT_FOCUS_CHANGED,
    NB_APPLICATION_EVENT_STOP
};

/*
 * FOCUS_CHANGED reports the recipient's new and previous focused windows.
 * Either field is NONE when that focus belongs outside the recipient.
 * WINDOW_OPENED uses succeeded=false and window=NONE when creation fails.
 * WINDOW_CLOSED/FOCUS_CHANGED requests emitted during teardown are ignored;
 * STOP is the final notification and must not emit requests.
 */
struct nb_application_event {
    enum nb_application_event_type type;
    nb_application_id application;
    nb_window_id window;
    nb_window_id previous_window;
    nb_menu_command menu_command;
    uint64_t cookie;
    bool succeeded;
};

enum nb_application_request_type {
    NB_APPLICATION_REQUEST_OPEN_WINDOW,
    NB_APPLICATION_REQUEST_CLOSE_WINDOW,
    NB_APPLICATION_REQUEST_ACTIVATE_WINDOW,
    NB_APPLICATION_REQUEST_PUBLISH_MENUS,
    NB_APPLICATION_REQUEST_EXIT
};

struct nb_application_request {
    enum nb_application_request_type type;
    nb_window_id window;
    struct nb_rect frame;
    uint64_t cookie;
    char title[NB_WINDOW_TITLE_CAPACITY];
};

/* Owned representation used so the shell never borrows client descriptors. */
struct nb_application_menu_snapshot {
    struct nb_menu_model model;
    struct nb_menu_spec menus[NB_MENU_MAX_MENUS];
    struct nb_menu_item_spec
        items[NB_MENU_MAX_MENUS][NB_MENU_MAX_ITEMS];
    char menu_labels[NB_MENU_MAX_MENUS][NB_MENU_TEXT_CAPACITY];
    char item_labels[NB_MENU_MAX_MENUS]
                    [NB_MENU_MAX_ITEMS]
                    [NB_MENU_TEXT_CAPACITY];
};

/*
 * A callback receives an empty, valid batch. Requests are copied into it and
 * applied only after the callback returns. Any rejected append invalidates the
 * whole batch. At most one menu publication is allowed in a batch, and EXIT
 * must be its only request. A batch containing a menu snapshot must not be
 * copied or retained because that snapshot contains pointers into the batch.
 */
struct nb_application_request_batch {
    struct nb_application_request
        requests[NB_APPLICATION_REQUEST_CAPACITY];
    struct nb_application_menu_snapshot published_menu;
    size_t request_count;
    bool valid;
    bool has_published_menu;
    bool has_exit;
};

typedef void (*nb_application_event_handler)(
    void *context,
    const struct nb_application_event *event,
    struct nb_application_request_batch *requests);

struct nb_application_spec {
    const char *name;
    const struct nb_menu_model *initial_menus;
    nb_application_event_handler handle_event;
    void *context;
};

struct nb_application_window_binding {
    nb_window_id window;
    uint64_t cookie;
};

struct nb_application_slot {
    bool occupied;
    bool running;
    bool stopping;
    nb_application_id id;
    char name[NB_APPLICATION_NAME_CAPACITY];
    nb_application_event_handler handle_event;
    void *context;
    struct nb_application_menu_snapshot menus[2];
    unsigned int active_menu;
    struct nb_application_window_binding windows[NB_DESKTOP_MAX_WINDOWS];
    size_t window_count;
};

struct nb_application_host {
    struct nb_shell *shell;
    struct nb_application_slot slots[NB_APPLICATION_MAX_APPLICATIONS];
    nb_application_id next_id;
    nb_application_id focused_application;
    nb_window_id focused_window;
    struct nb_application_event
        pending_events[NB_APPLICATION_EVENT_QUEUE_CAPACITY];
    size_t event_head;
    size_t event_count;
    bool pumping_events;
};

enum nb_application_dispatch_result {
    NB_APPLICATION_DISPATCH_ERROR = -1,
    NB_APPLICATION_DISPATCH_UNHANDLED,
    NB_APPLICATION_DISPATCH_HANDLED
};

/* Command IDs must be nonzero and unique within one published model. */
bool nb_application_menu_model_is_valid(const struct nb_menu_model *model);

void nb_application_request_batch_init(
    struct nb_application_request_batch *batch);
bool nb_application_requests_are_valid(
    const struct nb_application_request_batch *batch);
bool nb_application_request_open_window(
    struct nb_application_request_batch *batch,
    const char *title,
    struct nb_rect frame,
    uint64_t cookie);
bool nb_application_request_close_window(
    struct nb_application_request_batch *batch,
    nb_window_id window);
bool nb_application_request_activate_window(
    struct nb_application_request_batch *batch,
    nb_window_id window);
bool nb_application_request_publish_menus(
    struct nb_application_request_batch *batch,
    const struct nb_menu_model *menus);
bool nb_application_request_exit(
    struct nb_application_request_batch *batch);

/*
 * The shell is borrowed and must outlive the initialized host. The host's
 * address must remain stable while applications are registered because its
 * owned menu descriptors contain pointers into host storage.
 */
bool nb_application_host_init(struct nb_application_host *host,
                              struct nb_shell *shell);
nb_application_id nb_application_host_register(
    struct nb_application_host *host,
    const struct nb_application_spec *spec);
bool nb_application_host_unregister(struct nb_application_host *host,
                                    nb_application_id application);

bool nb_application_host_start(struct nb_application_host *host,
                               nb_application_id application);
bool nb_application_host_stop(struct nb_application_host *host,
                              nb_application_id application);
bool nb_application_host_restart(struct nb_application_host *host,
                                 nb_application_id application);

enum nb_application_dispatch_result
nb_application_host_dispatch_shell_action(
    struct nb_application_host *host,
    struct nb_shell_action action);
bool nb_application_host_sync_focus(struct nb_application_host *host);

nb_application_id nb_application_host_window_owner(
    const struct nb_application_host *host,
    nb_window_id window);
bool nb_application_host_owns_window(
    const struct nb_application_host *host,
    nb_application_id application,
    nb_window_id window);
bool nb_application_host_is_running(
    const struct nb_application_host *host,
    nb_application_id application);
size_t nb_application_host_window_count(
    const struct nb_application_host *host,
    nb_application_id application);
nb_window_id nb_application_host_window_at(
    const struct nb_application_host *host,
    nb_application_id application,
    size_t index);
void *nb_application_host_context(
    const struct nb_application_host *host,
    nb_application_id application);
/* Borrowed; invalidated by the application's next successful publication. */
const struct nb_menu_model *nb_application_host_menu_model(
    const struct nb_application_host *host,
    nb_application_id application);

#endif
