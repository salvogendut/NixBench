#include "wayland_renderer.h"

bool nb_wayland_render_content(SDL_Renderer *renderer,
                               nb_window_id id,
                               const struct nb_window *window,
                               struct nb_rect content_rect,
                               void *context)
{
    const struct nb_wayland_server *server = context;
    struct nb_wayland_surface_snapshot snapshot;
    SDL_Texture *texture;
    SDL_FRect destination;
    SDL_FRect source;
    int render_width;
    int render_height;
    bool rendered;

    (void)window;
    if (renderer == NULL || server == NULL ||
        !nb_wayland_server_surface_snapshot(server, id, &snapshot) ||
        snapshot.pixels == NULL || snapshot.width <= 0 ||
        snapshot.height <= 0 || snapshot.stride <= 0) {
        return false;
    }

    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STATIC,
                                snapshot.width,
                                snapshot.height);
    if (texture == NULL) {
        return false;
    }

    if (snapshot.geometry_set && snapshot.geometry_width > 0 &&
        snapshot.geometry_height > 0) {
        render_width = snapshot.geometry_width;
        render_height = snapshot.geometry_height;
    } else {
        render_width = snapshot.width < content_rect.width
                           ? snapshot.width
                           : content_rect.width;
        render_height = snapshot.height < content_rect.height
                            ? snapshot.height
                            : content_rect.height;
    }
    if (render_width <= 0 || render_height <= 0) {
        SDL_DestroyTexture(texture);
        return false;
    }
    destination.x = (float)content_rect.x;
    destination.y = (float)content_rect.y;
    destination.w = (float)render_width;
    destination.h = (float)render_height;
    source.x = snapshot.geometry_set ? (float)snapshot.geometry_x : 0.0f;
    source.y = snapshot.geometry_set ? (float)snapshot.geometry_y : 0.0f;
    source.w = (float)render_width;
    source.h = (float)render_height;

    rendered = SDL_SetTextureBlendMode(texture,
                                       SDL_BLENDMODE_BLEND) &&
               SDL_UpdateTexture(texture,
                                 NULL,
                                 snapshot.pixels,
                                 snapshot.stride) &&
               SDL_RenderTexture(renderer,
                                 texture,
                                 &source,
                                 &destination);
    SDL_DestroyTexture(texture);
    return rendered;
}
