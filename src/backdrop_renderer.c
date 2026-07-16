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
    const int right_position = viewport.width - 1;
    const int bottom_position = viewport.height - 1;
    const SDL_FColor primary = {
        preferences->backdrop_primary.red / 255.0f,
        preferences->backdrop_primary.green / 255.0f,
        preferences->backdrop_primary.blue / 255.0f,
        1.0f
    };
    const SDL_FColor top_right = {
        interpolate(preferences->backdrop_primary.red,
                    preferences->backdrop_secondary.red,
                    right_position,
                    maximum) / 255.0f,
        interpolate(preferences->backdrop_primary.green,
                    preferences->backdrop_secondary.green,
                    right_position,
                    maximum) / 255.0f,
        interpolate(preferences->backdrop_primary.blue,
                    preferences->backdrop_secondary.blue,
                    right_position,
                    maximum) / 255.0f,
        1.0f
    };
    const SDL_FColor bottom_left = {
        interpolate(preferences->backdrop_primary.red,
                    preferences->backdrop_secondary.red,
                    bottom_position,
                    maximum) / 255.0f,
        interpolate(preferences->backdrop_primary.green,
                    preferences->backdrop_secondary.green,
                    bottom_position,
                    maximum) / 255.0f,
        interpolate(preferences->backdrop_primary.blue,
                    preferences->backdrop_secondary.blue,
                    bottom_position,
                    maximum) / 255.0f,
        1.0f
    };
    const SDL_FColor secondary = {
        preferences->backdrop_secondary.red / 255.0f,
        preferences->backdrop_secondary.green / 255.0f,
        preferences->backdrop_secondary.blue / 255.0f,
        1.0f
    };
    const float left = (float)viewport.x;
    const float top = (float)viewport.y;
    const float right = (float)(viewport.x + viewport.width);
    const float bottom = (float)(viewport.y + viewport.height);
    const SDL_Vertex vertices[] = {
        {{left, top}, primary, {0.0f, 0.0f}},
        {{right, top}, top_right, {0.0f, 0.0f}},
        {{right, bottom}, secondary, {0.0f, 0.0f}},
        {{left, bottom}, bottom_left, {0.0f, 0.0f}}
    };
    const int indices[] = {0, 1, 2, 0, 2, 3};

    /*
     * The former implementation issued one negative-slope SDL_RenderLine
     * for every anti-diagonal.  SDL's NetBSD software renderer becomes
     * unstable on long lines of that shape at console-sized resolutions.
     * A single pair of color-interpolated triangles describes the same
     * linear function of x+y without exercising that line path.
     */
    return SDL_RenderGeometry(renderer,
                              NULL,
                              vertices,
                              (int)(sizeof(vertices) / sizeof(vertices[0])),
                              indices,
                              (int)(sizeof(indices) / sizeof(indices[0])));
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
