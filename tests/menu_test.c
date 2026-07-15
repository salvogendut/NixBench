#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "menu.h"

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
    COMMAND_OPEN = 11,
    COMMAND_DISABLED = 12,
    COMMAND_QUIT = 13,
    COMMAND_CLOSE = 21
};

static const struct nb_menu_item_spec project_items[] = {
    {"Open", COMMAND_OPEN, NB_MENU_ITEM_COMMAND, true, true},
    {NULL, NB_MENU_COMMAND_NONE, NB_MENU_ITEM_SEPARATOR, false, false},
    {"Disabled", COMMAND_DISABLED, NB_MENU_ITEM_COMMAND, false, false},
    {"Quit", COMMAND_QUIT, NB_MENU_ITEM_COMMAND, true, false}
};

static const struct nb_menu_item_spec window_items[] = {
    {"Close", COMMAND_CLOSE, NB_MENU_ITEM_COMMAND, true, false}
};

static const struct nb_menu_spec menus[] = {
    {"Project", project_items,
     sizeof(project_items) / sizeof(project_items[0])},
    {"Window", window_items,
     sizeof(window_items) / sizeof(window_items[0])}
};

static const struct nb_menu_model model = {
    menus,
    sizeof(menus) / sizeof(menus[0])
};

static struct nb_menu make_menu(void)
{
    struct nb_menu menu;

    nb_menu_init(&menu);
    nb_menu_set_model(&menu, &model);
    return menu;
}

static void test_geometry_and_hit_testing(void)
{
    const struct nb_rect viewport = {10, 20, 400, 220};
    struct nb_menu menu = make_menu();
    struct nb_rect rect;
    struct nb_menu_hit hit;

    CHECK(nb_menu_item_is_actionable(&project_items[0]));
    CHECK(!nb_menu_item_is_actionable(&project_items[1]));
    CHECK(!nb_menu_item_is_actionable(&project_items[2]));
    CHECK(nb_menu_item_is_actionable(&project_items[3]));

    rect = nb_menu_bar_rect(viewport);
    CHECK(rect.x == 10 && rect.y == 20);
    CHECK(rect.width == 400 && rect.height == 24);
    rect = nb_menu_work_area(viewport);
    CHECK(rect.x == 10 && rect.y == 44);
    CHECK(rect.width == 400 && rect.height == 196);
    rect = nb_menu_clock_rect(viewport);
    CHECK(rect.x == 342 && rect.y == 22);
    CHECK(rect.width == 64 && rect.height == 20);

    rect = nb_menu_label_rect(&menu, viewport, 0);
    CHECK(rect.x == 14 && rect.y == 22);
    CHECK(rect.width == 72 && rect.height == 20);
    rect = nb_menu_label_rect(&menu, viewport, 1);
    CHECK(rect.x == 86 && rect.y == 22);
    CHECK(rect.width == 64 && rect.height == 20);
    CHECK(nb_menu_label_rect(&menu, viewport, 2).width == 0);

    hit = nb_menu_hit_test(&menu, 14, 22, viewport);
    CHECK(hit.kind == NB_MENU_HIT_LABEL && hit.menu_index == 0);
    hit = nb_menu_hit_test(&menu, 85, 41, viewport);
    CHECK(hit.kind == NB_MENU_HIT_LABEL && hit.menu_index == 0);
    hit = nb_menu_hit_test(&menu, 86, 22, viewport);
    CHECK(hit.kind == NB_MENU_HIT_LABEL && hit.menu_index == 1);
    CHECK(nb_menu_hit_test(&menu, 150, 30, viewport).kind ==
          NB_MENU_HIT_BAR);
    CHECK(nb_menu_hit_test(&menu, 410, 30, viewport).kind ==
          NB_MENU_HIT_NONE);
    CHECK(nb_menu_hit_test(&menu, 20, 44, viewport).kind ==
          NB_MENU_HIT_NONE);

    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_TOGGLE) ==
          NB_MENU_COMMAND_NONE);
    rect = nb_menu_panel_rect(&menu, viewport);
    CHECK(rect.x == 14 && rect.y == 44);
    CHECK(rect.width == 144 && rect.height == 82);
    rect = nb_menu_item_rect(&menu, viewport, 0);
    CHECK(rect.x == 16 && rect.y == 46);
    CHECK(rect.width == 140 && rect.height == 24);
    rect = nb_menu_item_rect(&menu, viewport, 1);
    CHECK(rect.x == 16 && rect.y == 70);
    CHECK(rect.width == 140 && rect.height == 6);
    rect = nb_menu_item_rect(&menu, viewport, 2);
    CHECK(rect.y == 76 && rect.height == 24);
    rect = nb_menu_item_rect(&menu, viewport, 3);
    CHECK(rect.y == 100 && rect.height == 24);

    hit = nb_menu_hit_test(&menu, 16, 46, viewport);
    CHECK(hit.kind == NB_MENU_HIT_ITEM && hit.item_index == 0);
    hit = nb_menu_hit_test(&menu, 155, 69, viewport);
    CHECK(hit.kind == NB_MENU_HIT_ITEM && hit.item_index == 0);
    hit = nb_menu_hit_test(&menu, 156, 69, viewport);
    CHECK(hit.kind == NB_MENU_HIT_PANEL);
    hit = nb_menu_hit_test(&menu, 16, 70, viewport);
    CHECK(hit.kind == NB_MENU_HIT_ITEM && hit.item_index == 1);
    hit = nb_menu_hit_test(&menu, 20, 125, viewport);
    CHECK(hit.kind == NB_MENU_HIT_PANEL);
    CHECK(nb_menu_hit_test(&menu, 20, 126, viewport).kind ==
          NB_MENU_HIT_NONE);
}

