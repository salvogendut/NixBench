#include "wayland_renderer.h"

#include <stdlib.h>
#include <string.h>

struct nb_wayland_texture_cache_entry {
    nb_window_id window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int width;
    int height;
    uint64_t revision;
};

struct nb_wayland_renderer {
    const struct nb_wayland_server *server;
    struct nb_damage_region damage;
    bool damage_valid;
    struct nb_wayland_texture_cache_entry
        entries[NB_WAYLAND_MAX_SURFACES];
};

static void clear_entry(struct nb_wayland_texture_cache_entry *entry)
{
    if (entry != NULL) {
        SDL_DestroyTexture(entry->texture);
        (void)memset(entry, 0, sizeof(*entry));
    }
}

struct nb_wayland_renderer *nb_wayland_renderer_create(
    const struct nb_wayland_server *server)
{
    struct nb_wayland_renderer *renderer;

    if (server == NULL) {
        return NULL;
    }
    renderer = calloc(1, sizeof(*renderer));
    if (renderer != NULL) {
        renderer->server = server;
    }
    return renderer;
}

void nb_wayland_renderer_reset(struct nb_wayland_renderer *renderer)
{
    size_t index;

    if (renderer == NULL) {
        return;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        clear_entry(&renderer->entries[index]);
    }
}

void nb_wayland_renderer_destroy(struct nb_wayland_renderer *renderer)
{
    if (renderer != NULL) {
        nb_wayland_renderer_reset(renderer);
        free(renderer);
    }
}

void nb_wayland_renderer_set_damage(
    struct nb_wayland_renderer *renderer,
    const struct nb_damage_region *damage)
{
    if (renderer == NULL) {
        return;
    }
    renderer->damage_valid = damage != NULL && !damage->full &&
                             damage->count != 0;
    if (renderer->damage_valid) {
        renderer->damage = *damage;
    } else {
        nb_damage_region_clear(&renderer->damage);
    }
}

static struct nb_wayland_texture_cache_entry *find_entry(
    struct nb_wayland_renderer *renderer,
    nb_window_id window)
{
    struct nb_wayland_texture_cache_entry *free_entry = NULL;
    size_t index;

    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_texture_cache_entry *entry =
            &renderer->entries[index];

        if (entry->window == window) {
            return entry;
        }
        if (free_entry == NULL && entry->window == NB_WINDOW_ID_NONE) {
            free_entry = entry;
        }
    }
    if (free_entry == NULL) {
        free_entry = &renderer->entries[
            (size_t)(window % NB_WAYLAND_MAX_SURFACES)];
        clear_entry(free_entry);
    }
    free_entry->window = window;
    return free_entry;
}

static bool intersection(struct nb_damage_rect left,
                         struct nb_damage_rect right,
                         struct nb_damage_rect *result)
{
    const int x = left.x > right.x ? left.x : right.x;
    const int y = left.y > right.y ? left.y : right.y;
    const int left_right = left.x + left.width;
    const int right_right = right.x + right.width;
    const int left_bottom = left.y + left.height;
    const int right_bottom = right.y + right.height;
    const int maximum_x = left_right < right_right
                              ? left_right
                              : right_right;
    const int maximum_y = left_bottom < right_bottom
                              ? left_bottom
                              : right_bottom;

    if (result == NULL || maximum_x <= x || maximum_y <= y) {
        return false;
    }
    *result = (struct nb_damage_rect){x,
                                      y,
                                      maximum_x - x,
                                      maximum_y - y};
    return true;
}

static bool update_texture(
    struct nb_wayland_renderer *renderer,
    struct nb_wayland_texture_cache_entry *entry,
    SDL_Renderer *sdl_renderer,
    const struct nb_wayland_surface_snapshot *snapshot,
    struct nb_damage_rect destination)
{
    bool created = false;
    bool updated = false;
    size_t index;

