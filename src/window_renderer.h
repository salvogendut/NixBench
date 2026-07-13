#ifndef NIXBENCH_WINDOW_RENDERER_H
#define NIXBENCH_WINDOW_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "window.h"

/* Draw the frame, decorations, and content background without body content. */
bool nb_window_render_base(SDL_Renderer *renderer,
                           const struct nb_window *window);

/* Draw the generic placeholder body without redrawing the window base. */
bool nb_window_render_default_content(SDL_Renderer *renderer,
                                      const struct nb_window *window);

/* Draw the base window and its generic two-line placeholder body. */
bool nb_window_render(SDL_Renderer *renderer, const struct nb_window *window);

#endif