static void test_pointer_interaction(void)
{
    const struct nb_rect viewport = {10, 20, 400, 220};
    struct nb_menu menu = make_menu();

    CHECK(!nb_menu_is_open(&menu));
    CHECK(!nb_menu_is_tracking(&menu));
    CHECK(!nb_menu_pointer_down(&menu, 200, 180, viewport));
    CHECK(nb_menu_pointer_down(&menu, 20, 30, viewport));
    CHECK(nb_menu_is_tracking(&menu));
    CHECK(nb_menu_pointer_move(&menu, 30, 55, viewport));
    CHECK(menu.hot_item == 0);
    CHECK(nb_menu_pointer_up(&menu, 30, 55, viewport) == COMMAND_OPEN);
    CHECK(!nb_menu_is_open(&menu));

    CHECK(nb_menu_pointer_down(&menu, 20, 30, viewport));
    CHECK(nb_menu_pointer_up(&menu, 20, 30, viewport) ==
          NB_MENU_COMMAND_NONE);
    CHECK(nb_menu_is_open(&menu));
    CHECK(!nb_menu_is_tracking(&menu));
    CHECK(nb_menu_pointer_move(&menu, 30, 110, viewport));
    CHECK(menu.hot_item == 3);
    CHECK(nb_menu_pointer_down(&menu, 30, 110, viewport));
    CHECK(nb_menu_pointer_up(&menu, 30, 110, viewport) == COMMAND_QUIT);
    CHECK(!nb_menu_is_open(&menu));

    CHECK(nb_menu_pointer_down(&menu, 20, 30, viewport));
    CHECK(nb_menu_pointer_up(&menu, 20, 30, viewport) ==
          NB_MENU_COMMAND_NONE);
    CHECK(!nb_menu_pointer_move(&menu, 30, 80, viewport));
    CHECK(menu.hot_item == SIZE_MAX);
    CHECK(nb_menu_pointer_down(&menu, 30, 80, viewport));
    CHECK(nb_menu_pointer_up(&menu, 30, 80, viewport) ==
          NB_MENU_COMMAND_NONE);
    CHECK(!nb_menu_is_open(&menu));

    CHECK(nb_menu_pointer_down(&menu, 20, 30, viewport));
    CHECK(nb_menu_pointer_move(&menu, 100, 30, viewport));
    CHECK(menu.open_menu == 1);
    CHECK(nb_menu_pointer_move(&menu, 100, 55, viewport));
    CHECK(nb_menu_pointer_up(&menu, 100, 55, viewport) == COMMAND_CLOSE);

    CHECK(nb_menu_pointer_down(&menu, 20, 30, viewport));
    CHECK(nb_menu_pointer_up(&menu, 20, 30, viewport) ==
          NB_MENU_COMMAND_NONE);
    CHECK(nb_menu_pointer_down(&menu, 300, 180, viewport));
    CHECK(nb_menu_is_tracking(&menu));
    CHECK(nb_menu_pointer_down(&menu, 30, 55, viewport));
    CHECK(nb_menu_pointer_up(&menu, 300, 180, viewport) ==
          NB_MENU_COMMAND_NONE);
    CHECK(!nb_menu_is_open(&menu));

    CHECK(nb_menu_pointer_down(&menu, 20, 30, viewport));
    CHECK(nb_menu_pointer_up(&menu, 20, 30, viewport) ==
          NB_MENU_COMMAND_NONE);
    CHECK(nb_menu_pointer_down(&menu, 20, 30, viewport));
    CHECK(!nb_menu_is_open(&menu));
}

