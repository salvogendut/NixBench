#include "menu_renderer.h"

#include <stddef.h>

struct color {
    Uint8 red;
    Uint8 green;
    Uint8 blue;
};

static const struct color bar_color = {172, 184, 184};
static const struct color highlight_color = {239, 246, 240};
static const struct color shadow_color = {49, 61, 68};
static const struct color panel_color = {213, 219, 211};
static const struct color selection_color = {43, 113, 137};
static const struct color text_color = {18, 28, 33};
static const struct color selected_text_color = {245, 247, 238};
static const struct color disabled_text_color = {96, 108, 108};

static bool set_color(SDL_Renderer *renderer, struct color color)
{
    return SDL_SetRenderDrawColor(renderer,
                                  color.red,
                                  color.green,
                                  color.blue,
                                  SDL_ALPHA_OPAQUE);
}

static bool fill_rect(SDL_Renderer *renderer,
                      struct nb_rect rect,
                      struct color color)
{
    const SDL_FRect destination = {
        (float)rect.x,
        (float)rect.y,
        (float)rect.width,
        (float)rect.height
    };

    if (rect.width <= 0 || rect.height <= 0) {
        return true;
    }
    return set_color(renderer, color) &&
           SDL_RenderFillRect(renderer, &destination);
}

static bool render_bevel(SDL_Renderer *renderer,
                         struct nb_rect rect,
                         bool pressed)
{
    const struct color top_left = pressed ? shadow_color : highlight_color;
    const struct color bottom_right = pressed ? highlight_color : shadow_color;
    float left;
    float top;
    float right;
    float bottom;

    if (rect.width <= 0 || rect.height <= 0) {
        return true;
    }

    left = (float)rect.x;
    top = (float)rect.y;
    right = (float)(rect.x + rect.width - 1);
    bottom = (float)(rect.y + rect.height - 1);
    return set_color(renderer, top_left) &&
           SDL_RenderLine(renderer, left, bottom, left, top) &&
           SDL_RenderLine(renderer, left, top, right, top) &&
           set_color(renderer, bottom_right) &&
           SDL_RenderLine(renderer, right, top, right, bottom) &&
           SDL_RenderLine(renderer, right, bottom, left, bottom);
}

static bool render_text(SDL_Renderer *renderer,
                        struct nb_rect rect,
                        int horizontal_padding,
                        struct color color,
                        const char *text,
                        bool right_aligned)
{
    char clipped[NB_MENU_TEXT_CAPACITY];
    const int available_width = rect.width - (2 * horizontal_padding);
    size_t maximum_characters;
    size_t length = 0;
    int text_x;
    int text_y;

    if (text == NULL || available_width < 8 || rect.height < 8) {
        return true;
    }

    maximum_characters = (size_t)(available_width / 8);
    if (maximum_characters >= sizeof(clipped)) {
        maximum_characters = sizeof(clipped) - 1;
    }
    while (length < maximum_characters && text[length] != '\0') {
        clipped[length] = text[length];
        ++length;
    }
    clipped[length] = '\0';
    if (length == 0) {
        return true;
    }

    text_x = right_aligned ?
             rect.x + rect.width - horizontal_padding - (int)(length * 8) :
             rect.x + horizontal_padding;
    text_y = rect.y + ((rect.height - 8) / 2);
    return set_color(renderer, color) &&
           SDL_RenderDebugText(renderer,
                               (float)text_x,
                               (float)text_y,
                               clipped);
}

static size_t menu_count(const struct nb_menu *menu)
{
    if (menu->model == NULL || menu->model->menus == NULL) {
        return 0;
    }
    return menu->model->menu_count < NB_MENU_MAX_MENUS ?
           menu->model->menu_count : NB_MENU_MAX_MENUS;
}

static size_t item_count(const struct nb_menu_spec *spec)
{
    if (spec == NULL || spec->items == NULL) {
        return 0;
    }
    return spec->item_count < NB_MENU_MAX_ITEMS ?
           spec->item_count : NB_MENU_MAX_ITEMS;
}

static bool render_bar(SDL_Renderer *renderer,
                       const struct nb_menu *menu,
                       struct nb_rect viewport,
                       const char *clock_text)
{
    const struct nb_rect bar = nb_menu_bar_rect(viewport);
    const struct nb_rect clock = nb_menu_clock_rect(viewport);
    size_t index;

    if (!fill_rect(renderer, bar, bar_color) ||
        !render_bevel(renderer, bar, false)) {
        return false;
    }

    for (index = 0; index < menu_count(menu); ++index) {
        const struct nb_rect label = nb_menu_label_rect(menu, viewport, index);
        const bool open = nb_menu_is_open(menu) && menu->open_menu == index;

        if (open &&
            (!fill_rect(renderer, label, selection_color) ||
             !render_bevel(renderer, label, true))) {
            return false;
        }
        if (!render_text(renderer,
                         label,
                         8,
                         open ? selected_text_color : text_color,
                         menu->model->menus[index].label,
                         false)) {
            return false;
        }
    }

    return fill_rect(renderer, clock, panel_color) &&
           render_bevel(renderer, clock, true) &&
           render_text(renderer,
                       clock,
                       6,
                       text_color,
                       clock_text,
                       true);
}

static bool render_separator(SDL_Renderer *renderer, struct nb_rect rect)
{
    const float left = (float)(rect.x + 5);
    const float right = (float)(rect.x + rect.width - 6);
    const float middle = (float)(rect.y + (rect.height / 2));

    if (rect.width < 12 || rect.height < 3) {
        return true;
    }
    return set_color(renderer, shadow_color) &&
           SDL_RenderLine(renderer, left, middle, right, middle) &&
           set_color(renderer, highlight_color) &&
           SDL_RenderLine(renderer, left, middle + 1.0f, right, middle + 1.0f);
}

static bool render_panel(SDL_Renderer *renderer,
                         const struct nb_menu *menu,
                         struct nb_rect viewport)
{
    const struct nb_rect panel = nb_menu_panel_rect(menu, viewport);
    const struct nb_menu_spec *spec;
    struct nb_rect shadow = panel;
    size_t index;

    if (!nb_menu_is_open(menu) || menu->open_menu >= menu_count(menu)) {
        return true;
    }
    spec = &menu->model->menus[menu->open_menu];
    shadow.x += 3;
    shadow.y += 3;
    if (!fill_rect(renderer, shadow, shadow_color) ||
        !fill_rect(renderer, panel, panel_color) ||
        !render_bevel(renderer, panel, false)) {
        return false;
    }

    for (index = 0; index < item_count(spec); ++index) {
        const struct nb_menu_item_spec *item = &spec->items[index];
        const struct nb_rect item_rect =
            nb_menu_item_rect(menu, viewport, index);
        const bool actionable = nb_menu_item_is_actionable(item);
        const bool hot = index == menu->hot_item && actionable;

        if (item->kind == NB_MENU_ITEM_SEPARATOR) {
            if (!render_separator(renderer, item_rect)) {
                return false;
            }
            continue;
        }

        if (hot && !fill_rect(renderer, item_rect, selection_color)) {
            return false;
        }
        if (!render_text(renderer,
                         item_rect,
                         10,
                         hot ? selected_text_color :
                         (actionable ? text_color : disabled_text_color),
                         item->label,
                         false)) {
            return false;
        }
    }
    return true;
}

bool nb_menu_render(SDL_Renderer *renderer,
                    const struct nb_menu *menu,
                    struct nb_rect viewport,
                    const char *clock_text)
{
    return render_bar(renderer, menu, viewport, clock_text) &&
           render_panel(renderer, menu, viewport);
}
