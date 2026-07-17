#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "shell.h"

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
    SOURCE_A = 2,
    SOURCE_B = 3,
    SOURCE_C = 4,
    DESKTOP_COMMAND = 100,
    COMMAND_A = 200,
    COMMAND_A_UPDATED = 201,
    COMMAND_B = 300,
    COMMAND_B_UPDATED = 301
};

static const struct nb_menu_item_spec desktop_items[] = {
    {"Desktop action", DESKTOP_COMMAND, NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec desktop_menus[] = {
    {"NixBench", desktop_items, 1}
};
static const struct nb_menu_model desktop_model = {desktop_menus, 1};

static const struct nb_menu_item_spec items_a[] = {
    {"Action A", COMMAND_A, NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec menus_a[] = {{"Alpha", items_a, 1}};
static const struct nb_menu_model model_a = {menus_a, 1};

static const struct nb_menu_item_spec items_a_updated[] = {
    {"Updated A", COMMAND_A_UPDATED, NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec menus_a_updated[] = {
    {"Alpha 2", items_a_updated, 1}
};
static const struct nb_menu_model model_a_updated = {menus_a_updated, 1};

static const struct nb_menu_item_spec items_b[] = {
    {"Action B", COMMAND_B, NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec menus_b[] = {{"Beta", items_b, 1}};
static const struct nb_menu_model model_b = {menus_b, 1};

static const struct nb_menu_item_spec items_b_updated[] = {
    {"Updated B", COMMAND_B_UPDATED, NB_MENU_ITEM_COMMAND, true, false}
};
static const struct nb_menu_spec menus_b_updated[] = {
    {"Beta 2", items_b_updated, 1}
};
static const struct nb_menu_model model_b_updated = {menus_b_updated, 1};

struct fixture {
    struct nb_shell shell;
    nb_window_id a;
    nb_window_id b;
};

static struct fixture make_fixture(void)
{
    struct fixture fixture;

    nb_shell_init(&fixture.shell, DESKTOP_SOURCE, &desktop_model);
    fixture.a = nb_shell_open_window(&fixture.shell,
                                     "A",
                                     (struct nb_rect){40, 50, 300, 220},
                                     SOURCE_A,
                                     &model_a);
    fixture.b = nb_shell_open_window(&fixture.shell,
                                     "B",
                                     (struct nb_rect){140, 90, 300, 220},
                                     SOURCE_B,
                                     &model_b);
    return fixture;
}

static void test_active_application_menu(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();
    struct nb_shell_action action;
    struct nb_rect clock;

    CHECK(fixture.a != NB_WINDOW_ID_NONE);
    CHECK(fixture.b != NB_WINDOW_ID_NONE);
    CHECK(fixture.shell.active_menu_source == SOURCE_B);
    CHECK(fixture.shell.menu.model == &model_b);

    clock = nb_menu_clock_rect(viewport);
    CHECK(nb_shell_pointer_down(&fixture.shell,
                                clock.x + (clock.width / 2),
                                clock.y + (clock.height / 2),
                                viewport));
    CHECK(!nb_shell_has_pointer_interaction(&fixture.shell));
    action = nb_shell_pointer_up(&fixture.shell,
                                 clock.x + (clock.width / 2),
                                 clock.y + (clock.height / 2),
                                 viewport);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.b);

    CHECK(nb_shell_pointer_down(&fixture.shell, 80, 160, viewport));
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.a);
    CHECK(fixture.shell.active_menu_source == SOURCE_A);
    CHECK(fixture.shell.menu.model == &model_a);
    CHECK(nb_shell_pointer_up(&fixture.shell, 80, 160, viewport).type ==
          NB_SHELL_ACTION_NONE);

    CHECK(!nb_shell_pointer_down(&fixture.shell, 760, 550, viewport));
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) ==
          NB_WINDOW_ID_NONE);
    CHECK(fixture.shell.active_menu_source == DESKTOP_SOURCE);
    CHECK(fixture.shell.active_menu_window == NB_WINDOW_ID_NONE);
    CHECK(fixture.shell.menu.model == &desktop_model);
    action = nb_shell_menu_key_press(&fixture.shell, NB_MENU_KEY_TOGGLE);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    action = nb_shell_menu_key_press(&fixture.shell, NB_MENU_KEY_ACTIVATE);
    CHECK(action.type == NB_SHELL_ACTION_MENU_COMMAND);
    CHECK(action.menu_source == DESKTOP_SOURCE);
    CHECK(action.menu_command == DESKTOP_COMMAND);
    CHECK(action.window == NB_WINDOW_ID_NONE);

    CHECK(nb_shell_activate_window(&fixture.shell, fixture.b));
    CHECK(fixture.shell.menu.model == &model_b);
    CHECK(nb_shell_update_menu_source(&fixture.shell,
                                      SOURCE_B,
                                      &model_b_updated));
    CHECK(fixture.shell.menu.model == &model_b_updated);
    CHECK(!nb_shell_update_menu_source(&fixture.shell, 99, &model_a));

    {
        const struct nb_window *window =
            nb_desktop_find_window(&fixture.shell.desktop, fixture.b);
        const struct nb_rect maximize = nb_window_maximize_rect(window);

        CHECK(window != NULL);
        CHECK(nb_shell_pointer_down(&fixture.shell,
                                    maximize.x + (maximize.width / 2),
                                    maximize.y + (maximize.height / 2),
                                    viewport));
        action = nb_shell_pointer_up(&fixture.shell,
                                     maximize.x + (maximize.width / 2),
                                     maximize.y + (maximize.height / 2),
                                     viewport);
    }
    CHECK(action.type == NB_SHELL_ACTION_WINDOW_MAXIMIZE_TOGGLED);
    CHECK(action.window == fixture.b);
}

static void test_minimized_window_bar(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();
    const struct nb_window *window_b =
        nb_desktop_find_window(&fixture.shell.desktop, fixture.b);
    const struct nb_rect minimize = nb_window_minimize_rect(window_b);
    struct nb_rect button;
    struct nb_shell_action action;

    CHECK(nb_shell_pointer_down(&fixture.shell,
                                minimize.x + minimize.width / 2,
                                minimize.y + minimize.height / 2,
                                viewport));
    action = nb_shell_pointer_up(&fixture.shell,
                                 minimize.x + minimize.width / 2,
                                 minimize.y + minimize.height / 2,
                                 viewport);
    CHECK(action.type == NB_SHELL_ACTION_WINDOW_MINIMIZE_TOGGLED);
    CHECK(action.window == fixture.b);
    CHECK(nb_shell_toggle_window_minimized(&fixture.shell, fixture.b));
    CHECK(window_b->minimized);
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.a);
    CHECK(fixture.shell.menu.model == &model_a);

    button = nb_shell_minimized_window_rect(&fixture.shell,
                                            viewport,
                                            fixture.b);
    CHECK(button.width > 0);
    CHECK(button.height == 20);
    CHECK(button.x + button.width <= nb_menu_clock_rect(viewport).x);
    CHECK(nb_shell_pointer_down(&fixture.shell,
                                button.x + button.width / 2,
                                button.y + button.height / 2,
                                viewport));
    CHECK(fixture.shell.pointer_owner ==
          NB_SHELL_POINTER_MINIMIZED_WINDOW);
    action = nb_shell_pointer_up(&fixture.shell,
                                 button.x + button.width / 2,
                                 button.y + button.height / 2,
                                 viewport);
    CHECK(action.type == NB_SHELL_ACTION_WINDOW_MINIMIZE_TOGGLED);
    CHECK(action.window == fixture.b);
    CHECK(nb_shell_toggle_window_minimized(&fixture.shell, fixture.b));
    CHECK(!window_b->minimized);
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.b);
    CHECK(fixture.shell.menu.model == &model_b);
    CHECK(nb_shell_minimized_window_rect(&fixture.shell,
                                         viewport,
                                         fixture.b).width == 0);
}

static void test_menu_routing_and_actions(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();
    const struct nb_window *window_b =
        nb_desktop_find_window(&fixture.shell.desktop, fixture.b);
    const struct nb_rect close_b = nb_window_close_rect(window_b);
    const int label_x = nb_menu_label_rect(&fixture.shell.menu,
                                           viewport,
                                           0).x + 5;
    struct nb_shell_action action;
    struct nb_rect item;

    CHECK(window_b != NULL);
    CHECK(nb_shell_pointer_down(&fixture.shell, label_x, 10, viewport));
    CHECK(fixture.shell.pointer_owner == NB_SHELL_POINTER_MENU);
    CHECK(!nb_shell_pointer_move(&fixture.shell,
                                 close_b.x + 5,
                                 close_b.y + 5,
                                 viewport));
    CHECK(window_b->pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!window_b->close_pressed);
    action = nb_shell_pointer_up(&fixture.shell,
                                 close_b.x + 5,
                                 close_b.y + 5,
                                 viewport);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.b);

    CHECK(nb_shell_pointer_down(&fixture.shell, label_x, 10, viewport));
    item = nb_menu_item_rect(&fixture.shell.menu, viewport, 0);
    CHECK(nb_shell_pointer_move(&fixture.shell,
                                item.x + 10,
                                item.y + 10,
                                viewport));
    action = nb_shell_pointer_up(&fixture.shell,
                                 item.x + 10,
                                 item.y + 10,
                                 viewport);
    CHECK(action.type == NB_SHELL_ACTION_MENU_COMMAND);
    CHECK(action.menu_source == SOURCE_B);
    CHECK(action.menu_command == COMMAND_B);
    CHECK(action.window == fixture.b);

    CHECK(nb_shell_pointer_down(&fixture.shell, label_x, 10, viewport));
    action = nb_shell_pointer_up(&fixture.shell, label_x, 10, viewport);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    CHECK(nb_menu_is_open(&fixture.shell.menu));
    CHECK(nb_shell_pointer_down(&fixture.shell, 80, 160, viewport));
    action = nb_shell_pointer_up(&fixture.shell, 80, 160, viewport);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    CHECK(!nb_menu_is_open(&fixture.shell.menu));
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.b);
    CHECK(nb_shell_pointer_down(&fixture.shell, 80, 160, viewport));
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.a);
}

