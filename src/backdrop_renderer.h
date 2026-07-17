#ifndef NIXBENCH_BACKDROP_RENDERER_H
#define NIXBENCH_BACKDROP_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "preferences.h"
#include "window.h"

struct nb_backdrop_cache;

bool nb_backdrop_render(SDL_Renderer *renderer,
                        struct nb_rect viewport,
                        const struct nb_user_preferences *preferences);

struct nb_backdrop_cache *nb_backdrop_cache_create(void);
void nb_backdrop_cache_invalidate(struct nb_backdrop_cache *cache);
void nb_backdrop_cache_destroy(struct nb_backdrop_cache *cache);
bool nb_backdrop_cache_render(
    struct nb_backdrop_cache *cache,
    SDL_Renderer *renderer,
    struct nb_rect viewport,
    const struct nb_user_preferences *preferences);

#endif
