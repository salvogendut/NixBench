#ifndef NIXBENCH_BACKDROP_RENDERER_H
#define NIXBENCH_BACKDROP_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "preferences.h"
#include "window.h"

bool nb_backdrop_render(SDL_Renderer *renderer,
                        struct nb_rect viewport,
                        const struct nb_user_preferences *preferences);

#endif
