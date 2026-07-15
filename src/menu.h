#ifndef NIXBENCH_MENU_H
#define NIXBENCH_MENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "window.h"

enum {
    NB_MENU_BAR_HEIGHT = 24,
    NB_MENU_CLOCK_AREA_WIDTH = 72,
    NB_MENU_MAX_MENUS = 8,
    NB_MENU_MAX_ITEMS = 16,
    NB_MENU_TEXT_CAPACITY = 32,
    NB_MENU_ITEM_GUTTER_WIDTH = 18
};

typedef uint32_t nb_menu_command;

#define NB_MENU_COMMAND_NONE UINT32_C(0)

enum nb_menu_item_kind {
    NB_MENU_ITEM_COMMAND,
    NB_MENU_ITEM_SEPARATOR
};

struct nb_menu_item_spec {
    const char *label;
    nb_menu_command command;
    enum nb_menu_item_kind kind;
    bool enabled;
    /* Command state; validated separator descriptors require false. */
    bool checked;
};

struct nb_menu_spec {
    const char *label;
    const struct nb_menu_item_spec *items;
    size_t item_count;
};

/*
 * Descriptor strings and arrays are borrowed and must outlive their use.
 * Counts beyond the fixed maxima are ignored defensively.
 */
struct nb_menu_model {
    const struct nb_menu_spec *menus;
    size_t menu_count;
};

bool nb_menu_item_is_actionable(const struct nb_menu_item_spec *item);

enum nb_menu_phase {
    NB_MENU_CLOSED,
    NB_MENU_OPEN,
    NB_MENU_TRACKING
};

enum nb_menu_key {
    NB_MENU_KEY_TOGGLE,
    NB_MENU_KEY_NEXT_ITEM,
    NB_MENU_KEY_PREVIOUS_ITEM,
    NB_MENU_KEY_NEXT_MENU,
    NB_MENU_KEY_PREVIOUS_MENU,
    NB_MENU_KEY_ACTIVATE,
    NB_MENU_KEY_DISMISS
};

enum nb_menu_hit_kind {
    NB_MENU_HIT_NONE,
    NB_MENU_HIT_BAR,
    NB_MENU_HIT_LABEL,
    NB_MENU_HIT_PANEL,
    NB_MENU_HIT_ITEM
};

struct nb_menu_hit {
    enum nb_menu_hit_kind kind;
    size_t menu_index;
    size_t item_index;
};

struct nb_menu {
    const struct nb_menu_model *model;
    enum nb_menu_phase phase;
    size_t open_menu;
    size_t hot_item;
};

void nb_menu_init(struct nb_menu *menu);
void nb_menu_set_model(struct nb_menu *menu,
                       const struct nb_menu_model *model);
void nb_menu_cancel(struct nb_menu *menu);

struct nb_rect nb_menu_bar_rect(struct nb_rect viewport);
struct nb_rect nb_menu_work_area(struct nb_rect viewport);
struct nb_rect nb_menu_clock_rect(struct nb_rect viewport);
struct nb_rect nb_menu_label_rect(const struct nb_menu *menu,
                                  struct nb_rect viewport,
                                  size_t menu_index);
struct nb_rect nb_menu_panel_rect(const struct nb_menu *menu,
                                  struct nb_rect viewport);
struct nb_rect nb_menu_item_rect(const struct nb_menu *menu,
                                 struct nb_rect viewport,
                                 size_t item_index);

struct nb_menu_hit nb_menu_hit_test(const struct nb_menu *menu,
                                    int x,
                                    int y,
                                    struct nb_rect viewport);
bool nb_menu_pointer_down(struct nb_menu *menu,
                          int x,
                          int y,
                          struct nb_rect viewport);
bool nb_menu_pointer_move(struct nb_menu *menu,
                          int x,
                          int y,
                          struct nb_rect viewport);
nb_menu_command nb_menu_pointer_up(struct nb_menu *menu,
                                   int x,
                                   int y,
                                   struct nb_rect viewport);
nb_menu_command nb_menu_key_press(struct nb_menu *menu,
                                  enum nb_menu_key key);

bool nb_menu_is_open(const struct nb_menu *menu);
bool nb_menu_is_tracking(const struct nb_menu *menu);

#endif