static void test_keyboard_and_model_switching(void)
{
    struct nb_menu menu = make_menu();

    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_TOGGLE) ==
          NB_MENU_COMMAND_NONE);
    CHECK(menu.open_menu == 0 && menu.hot_item == 0);
    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_NEXT_ITEM) ==
          NB_MENU_COMMAND_NONE);
    CHECK(menu.hot_item == 3);
    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_NEXT_ITEM) ==
          NB_MENU_COMMAND_NONE);
    CHECK(menu.hot_item == 0);
    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_PREVIOUS_ITEM) ==
          NB_MENU_COMMAND_NONE);
    CHECK(menu.hot_item == 3);
    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_NEXT_MENU) ==
          NB_MENU_COMMAND_NONE);
    CHECK(menu.open_menu == 1 && menu.hot_item == 0);
    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_PREVIOUS_MENU) ==
          NB_MENU_COMMAND_NONE);
    CHECK(menu.open_menu == 0 && menu.hot_item == 0);
    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_ACTIVATE) == COMMAND_OPEN);
    CHECK(!nb_menu_is_open(&menu));

    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_TOGGLE) ==
          NB_MENU_COMMAND_NONE);
    nb_menu_set_model(&menu, NULL);
    CHECK(!nb_menu_is_open(&menu));
    CHECK(menu.model == NULL);
    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_TOGGLE) ==
          NB_MENU_COMMAND_NONE);
    CHECK(!nb_menu_is_open(&menu));
}

static void test_tiny_viewport(void)
{
    const struct nb_rect viewport = {5, 7, 60, 20};
    struct nb_menu menu = make_menu();
    const struct nb_rect bar = nb_menu_bar_rect(viewport);
    const struct nb_rect work = nb_menu_work_area(viewport);
    const struct nb_rect clock = nb_menu_clock_rect(viewport);
    const struct nb_rect label = nb_menu_label_rect(&menu, viewport, 0);

    CHECK(bar.x == 5 && bar.y == 7 && bar.width == 60 && bar.height == 20);
    CHECK(work.x == 5 && work.y == 27 && work.width == 60 && work.height == 0);
    CHECK(clock.x == 9 && clock.y == 9);
    CHECK(clock.width == 52 && clock.height == 16);
    CHECK(label.width == 0);
    CHECK(nb_menu_key_press(&menu, NB_MENU_KEY_TOGGLE) ==
          NB_MENU_COMMAND_NONE);
    CHECK(nb_menu_panel_rect(&menu, viewport).height == 0);
    CHECK(nb_menu_item_rect(&menu, viewport, 0).height == 0);

    {
        const struct nb_rect narrow_viewport = {0, 0, 100, 100};
        const struct nb_rect narrow_label =
            nb_menu_label_rect(&menu, narrow_viewport, 0);
        const struct nb_rect narrow_clock =
            nb_menu_clock_rect(narrow_viewport);

        CHECK(narrow_label.x + narrow_label.width <= narrow_clock.x);
        CHECK(nb_menu_hit_test(&menu, 40, 10, narrow_viewport).kind ==
              NB_MENU_HIT_BAR);
    }
}

int main(void)
{
    test_geometry_and_hit_testing();
    test_pointer_interaction();
    test_keyboard_and_model_switching();
    test_tiny_viewport();

    if (failures != 0) {
        fprintf(stderr, "%d menu model check(s) failed\n", failures);
        return 1;
    }

    puts("menu model checks passed");
    return 0;
}
