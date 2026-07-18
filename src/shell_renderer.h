#ifndef NIXBENCH_SHELL_RENDERER_H
#define NIXBENCH_SHELL_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "desktop_renderer.h"
#include "shell.h"

bool nb_shell_render(SDL_Renderer *renderer,
                     const struct nb_shell *shell,
                     struct nb_rect viewport,
                     const char *clock_text);
bool nb_shell_render_with_content(
    SDL_Renderer *renderer,
    const struct nb_shell *shell,
    struct nb_rect viewport,
    const char *clock_text,
    nb_window_content_render_callback render_content,
    void *context);
bool nb_shell_render_with_callbacks(
    SDL_Renderer *renderer,
    const struct nb_shell *shell,
    struct nb_rect viewport,
    const char *clock_text,
    nb_window_decoration_render_callback render_decoration,
    nb_window_content_render_callback render_content,
    void *context);

#endif
