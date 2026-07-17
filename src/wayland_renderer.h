#ifndef NIXBENCH_WAYLAND_RENDERER_H
#define NIXBENCH_WAYLAND_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "wayland_server.h"
#include "window.h"

struct nb_wayland_renderer;

struct nb_wayland_renderer *nb_wayland_renderer_create(
    const struct nb_wayland_server *server);
void nb_wayland_renderer_destroy(struct nb_wayland_renderer *renderer);
void nb_wayland_renderer_reset(struct nb_wayland_renderer *renderer);
void nb_wayland_renderer_set_damage(
    struct nb_wayland_renderer *renderer,
    const struct nb_damage_region *damage);

/* Matches nb_window_content_render_callback. */
bool nb_wayland_render_content(SDL_Renderer *renderer,
                               nb_window_id id,
                               const struct nb_window *window,
                               struct nb_rect content_rect,
                               void *context);

#endif
