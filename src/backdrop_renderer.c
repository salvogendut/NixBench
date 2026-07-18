#include "backdrop_renderer.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "png_loader.h"

struct nb_backdrop_cache {
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    int width;
    int height;
    struct nb_color primary;
    struct nb_color secondary;
    bool gradient_enabled;
    enum nb_backdrop_gradient_direction gradient_direction;
    enum nb_wallpaper_mode wallpaper_mode;
    char wallpaper[NB_PREFERENCES_WALLPAPER_CAPACITY];
};

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

struct nb_backdrop_cache *nb_backdrop_cache_create(void)
{
    return calloc(1, sizeof(struct nb_backdrop_cache));
}

void nb_backdrop_cache_invalidate(struct nb_backdrop_cache *cache)
{
    if (cache == NULL) {
        return;
    }
    SDL_DestroyTexture(cache->texture);
    cache->texture = NULL;
    cache->renderer = NULL;
    cache->width = 0;
    cache->height = 0;
}

void nb_backdrop_cache_destroy(struct nb_backdrop_cache *cache)
{
    if (cache == NULL) {
        return;
    }
    nb_backdrop_cache_invalidate(cache);
    free(cache);
}

static bool cache_matches(
    const struct nb_backdrop_cache *cache,
    SDL_Renderer *renderer,
    struct nb_rect viewport,
    const struct nb_user_preferences *preferences)
{
    return cache->texture != NULL && cache->renderer == renderer &&
           cache->width == viewport.width &&
           cache->height == viewport.height &&
           nb_color_equal(cache->primary, preferences->backdrop_primary) &&
           nb_color_equal(cache->secondary,
                          preferences->backdrop_secondary) &&
           cache->gradient_enabled ==
               preferences->backdrop_gradient_enabled &&
           cache->gradient_direction ==
               preferences->backdrop_gradient_direction &&
           cache->wallpaper_mode == preferences->wallpaper_mode &&
           strcmp(cache->wallpaper, preferences->wallpaper) == 0;
}

static SDL_FRect wallpaper_destination(struct nb_rect viewport,
                                       int image_width,
                                       int image_height,
                                       enum nb_wallpaper_mode mode)
{
    int width = image_width;
    int height = image_height;

    if (mode == NB_WALLPAPER_FIT) {
        if ((int64_t)viewport.width * image_height <=
            (int64_t)viewport.height * image_width) {
            width = viewport.width;
            height = (int)((int64_t)image_height * width / image_width);
        } else {
            height = viewport.height;
            width = (int)((int64_t)image_width * height / image_height);
        }
    } else if (mode == NB_WALLPAPER_FILL) {
        if ((int64_t)viewport.width * image_height >=
            (int64_t)viewport.height * image_width) {
            width = viewport.width;
            height = (int)((int64_t)image_height * width / image_width);
        } else {
            height = viewport.height;
            width = (int)((int64_t)image_width * height / image_height);
        }
    }
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    return (SDL_FRect){
        (float)(viewport.x + (viewport.width - width) / 2),
        (float)(viewport.y + (viewport.height - height) / 2),
        (float)width,
        (float)height
    };
}

