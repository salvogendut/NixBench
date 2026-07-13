#ifndef NIXBENCH_WINDOW_RENDERER_H
#define NIXBENCH_WINDOW_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "window.h"

bool nb_window_render(SDL_Renderer *renderer, const struct nb_window *window);

#endif
