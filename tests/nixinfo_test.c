#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nixinfo.h"

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
    FOREIGN_SOURCE = 99,
    DESKTOP_COMMAND = 100,
    FOREIGN_COMMAND = 200
};

static const struct nb_menu_item_spec desktop_items[] = {
    {"Desktop", DESKTOP_COMMAND, NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec desktop_menus[] = {
    {"NixBench", desktop_items, 1}
};
static const struct nb_menu_model desktop_model = {desktop_menus, 1};

static const struct nb_menu_item_spec foreign_items[] = {
    {"Foreign", FOREIGN_COMMAND, NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec foreign_menus[] = {
    {"Foreign", foreign_items, 1}
};
static const struct nb_menu_model foreign_model = {foreign_menus, 1};

struct fake_provider {
    unsigned int calls;
    bool fail_next;
};

struct fixture {
    struct nb_shell shell;
    struct nb_application_host host;
    struct nb_nixinfo nixinfo;
    struct fake_provider provider;
    nb_application_id application;
};

static bool provide_snapshot(
    void *context,
    struct nb_nixinfo_system_snapshot *snapshot)
{
    struct fake_provider *provider = context;

    ++provider->calls;
    if (provider->fail_next) {
        provider->fail_next = false;
        return false;
    }

    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->available = NB_NIXINFO_SYSTEM_HAS_HOSTNAME |
                          NB_NIXINFO_SYSTEM_HAS_SYSTEM_NAME |
                          NB_NIXINFO_SYSTEM_HAS_RELEASE |
                          NB_NIXINFO_SYSTEM_HAS_VERSION |
                          NB_NIXINFO_SYSTEM_HAS_ARCHITECTURE |
                          NB_NIXINFO_SYSTEM_HAS_CPU_MODEL |
                          NB_NIXINFO_SYSTEM_HAS_CPU_COUNT |
                          NB_NIXINFO_SYSTEM_HAS_PHYSICAL_MEMORY |
                          NB_NIXINFO_SYSTEM_HAS_UPTIME |
                          NB_NIXINFO_SYSTEM_HAS_LOAD_AVERAGES |
                          NB_NIXINFO_SYSTEM_HAS_ROOT_FILESYSTEM;
    (void)snprintf(snapshot->hostname,
                   sizeof(snapshot->hostname),
                   "test-host-%u",
                   provider->calls);
    (void)snprintf(snapshot->system_name,
                   sizeof(snapshot->system_name),
                   "NetBSD");
    (void)snprintf(snapshot->release,
                   sizeof(snapshot->release),
                   "10.1");
    (void)snprintf(snapshot->version,
                   sizeof(snapshot->version),
                   "GENERIC test snapshot %u",
                   provider->calls);
    (void)snprintf(snapshot->architecture,
                   sizeof(snapshot->architecture),
                   "amd64");
    (void)snprintf(snapshot->cpu_model,
                   sizeof(snapshot->cpu_model),
                   "Test CPU");
    snapshot->online_cpu_count = 4;
    snapshot->physical_memory_bytes = UINT64_C(8589934592);
    snapshot->uptime_seconds = UINT64_C(93784);
    snapshot->load_averages[0] = 0.25;
    snapshot->load_averages[1] = 0.5;
    snapshot->load_averages[2] = 0.75;
    snapshot->root_total_bytes = UINT64_C(107374182400);
    snapshot->root_available_bytes = UINT64_C(53687091200);
    return true;
}

static void init_fixture(struct fixture *fixture)
{
    struct nb_application_spec spec;

    (void)memset(fixture, 0, sizeof(*fixture));
    nb_shell_init(&fixture->shell, DESKTOP_SOURCE, &desktop_model);
    CHECK(nb_application_host_init(&fixture->host, &fixture->shell));
    nb_nixinfo_init(&fixture->nixinfo,
                    provide_snapshot,
                    &fixture->provider);
    spec = nb_nixinfo_application_spec(&fixture->nixinfo);
    fixture->application =
        nb_application_host_register(&fixture->host, &spec);
    CHECK(fixture->application != NB_APPLICATION_ID_NONE);
    CHECK(nb_application_host_start(&fixture->host,
                                    fixture->application));
}

static enum nb_application_dispatch_result dispatch_menu(
    struct fixture *fixture,
    nb_window_id window,
    nb_menu_command command)
{
    const struct nb_shell_action action = {
        NB_SHELL_ACTION_MENU_COMMAND,
        window,
        fixture->application,
        command
    };

    return nb_application_host_dispatch_shell_action(&fixture->host,
                                                       action);
}

static enum nb_application_dispatch_result dispatch_close(
    struct fixture *fixture,
    nb_window_id window)
{
    const struct nb_shell_action action = {
        NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED,
        window,
        NB_MENU_SOURCE_NONE,
        NB_MENU_COMMAND_NONE
    };

    return nb_application_host_dispatch_shell_action(&fixture->host,
                                                       action);
}

static const struct nb_menu_item_spec *menu_item_for(
    const struct nb_menu_model *model,
    nb_menu_command command)
{
    size_t menu_index;

    for (menu_index = 0; menu_index < model->menu_count; ++menu_index) {
        const struct nb_menu_spec *menu = &model->menus[menu_index];
        size_t item_index;

        for (item_index = 0; item_index < menu->item_count; ++item_index) {
            if (menu->items[item_index].command == command) {
                return &menu->items[item_index];
            }
        }
    }
    return NULL;
}

static bool menu_item_exists(const struct nb_menu_model *model,
                             nb_menu_command command)
{
    return menu_item_for(model, command) != NULL;
}

static bool menu_item_enabled_is(const struct nb_menu_model *model,
                                 nb_menu_command command,
                                 bool enabled)
{
    const struct nb_menu_item_spec *item = menu_item_for(model, command);

    return item != NULL && item->enabled == enabled;
}

static bool menu_item_label_is(const struct nb_menu_model *model,
                               nb_menu_command command,
                               const char *label)
{
    const struct nb_menu_item_spec *item = menu_item_for(model, command);

    return item != NULL && item->label != NULL &&
           strcmp(item->label, label) == 0;
}

static void test_launch_and_menu_contract(void)
{
    struct fixture fixture;
    const struct nb_menu_model *menus;
    nb_window_id window;

    init_fixture(&fixture);
    window = nb_application_host_window_at(&fixture.host,
                                            fixture.application,
                                            0);
    menus = nb_application_host_menu_model(&fixture.host,
                                            fixture.application);

    CHECK(nb_application_host_is_running(&fixture.host,
                                         fixture.application));
    CHECK(nb_application_host_window_count(&fixture.host,
                                            fixture.application) == 1);
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == 1);
    CHECK(window != NB_WINDOW_ID_NONE);
    CHECK(nb_nixinfo_owns_window(&fixture.nixinfo, window));
    CHECK(fixture.shell.active_menu_source == fixture.application);
    CHECK(fixture.shell.active_menu_window == window);
    CHECK(fixture.shell.menu.model == menus);
    CHECK(fixture.provider.calls == 1);
    CHECK(menus != NULL && menus->menu_count == NB_NIXINFO_MENU_COUNT);
    CHECK(strcmp(menus->menus[0].label, "Project") == 0);
    CHECK(strcmp(menus->menus[1].label, "View") == 0);
    CHECK(strcmp(menus->menus[2].label, "Window") == 0);
    CHECK(menu_item_exists(menus, NB_NIXINFO_COMMAND_NEW_WINDOW));
    CHECK(menu_item_enabled_is(menus,
                               NB_NIXINFO_COMMAND_REFRESH,
                               true));
    CHECK(menu_item_enabled_is(menus,
                               NB_NIXINFO_COMMAND_CLOSE_OTHER_WINDOWS,
                               false));
}

static void test_context_refresh_and_dynamic_view(void)
{
    struct fixture fixture;
    nb_window_id first;
    nb_window_id second;
    const struct nb_nixinfo_window_state *first_state;
    const struct nb_nixinfo_window_state *second_state;
    struct nb_shell_action captured_refresh;
    uint64_t first_revision;
    uint64_t second_revision;
    char first_hostname[NB_NIXINFO_SYSTEM_TEXT_CAPACITY];

    init_fixture(&fixture);
    first = nb_application_host_window_at(&fixture.host,
                                           fixture.application,
                                           0);
    CHECK(dispatch_menu(&fixture,
                        first,
                        NB_NIXINFO_COMMAND_NEW_WINDOW) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    second = nb_application_host_window_at(&fixture.host,
                                            fixture.application,
                                            1);
    CHECK(second != NB_WINDOW_ID_NONE && second != first);
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == 2);
    CHECK(fixture.shell.active_menu_window == second);
    CHECK(menu_item_enabled_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_CLOSE_OTHER_WINDOWS,
        true));

    first_state = nb_nixinfo_find_window(&fixture.nixinfo, first);
    second_state = nb_nixinfo_find_window(&fixture.nixinfo, second);
    first_revision = first_state->revision;
    second_revision = second_state->revision;
    captured_refresh = (struct nb_shell_action){
        NB_SHELL_ACTION_MENU_COMMAND,
        first,
        fixture.application,
        NB_NIXINFO_COMMAND_REFRESH
    };
    CHECK(nb_shell_activate_window(&fixture.shell, second));
    CHECK(nb_application_host_sync_focus(&fixture.host));
    CHECK(nb_application_host_dispatch_shell_action(&fixture.host,
                                                      captured_refresh) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(nb_nixinfo_find_window(&fixture.nixinfo, first)->revision ==
          first_revision + 1);
    CHECK(nb_nixinfo_find_window(&fixture.nixinfo, second)->revision ==
          second_revision);

    first_state = nb_nixinfo_find_window(&fixture.nixinfo, first);
    (void)snprintf(first_hostname,
                   sizeof(first_hostname),
                   "%s",
                   first_state->snapshot.hostname);
    fixture.provider.fail_next = true;
    CHECK(nb_shell_activate_window(&fixture.shell, first));
    CHECK(nb_application_host_sync_focus(&fixture.host));
    first_revision = first_state->revision;
    CHECK(dispatch_menu(&fixture,
                        first,
                        NB_NIXINFO_COMMAND_REFRESH) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    first_state = nb_nixinfo_find_window(&fixture.nixinfo, first);
    CHECK(first_state->revision == first_revision);
    CHECK(first_state->refresh_failed);
    CHECK(strcmp(first_state->snapshot.hostname, first_hostname) == 0);

    CHECK(dispatch_menu(&fixture,
                        first,
                        NB_NIXINFO_COMMAND_TOGGLE_VERSION) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(nb_nixinfo_find_window(&fixture.nixinfo,
                                 first)->show_full_version);
    CHECK(menu_item_enabled_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_TOGGLE_VERSION,
        true));
    CHECK(menu_item_label_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_TOGGLE_VERSION,
        "Hide Kernel Version"));
    CHECK(nb_shell_activate_window(&fixture.shell, second));
    CHECK(nb_application_host_sync_focus(&fixture.host));
    CHECK(menu_item_label_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_TOGGLE_VERSION,
        "Show Kernel Version"));
    CHECK(nb_shell_activate_window(&fixture.shell, first));
    CHECK(nb_application_host_sync_focus(&fixture.host));
    CHECK(menu_item_label_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_TOGGLE_VERSION,
        "Hide Kernel Version"));
}

static void test_about_close_and_captured_context(void)
{
    struct fixture fixture;
    nb_window_id first;
    nb_window_id second;
    nb_window_id third;
    nb_window_id about;
    size_t count;
    struct nb_shell_action close_first;
    const struct nb_menu_model *menus;

    init_fixture(&fixture);
    first = nb_application_host_window_at(&fixture.host,
                                           fixture.application,
                                           0);
    CHECK(dispatch_menu(&fixture, first, NB_NIXINFO_COMMAND_ABOUT) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    about = nb_nixinfo_about_window(&fixture.nixinfo);
    count = nb_nixinfo_window_count(&fixture.nixinfo);
    CHECK(about != NB_WINDOW_ID_NONE);
    CHECK(fixture.shell.active_menu_window == about);
    menus = nb_application_host_menu_model(&fixture.host,
                                            fixture.application);
    CHECK(menu_item_enabled_is(menus,
                               NB_NIXINFO_COMMAND_REFRESH,
                               false));
    CHECK(menu_item_enabled_is(menus,
                               NB_NIXINFO_COMMAND_TOGGLE_VERSION,
                               false));
    CHECK(dispatch_menu(&fixture, about, NB_NIXINFO_COMMAND_ABOUT) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(nb_nixinfo_about_window(&fixture.nixinfo) == about);
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == count);

    CHECK(dispatch_close(&fixture, about) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(nb_nixinfo_about_window(&fixture.nixinfo) == NB_WINDOW_ID_NONE);
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == 1);
    CHECK(fixture.shell.active_menu_window == first);

    CHECK(dispatch_menu(&fixture,
                        first,
                        NB_NIXINFO_COMMAND_NEW_WINDOW) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    second = nb_application_host_window_at(&fixture.host,
                                            fixture.application,
                                            1);
    close_first = (struct nb_shell_action){
        NB_SHELL_ACTION_MENU_COMMAND,
        first,
        fixture.application,
        NB_NIXINFO_COMMAND_CLOSE_WINDOW
    };
    CHECK(nb_shell_activate_window(&fixture.shell, second));
    CHECK(nb_application_host_sync_focus(&fixture.host));
    CHECK(nb_application_host_dispatch_shell_action(&fixture.host,
                                                      close_first) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(!nb_nixinfo_owns_window(&fixture.nixinfo, first));
    CHECK(nb_nixinfo_owns_window(&fixture.nixinfo, second));
    CHECK(nb_desktop_find_window(&fixture.shell.desktop, first) == NULL);
    CHECK(nb_desktop_find_window(&fixture.shell.desktop, second) != NULL);

    CHECK(dispatch_menu(&fixture,
                        second,
                        NB_NIXINFO_COMMAND_NEW_WINDOW) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    third = nb_application_host_window_at(&fixture.host,
                                           fixture.application,
                                           1);
    CHECK(third != NB_WINDOW_ID_NONE && third != second);
    CHECK(dispatch_menu(&fixture,
                        third,
                        NB_NIXINFO_COMMAND_CLOSE_OTHER_WINDOWS) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == 1);
    CHECK(!nb_nixinfo_owns_window(&fixture.nixinfo, second));
    CHECK(nb_nixinfo_owns_window(&fixture.nixinfo, third));

    CHECK(dispatch_close(&fixture, third) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(nb_application_host_is_running(&fixture.host,
                                         fixture.application));
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == 0);
    CHECK(fixture.shell.active_menu_source == DESKTOP_SOURCE);
    CHECK(nb_application_host_restart(&fixture.host,
                                      fixture.application));
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == 1);
    CHECK(fixture.shell.active_menu_source == fixture.application);
}

static void test_capacity_and_disabled_commands(void)
{
    struct fixture fixture;
    size_t index;
    nb_window_id active;

    init_fixture(&fixture);
    for (index = 1; index < NB_NIXINFO_MAX_WINDOWS; ++index) {
        active = nb_nixinfo_active_window(&fixture.nixinfo);
        CHECK(dispatch_menu(&fixture,
                            active,
                            NB_NIXINFO_COMMAND_NEW_WINDOW) ==
              NB_APPLICATION_DISPATCH_HANDLED);
    }
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) ==
          NB_NIXINFO_MAX_WINDOWS);
    CHECK(menu_item_enabled_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_NEW_WINDOW,
        false));
    CHECK(menu_item_enabled_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_ABOUT,
        false));
    active = nb_nixinfo_active_window(&fixture.nixinfo);
    CHECK(dispatch_menu(&fixture,
                        active,
                        NB_NIXINFO_COMMAND_NEW_WINDOW) ==
          NB_APPLICATION_DISPATCH_ERROR);
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) ==
          NB_NIXINFO_MAX_WINDOWS);

    CHECK(dispatch_close(&fixture,
                         nb_application_host_window_at(&fixture.host,
                                                       fixture.application,
                                                       0)) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(menu_item_enabled_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_NEW_WINDOW,
        true));
    CHECK(menu_item_enabled_is(
        nb_application_host_menu_model(&fixture.host,
                                       fixture.application),
        NB_NIXINFO_COMMAND_ABOUT,
        true));
}