static bool render_wallpaper(SDL_Renderer *renderer,
                             struct nb_rect viewport,
                             const struct nb_user_preferences *preferences)
{
    enum { TILE_RENDER_LIMIT = 65536 };
    struct nb_png_image image = {0};
    SDL_Surface *surface = NULL;
    SDL_Texture *texture = NULL;
    char error[256] = {0};
    bool rendered = true;

    if (preferences->wallpaper[0] == '\0') {
        return true;
    }
    if (!nb_png_load(preferences->wallpaper,
                     &image,
                     error,
                     sizeof(error))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not load wallpaper '%s': %s",
                    preferences->wallpaper,
                    error);
        return true;
    }
    surface = SDL_CreateSurfaceFrom(image.width,
                                    image.height,
                                    SDL_PIXELFORMAT_RGBA32,
                                    image.pixels,
                                    (int)image.pitch);
    if (surface != NULL) {
        texture = SDL_CreateTextureFromSurface(renderer, surface);
    }
    if (texture == NULL ||
        !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not prepare wallpaper '%s': %s",
                    preferences->wallpaper,
                    SDL_GetError());
        rendered = true;
        goto cleanup;
    }
    if (preferences->wallpaper_mode == NB_WALLPAPER_TILE) {
        const uint64_t columns =
            ((uint64_t)viewport.width + (uint64_t)image.width - 1) /
            (uint64_t)image.width;
        const uint64_t rows =
            ((uint64_t)viewport.height + (uint64_t)image.height - 1) /
            (uint64_t)image.height;
        int y;

        if (columns * rows > TILE_RENDER_LIMIT) {
            const SDL_FRect destination = {
                (float)viewport.x,
                (float)viewport.y,
                (float)viewport.width,
                (float)viewport.height
            };

            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Wallpaper tile count is excessive; stretching the "
                        "image for this session");
            rendered = SDL_RenderTexture(renderer,
                                         texture,
                                         NULL,
                                         &destination);
        } else {
            for (y = viewport.y;
                 rendered && y < viewport.y + viewport.height;
                 y += image.height) {
                int x;

                for (x = viewport.x;
                     rendered && x < viewport.x + viewport.width;
                     x += image.width) {
                    const SDL_FRect destination = {
                        (float)x,
                        (float)y,
                        (float)image.width,
                        (float)image.height
                    };

                    rendered = SDL_RenderTexture(renderer,
                                                 texture,
                                                 NULL,
                                                 &destination);
                }
            }
        }
    } else {
        const SDL_FRect destination =
            wallpaper_destination(viewport,
                                  image.width,
                                  image.height,
                                  preferences->wallpaper_mode);

        rendered = SDL_RenderTexture(renderer, texture, NULL, &destination);
    }

cleanup:
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
    nb_png_image_destroy(&image);
    return rendered;
}

static bool rebuild_cache(
    struct nb_backdrop_cache *cache,
    SDL_Renderer *renderer,
    struct nb_rect viewport,
    const struct nb_user_preferences *preferences)
{
    SDL_Surface *surface = NULL;
    SDL_Renderer *surface_renderer = NULL;
    SDL_Texture *texture = NULL;
    const struct nb_rect local_viewport = {
        0,
        0,
        viewport.width,
        viewport.height
    };
    bool rendered = false;

    surface = SDL_CreateSurface(viewport.width,
                                viewport.height,
                                SDL_PIXELFORMAT_XRGB8888);
    if (surface != NULL) {
        surface_renderer = SDL_CreateSoftwareRenderer(surface);
    }
    if (surface_renderer != NULL &&
        nb_backdrop_render(surface_renderer,
                           local_viewport,
                           preferences) &&
        render_wallpaper(surface_renderer,
                         local_viewport,
                         preferences) &&
        SDL_RenderPresent(surface_renderer)) {
        texture = SDL_CreateTextureFromSurface(renderer, surface);
    }
    if (texture != NULL &&
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE)) {
        SDL_DestroyTexture(cache->texture);
        cache->texture = texture;
        cache->renderer = renderer;
        cache->width = viewport.width;
        cache->height = viewport.height;
        cache->primary = preferences->backdrop_primary;
        cache->secondary = preferences->backdrop_secondary;
        cache->gradient_enabled =
            preferences->backdrop_gradient_enabled;
        cache->gradient_direction =
            preferences->backdrop_gradient_direction;
        cache->wallpaper_mode = preferences->wallpaper_mode;
        (void)snprintf(cache->wallpaper,
                       sizeof(cache->wallpaper),
                       "%s",
                       preferences->wallpaper);
        texture = NULL;
        rendered = true;
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(surface_renderer);
    SDL_DestroySurface(surface);
    return rendered;
}

bool nb_backdrop_cache_render(
    struct nb_backdrop_cache *cache,
    SDL_Renderer *renderer,
    struct nb_rect viewport,
    const struct nb_user_preferences *preferences)
{
    const SDL_FRect destination = {
        (float)viewport.x,
        (float)viewport.y,
        (float)viewport.width,
        (float)viewport.height
    };

    if (cache == NULL || renderer == NULL || viewport.width <= 0 ||
        viewport.height <= 0 ||
        !nb_user_preferences_is_valid(preferences)) {
        return false;
    }
    if (!cache_matches(cache, renderer, viewport, preferences) &&
        !rebuild_cache(cache, renderer, viewport, preferences)) {
        return false;
    }
    return SDL_RenderTexture(renderer,
                             cache->texture,
                             NULL,
                             &destination);
}
