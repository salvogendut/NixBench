#include "preferences.h"

#include <stdio.h>
#include <string.h>

static bool terminated_string(const char *text, size_t capacity)
{
    return text != NULL && memchr(text, '\0', capacity) != NULL;
}

void nb_user_preferences_init(struct nb_user_preferences *preferences)
{
    size_t index;

    if (preferences == NULL) {
        return;
    }
    memset(preferences, 0, sizeof(*preferences));
    for (index = 0; index < NB_PINNED_APPLICATION_COUNT; ++index) {
        preferences->pinned_applications[index] = true;
    }
    preferences->backdrop_primary = (struct nb_color){24, 54, 76};
    preferences->backdrop_secondary = (struct nb_color){43, 113, 137};
    preferences->backdrop_gradient_direction =
        NB_BACKDROP_GRADIENT_VERTICAL;
    preferences->minimize_gadget_visible = true;
    preferences->maximize_gadget_visible = true;
    preferences->window_control_layout = NB_WINDOW_CONTROLS_RIGHT;
    (void)snprintf(preferences->desktop_theme,
                   sizeof(preferences->desktop_theme),
                   "%s",
                   "classic");
    (void)snprintf(preferences->window_theme,
                   sizeof(preferences->window_theme),
                   "%s",
                   "classic");
}

bool nb_user_preferences_is_valid(
    const struct nb_user_preferences *preferences)
{
    return preferences != NULL &&
           preferences->backdrop_gradient_direction >=
               NB_BACKDROP_GRADIENT_VERTICAL &&
           preferences->backdrop_gradient_direction <=
               NB_BACKDROP_GRADIENT_DIAGONAL &&
           preferences->window_control_layout >= NB_WINDOW_CONTROLS_SPLIT &&
           preferences->window_control_layout <= NB_WINDOW_CONTROLS_RIGHT &&
           terminated_string(preferences->wallpaper,
                             sizeof(preferences->wallpaper)) &&
           terminated_string(preferences->desktop_theme,
                             sizeof(preferences->desktop_theme)) &&
           terminated_string(preferences->window_theme,
                             sizeof(preferences->window_theme));
}

bool nb_color_equal(struct nb_color first, struct nb_color second)
{
    return first.red == second.red && first.green == second.green &&
           first.blue == second.blue;
}
