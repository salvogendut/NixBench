#ifndef NIXBENCH_SETTINGS_UI_H
#define NIXBENCH_SETTINGS_UI_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL.h>

#include "preferences.h"
#include "window.h"

enum nb_settings_color_target {
    NB_SETTINGS_COLOR_PRIMARY = 0,
    NB_SETTINGS_COLOR_SECONDARY
};

enum nb_settings_action {
    NB_SETTINGS_ACTION_NONE = 0,
    NB_SETTINGS_ACTION_SELECT_PRIMARY,
    NB_SETTINGS_ACTION_SELECT_SECONDARY,
    NB_SETTINGS_ACTION_COLOR_FIRST,
    NB_SETTINGS_ACTION_COLOR_LAST = NB_SETTINGS_ACTION_COLOR_FIRST + 11,
    NB_SETTINGS_ACTION_TOGGLE_GRADIENT,
    NB_SETTINGS_ACTION_CYCLE_GRADIENT_DIRECTION,
    NB_SETTINGS_ACTION_TOGGLE_NIXCLOCK_PIN,
    NB_SETTINGS_ACTION_TOGGLE_SAKURA_PIN,
    NB_SETTINGS_ACTION_TOGGLE_MIDORI_PIN,
    NB_SETTINGS_ACTION_TOGGLE_THUNAR_PIN,
    NB_SETTINGS_ACTION_TOGGLE_MINIMIZE,
    NB_SETTINGS_ACTION_TOGGLE_MAXIMIZE,
    NB_SETTINGS_ACTION_CYCLE_CONTROL_LAYOUT,
    NB_SETTINGS_ACTION_CHOOSE_WALLPAPER,
    NB_SETTINGS_ACTION_CYCLE_WALLPAPER_MODE
};

size_t nb_settings_palette_size(void);
bool nb_settings_palette_color(size_t index, struct nb_color *color);
enum nb_settings_action nb_settings_hit_test(struct nb_rect content,
                                             int x,
                                             int y);
bool nb_settings_render(SDL_Renderer *renderer,
                        struct nb_rect content,
                        const struct nb_user_preferences *preferences,
                        enum nb_settings_color_target selected_color);

#endif