static void test_shared_application_menu_source(void)
{
    struct fixture fixture = make_fixture();
    struct nb_shell_action action;
    const struct nb_rect viewport = {0, 0, 800, 600};
    const nb_window_id second_a = nb_shell_open_window(
        &fixture.shell,
        "A second window",
        (struct nb_rect){260, 180, 240, 160},
        SOURCE_A,
        &model_a);

    CHECK(second_a != NB_WINDOW_ID_NONE);
    CHECK(fixture.shell.active_menu_source == SOURCE_A);
    CHECK(fixture.shell.active_menu_window == second_a);
    CHECK(fixture.shell.menu.model == &model_a);
    action = nb_shell_menu_key_press(&fixture.shell, NB_MENU_KEY_TOGGLE);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    action = nb_shell_menu_key_press(&fixture.shell, NB_MENU_KEY_ACTIVATE);
    CHECK(action.type == NB_SHELL_ACTION_MENU_COMMAND);
    CHECK(action.menu_source == SOURCE_A);
    CHECK(action.menu_command == COMMAND_A);
    CHECK(action.window == second_a);
    CHECK(nb_shell_update_menu_source(&fixture.shell,
                                      SOURCE_A,
                                      &model_a_updated));
    CHECK(fixture.shell.menu.model == &model_a_updated);
    CHECK(nb_shell_activate_window(&fixture.shell, fixture.a));
    CHECK(fixture.shell.active_menu_source == SOURCE_A);
    CHECK(fixture.shell.active_menu_window == fixture.a);
    CHECK(fixture.shell.menu.model == &model_a_updated);
    CHECK(nb_shell_pointer_down(&fixture.shell, 10, 10, viewport));
    CHECK(nb_shell_pointer_up(&fixture.shell, 10, 10,
                              viewport).type ==
          NB_SHELL_ACTION_NONE);
    CHECK(nb_menu_is_open(&fixture.shell.menu));
    CHECK(nb_shell_activate_window(&fixture.shell, second_a));
    CHECK(!nb_menu_is_open(&fixture.shell.menu));
    CHECK(fixture.shell.active_menu_window == second_a);
    CHECK(nb_shell_pointer_down(&fixture.shell, 10, 10, viewport));
    CHECK(fixture.shell.pointer_owner == NB_SHELL_POINTER_MENU);
    CHECK(nb_shell_update_menu_source(&fixture.shell,
                                      SOURCE_A,
                                      &model_a_updated));
    CHECK(!nb_shell_has_pointer_interaction(&fixture.shell));
    CHECK(!nb_menu_is_open(&fixture.shell.menu));
    CHECK(nb_shell_open_window(&fixture.shell,
                               "Conflicting source",
                               (struct nb_rect){20, 30, 200, 120},
                               SOURCE_A,
                               &model_a) == NB_WINDOW_ID_NONE);
    CHECK(nb_shell_open_window(&fixture.shell,
                               "Desktop source collision",
                               (struct nb_rect){20, 30, 200, 120},
                               DESKTOP_SOURCE,
                               &desktop_model) == NB_WINDOW_ID_NONE);
    CHECK(nb_desktop_window_count(&fixture.shell.desktop) == 3);
    CHECK(nb_shell_destroy_window(&fixture.shell, second_a));
    CHECK(fixture.shell.active_menu_source == SOURCE_A);
    CHECK(fixture.shell.active_menu_window == fixture.a);
}

