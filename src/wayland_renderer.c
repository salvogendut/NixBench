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
    struct nb_wayland_texture_cache_entry atlas_entry;
    struct nb_wayland_texture_cache_entry
        entries[NB_WAYLAND_MAX_SURFACES];
};

struct nb_cde_color {
    Uint8 red;
    Uint8 green;
    Uint8 blue;
};

static const struct nb_cde_color cde_face = {184, 178, 170};
static const struct nb_cde_color cde_light = {226, 221, 211};
static const struct nb_cde_color cde_shadow = {104, 97, 91};
static const struct nb_cde_color cde_dark = {49, 45, 42};
static const struct nb_cde_color cde_active = {197, 106, 37};
static const struct nb_cde_color cde_inactive = {96, 119, 137};
static const struct nb_cde_color cde_cyan = {79, 174, 177};
static const struct nb_cde_color cde_title_text = {246, 243, 237};
static const struct nb_cde_color cde_desktop = {97, 120, 138};

static bool cde_set_color(SDL_Renderer *renderer,
                          struct nb_cde_color color)
{
    return SDL_SetRenderDrawColor(renderer,
                                  color.red,
                                  color.green,
                                  color.blue,
                                  SDL_ALPHA_OPAQUE);
}

static bool cde_fill(SDL_Renderer *renderer,
                     struct nb_rect rect,
                     struct nb_cde_color color)
{
    const SDL_FRect destination = {
        (float)rect.x,
        (float)rect.y,
        (float)rect.width,
        (float)rect.height
    };

    return rect.width <= 0 || rect.height <= 0 ||
           (cde_set_color(renderer, color) &&
            SDL_RenderFillRect(renderer, &destination));
}

static bool cde_bevel(SDL_Renderer *renderer,
                      struct nb_rect rect,
                      bool pressed)
{
    const struct nb_cde_color top_left = pressed ? cde_shadow : cde_light;
    const struct nb_cde_color bottom_right = pressed ? cde_light : cde_shadow;
    const float left = (float)rect.x;
    const float top = (float)rect.y;
    const float right = (float)(rect.x + rect.width - 1);
    const float bottom = (float)(rect.y + rect.height - 1);

    return rect.width <= 0 || rect.height <= 0 ||
           (cde_set_color(renderer, top_left) &&
            SDL_RenderLine(renderer, left, bottom, left, top) &&
            SDL_RenderLine(renderer, left, top, right, top) &&
            cde_set_color(renderer, bottom_right) &&
            SDL_RenderLine(renderer, right, top, right, bottom) &&
            SDL_RenderLine(renderer, right, bottom, left, bottom));
}

static bool cde_gadget(SDL_Renderer *renderer,
                       struct nb_rect rect,
                       struct nb_cde_color face,
                       enum nb_window_hit hit,
                       const struct nb_window *window)
{
    const bool pressed =
        (hit == NB_WINDOW_HIT_CLOSE && window->close_pressed) ||
        (hit == NB_WINDOW_HIT_MINIMIZE &&
         window->pointer_mode == NB_WINDOW_POINTER_MINIMIZE) ||
        (hit == NB_WINDOW_HIT_MAXIMIZE &&
         window->pointer_mode == NB_WINDOW_POINTER_MAXIMIZE);
    const float inset = pressed ? 1.0f : 0.0f;
    const float left = (float)(rect.x + 4) + inset;
    const float top = (float)(rect.y + 4) + inset;
    const float right = (float)(rect.x + rect.width - 5) + inset;
    const float bottom = (float)(rect.y + rect.height - 5) + inset;

    if (rect.width <= 0 || rect.height <= 0) {
        return true;
    }
    if (!cde_fill(renderer, rect, face) ||
        !cde_bevel(renderer, rect, pressed) || rect.width < 8 ||
        rect.height < 8 || !cde_set_color(renderer, cde_dark)) {
        return rect.width < 8 || rect.height < 8;
    }
    if (hit == NB_WINDOW_HIT_MINIMIZE) {
        return SDL_RenderLine(renderer,
                              left,
                              (float)(rect.y + rect.height - 4) + inset,
                              right,
                              (float)(rect.y + rect.height - 4) + inset);
    }
    if (hit == NB_WINDOW_HIT_MAXIMIZE) {
        return SDL_RenderLine(renderer, left, top, right, top) &&
               SDL_RenderLine(renderer, right, top, right, bottom) &&
               SDL_RenderLine(renderer, right, bottom, left, bottom) &&
               SDL_RenderLine(renderer, left, bottom, left, top);
    }
    return SDL_RenderLine(renderer, left, top, right, bottom) &&
           SDL_RenderLine(renderer, right, top, left, bottom);
}

