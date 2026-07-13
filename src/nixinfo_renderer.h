#ifndef NIXBENCH_NIXINFO_RENDERER_H
#define NIXBENCH_NIXINFO_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "nixinfo.h"
#include "window.h"

/* Matches nb_window_content_render_callback; context is struct nb_nixinfo *. */
bool nb_nixinfo_render_content(SDL_Renderer *renderer,
                               nb_window_id id,
                               const struct nb_window *window,
                               struct nb_rect content_rect,
                               void *context);

#endif
