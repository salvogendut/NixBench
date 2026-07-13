#include "menu.h"

#include <stdint.h>

enum {
    MENU_LABEL_INSET_X = 4,
    MENU_LABEL_PADDING_X = 8,
    MENU_LABEL_INSET_Y = 2,
    MENU_PANEL_BORDER = 2,
    MENU_PANEL_MIN_WIDTH = 144,
    MENU_ITEM_PADDING_X = 12,
    MENU_ITEM_HEIGHT = 24,
    MENU_SEPARATOR_HEIGHT = 6,
    MENU_GLYPH_WIDTH = 8
};

static const size_t no_index = SIZE_MAX;

static int maximum(int left, int right)
{
    return left > right ? left : right;
}

static int minimum(int left, int right)
{
    return left < right ? left : right;
}

static size_t bounded_count(size_t count, size_t maximum_count)
{
    return count < maximum_count ? count : maximum_count;
}

static size_t bounded_text_length(const char *text)
{
    size_t length = 0;

    if (text == NULL) {
        return 0;
    }
    while (length + 1 < NB_MENU_TEXT_CAPACITY && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool rect_contains(struct nb_rect rect, int x, int y)
{
    return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
           (int64_t)x < (int64_t)rect.x + rect.width &&
           (int64_t)y < (int64_t)rect.y + rect.height;
}

static struct nb_rect clip_rect(struct nb_rect rect, struct nb_rect clip)
{
    const int64_t rect_right = (int64_t)rect.x + maximum(0, rect.width);
    const int64_t rect_bottom = (int64_t)rect.y + maximum(0, rect.height);
    const int64_t clip_right = (int64_t)clip.x + maximum(0, clip.width);
    const int64_t clip_bottom = (int64_t)clip.y + maximum(0, clip.height);
    const int left = maximum(rect.x, clip.x);
    const int top = maximum(rect.y, clip.y);
    const int64_t right = rect_right < clip_right ? rect_right : clip_right;
    const int64_t bottom = rect_bottom < clip_bottom ? rect_bottom : clip_bottom;
    struct nb_rect result = {
        left,
        top,
        right > left ? (int)(right - left) : 0,
        bottom > top ? (int)(bottom - top) : 0
    };

    return result;
}

static size_t menu_count(const struct nb_menu *menu)
{
    if (menu->model == NULL || menu->model->menus == NULL) {
        return 0;
    }
    return bounded_count(menu->model->menu_count, NB_MENU_MAX_MENUS);
}

static const struct nb_menu_spec *menu_spec(const struct nb_menu *menu,
                                            size_t menu_index)
{
    return menu_index < menu_count(menu) ?
           &menu->model->menus[menu_index] : NULL;
}

static size_t item_count(const struct nb_menu_spec *spec)
{
    if (spec == NULL || spec->items == NULL) {
        return 0;
    }
    return bounded_count(spec->item_count, NB_MENU_MAX_ITEMS);
}

bool nb_menu_item_is_actionable(const struct nb_menu_item_spec *item)
{
    return item != NULL && item->kind == NB_MENU_ITEM_COMMAND &&
           item->enabled && item->command != NB_MENU_COMMAND_NONE &&
           item->label != NULL && item->label[0] != '\0';
}

static int label_natural_width(const struct nb_menu_spec *spec)
{
    const size_t text_length = spec == NULL ? 0 :
                               bounded_text_length(spec->label);

    return text_length == 0 ? 0 :
           (int)(text_length * MENU_GLYPH_WIDTH) +
               (2 * MENU_LABEL_PADDING_X);
}

static int item_natural_height(const struct nb_menu_item_spec *item)
{
    return item != NULL && item->kind == NB_MENU_ITEM_SEPARATOR ?
           MENU_SEPARATOR_HEIGHT : MENU_ITEM_HEIGHT;
}

static int panel_natural_width(const struct nb_menu_spec *spec)
{
    size_t index;
    int width = MENU_PANEL_MIN_WIDTH;

    for (index = 0; index < item_count(spec); ++index) {
        const struct nb_menu_item_spec *item = &spec->items[index];
        const int item_width =
            (int)(bounded_text_length(item->label) * MENU_GLYPH_WIDTH) +
            (2 * MENU_ITEM_PADDING_X) + (2 * MENU_PANEL_BORDER);

        if (item_width > width) {
            width = item_width;
        }
    }
    return width;
}

static int panel_natural_height(const struct nb_menu_spec *spec)
{
    size_t index;
    int height = 2 * MENU_PANEL_BORDER;

    for (index = 0; index < item_count(spec); ++index) {
        height += item_natural_height(&spec->items[index]);
    }
    return height;
}

static size_t first_enabled_item(const struct nb_menu_spec *spec)
{
    size_t index;

    for (index = 0; index < item_count(spec); ++index) {
        if (nb_menu_item_is_actionable(&spec->items[index])) {
            return index;
        }
    }
    return no_index;
}

static size_t next_enabled_item(const struct nb_menu_spec *spec,
                                size_t current,
                                bool forward)
{
    const size_t count = item_count(spec);
    size_t offset;

    if (count == 0) {
        return no_index;
    }

    for (offset = 1; offset <= count; ++offset) {
        size_t candidate;

        if (current == no_index) {
            candidate = forward ? offset - 1 : count - offset;
        } else if (forward) {
            candidate = (current + offset) % count;
        } else {
            candidate = (current + count - (offset % count)) % count;
        }

        if (nb_menu_item_is_actionable(&spec->items[candidate])) {
            return candidate;
        }
    }
    return no_index;
}

static void open_menu_index(struct nb_menu *menu,
                            size_t menu_index,
                            enum nb_menu_phase phase,
                            bool select_first)
{
    const struct nb_menu_spec *spec = menu_spec(menu, menu_index);

    if (spec == NULL) {
        nb_menu_cancel(menu);
        return;
    }
    menu->phase = phase;
    menu->open_menu = menu_index;
    menu->hot_item = select_first ? first_enabled_item(spec) : no_index;
}

void nb_menu_init(struct nb_menu *menu)
{
    menu->model = NULL;
    menu->phase = NB_MENU_CLOSED;
    menu->open_menu = no_index;
    menu->hot_item = no_index;
}

void nb_menu_set_model(struct nb_menu *menu,
                       const struct nb_menu_model *model)
{
    nb_menu_cancel(menu);
    menu->model = model;
}

void nb_menu_cancel(struct nb_menu *menu)
{
    menu->phase = NB_MENU_CLOSED;
    menu->open_menu = no_index;
    menu->hot_item = no_index;
}

struct nb_rect nb_menu_bar_rect(struct nb_rect viewport)
{
    struct nb_rect bar = {
        viewport.x,
        viewport.y,
        maximum(0, viewport.width),
        minimum(NB_MENU_BAR_HEIGHT, maximum(0, viewport.height))
    };

    return bar;
}

struct nb_rect nb_menu_work_area(struct nb_rect viewport)
{
    const struct nb_rect bar = nb_menu_bar_rect(viewport);
    struct nb_rect work_area = {
        viewport.x,
        bar.y + bar.height,
        maximum(0, viewport.width),
        maximum(0, viewport.height - bar.height)
    };

    return work_area;
}

struct nb_rect nb_menu_clock_rect(struct nb_rect viewport)
{
    const struct nb_rect bar = nb_menu_bar_rect(viewport);
    const int area_width = minimum(NB_MENU_CLOCK_AREA_WIDTH, bar.width);
    struct nb_rect clock = {
        bar.x + bar.width - area_width + MENU_LABEL_INSET_X,
        bar.y + MENU_LABEL_INSET_Y,
        maximum(0, area_width - (2 * MENU_LABEL_INSET_X)),
        maximum(0, bar.height - (2 * MENU_LABEL_INSET_Y))
    };

    return clip_rect(clock, bar);
}

struct nb_rect nb_menu_label_rect(const struct nb_menu *menu,
                                  struct nb_rect viewport,
                                  size_t menu_index)
{
    const struct nb_rect bar = nb_menu_bar_rect(viewport);
    const struct nb_menu_spec *spec = menu_spec(menu, menu_index);
    struct nb_rect menu_area = bar;
    size_t index;
    int x = bar.x + MENU_LABEL_INSET_X;
    struct nb_rect label;

    if (spec == NULL) {
        return (struct nb_rect){bar.x, bar.y, 0, 0};
    }

    for (index = 0; index < menu_index && index < menu_count(menu); ++index) {
        x += label_natural_width(menu_spec(menu, index));
    }

    menu_area.width = maximum(0, menu_area.width - NB_MENU_CLOCK_AREA_WIDTH);
    label.x = x;
    label.y = bar.y + MENU_LABEL_INSET_Y;
    label.width = label_natural_width(spec);
    label.height = maximum(0, bar.height - (2 * MENU_LABEL_INSET_Y));
    return clip_rect(label, menu_area);
}

struct nb_rect nb_menu_panel_rect(const struct nb_menu *menu,
                                  struct nb_rect viewport)
{
    const struct nb_rect bar = nb_menu_bar_rect(viewport);
    const struct nb_menu_spec *spec = menu_spec(menu, menu->open_menu);
    struct nb_rect label;
    struct nb_rect panel;

    if (!nb_menu_is_open(menu) || spec == NULL) {
        return (struct nb_rect){bar.x, bar.y + bar.height, 0, 0};
    }

    label = nb_menu_label_rect(menu, viewport, menu->open_menu);
    panel.x = label.x;
    panel.y = bar.y + bar.height;
    panel.width = panel_natural_width(spec);
    panel.height = panel_natural_height(spec);
    return clip_rect(panel, viewport);
}

struct nb_rect nb_menu_item_rect(const struct nb_menu *menu,
                                 struct nb_rect viewport,
                                 size_t item_index)
{
    const struct nb_rect panel = nb_menu_panel_rect(menu, viewport);
    const struct nb_menu_spec *spec = menu_spec(menu, menu->open_menu);
    size_t index;
    int y = panel.y + MENU_PANEL_BORDER;
    struct nb_rect item_rect;

    if (item_index >= item_count(spec)) {
        return (struct nb_rect){panel.x, panel.y, 0, 0};
    }

    for (index = 0; index < item_index; ++index) {
        y += item_natural_height(&spec->items[index]);
    }

    item_rect.x = panel.x + MENU_PANEL_BORDER;
    item_rect.y = y;
    item_rect.width = maximum(0, panel.width - (2 * MENU_PANEL_BORDER));
    item_rect.height = item_natural_height(&spec->items[item_index]);
    return clip_rect(item_rect, panel);
}

struct nb_menu_hit nb_menu_hit_test(const struct nb_menu *menu,
                                    int x,
                                    int y,
                                    struct nb_rect viewport)
{
    struct nb_menu_hit hit = {NB_MENU_HIT_NONE, no_index, no_index};
    size_t index;

    if (nb_menu_is_open(menu)) {
        const struct nb_menu_spec *spec = menu_spec(menu, menu->open_menu);

        for (index = 0; index < item_count(spec); ++index) {
            if (rect_contains(nb_menu_item_rect(menu, viewport, index), x, y)) {
                hit.kind = NB_MENU_HIT_ITEM;
                hit.menu_index = menu->open_menu;
                hit.item_index = index;
                return hit;
            }
        }
        if (rect_contains(nb_menu_panel_rect(menu, viewport), x, y)) {
            hit.kind = NB_MENU_HIT_PANEL;
            hit.menu_index = menu->open_menu;
            return hit;
        }
    }

    for (index = 0; index < menu_count(menu); ++index) {
        if (rect_contains(nb_menu_label_rect(menu, viewport, index), x, y)) {
            hit.kind = NB_MENU_HIT_LABEL;
            hit.menu_index = index;
            return hit;
        }
    }

    if (rect_contains(nb_menu_bar_rect(viewport), x, y)) {
        hit.kind = NB_MENU_HIT_BAR;
    }
    return hit;
}

static size_t enabled_item_for_hit(const struct nb_menu *menu,
                                   struct nb_menu_hit hit)
{
    const struct nb_menu_spec *spec = menu_spec(menu, hit.menu_index);

    if (hit.kind != NB_MENU_HIT_ITEM || hit.item_index >= item_count(spec) ||
        !nb_menu_item_is_actionable(&spec->items[hit.item_index])) {
        return no_index;
    }
    return hit.item_index;
}

bool nb_menu_pointer_down(struct nb_menu *menu,
                          int x,
                          int y,
                          struct nb_rect viewport)
{
    const struct nb_menu_hit hit = nb_menu_hit_test(menu, x, y, viewport);

    if (menu->phase == NB_MENU_TRACKING) {
        return true;
    }

    if (menu->phase == NB_MENU_CLOSED) {
        if (hit.kind == NB_MENU_HIT_LABEL) {
            open_menu_index(menu, hit.menu_index, NB_MENU_TRACKING, false);
            return true;
        }
        return hit.kind == NB_MENU_HIT_BAR;
    }

    if (hit.kind == NB_MENU_HIT_LABEL &&
        hit.menu_index == menu->open_menu) {
        nb_menu_cancel(menu);
        return true;
    }

    if (hit.kind == NB_MENU_HIT_LABEL) {
        open_menu_index(menu, hit.menu_index, NB_MENU_TRACKING, false);
    } else {
        menu->phase = NB_MENU_TRACKING;
        menu->hot_item = enabled_item_for_hit(menu, hit);
    }
    return true;
}

bool nb_menu_pointer_move(struct nb_menu *menu,
                          int x,
                          int y,
                          struct nb_rect viewport)
{
    const struct nb_menu_hit hit = nb_menu_hit_test(menu, x, y, viewport);
    size_t hot_item;

    if (!nb_menu_is_open(menu)) {
        return false;
    }

    if (hit.kind == NB_MENU_HIT_LABEL &&
        hit.menu_index != menu->open_menu) {
        open_menu_index(menu, hit.menu_index, menu->phase, false);
        return true;
    }

    hot_item = enabled_item_for_hit(menu, hit);
    if (hot_item == menu->hot_item) {
        return false;
    }
    menu->hot_item = hot_item;
    return true;
}

nb_menu_command nb_menu_pointer_up(struct nb_menu *menu,
                                   int x,
                                   int y,
                                   struct nb_rect viewport)
{
    const struct nb_menu_hit hit = nb_menu_hit_test(menu, x, y, viewport);
    const struct nb_menu_spec *spec = menu_spec(menu, menu->open_menu);
    const size_t item_index = enabled_item_for_hit(menu, hit);
    nb_menu_command command = NB_MENU_COMMAND_NONE;

    if (menu->phase != NB_MENU_TRACKING) {
        return command;
    }

    if (hit.kind == NB_MENU_HIT_LABEL) {
        open_menu_index(menu, hit.menu_index, NB_MENU_OPEN, false);
        return command;
    }

    if (item_index != no_index && item_index < item_count(spec)) {
        command = spec->items[item_index].command;
    }
    nb_menu_cancel(menu);
    return command;
}

static void move_between_menus(struct nb_menu *menu, bool forward)
{
    const size_t count = menu_count(menu);
    size_t menu_index;

    if (count == 0) {
        return;
    }
    if (menu->open_menu == no_index) {
        menu_index = forward ? 0 : count - 1;
    } else if (forward) {
        menu_index = (menu->open_menu + 1) % count;
    } else {
        menu_index = (menu->open_menu + count - 1) % count;
    }
    open_menu_index(menu, menu_index, NB_MENU_OPEN, true);
}

nb_menu_command nb_menu_key_press(struct nb_menu *menu,
                                  enum nb_menu_key key)
{
    const struct nb_menu_spec *spec;
    nb_menu_command command = NB_MENU_COMMAND_NONE;

    if (key == NB_MENU_KEY_TOGGLE) {
        if (nb_menu_is_open(menu)) {
            nb_menu_cancel(menu);
        } else if (menu_count(menu) > 0) {
            open_menu_index(menu, 0, NB_MENU_OPEN, true);
        }
        return command;
    }

    if (key == NB_MENU_KEY_DISMISS) {
        nb_menu_cancel(menu);
        return command;
    }

    if (!nb_menu_is_open(menu)) {
        return command;
    }

    if (key == NB_MENU_KEY_NEXT_MENU) {
        move_between_menus(menu, true);
        return command;
    }
    if (key == NB_MENU_KEY_PREVIOUS_MENU) {
        move_between_menus(menu, false);
        return command;
    }

    spec = menu_spec(menu, menu->open_menu);
    if (key == NB_MENU_KEY_NEXT_ITEM) {
        menu->hot_item = next_enabled_item(spec, menu->hot_item, true);
    } else if (key == NB_MENU_KEY_PREVIOUS_ITEM) {
        menu->hot_item = next_enabled_item(spec, menu->hot_item, false);
    } else if (key == NB_MENU_KEY_ACTIVATE &&
               menu->hot_item < item_count(spec) &&
               nb_menu_item_is_actionable(&spec->items[menu->hot_item])) {
        command = spec->items[menu->hot_item].command;
        nb_menu_cancel(menu);
    }

    return command;
}

bool nb_menu_is_open(const struct nb_menu *menu)
{
    return menu->phase != NB_MENU_CLOSED;
}

bool nb_menu_is_tracking(const struct nb_menu *menu)
{
    return menu->phase == NB_MENU_TRACKING;
}