static void test_per_window_menu_rebinding(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();

    CHECK(nb_shell_pointer_down(&fixture.shell, 10, 10, viewport));
    CHECK(nb_menu_is_open(&fixture.shell.menu));
    CHECK(nb_shell_update_window_menu(&fixture.shell,
                                      fixture.b,
                                      SOURCE_C,
                                      &model_b_updated));
    CHECK(!nb_menu_is_open(&fixture.shell.menu));
    CHECK(!nb_shell_has_pointer_interaction(&fixture.shell));
    CHECK(fixture.shell.active_menu_source == SOURCE_C);
    CHECK(fixture.shell.active_menu_window == fixture.b);
    CHECK(fixture.shell.menu.model == &model_b_updated);

    CHECK(nb_shell_activate_window(&fixture.shell, fixture.a));
    CHECK(fixture.shell.active_menu_source == SOURCE_A);
    CHECK(fixture.shell.menu.model == &model_a);
    CHECK(!nb_shell_update_window_menu(&fixture.shell,
                                       fixture.b,
                                       SOURCE_A,
                                       &model_b));
    CHECK(!nb_shell_update_window_menu(&fixture.shell,
                                       NB_WINDOW_ID_NONE,
                                       SOURCE_C,
                                       &model_b));
    CHECK(!nb_shell_update_window_menu(&fixture.shell,
                                       fixture.b,
                                       NB_MENU_SOURCE_NONE,
                                       &model_b));
    CHECK(!nb_shell_update_window_menu(&fixture.shell,
                                       fixture.b,
                                       SOURCE_C,
                                       NULL));
    CHECK(nb_shell_activate_window(&fixture.shell, fixture.b));
    CHECK(fixture.shell.active_menu_source == SOURCE_C);
    CHECK(fixture.shell.menu.model == &model_b_updated);
}

