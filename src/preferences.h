#ifndef NIXBENCH_PREFERENCES_H
#define NIXBENCH_PREFERENCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_PREFERENCES_THEME_CAPACITY = 32,
    NB_PREFERENCES_WALLPAPER_CAPACITY = 512
};

enum nb_pinned_application {
    NB_PINNED_APPLICATION_NIXCLOCK = 0,
    NB_PINNED_APPLICATION_SAKURA,
    NB_PINNED_APPLICATION_MIDORI,
    NB_PINNED_APPLICATION_PCMANFM,
    NB_PINNED_APPLICATION_COUNT
};

enum nb_backdrop_gradient_direction {
    NB_BACKDROP_GRADIENT_VERTICAL = 0,
    NB_BACKDROP_GRADIENT_HORIZONTAL,
    NB_BACKDROP_GRADIENT_DIAGONAL
};

enum nb_wallpaper_mode {
    NB_WALLPAPER_CENTER = 0,
    NB_WALLPAPER_TILE,
    NB_WALLPAPER_FIT,
    NB_WALLPAPER_FILL
};

enum nb_window_control_layout {
    NB_WINDOW_CONTROLS_SPLIT = 0,
    NB_WINDOW_CONTROLS_LEFT,
    NB_WINDOW_CONTROLS_RIGHT
};

struct nb_color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct nb_user_preferences {
    bool pinned_applications[NB_PINNED_APPLICATION_COUNT];
    struct nb_color backdrop_primary;
    struct nb_color backdrop_secondary;
    bool backdrop_gradient_enabled;
    enum nb_backdrop_gradient_direction backdrop_gradient_direction;
    enum nb_wallpaper_mode wallpaper_mode;
    bool maximize_gadget_visible;
    bool minimize_gadget_visible;
    enum nb_window_control_layout window_control_layout;
    char wallpaper[NB_PREFERENCES_WALLPAPER_CAPACITY];
    char desktop_theme[NB_PREFERENCES_THEME_CAPACITY];
    char window_theme[NB_PREFERENCES_THEME_CAPACITY];
};

void nb_user_preferences_init(struct nb_user_preferences *preferences);
bool nb_user_preferences_is_valid(
    const struct nb_user_preferences *preferences);
bool nb_color_equal(struct nb_color first, struct nb_color second);

#endif
