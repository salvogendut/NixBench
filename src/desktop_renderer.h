#ifndef NIXBENCH_DESKTOP_RENDERER_H
#define NIXBENCH_DESKTOP_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "desktop.h"

/*
 * Called once for each visible window while rendering back to front. The
 * renderer is clipped to content_rect for the call; its prior clip is restored
 * afterwards. Returning false aborts the frame.
 */
typedef bool (*nb_window_content_render_callback)(
    SDL_Renderer *renderer,
    nb_window_id id,
    const struct nb_window *window,
    struct nb_rect content_rect,
    void *context);

/* Called after the native fallback base and before clipped client content. */
typedef bool (*nb_window_decoration_render_callback)(
    SDL_Renderer *renderer,
    nb_window_id id,
    const struct nb_window *window,
    void *context);

bool nb_desktop_render(SDL_Renderer *renderer,
                       const struct nb_desktop *desktop);
/* A NULL callback selects the same generic body as nb_desktop_render(). */
bool nb_desktop_render_with_content(
    SDL_Renderer *renderer,
    const struct nb_desktop *desktop,
    nb_window_content_render_callback render_content,
    void *context);
bool nb_desktop_render_with_callbacks(
    SDL_Renderer *renderer,
    const struct nb_desktop *desktop,
    nb_window_decoration_render_callback render_decoration,
    nb_window_content_render_callback render_content,
    void *context);

#endif