static void test_per_window_menu_label(void)
{
    struct fixture fixture = make_fixture();
    const struct nb_menu_model *visible;
    struct nb_shell_action action;

    CHECK(nb_shell_set_window_menu_label(&fixture.shell,
                                         fixture.b,
                                         "xclock"));
    visible = fixture.shell.menu.model;
    CHECK(visible == &fixture.shell.composed_menu_model);
    CHECK(visible->menu_count == model_b.menu_count);
    CHECK(strcmp(visible->menus[0].label, "xclock") == 0);
    CHECK(visible->menus[0].items == model_b.menus[0].items);
    CHECK(fixture.shell.active_menu_source == SOURCE_B);

    action = nb_shell_menu_key_press(&fixture.shell, NB_MENU_KEY_TOGGLE);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    action = nb_shell_menu_key_press(&fixture.shell, NB_MENU_KEY_ACTIVATE);
    CHECK(action.type == NB_SHELL_ACTION_MENU_COMMAND);
    CHECK(action.menu_source == SOURCE_B);
    CHECK(action.menu_command == COMMAND_B);
    CHECK(action.window == fixture.b);

    CHECK(nb_shell_set_window_menu_label(&fixture.shell,
                                         fixture.a,
                                         "xeyes"));
    CHECK(nb_shell_activate_window(&fixture.shell, fixture.a));
    visible = fixture.shell.menu.model;
    CHECK(visible == &fixture.shell.composed_menu_model);
    CHECK(strcmp(visible->menus[0].label, "xeyes") == 0);
    CHECK(visible->menus[0].items == model_a.menus[0].items);
    CHECK(fixture.shell.active_menu_source == SOURCE_A);

    CHECK(nb_shell_update_menu_source(&fixture.shell,
                                      SOURCE_A,
                                      &model_a_updated));
    visible = fixture.shell.menu.model;
    CHECK(strcmp(visible->menus[0].label, "xeyes") == 0);
    CHECK(visible->menus[0].items == model_a_updated.menus[0].items);

    CHECK(nb_shell_set_window_menu_label(&fixture.shell,
                                         fixture.a,
                                         NULL));
    CHECK(fixture.shell.menu.model == &model_a_updated);
    CHECK(!nb_shell_set_window_menu_label(&fixture.shell,
                                          NB_WINDOW_ID_NONE,
                                          "invalid"));
}