static bool render_cde_transition_decoration(
    SDL_Renderer *renderer,
    const struct nb_window *window)
{
    const struct nb_rect title = nb_window_title_rect(window);
    const struct nb_rect menu = nb_window_menu_rect(window);
    const struct nb_rect footer = nb_window_footer_rect(window);
    const struct nb_rect close = nb_window_close_rect(window);
    const struct nb_rect minimize = nb_window_minimize_rect(window);
    const struct nb_rect maximize = nb_window_maximize_rect(window);
    const struct nb_cde_color title_face =
        window->active ? cde_active : cde_inactive;
    char clipped[64];
    size_t length = strlen(window->title);
    size_t maximum;
    float text_x;
    float text_y;

    if (!cde_fill(renderer, window->frame, cde_face) ||
        !cde_bevel(renderer, window->frame, false) ||
        !cde_fill(renderer, title, title_face) ||
        !cde_bevel(renderer, title, false) ||
        !cde_gadget(renderer,
                    minimize,
                    title_face,
                    NB_WINDOW_HIT_MINIMIZE,
                    window) ||
        !cde_gadget(renderer,
                    maximize,
                    title_face,
                    NB_WINDOW_HIT_MAXIMIZE,
                    window) ||
        !cde_gadget(renderer,
                    close,
                    title_face,
                    NB_WINDOW_HIT_CLOSE,
                    window) ||
        !cde_fill(renderer, menu, cde_cyan) ||
        !cde_bevel(renderer, menu, false) ||
        !cde_fill(renderer, footer, cde_face) ||
        !cde_bevel(renderer, footer, false)) {
        return false;
    }
    if (window->title[0] == '\0' || title.width < 32) {
        return true;
    }
    maximum = (size_t)(title.width > 100 ? (title.width - 100) / 8 : 0);
    if (maximum >= sizeof(clipped)) {
        maximum = sizeof(clipped) - 1;
    }
    if (length > maximum) {
        length = maximum;
    }
    memcpy(clipped, window->title, length);
    clipped[length] = '\0';
    text_x = (float)(title.x + (title.width - (int)length * 8) / 2);
    text_y = (float)(title.y + (title.height - 8) / 2);
    return length == 0 ||
           (cde_set_color(renderer, cde_title_text) &&
            SDL_RenderDebugText(renderer, text_x, text_y, clipped));
}

static bool use_cde_transition(
    const struct nb_wayland_renderer *renderer)
{
    return renderer != NULL &&
           nb_wayland_server_html_theme_is(renderer->server, "cde");
}

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
    clear_entry(&renderer->atlas_entry);
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

static bool update_atlas_texture(
    struct nb_wayland_texture_cache_entry *entry,
    SDL_Renderer *renderer,
    const struct nb_wayland_surface_snapshot *snapshot)
{
    if (entry->texture == NULL || entry->renderer != renderer ||
        entry->width != snapshot->width ||
        entry->height != snapshot->height) {
        clear_entry(entry);
        entry->texture = SDL_CreateTexture(renderer,
                                           SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING,
                                           snapshot->width,
                                           snapshot->height);
        if (entry->texture == NULL) {
            return false;
        }
        entry->renderer = renderer;
        entry->width = snapshot->width;
        entry->height = snapshot->height;
    }
    if (!SDL_SetTextureBlendMode(entry->texture, SDL_BLENDMODE_BLEND)) {
        return false;
    }
    if (entry->revision != snapshot->revision) {
        if (!SDL_UpdateTexture(entry->texture,
                               NULL,
                               snapshot->pixels,
                               snapshot->stride)) {
            return false;
        }
        entry->revision = snapshot->revision;
    }
    return true;
}