    if (entry->texture == NULL || entry->renderer != sdl_renderer ||
        entry->width != snapshot->width ||
        entry->height != snapshot->height) {
        const nb_window_id window = entry->window;

        clear_entry(entry);
        entry->window = window;
        entry->texture = SDL_CreateTexture(sdl_renderer,
                                            SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            snapshot->width,
                                            snapshot->height);
        if (entry->texture == NULL) {
            return false;
        }
        entry->renderer = sdl_renderer;
        entry->width = snapshot->width;
        entry->height = snapshot->height;
        created = true;
    }
    if (!SDL_SetTextureBlendMode(entry->texture, SDL_BLENDMODE_BLEND)) {
        return false;
    }
    if (created || entry->revision == 0 || !renderer->damage_valid) {
        updated = SDL_UpdateTexture(entry->texture,
                                    NULL,
                                    snapshot->pixels,
                                    snapshot->stride);
    } else if (entry->revision != snapshot->revision) {
        for (index = 0; index < renderer->damage.count; ++index) {
            struct nb_damage_rect dirty;

            if (intersection(renderer->damage.rects[index],
                             destination,
                             &dirty)) {
                const SDL_Rect source = {
                    snapshot->geometry_set
                        ? snapshot->geometry_x + dirty.x - destination.x
                        : dirty.x - destination.x,
                    snapshot->geometry_set
                        ? snapshot->geometry_y + dirty.y - destination.y
                        : dirty.y - destination.y,
                    dirty.width,
                    dirty.height
                };
                const uint32_t *pixels =
                    snapshot->pixels +
                    (size_t)source.y *
                        ((size_t)snapshot->stride / sizeof(uint32_t)) +
                    (size_t)source.x;

                if (!SDL_UpdateTexture(entry->texture,
                                       &source,
                                       pixels,
                                       snapshot->stride)) {
                    return false;
                }
                updated = true;
            }
        }
        if (!updated) {
            updated = SDL_UpdateTexture(entry->texture,
                                        NULL,
                                        snapshot->pixels,
                                        snapshot->stride);
        }
    } else {
        updated = true;
    }
    if (updated) {
        entry->revision = snapshot->revision;
    }
    return updated;
}

bool nb_wayland_render_content(SDL_Renderer *renderer,
                               nb_window_id id,
                               const struct nb_window *window,
                               struct nb_rect content_rect,
                               void *context)
{
    struct nb_wayland_renderer *wayland_renderer = context;
    struct nb_wayland_surface_snapshot snapshot;
    struct nb_wayland_texture_cache_entry *entry;
    struct nb_damage_rect destination;
    SDL_FRect full_destination;
    SDL_FRect full_source;
    int render_width;
    int render_height;
    size_t index;
    bool rendered = false;

    (void)window;
    if (renderer == NULL || wayland_renderer == NULL ||
        !nb_wayland_server_surface_snapshot(wayland_renderer->server,
                                            id,
                                            &snapshot) ||
        snapshot.pixels == NULL || snapshot.width <= 0 ||
        snapshot.height <= 0 || snapshot.stride <= 0) {
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
        return false;
    }
    destination = (struct nb_damage_rect){content_rect.x,
                                           content_rect.y,
                                           render_width,
                                           render_height};
    entry = find_entry(wayland_renderer, id);
    if (entry == NULL ||
        !update_texture(wayland_renderer,
                        entry,
                        renderer,
                        &snapshot,
                        destination)) {
        return false;
    }

    full_destination = (SDL_FRect){(float)destination.x,
                                    (float)destination.y,
                                    (float)destination.width,
                                    (float)destination.height};
    full_source = (SDL_FRect){snapshot.geometry_set
                                  ? (float)snapshot.geometry_x
                                  : 0.0f,
                              snapshot.geometry_set
                                  ? (float)snapshot.geometry_y
                                  : 0.0f,
                              (float)render_width,
                              (float)render_height};
    if (!wayland_renderer->damage_valid) {
        return SDL_RenderTexture(renderer,
                                 entry->texture,
                                 &full_source,
                                 &full_destination);
    }
    for (index = 0; index < wayland_renderer->damage.count; ++index) {
        struct nb_damage_rect dirty;

        if (intersection(wayland_renderer->damage.rects[index],
                         destination,
                         &dirty)) {
            const SDL_FRect dirty_destination = {
                (float)dirty.x,
                (float)dirty.y,
                (float)dirty.width,
                (float)dirty.height
            };
            const SDL_FRect dirty_source = {
                full_source.x + (float)(dirty.x - destination.x),
                full_source.y + (float)(dirty.y - destination.y),
                (float)dirty.width,
                (float)dirty.height
            };

            if (!SDL_RenderTexture(renderer,
                                   entry->texture,
                                   &dirty_source,
                                   &dirty_destination)) {
                return false;
            }
            rendered = true;
        }
    }
    return rendered;
}
