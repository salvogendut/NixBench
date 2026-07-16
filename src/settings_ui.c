#include "settings_ui.h"

#include <stdio.h>
#include <string.h>

struct color {
    Uint8 red;
    Uint8 green;
    Uint8 blue;
};

enum {
    SETTINGS_PADDING = 16,
    SETTINGS_ROW_HEIGHT = 20,
    SETTINGS_ROW_GAP = 4,
    SETTINGS_SWATCH_WIDTH = 30,
    SETTINGS_SWATCH_HEIGHT = 20,
    SETTINGS_SWATCH_GAP = 7,
    SETTINGS_SWATCH_COLUMNS = 6
};

static const struct color panel_color = {225, 230, 222};
static const struct color panel_shadow = {74, 89, 91};
static const struct color selection_color = {43, 113, 137};
static const struct color selection_text = {248, 249, 241};
static const struct color text_color = {18, 30, 34};
static const struct color muted_text = {76, 88, 88};

static const struct nb_color palette[] = {
    {24, 54, 76},
    {43, 113, 137},
    {59, 126, 93},
    {100, 72, 127},
    {142, 47, 39},
    {196, 112, 45},
    {96, 116, 123},
    {28, 33, 36},
    {225, 230, 222},
    {92, 160, 184},
    {222, 205, 154},
    {112, 78, 56}
};

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

    return rect.width <= 0 || rect.height <= 0 ||
           (set_color(renderer, color) &&
            SDL_RenderFillRect(renderer, &destination));
}

static bool outline_rect(SDL_Renderer *renderer,
                         struct nb_rect rect,
                         struct color color)
{
    const SDL_FRect destination = {
        (float)rect.x,
        (float)rect.y,
        (float)rect.width,
        (float)rect.height
    };

    return rect.width <= 0 || rect.height <= 0 ||
           (set_color(renderer, color) &&
            SDL_RenderRect(renderer, &destination));
}

static bool contains(struct nb_rect rect, int x, int y)
{
    return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
           x < rect.x + rect.width && y < rect.y + rect.height;
}

static struct nb_rect full_row(struct nb_rect content, int offset)
{
    return (struct nb_rect){
        content.x + SETTINGS_PADDING,
        content.y + offset,
        content.width - (2 * SETTINGS_PADDING),
        SETTINGS_ROW_HEIGHT
    };
}

static struct nb_rect swatch_rect(struct nb_rect content, size_t index)
{
    const int column = (int)(index % SETTINGS_SWATCH_COLUMNS);
    const int row = (int)(index / SETTINGS_SWATCH_COLUMNS);

    return (struct nb_rect){
        content.x + SETTINGS_PADDING +
            column * (SETTINGS_SWATCH_WIDTH + SETTINGS_SWATCH_GAP),
        content.y + 112 +
            row * (SETTINGS_SWATCH_HEIGHT + SETTINGS_SWATCH_GAP),
        SETTINGS_SWATCH_WIDTH,
        SETTINGS_SWATCH_HEIGHT
    };
}

static bool render_text(SDL_Renderer *renderer,
                        int x,
                        int y,
                        int right,
                        const char *text,
                        struct color color)
{
    char clipped[128];
    size_t capacity;
    size_t length;

    if (text == NULL || text[0] == '\0' || right - x < 8) {
        return true;
    }
    capacity = (size_t)((right - x) / 8);
    if (capacity >= sizeof(clipped)) {
        capacity = sizeof(clipped) - 1;
    }
    length = strlen(text);
    if (length > capacity) {
        length = capacity;
    }
    (void)memcpy(clipped, text, length);
    clipped[length] = '\0';
    return clipped[0] == '\0' ||
           (set_color(renderer, color) &&
            SDL_RenderDebugText(renderer, (float)x, (float)y, clipped));
}