static void test_window_capture_crosses_bar(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();
    const struct nb_window *window_a =
        nb_desktop_find_window(&fixture.shell.desktop, fixture.a);
    struct nb_shell_action action;

    CHECK(window_a != NULL);
    CHECK(nb_shell_pointer_down(&fixture.shell, 100, 60, viewport));
    CHECK(fixture.shell.pointer_owner == NB_SHELL_POINTER_WINDOW);
    CHECK(nb_shell_pointer_move(&fixture.shell, 160, 0, viewport));
    CHECK(window_a->frame.x == 100);
    CHECK(window_a->frame.y == NB_MENU_BAR_HEIGHT);
    CHECK(!nb_menu_is_open(&fixture.shell.menu));
    action = nb_shell_pointer_up(&fixture.shell, 160, 0, viewport);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    CHECK(!nb_shell_has_pointer_interaction(&fixture.shell));
}

static void test_pointer_target_query(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();
    struct nb_shell_pointer_target target;
    const nb_window_id active_before =
        nb_desktop_active_window_id(&fixture.shell.desktop);
    const enum nb_shell_pointer_owner owner_before =
        fixture.shell.pointer_owner;

    target = nb_shell_pointer_target_at(&fixture.shell,
                                        180,
                                        150,
                                        viewport);
    CHECK(target.window == fixture.b);
    CHECK(target.hit == NB_WINDOW_HIT_CONTENT);

    target = nb_shell_pointer_target_at(&fixture.shell,
                                        200,
                                        100,
                                        viewport);
    CHECK(target.window == fixture.b);
    CHECK(target.hit == NB_WINDOW_HIT_TITLE);

    target = nb_shell_pointer_target_at(&fixture.shell,
                                        760,
                                        550,
                                        viewport);
    CHECK(target.window == NB_WINDOW_ID_NONE);
    CHECK(target.hit == NB_WINDOW_HIT_NONE);

    target = nb_shell_pointer_target_at(&fixture.shell,
                                        10,
                                        10,
                                        viewport);
    CHECK(target.window == NB_WINDOW_ID_NONE);
    CHECK(target.hit == NB_WINDOW_HIT_NONE);

    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) ==
          active_before);
    CHECK(fixture.shell.pointer_owner == owner_before);
    CHECK(!nb_menu_is_open(&fixture.shell.menu));

    CHECK(nb_shell_menu_key_press(&fixture.shell,
                                  NB_MENU_KEY_TOGGLE).type ==
          NB_SHELL_ACTION_NONE);
    CHECK(nb_menu_is_open(&fixture.shell.menu));
    target = nb_shell_pointer_target_at(&fixture.shell,
                                        180,
                                        150,
                                        viewport);
    CHECK(target.window == NB_WINDOW_ID_NONE);
    CHECK(target.hit == NB_WINDOW_HIT_NONE);
    CHECK(nb_menu_is_open(&fixture.shell.menu));
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) ==
          active_before);
}

