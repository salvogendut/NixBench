#include "backdrop_renderer.h"

#include <stdint.h>

static Uint8 interpolate(Uint8 first,
                         Uint8 second,
                         int position,
                         int maximum)
{
    const int64_t numerator =
        (int64_t)first * (maximum - position) +
        (int64_t)second * position;

    return maximum <= 0 ? first : (Uint8)(numerator / maximum);
}

static bool set_interpolated_color(
    SDL_Renderer *renderer,
    const struct nb_user_preferences *preferences,
    int position,
    int maximum)
{
    return SDL_SetRenderDrawColor(
        renderer,
        interpolate(preferences->backdrop_primary.red,
                    preferences->backdrop_secondary.red,
                    position,
                    maximum),
        interpolate(preferences->backdrop_primary.green,
                    preferences->backdrop_secondary.green,
                    position,
                    maximum),
        interpolate(preferences->backdrop_primary.blue,
                    preferences->backdrop_secondary.blue,
                    position,
                    maximum),
        SDL_ALPHA_OPAQUE);
}

static bool render_vertical(
    SDL_Renderer *renderer,
    struct nb_rect viewport,
    const struct nb_user_preferences *preferences)
{
    int row;
    const int maximum = viewport.height - 1;

    for (row = 0; row < viewport.height; ++row) {
        if (!set_interpolated_color(renderer,
                                    preferences,
                                    row,
                                    maximum) ||
            !SDL_RenderLine(renderer,
                            (float)viewport.x,
                            (float)(viewport.y + row),
                            (float)(viewport.x + viewport.width - 1),
                            (float)(viewport.y + row))) {
            return false;
        }
    }
    return true;
}

static bool render_horizontal(
    SDL_Renderer *renderer,
    struct nb_rect viewport,
    const struct nb_user_preferences *preferences)
{
    int column;
    const int maximum = viewport.width - 1;

    for (column = 0; column < viewport.width; ++column) {
        if (!set_interpolated_color(renderer,
                                    preferences,
                                    column,
                                    maximum) ||
            !SDL_RenderLine(renderer,
                            (float)(viewport.x + column),
                            (float)viewport.y,
                            (float)(viewport.x + column),
                            (float)(viewport.y + viewport.height - 1))) {
            return false;
        }
    }
    return true;
}

static bool render_diagonal(
    SDL_Renderer *renderer,
    struct nb_rect viewport,
    const struct nb_user_preferences *preferences)
{
    const int maximum = viewport.width + viewport.height - 2;
    int diagonal;

    for (diagonal = 0; diagonal <= maximum; ++diagonal) {
        int first_x = diagonal - (viewport.height - 1);
        int last_x = diagonal;
        int first_y;
        int last_y;

        if (first_x < 0) {
            first_x = 0;
        }
        if (last_x >= viewport.width) {
            last_x = viewport.width - 1;
        }
        first_y = diagonal - first_x;
        last_y = diagonal - last_x;
        if (!set_interpolated_color(renderer,
                                    preferences,
                                    diagonal,
                                    maximum) ||
            !SDL_RenderLine(renderer,
                            (float)(viewport.x + first_x),
                            (float)(viewport.y + first_y),
                            (float)(viewport.x + last_x),
                            (float)(viewport.y + last_y))) {
            return false;
        }
    }
    return true;
}

bool nb_backdrop_render(SDL_Renderer *renderer,
                        struct nb_rect viewport,
                        const struct nb_user_preferences *preferences)
{
    const SDL_FRect destination = {
        (float)viewport.x,
        (float)viewport.y,
        (float)viewport.width,
        (float)viewport.height
    };

    if (renderer == NULL || viewport.width <= 0 || viewport.height <= 0 ||
        !nb_user_preferences_is_valid(preferences)) {
        return false;
    }
    if (!preferences->backdrop_gradient_enabled ||
        nb_color_equal(preferences->backdrop_primary,
                       preferences->backdrop_secondary)) {
        return SDL_SetRenderDrawColor(renderer,
                                      preferences->backdrop_primary.red,
                                      preferences->backdrop_primary.green,
                                      preferences->backdrop_primary.blue,
                                      SDL_ALPHA_OPAQUE) &&
               SDL_RenderFillRect(renderer, &destination);
    }
    switch (preferences->backdrop_gradient_direction) {
    case NB_BACKDROP_GRADIENT_VERTICAL:
        return render_vertical(renderer, viewport, preferences);
    case NB_BACKDROP_GRADIENT_HORIZONTAL:
        return render_horizontal(renderer, viewport, preferences);
    case NB_BACKDROP_GRADIENT_DIAGONAL:
        return render_diagonal(renderer, viewport, preferences);
    }
    return false;
}