static void test_quit_is_application_scoped_and_restartable(void)
{
    struct fixture fixture;
    nb_window_id app_window;
    nb_window_id foreign_window;

    init_fixture(&fixture);
    app_window = nb_application_host_window_at(&fixture.host,
                                                fixture.application,
                                                0);
    CHECK(dispatch_menu(&fixture,
                        app_window,
                        NB_NIXINFO_COMMAND_NEW_WINDOW) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    foreign_window = nb_shell_open_window(
        &fixture.shell,
        "Foreign",
        (struct nb_rect){700, 100, 220, 160},
        FOREIGN_SOURCE,
        &foreign_model);
    CHECK(foreign_window != NB_WINDOW_ID_NONE);
    CHECK(nb_application_host_sync_focus(&fixture.host));

    CHECK(dispatch_menu(&fixture,
                        app_window,
                        NB_NIXINFO_COMMAND_QUIT) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    CHECK(!nb_application_host_is_running(&fixture.host,
                                          fixture.application));
    CHECK(nb_application_host_window_count(&fixture.host,
                                            fixture.application) == 0);
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == 0);
    CHECK(nb_desktop_find_window(&fixture.shell.desktop,
                                 foreign_window) != NULL);
    CHECK(nb_desktop_window_count(&fixture.shell.desktop) == 1);
    CHECK(fixture.shell.active_menu_source == FOREIGN_SOURCE);

    CHECK(nb_application_host_start(&fixture.host,
                                    fixture.application));
    CHECK(nb_application_host_is_running(&fixture.host,
                                         fixture.application));
    CHECK(nb_nixinfo_window_count(&fixture.nixinfo) == 1);
    CHECK(nb_desktop_find_window(&fixture.shell.desktop,
                                 foreign_window) != NULL);
    CHECK(fixture.shell.active_menu_source == fixture.application);
}

static void test_invalid_actions_are_rejected(void)
{
    struct fixture fixture;
    nb_window_id window;
    nb_window_id about;
    struct nb_shell_action action;

    init_fixture(&fixture);
    window = nb_application_host_window_at(&fixture.host,
                                            fixture.application,
                                            0);
    action = (struct nb_shell_action){
        NB_SHELL_ACTION_MENU_COMMAND,
        window,
        UINT64_C(777),
        NB_NIXINFO_COMMAND_REFRESH
    };
    CHECK(nb_application_host_dispatch_shell_action(&fixture.host, action) ==
          NB_APPLICATION_DISPATCH_UNHANDLED);
    action.menu_source = fixture.application;
    action.menu_command = UINT32_C(9999);
    CHECK(nb_application_host_dispatch_shell_action(&fixture.host, action) ==
          NB_APPLICATION_DISPATCH_ERROR);
    action.menu_command = NB_NIXINFO_COMMAND_REFRESH;
    action.window = UINT64_C(9999);
    CHECK(nb_application_host_dispatch_shell_action(&fixture.host, action) ==
          NB_APPLICATION_DISPATCH_ERROR);

    CHECK(dispatch_menu(&fixture, window, NB_NIXINFO_COMMAND_ABOUT) ==
          NB_APPLICATION_DISPATCH_HANDLED);
    about = nb_nixinfo_about_window(&fixture.nixinfo);
    CHECK(dispatch_menu(&fixture, about, NB_NIXINFO_COMMAND_REFRESH) ==
          NB_APPLICATION_DISPATCH_ERROR);
}

int main(void)
{
    test_launch_and_menu_contract();
    test_context_refresh_and_dynamic_view();
    test_about_close_and_captured_context();
    test_capacity_and_disabled_commands();
    test_quit_is_application_scoped_and_restartable();
    test_invalid_actions_are_rejected();

    if (failures != 0) {
        fprintf(stderr, "%d nixinfo check(s) failed\n", failures);
        return 1;
    }

    puts("nixinfo application checks passed");
    return 0;
}