static void test_close_and_keyboard_actions(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();
    const struct nb_window *window_b =
        nb_desktop_find_window(&fixture.shell.desktop, fixture.b);
    const struct nb_rect close_b = nb_window_close_rect(window_b);
    struct nb_shell_action action;

    CHECK(nb_shell_pointer_down(&fixture.shell,
                                close_b.x + 5,
                                close_b.y + 5,
                                viewport));
    action = nb_shell_pointer_up(&fixture.shell,
                                 close_b.x + 5,
                                 close_b.y + 5,
                                 viewport);
    CHECK(action.type == NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED);
    CHECK(action.window == fixture.b);
    CHECK(action.menu_command == NB_MENU_COMMAND_NONE);
    CHECK(nb_shell_destroy_window(&fixture.shell, action.window));
    CHECK(fixture.shell.active_menu_source == SOURCE_A);

    action = nb_shell_menu_key_press(&fixture.shell, NB_MENU_KEY_TOGGLE);
    CHECK(action.type == NB_SHELL_ACTION_NONE);
    CHECK(nb_menu_is_open(&fixture.shell.menu));
    action = nb_shell_menu_key_press(&fixture.shell, NB_MENU_KEY_ACTIVATE);
    CHECK(action.type == NB_SHELL_ACTION_MENU_COMMAND);
    CHECK(action.menu_source == SOURCE_A);
    CHECK(action.menu_command == COMMAND_A);
    CHECK(action.window == fixture.a);
    CHECK(!nb_menu_is_open(&fixture.shell.menu));
}

static void test_cancel_and_clamp(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();
    const struct nb_window *window_a =
        nb_desktop_find_window(&fixture.shell.desktop, fixture.a);

    CHECK(nb_shell_activate_window(&fixture.shell, fixture.a));
    CHECK(nb_shell_pointer_down(&fixture.shell, 100, 60, viewport));
    CHECK(nb_shell_has_pointer_interaction(&fixture.shell));
    nb_shell_pointer_cancel(&fixture.shell);
    CHECK(!nb_shell_has_pointer_interaction(&fixture.shell));
    CHECK(!nb_menu_is_open(&fixture.shell.menu));
    CHECK(window_a->pointer_mode == NB_WINDOW_POINTER_IDLE);

    CHECK(nb_shell_pointer_down(&fixture.shell, 100, 60, viewport));
    CHECK(nb_shell_has_pointer_interaction(&fixture.shell));
    CHECK(nb_shell_activate_window(&fixture.shell, fixture.b));
    CHECK(!nb_shell_has_pointer_interaction(&fixture.shell));
    CHECK(window_a->pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.b);

    ((struct nb_window *)window_a)->frame.y = 0;
    CHECK(nb_shell_clamp_windows(&fixture.shell, viewport));
    CHECK(window_a->frame.y == NB_MENU_BAR_HEIGHT);
}

static void test_fullscreen_routes_through_menu_bar(void)
{
    const struct nb_rect viewport = {0, 0, 800, 600};
    struct fixture fixture = make_fixture();
    struct nb_shell_pointer_target target;
    const struct nb_window *window_b;

    CHECK(nb_desktop_set_window_fullscreen(&fixture.shell.desktop,
                                           fixture.b,
                                           true,
                                           viewport));
    window_b = nb_desktop_find_window(&fixture.shell.desktop, fixture.b);
    CHECK(window_b != NULL && window_b->fullscreen);
    target = nb_shell_pointer_target_at(&fixture.shell, 10, 10, viewport);
    CHECK(target.window == fixture.b);
    CHECK(target.hit == NB_WINDOW_HIT_CONTENT);
    CHECK(nb_shell_pointer_down(&fixture.shell, 10, 10, viewport));
    CHECK(!nb_menu_is_open(&fixture.shell.menu));
    CHECK(nb_desktop_active_window_id(&fixture.shell.desktop) == fixture.b);
}

int main(void)
{
    test_active_application_menu();
    test_minimized_window_bar();
    test_menu_routing_and_actions();
    test_shared_application_menu_source();
    test_per_window_menu_rebinding();
    test_per_window_menu_label();
    test_window_capture_crosses_bar();
    test_pointer_target_query();
    test_close_and_keyboard_actions();
    test_cancel_and_clamp();
    test_fullscreen_routes_through_menu_bar();

    if (failures != 0) {
        fprintf(stderr, "%d shell model check(s) failed\n", failures);
        return 1;
    }

    puts("shell model checks passed");
    return 0;
}
