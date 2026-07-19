#include "desktop_renderer.h"

#include <stddef.h>

#include "window_renderer.h"

static bool render_clipped_content(
    SDL_Renderer *renderer,
    nb_window_id id,
    const struct nb_window *window,
    nb_window_content_render_callback render_content,
    void *context)
{
    const struct nb_rect content = nb_window_content_rect(window);
    SDL_Rect previous_clip;
    SDL_Rect content_clip = {
        content.x,
        content.y,
        content.width > 0 ? content.width : 0,
        content.height > 0 ? content.height : 0
    };
    bool previous_clip_enabled;
    bool content_rendered;
    bool clip_restored;

    if (!SDL_GetRenderClipRect(renderer, &previous_clip)) {
        return false;
    }
    previous_clip_enabled = SDL_RenderClipEnabled(renderer);
    if (previous_clip_enabled) {
        SDL_Rect intersection;

        if (SDL_GetRectIntersection(&content_clip,
                                    &previous_clip,
                                    &intersection)) {
            content_clip = intersection;
        } else {
            content_clip.w = 0;
            content_clip.h = 0;
        }
    }

    if (!SDL_SetRenderClipRect(renderer, &content_clip)) {
        (void)SDL_SetRenderClipRect(
            renderer,
            previous_clip_enabled ? &previous_clip : NULL);
        return false;
    }

    content_rendered = render_content(renderer,
                                      id,
                                      window,
                                      content,
                                      context);
    clip_restored = SDL_SetRenderClipRect(
        renderer,
        previous_clip_enabled ? &previous_clip : NULL);
    return content_rendered && clip_restored;
}

bool nb_desktop_render_with_layer_callbacks(
    SDL_Renderer *renderer,
    const struct nb_desktop *desktop,
    nb_window_decoration_render_callback render_base,
    nb_window_decoration_render_callback render_decoration,
    nb_window_content_render_callback render_content,
    nb_window_decoration_render_callback render_overlay,
    void *context)
{
    size_t index;

    for (index = 0; index < nb_desktop_window_count(desktop); ++index) {
        const nb_window_id id = nb_desktop_window_id_at(desktop, index);
        const struct nb_window *window = nb_desktop_window_at(desktop, index);

        if (window == NULL || id == NB_WINDOW_ID_NONE) {
            return false;
        }
        if (!window->visible || window->minimized) {
            continue;
        }
        if (render_content == NULL) {
            if (!nb_window_render(renderer, window)) {
                return false;
            }
        } else if ((render_base != NULL
                        ? !render_base(renderer, id, window, context)
                        : !nb_window_render_base(renderer, window)) ||
                   (render_decoration != NULL && !window->fullscreen &&
                    !render_decoration(renderer, id, window, context)) ||
                   !render_clipped_content(renderer,
                                           id,
                                           window,
                                           render_content,
                                           context) ||
                   (render_overlay != NULL && !window->fullscreen &&
                    !render_overlay(renderer, id, window, context))) {
            return false;
        }
    }

    return true;
}

bool nb_desktop_render_with_callbacks(
    SDL_Renderer *renderer,
    const struct nb_desktop *desktop,
    nb_window_decoration_render_callback render_decoration,
    nb_window_content_render_callback render_content,
    void *context)
{
    return nb_desktop_render_with_layer_callbacks(renderer,
                                                   desktop,
                                                   NULL,
                                                   render_decoration,
                                                   render_content,
                                                   NULL,
                                                   context);
}

bool nb_desktop_render_with_content(
    SDL_Renderer *renderer,
    const struct nb_desktop *desktop,
    nb_window_content_render_callback render_content,
    void *context)
{
    return nb_desktop_render_with_callbacks(renderer,
                                             desktop,
                                             NULL,
                                             render_content,
                                             context);
}

bool nb_desktop_render(SDL_Renderer *renderer,
                       const struct nb_desktop *desktop)
{
    return nb_desktop_render_with_content(renderer, desktop, NULL, NULL);
}
