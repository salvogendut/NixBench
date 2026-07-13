#ifndef NIXBENCH_DESKTOP_RENDERER_H
#define NIXBENCH_DESKTOP_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "desktop.h"

bool nb_desktop_render(SDL_Renderer *renderer,
                       const struct nb_desktop *desktop);

#endif