static bool render_heading(SDL_Renderer *renderer,
                           struct nb_rect content,
                           int offset,
                           const char *text)
{
    return render_text(renderer,
                       content.x + SETTINGS_PADDING,
                       content.y + offset,
                       content.x + content.width - SETTINGS_PADDING,
                       text,
                       selection_color);
}

static bool render_button(SDL_Renderer *renderer,
                          struct nb_rect row,
                          const char *label,
                          bool selected)
{
    const struct color background = selected ? selection_color : panel_color;
    const struct color foreground = selected ? selection_text : text_color;

    return fill_rect(renderer, row, background) &&
           outline_rect(renderer, row, panel_shadow) &&
           render_text(renderer,
                       row.x + 7,
                       row.y + 6,
                       row.x + row.width - 5,
                       label,
                       foreground);
}

static bool render_checkbox(SDL_Renderer *renderer,
                            struct nb_rect row,
                            const char *label,
                            bool checked)
{
    char text[160];

    (void)snprintf(text,
                   sizeof(text),
                   "[%c] %s",
                   checked ? 'x' : ' ',
                   label);
    return render_button(renderer, row, text, false);
}

static const char *direction_name(enum nb_backdrop_gradient_direction value)
{
    if (value == NB_BACKDROP_GRADIENT_HORIZONTAL) {
        return "Horizontal";
    }
    if (value == NB_BACKDROP_GRADIENT_DIAGONAL) {
        return "Diagonal";
    }
    return "Vertical";
}

static const char *layout_name(enum nb_window_control_layout value)
{
    if (value == NB_WINDOW_CONTROLS_LEFT) {
        return "Left";
    }
    if (value == NB_WINDOW_CONTROLS_RIGHT) {
        return "Right";
    }
    return "Split";
}

size_t nb_settings_palette_size(void)
{
    return sizeof(palette) / sizeof(palette[0]);
}

bool nb_settings_palette_color(size_t index, struct nb_color *color)
{
    if (index >= nb_settings_palette_size() || color == NULL) {
        return false;
    }
    *color = palette[index];
    return true;
}

enum nb_settings_action nb_settings_hit_test(struct nb_rect content,
                                             int x,
                                             int y)
{
    size_t index;

    if (contains(full_row(content, 38), x, y)) {
        return NB_SETTINGS_ACTION_SELECT_PRIMARY;
    }
    if (contains(full_row(content, 62), x, y)) {
        return NB_SETTINGS_ACTION_SELECT_SECONDARY;
    }
    for (index = 0; index < nb_settings_palette_size(); ++index) {
        if (contains(swatch_rect(content, index), x, y)) {
            return (enum nb_settings_action)(NB_SETTINGS_ACTION_COLOR_FIRST +
                                             (int)index);
        }
    }
    if (contains(full_row(content, 170), x, y)) {
        return NB_SETTINGS_ACTION_TOGGLE_GRADIENT;
    }
    if (contains(full_row(content, 194), x, y)) {
        return NB_SETTINGS_ACTION_CYCLE_GRADIENT_DIRECTION;
    }
    if (contains(full_row(content, 246), x, y)) {
        return NB_SETTINGS_ACTION_TOGGLE_NIXCLOCK_PIN;
    }
    if (contains(full_row(content, 270), x, y)) {
        return NB_SETTINGS_ACTION_TOGGLE_SAKURA_PIN;
    }
    if (contains(full_row(content, 294), x, y)) {
        return NB_SETTINGS_ACTION_TOGGLE_MIDORI_PIN;
    }
    if (contains(full_row(content, 346), x, y)) {
        return NB_SETTINGS_ACTION_TOGGLE_MAXIMIZE;
    }
    if (contains(full_row(content, 370), x, y)) {
        return NB_SETTINGS_ACTION_CYCLE_CONTROL_LAYOUT;
    }
    return NB_SETTINGS_ACTION_NONE;
}

bool nb_settings_render(SDL_Renderer *renderer,
                        struct nb_rect content,
                        const struct nb_user_preferences *preferences,
                        enum nb_settings_color_target selected_color)
{
    char label[128];
    size_t index;

