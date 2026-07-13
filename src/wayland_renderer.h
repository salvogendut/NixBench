#ifndef NIXBENCH_WAYLAND_RENDERER_H
#define NIXBENCH_WAYLAND_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "wayland_server.h"
#include "window.h"

/* Matches nb_window_content_render_callback. */
bool nb_wayland_render_content(SDL_Renderer *renderer,
                               nb_window_id id,
                               const struct nb_window *window,
                               struct nb_rect content_rect,
                               void *context);

#endif