bool nb_wayland_render_desktop(SDL_Renderer *renderer,
                               struct nb_rect viewport,
                               void *context)
{
    struct nb_wayland_renderer *wayland_renderer = context;
    struct nb_wayland_html_theme_snapshot snapshot;
    const struct nb_theme_tile *tile;
    SDL_FRect source;
    SDL_FRect destination;

    if (renderer == NULL || wayland_renderer == NULL || viewport.width <= 0 ||
        viewport.height <= 0) {
        return false;
    }
    if (!nb_wayland_server_html_theme_snapshot(wayland_renderer->server,
                                                &snapshot)) {
        return !use_cde_transition(wayland_renderer) ||
               cde_fill(renderer, viewport, cde_desktop);
    }
    tile = nb_theme_atlas_find_tile(snapshot.layout,
                                    NB_THEME_TILE_DESKTOP,
                                    0);
    if (tile == NULL || tile->atlas_rect.width != viewport.width ||
        tile->atlas_rect.height != viewport.height) {
        return !use_cde_transition(wayland_renderer) ||
               cde_fill(renderer, viewport, cde_desktop);
    }
    if (!update_atlas_texture(&wayland_renderer->atlas_entry,
                              renderer,
                              &snapshot.surface)) {
        return false;
    }
    source = (SDL_FRect){(float)tile->atlas_rect.x,
                         (float)tile->atlas_rect.y,
                         (float)tile->atlas_rect.width,
                         (float)tile->atlas_rect.height};
    destination = (SDL_FRect){(float)viewport.x,
                              (float)viewport.y,
                              (float)viewport.width,
                              (float)viewport.height};
    return SDL_RenderTexture(renderer,
                             wayland_renderer->atlas_entry.texture,
                             &source,
                             &destination);
}

bool nb_wayland_render_decoration(SDL_Renderer *renderer,
                                  nb_window_id id,
                                  const struct nb_window *window,
                                  void *context)
{
    struct nb_wayland_renderer *wayland_renderer = context;
    struct nb_wayland_html_theme_snapshot snapshot;
    const struct nb_theme_tile *tile;
    SDL_FRect source;
    SDL_FRect destination;

    if (renderer == NULL || window == NULL || wayland_renderer == NULL ||
        id == NB_WINDOW_ID_NONE) {
        return false;
    }
    if (!nb_wayland_server_html_theme_snapshot(wayland_renderer->server,
                                                &snapshot)) {
        return !use_cde_transition(wayland_renderer) ||
               render_cde_transition_decoration(renderer, window);
    }
    tile = nb_theme_atlas_find_tile(snapshot.layout,
                                    NB_THEME_TILE_WINDOW,
                                    (uint64_t)id);
    if (tile == NULL || tile->atlas_rect.width != window->frame.width ||
        tile->atlas_rect.height != window->frame.height) {
        return !use_cde_transition(wayland_renderer) ||
               render_cde_transition_decoration(renderer, window);
    }
    if (!update_atlas_texture(&wayland_renderer->atlas_entry,
                              renderer,
                              &snapshot.surface)) {
        return false;
    }
    source = (SDL_FRect){(float)tile->atlas_rect.x,
                         (float)tile->atlas_rect.y,
                         (float)tile->atlas_rect.width,
                         (float)tile->atlas_rect.height};
    destination = (SDL_FRect){(float)window->frame.x,
                              (float)window->frame.y,
                              (float)window->frame.width,
                              (float)window->frame.height};
    return SDL_RenderTexture(renderer,
                             wayland_renderer->atlas_entry.texture,
                             &source,
                             &destination);
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