    if (renderer == NULL || !nb_user_preferences_is_valid(preferences)) {
        return false;
    }
    if (!render_heading(renderer, content, 14, "Backdrop") ||
        !render_button(renderer,
                       full_row(content, 38),
                       "Color 1 (primary)",
                       selected_color == NB_SETTINGS_COLOR_PRIMARY) ||
        !render_button(renderer,
                       full_row(content, 62),
                       "Color 2 (gradient end)",
                       selected_color == NB_SETTINGS_COLOR_SECONDARY) ||
        !render_text(renderer,
                     content.x + SETTINGS_PADDING,
                     content.y + 94,
                     content.x + content.width - SETTINGS_PADDING,
                     "Palette - click a swatch for the selected color",
                     muted_text)) {
        return false;
    }
    for (index = 0; index < nb_settings_palette_size(); ++index) {
        const struct nb_rect swatch = swatch_rect(content, index);
        const struct nb_color value = palette[index];
        const struct nb_color selected =
            selected_color == NB_SETTINGS_COLOR_PRIMARY
                ? preferences->backdrop_primary
                : preferences->backdrop_secondary;
        const struct color swatch_color = {value.red, value.green, value.blue};

        if (!fill_rect(renderer, swatch, swatch_color) ||
            !outline_rect(renderer,
                          swatch,
                          nb_color_equal(value, selected)
                              ? selection_text
                              : panel_shadow)) {
            return false;
        }
    }
    (void)snprintf(label,
                   sizeof(label),
                   "Gradient: %s",
                   preferences->backdrop_gradient_enabled ? "On" : "Off");
    if (!render_checkbox(renderer,
                         full_row(content, 170),
                         label,
                         preferences->backdrop_gradient_enabled)) {
        return false;
    }
    (void)snprintf(label,
                   sizeof(label),
                   "Gradient direction: %s",
                   direction_name(preferences->backdrop_gradient_direction));
    if (!render_button(renderer, full_row(content, 194), label, false) ||
        !render_heading(renderer, content, 226, "Pinned Applications") ||
        !render_checkbox(
            renderer,
            full_row(content, 246),
            "NixClock",
            preferences->pinned_applications[
                NB_PINNED_APPLICATION_NIXCLOCK]) ||
        !render_checkbox(
            renderer,
            full_row(content, 270),
            "Sakura Terminal",
            preferences->pinned_applications[NB_PINNED_APPLICATION_SAKURA]) ||
        !render_checkbox(
            renderer,
            full_row(content, 294),
            "Midori Web Browser",
            preferences->pinned_applications[NB_PINNED_APPLICATION_MIDORI]) ||
        !render_heading(renderer, content, 326, "Windows") ||
        !render_checkbox(renderer,
                         full_row(content, 346),
                         "Show maximize gadget",
                         preferences->maximize_gadget_visible)) {
        return false;
    }
    (void)snprintf(label,
                   sizeof(label),
                   "Window gadget placement: %s",
                   layout_name(preferences->window_control_layout));
    if (!render_button(renderer, full_row(content, 370), label, false) ||
        !render_heading(renderer, content, 410, "Appearance (reserved)") ||
        !render_text(renderer,
                     content.x + SETTINGS_PADDING,
                     content.y + 430,
                     content.x + content.width - SETTINGS_PADDING,
                     "Wallpaper and skinnable themes are recorded in .nixbenchrc.",
                     muted_text)) {
        return false;
    }
    (void)snprintf(label,
                   sizeof(label),
                   "Desktop theme: %s   Window theme: %s",
                   preferences->desktop_theme,
                   preferences->window_theme);
    return render_text(renderer,
                       content.x + SETTINGS_PADDING,
                       content.y + 450,
                       content.x + content.width - SETTINGS_PADDING,
                       label,
                       muted_text);
}
