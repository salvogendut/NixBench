#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "application.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

enum {
    DESKTOP_SOURCE = 1,
    DESKTOP_COMMAND = 100,
    COMMAND_PUBLISH = 201,
    COMMAND_CLOSE = 202,
    COMMAND_ACTIVATE_FIRST = 203,
    COMMAND_EXIT = 204,
    COMMAND_CLOSE_FOREIGN = 205,
    EVENT_LOG_CAPACITY = 128
};

static const struct nb_menu_item_spec desktop_items[] = {
    {"Desktop", DESKTOP_COMMAND, NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec desktop_menus[] = {
    {"NixBench", desktop_items, 1}
};
static const struct nb_menu_model desktop_model = {desktop_menus, 1};

static char initial_menu_label[] = "Project";
static char initial_item_labels[][NB_MENU_TEXT_CAPACITY] = {
    "Publish menus",
    "Close window",
    "Activate first",
    "Exit",
    "Try foreign close"
};
static struct nb_menu_item_spec initial_items[] = {
    {initial_item_labels[0], COMMAND_PUBLISH, NB_MENU_ITEM_COMMAND, true, true},
    {initial_item_labels[1], COMMAND_CLOSE, NB_MENU_ITEM_COMMAND, true, false},
    {initial_item_labels[2], COMMAND_ACTIVATE_FIRST,
     NB_MENU_ITEM_COMMAND, true, false},
    {initial_item_labels[3], COMMAND_EXIT, NB_MENU_ITEM_COMMAND, true, false},
    {initial_item_labels[4], COMMAND_CLOSE_FOREIGN,
     NB_MENU_ITEM_COMMAND, true, false}
};
static struct nb_menu_spec initial_menus[] = {
    {initial_menu_label, initial_items,
     sizeof(initial_items) / sizeof(initial_items[0])}
};
static struct nb_menu_model initial_model = {initial_menus, 1};

static char updated_item_label[] = "Close current window";
static struct nb_menu_item_spec updated_items[] = {
    {"Publish menus", COMMAND_PUBLISH, NB_MENU_ITEM_COMMAND, true, false},
    {updated_item_label, COMMAND_CLOSE, NB_MENU_ITEM_COMMAND, false, true},
    {"Activate first", COMMAND_ACTIVATE_FIRST,
     NB_MENU_ITEM_COMMAND, true, false},
    {"Exit", COMMAND_EXIT, NB_MENU_ITEM_COMMAND, true, false},
    {"Try foreign close", COMMAND_CLOSE_FOREIGN,
     NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec updated_menus[] = {
    {"Project", updated_items,
     sizeof(updated_items) / sizeof(updated_items[0])}
};
static const struct nb_menu_model updated_model = {updated_menus, 1};

struct app_state {
    struct nb_application_event events[EVENT_LOG_CAPACITY];
    size_t event_count;
    size_t start_window_count;
    unsigned int start_generation;
    nb_window_id first_window;
    nb_window_id second_window;
    nb_window_id foreign_window;
    const struct nb_menu_model *publish_model;
    bool close_on_request;
    bool duplicate_close_request;
    bool reject_opened_event;
    bool publish_during_teardown;
    bool requests_succeeded;
};

static void append_open_request(struct app_state *state,
                                struct nb_application_request_batch *requests,
                                size_t index)
{
    char title[NB_WINDOW_TITLE_CAPACITY];
    const int written = snprintf(title,
                                 sizeof(title),
                                 "Test window %u-%u",
                                 state->start_generation,
                                 (unsigned int)(index + 1));
    const struct nb_rect frame = {
        40 + ((int)index * 80),
        50 + ((int)index * 50),
        260,
        180
    };

    if (written < 0 || (size_t)written >= sizeof(title) ||
        !nb_application_request_open_window(requests,
                                            title,
                                            frame,
                                            (uint64_t)(index + 10))) {
        state->requests_succeeded = false;
    }
}

static void handle_event(void *context,
                         const struct nb_application_event *event,
                         struct nb_application_request_batch *requests)
{
    struct app_state *state = context;
    size_t index;

    if (state->event_count < EVENT_LOG_CAPACITY) {
        state->events[state->event_count++] = *event;
    } else {
        state->requests_succeeded = false;
    }

    if (event->type == NB_APPLICATION_EVENT_START) {
        ++state->start_generation;
        state->first_window = NB_WINDOW_ID_NONE;
        state->second_window = NB_WINDOW_ID_NONE;
        for (index = 0; index < state->start_window_count; ++index) {
            append_open_request(state, requests, index);
        }
    } else if (event->type == NB_APPLICATION_EVENT_WINDOW_OPENED &&
               event->succeeded) {
        if (event->cookie == 10) {
            state->first_window = event->window;
        } else if (event->cookie == 11) {
            state->second_window = event->window;
        }
        if (state->reject_opened_event) {
            (void)nb_application_request_close_window(requests,
                                                       NB_WINDOW_ID_NONE);
        }
    } else if (event->type == NB_APPLICATION_EVENT_MENU_COMMAND) {
        bool accepted = true;

        if (event->menu_command == COMMAND_PUBLISH) {
            accepted = nb_application_request_publish_menus(
                requests,
                state->publish_model);
        } else if (event->menu_command == COMMAND_CLOSE) {
            accepted = nb_application_request_close_window(requests,
                                                             event->window);
            if (accepted && state->duplicate_close_request) {
                accepted = nb_application_request_close_window(
                    requests,
                    event->window);
            }
        } else if (event->menu_command == COMMAND_ACTIVATE_FIRST) {
            accepted = nb_application_request_activate_window(
                requests,
                state->first_window);
        } else if (event->menu_command == COMMAND_EXIT) {
            accepted = nb_application_request_exit(requests);
        } else if (event->menu_command == COMMAND_CLOSE_FOREIGN) {
            accepted = nb_application_request_close_window(
                requests,
                state->foreign_window);
        }
        if (!accepted) {
            state->requests_succeeded = false;
        }
    } else if (event->type ==
                   NB_APPLICATION_EVENT_WINDOW_CLOSE_REQUESTED &&
               state->close_on_request &&
               !nb_application_request_close_window(requests,
                                                     event->window)) {
        state->requests_succeeded = false;
    } else if (state->publish_during_teardown &&
               (event->type == NB_APPLICATION_EVENT_WINDOW_CLOSED ||
                event->type == NB_APPLICATION_EVENT_FOCUS_CHANGED) &&
               !nb_application_request_publish_menus(requests,
                                                     state->publish_model)) {
        state->requests_succeeded = false;
    }
}

static void handle_invalid_start(
    void *context,
    const struct nb_application_event *event,
    struct nb_application_request_batch *requests)
{
    (void)context;
    if (event->type == NB_APPLICATION_EVENT_START) {
        (void)nb_application_request_close_window(requests,
                                                   NB_WINDOW_ID_NONE);
    }
}

static void init_state(struct app_state *state, size_t start_windows)
{
    memset(state, 0, sizeof(*state));
    state->start_window_count = start_windows;
    state->publish_model = &updated_model;
    state->close_on_request = true;
    state->requests_succeeded = true;
}

static void init_host(struct nb_shell *shell,
                      struct nb_application_host *host)
{
    nb_shell_init(shell, DESKTOP_SOURCE, &desktop_model);
    CHECK(nb_application_host_init(host, shell));
}

static struct nb_shell_action menu_action(nb_application_id application,
                                          nb_window_id window,
                                          nb_menu_command command)
{
    const struct nb_shell_action action = {
        NB_SHELL_ACTION_MENU_COMMAND,
        window,
        application,
        command
    };

    return action;
}

static struct nb_shell_action close_action(nb_window_id window)
{
    const struct nb_shell_action action = {
        NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED,
        window,
        NB_MENU_SOURCE_NONE,
        NB_MENU_COMMAND_NONE
    };

    return action;
}

static const struct nb_menu_model *shell_menu_for_window(
    const struct nb_shell *shell,
    nb_window_id window)
{
    size_t index;

    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        if (shell->menu_bindings[index].window == window) {
            return shell->menu_bindings[index].menu_model;
        }
    }
    return NULL;
}

static const struct nb_application_event *last_event_of_type(
    const struct app_state *state,
    enum nb_application_event_type type)
{
    size_t index = state->event_count;

    while (index > 0) {
        --index;
        if (state->events[index].type == type) {
            return &state->events[index];
        }
    }
    return NULL;
}

static void test_menu_and_request_validation(void)
{
    struct nb_menu_item_spec item = {
        "Action", 1, NB_MENU_ITEM_COMMAND, true, false
    };
    struct nb_menu_spec menu = {"Menu", &item, 1};
    struct nb_menu_model model = {&menu, 1};
    struct nb_application_request_batch batch;
    char long_label[NB_MENU_TEXT_CAPACITY + 1];
    size_t index;

    CHECK(nb_application_menu_model_is_valid(&model));
    CHECK(!nb_application_menu_model_is_valid(NULL));

    model.menu_count = NB_MENU_MAX_MENUS + 1;
    CHECK(!nb_application_menu_model_is_valid(&model));
    model.menu_count = 1;
    model.menus = NULL;
    CHECK(!nb_application_menu_model_is_valid(&model));
    model.menus = &menu;

    memset(long_label, 'x', sizeof(long_label));
    long_label[sizeof(long_label) - 1] = '\0';
    menu.label = long_label;
    CHECK(!nb_application_menu_model_is_valid(&model));
    menu.label = "Menu";
    menu.item_count = NB_MENU_MAX_ITEMS + 1;
    CHECK(!nb_application_menu_model_is_valid(&model));
    menu.item_count = 1;
    menu.items = NULL;
    CHECK(!nb_application_menu_model_is_valid(&model));
    menu.items = &item;

    item.command = NB_MENU_COMMAND_NONE;
    CHECK(!nb_application_menu_model_is_valid(&model));
    item.command = 1;
    item.label = "";
    CHECK(!nb_application_menu_model_is_valid(&model));
    item.label = "Action";
    item.kind = (enum nb_menu_item_kind)99;
    CHECK(!nb_application_menu_model_is_valid(&model));
    item.kind = NB_MENU_ITEM_SEPARATOR;
    item.label = NULL;
    item.command = NB_MENU_COMMAND_NONE;
    item.enabled = false;
    item.checked = false;
    CHECK(nb_application_menu_model_is_valid(&model));
    item.enabled = true;
    CHECK(!nb_application_menu_model_is_valid(&model));
    item.enabled = false;
    item.checked = true;
    CHECK(!nb_application_menu_model_is_valid(&model));

    {
        struct nb_menu_item_spec duplicate_items[] = {
            {"First", 7, NB_MENU_ITEM_COMMAND, true, false},
            {"Second", 7, NB_MENU_ITEM_COMMAND, true, false}
        };
        const struct nb_menu_spec duplicate_menu = {
            "Duplicate", duplicate_items, 2
        };
        const struct nb_menu_model duplicate_model = {
            &duplicate_menu, 1
        };

        CHECK(!nb_application_menu_model_is_valid(&duplicate_model));
    }

    nb_application_request_batch_init(&batch);
    CHECK(nb_application_requests_are_valid(&batch));
    CHECK(!nb_application_request_open_window(
        &batch, "Bad", (struct nb_rect){0, 0, 0, 100}, 1));
    CHECK(!nb_application_requests_are_valid(&batch));

    nb_application_request_batch_init(&batch);
    for (index = 0; index < NB_APPLICATION_REQUEST_CAPACITY; ++index) {
        CHECK(nb_application_request_close_window(
            &batch, (nb_window_id)(index + 1)));
    }
    CHECK(!nb_application_request_close_window(&batch, 100));
    CHECK(!nb_application_requests_are_valid(&batch));

    item.kind = NB_MENU_ITEM_COMMAND;
    item.label = "Action";
    item.command = 1;
    item.enabled = true;
    item.checked = false;
    nb_application_request_batch_init(&batch);
    CHECK(nb_application_request_publish_menus(&batch, &model));
    CHECK(!nb_application_request_publish_menus(&batch, &model));
    CHECK(!nb_application_requests_are_valid(&batch));

    nb_application_request_batch_init(&batch);
    CHECK(nb_application_request_exit(&batch));
    CHECK(!nb_application_request_close_window(&batch, 1));
    CHECK(!nb_application_requests_are_valid(&batch));

    nb_application_request_batch_init(&batch);
    CHECK(nb_application_request_close_window(&batch, 1));
    CHECK(!nb_application_request_exit(&batch));

    nb_application_request_batch_init(&batch);
    batch.request_count = NB_APPLICATION_REQUEST_CAPACITY + 1;
    CHECK(!nb_application_requests_are_valid(&batch));
    nb_application_request_batch_init(&batch);
    batch.requests[0].type = (enum nb_application_request_type)99;
    batch.request_count = 1;
    CHECK(!nb_application_requests_are_valid(&batch));
}

static void test_registration_limits_and_copy(void)
{
    struct nb_shell shell;
    struct nb_application_host host;
    struct app_state state;
    struct nb_application_spec spec;
    nb_application_id ids[NB_APPLICATION_MAX_APPLICATIONS];
    const struct nb_menu_model *owned;
    nb_window_id unmanaged_window;
    size_t index;
    char long_name[NB_APPLICATION_NAME_CAPACITY + 1];

    init_state(&state, 0);
    nb_shell_init(&shell, DESKTOP_SOURCE, &desktop_model);
    unmanaged_window = nb_shell_open_window(&shell,
                                            "Unmanaged",
                                            (struct nb_rect){20, 30, 200, 120},
                                            2,
                                            &initial_model);
    CHECK(unmanaged_window != NB_WINDOW_ID_NONE);
    CHECK(nb_application_host_init(&host, &shell));
    spec.name = "Test application";
    spec.initial_menus = &initial_model;
    spec.handle_event = handle_event;
    spec.context = &state;

    ids[0] = nb_application_host_register(&host, &spec);
    CHECK(ids[0] != NB_APPLICATION_ID_NONE);
    CHECK(ids[0] != DESKTOP_SOURCE);
    CHECK(ids[0] != 2);
    owned = nb_application_host_menu_model(&host, ids[0]);
    CHECK(owned != NULL);
    CHECK(strcmp(owned->menus[0].label, "Project") == 0);
    CHECK(strcmp(owned->menus[0].items[0].label, "Publish menus") == 0);
    CHECK(owned->menus[0].items[0].checked);

    initial_menu_label[0] = 'X';
    initial_item_labels[0][0] = 'X';
    initial_items[0].checked = false;
    CHECK(strcmp(owned->menus[0].label, "Project") == 0);
    CHECK(strcmp(owned->menus[0].items[0].label, "Publish menus") == 0);
    CHECK(owned->menus[0].items[0].checked);
    initial_menu_label[0] = 'P';
    initial_item_labels[0][0] = 'P';
    initial_items[0].checked = true;

    for (index = 1; index < NB_APPLICATION_MAX_APPLICATIONS; ++index) {
        ids[index] = nb_application_host_register(&host, &spec);
        CHECK(ids[index] != NB_APPLICATION_ID_NONE);
        CHECK(ids[index] != DESKTOP_SOURCE);
        CHECK(ids[index] != ids[index - 1]);
    }
    CHECK(nb_application_host_register(&host, &spec) ==
          NB_APPLICATION_ID_NONE);

    for (index = 0; index < NB_APPLICATION_MAX_APPLICATIONS; ++index) {
        CHECK(nb_application_host_unregister(&host, ids[index]));
    }

    memset(long_name, 'n', sizeof(long_name));
    long_name[sizeof(long_name) - 1] = '\0';
    spec.name = long_name;
    CHECK(nb_application_host_register(&host, &spec) ==
          NB_APPLICATION_ID_NONE);
    spec.name = "Test";
    spec.handle_event = NULL;
    CHECK(nb_application_host_register(&host, &spec) ==
          NB_APPLICATION_ID_NONE);
}

static void test_lifecycle_routing_and_dynamic_menus(void)
{
    struct nb_shell shell;
    struct nb_application_host host;
    struct app_state state_a;
    struct app_state state_b;
    struct nb_application_spec spec_a;
    struct nb_application_spec spec_b;
    nb_application_id app_a;
    nb_application_id app_b;
    const struct nb_application_event *focus;
    const struct nb_menu_model *owned_menu;
    nb_window_id first_generation_window;
    size_t events_before_restart;

    init_state(&state_a, 2);
    init_state(&state_b, 1);
    init_host(&shell, &host);
    spec_a.name = "Application A";
    spec_a.initial_menus = &initial_model;
    spec_a.handle_event = handle_event;
    spec_a.context = &state_a;
    spec_b = spec_a;
    spec_b.name = "Application B";
    spec_b.context = &state_b;

    app_a = nb_application_host_register(&host, &spec_a);
    app_b = nb_application_host_register(&host, &spec_b);
    CHECK(app_a != NB_APPLICATION_ID_NONE);
    CHECK(app_b != NB_APPLICATION_ID_NONE);
    CHECK(app_a != app_b);

    CHECK(nb_application_host_start(&host, app_a));
    CHECK(state_a.requests_succeeded);
    CHECK(nb_application_host_is_running(&host, app_a));
    CHECK(nb_application_host_window_count(&host, app_a) == 2);
    CHECK(state_a.first_window != NB_WINDOW_ID_NONE);
    CHECK(state_a.second_window != NB_WINDOW_ID_NONE);
    CHECK(nb_application_host_window_owner(&host, state_a.first_window) ==
          app_a);
    CHECK(nb_application_host_owns_window(&host,
                                          app_a,
                                          state_a.second_window));
    CHECK(!nb_application_host_owns_window(&host,
                                           app_b,
                                           state_a.second_window));
    CHECK(nb_application_host_context(&host, app_a) == &state_a);
    CHECK(nb_application_host_window_at(&host, app_a, 0) ==
          state_a.first_window);
    CHECK(nb_application_host_window_at(&host, app_a, 99) ==
          NB_WINDOW_ID_NONE);

    state_a.duplicate_close_request = true;
    CHECK(nb_application_host_dispatch_shell_action(
              &host,
              menu_action(app_a,
                          state_a.first_window,
                          COMMAND_CLOSE)) ==
          NB_APPLICATION_DISPATCH_ERROR);
    CHECK(nb_application_host_window_count(&host, app_a) == 2);
    CHECK(nb_desktop_find_window(&shell.desktop,
                                 state_a.first_window) != NULL);
    CHECK(last_event_of_type(&state_a,
                             NB_APPLICATION_EVENT_WINDOW_CLOSED) == NULL);
    state_a.duplicate_close_request = false;

    owned_menu = nb_application_host_menu_model(&host, app_a);
    CHECK(shell_menu_for_window(&shell, state_a.first_window) == owned_menu);
    CHECK(shell_menu_for_window(&shell, state_a.second_window) == owned_menu);
    CHECK(shell.active_menu_source == app_a);
    CHECK(shell.active_menu_window == state_a.second_window);
    focus = last_event_of_type(&state_a,
                               NB_APPLICATION_EVENT_FOCUS_CHANGED);
    CHECK(focus != NULL);
    if (focus != NULL) {
        CHECK(focus->window == state_a.second_window);
        CHECK(focus->previous_window == state_a.first_window);
    }

    CHECK(nb_shell_activate_window(&shell, state_a.first_window));
    CHECK(nb_application_host_sync_focus(&host));
    focus = last_event_of_type(&state_a,
                               NB_APPLICATION_EVENT_FOCUS_CHANGED);
    CHECK(focus != NULL);
    if (focus != NULL) {
        CHECK(focus->window == state_a.first_window);
        CHECK(focus->previous_window == state_a.second_window);
    }

    CHECK(nb_application_host_dispatch_shell_action(
              &host,
              menu_action(app_a,
                          state_a.first_window,
                          COMMAND_ACTIVATE_FIRST)) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(nb_desktop_active_window_id(&shell.desktop) ==
          state_a.first_window);

    CHECK(nb_shell_menu_key_press(&shell, NB_MENU_KEY_TOGGLE).type ==
          NB_SHELL_ACTION_NONE);
    CHECK(nb_menu_is_open(&shell.menu));
    CHECK(nb_application_host_dispatch_shell_action(
              &host,
              menu_action(app_a,
                          state_a.first_window,
                          COMMAND_PUBLISH)) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(!nb_menu_is_open(&shell.menu));
    owned_menu = nb_application_host_menu_model(&host, app_a);
    CHECK(owned_menu != &initial_model);
    CHECK(!owned_menu->menus[0].items[1].enabled);
    CHECK(owned_menu->menus[0].items[1].checked);
    CHECK(strcmp(owned_menu->menus[0].items[1].label,
                 "Close current window") == 0);
    CHECK(shell_menu_for_window(&shell, state_a.first_window) == owned_menu);
    CHECK(shell_menu_for_window(&shell, state_a.second_window) == owned_menu);
    updated_item_label[0] = 'X';
    CHECK(strcmp(owned_menu->menus[0].items[1].label,
                 "Close current window") == 0);
    updated_item_label[0] = 'C';
    CHECK(nb_application_host_dispatch_shell_action(
              &host,
              menu_action(app_a,
                          state_a.first_window,
                          COMMAND_CLOSE)) ==
          NB_APPLICATION_DISPATCH_ERROR);
    CHECK(nb_desktop_find_window(&shell.desktop,
                                 state_a.first_window) != NULL);

    CHECK(nb_application_host_start(&host, app_b));
    CHECK(state_b.first_window != NB_WINDOW_ID_NONE);
    CHECK(nb_application_host_window_owner(&host, state_b.first_window) ==
          app_b);
    state_a.foreign_window = state_b.first_window;

    CHECK(nb_shell_activate_window(&shell, state_a.first_window));
    CHECK(nb_application_host_sync_focus(&host));
    CHECK(nb_application_host_dispatch_shell_action(
              &host,
              menu_action(app_a,
                          state_b.first_window,
                          COMMAND_EXIT)) ==
          NB_APPLICATION_DISPATCH_ERROR);
    CHECK(nb_desktop_find_window(&shell.desktop,
                                 state_b.first_window) != NULL);
    CHECK(nb_application_host_dispatch_shell_action(
              &host,
              menu_action(app_a,
                          state_a.first_window,
                          COMMAND_CLOSE_FOREIGN)) ==
          NB_APPLICATION_DISPATCH_ERROR);
    CHECK(nb_desktop_find_window(&shell.desktop,
                                 state_b.first_window) != NULL);

    CHECK(nb_application_host_dispatch_shell_action(
              &host, close_action(state_a.second_window)) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(nb_application_host_window_count(&host, app_a) == 1);
    CHECK(nb_application_host_window_owner(&host, state_a.second_window) ==
          NB_APPLICATION_ID_NONE);
    CHECK(last_event_of_type(&state_a,
                             NB_APPLICATION_EVENT_WINDOW_CLOSED) != NULL);

    first_generation_window = state_a.first_window;
    state_a.publish_during_teardown = true;
    CHECK(nb_application_host_dispatch_shell_action(
              &host,
              menu_action(app_a,
                          state_a.first_window,
                          COMMAND_EXIT)) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(!nb_application_host_is_running(&host, app_a));
    CHECK(nb_application_host_window_count(&host, app_a) == 0);
    CHECK(nb_desktop_find_window(&shell.desktop,
                                 first_generation_window) == NULL);
    CHECK(last_event_of_type(&state_a, NB_APPLICATION_EVENT_STOP) != NULL);
    CHECK(!nb_application_host_unregister(&host, app_b));

    events_before_restart = state_a.event_count;
    CHECK(nb_application_host_restart(&host, app_a));
    CHECK(nb_application_host_is_running(&host, app_a));
    CHECK(state_a.event_count > events_before_restart);
    CHECK(state_a.first_window != NB_WINDOW_ID_NONE);
    CHECK(state_a.first_window != first_generation_window);
    CHECK(nb_application_host_window_count(&host, app_a) == 2);
    CHECK(nb_application_host_menu_model(&host, app_a)->menus[0]
              .items[1].enabled == false);

    CHECK(nb_application_host_stop(&host, app_a));
    CHECK(nb_application_host_stop(&host, app_b));
    CHECK(nb_application_host_unregister(&host, app_a));
    CHECK(nb_application_host_unregister(&host, app_b));
    CHECK(state_a.requests_succeeded);
    CHECK(state_b.requests_succeeded);
}

static void test_rejected_start_restores_inert_state(void)
{
    struct nb_shell shell;
    struct nb_application_host host;
    const struct nb_application_spec spec = {
        "Invalid starter",
        &initial_model,
        handle_invalid_start,
        NULL
    };
    nb_application_id application;

    init_host(&shell, &host);
    application = nb_application_host_register(&host, &spec);
    CHECK(application != NB_APPLICATION_ID_NONE);
    CHECK(!nb_application_host_start(&host, application));
    CHECK(!nb_application_host_is_running(&host, application));
    CHECK(nb_application_host_window_count(&host, application) == 0);
    CHECK(nb_application_host_unregister(&host, application));
}

static void test_rejected_descendant_start_event_rolls_back(void)
{
    struct nb_shell shell;
    struct nb_application_host host;
    struct app_state state;
    struct nb_application_spec spec;
    nb_application_id application;
    nb_window_id opened_window;

    init_state(&state, 1);
    state.reject_opened_event = true;
    init_host(&shell, &host);
    spec = (struct nb_application_spec){
        "Invalid lifecycle application",
        &initial_model,
        handle_event,
        &state
    };
    application = nb_application_host_register(&host, &spec);
    CHECK(application != NB_APPLICATION_ID_NONE);
    CHECK(!nb_application_host_start(&host, application));
    opened_window = state.first_window;
    CHECK(opened_window != NB_WINDOW_ID_NONE);
    CHECK(!nb_application_host_is_running(&host, application));
    CHECK(nb_application_host_window_count(&host, application) == 0);
    CHECK(nb_desktop_window_count(&shell.desktop) == 0);
    CHECK(nb_desktop_find_window(&shell.desktop, opened_window) == NULL);
    CHECK(last_event_of_type(&state,
                             NB_APPLICATION_EVENT_WINDOW_CLOSED) != NULL);
    CHECK(last_event_of_type(&state, NB_APPLICATION_EVENT_STOP) != NULL);
    CHECK(nb_application_host_unregister(&host, application));
}

int main(void)
{
    test_menu_and_request_validation();
    test_registration_limits_and_copy();
    test_lifecycle_routing_and_dynamic_menus();
    test_rejected_start_restores_inert_state();
    test_rejected_descendant_start_event_rolls_back();

    if (failures != 0) {
        fprintf(stderr, "%d application host check(s) failed\n", failures);
        return 1;
    }

    puts("application host checks passed");
    return 0;
}
