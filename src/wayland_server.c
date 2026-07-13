#include "wayland_server.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "xdg-shell-server-protocol.h"

enum {
    NB_WAYLAND_COMPOSITOR_VERSION = 4,
    NB_WAYLAND_XDG_SHELL_VERSION = 1,
    NB_WAYLAND_MAX_FRAME_CALLBACKS = 16,
    NB_WAYLAND_INITIAL_CONTENT_WIDTH = 560,
    NB_WAYLAND_INITIAL_CONTENT_HEIGHT = 300,
    NB_WAYLAND_INITIAL_X = 120,
    NB_WAYLAND_INITIAL_Y = 78,
    NB_WAYLAND_CASCADE = 28,
    NB_WAYLAND_CASCADE_COUNT = 8
};

struct nb_wayland_server;

struct nb_wayland_surface {
    bool occupied;
    bool role_assigned;
    bool configure_sent;
    bool configured;
    struct nb_wayland_server *server;
    struct wl_resource *surface_resource;
    struct wl_resource *xdg_surface_resource;
    struct wl_resource *toplevel_resource;
    struct wl_resource *wm_base_resource;
    struct wl_resource *pending_buffer_resource;
    struct wl_listener pending_buffer_destroy;
    bool pending_buffer_listener_attached;
    bool pending_attach;
    bool committed_once;
    uint32_t *pixels;
    int width;
    int height;
    uint64_t revision;
    uint32_t configure_serial;
    struct wl_resource
        *pending_frames[NB_WAYLAND_MAX_FRAME_CALLBACKS];
    size_t pending_frame_count;
    struct wl_resource *ready_frames[NB_WAYLAND_MAX_FRAME_CALLBACKS];
    size_t ready_frame_count;
    nb_window_id window;
    char title[NB_WINDOW_TITLE_CAPACITY];
    char app_id[NB_WINDOW_TITLE_CAPACITY];
};

struct nb_wayland_server {
    struct nb_shell *shell;
    nb_menu_source_id menu_source;
    const struct nb_menu_model *menu_model;
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wl_global *compositor_global;
    struct wl_global *xdg_wm_base_global;
    struct nb_wayland_surface surfaces[NB_WAYLAND_MAX_SURFACES];
    unsigned int next_window_position;
    char display_name[NB_WAYLAND_DISPLAY_NAME_CAPACITY];
};

static const struct wl_surface_interface surface_implementation;
static const struct xdg_surface_interface xdg_surface_implementation;
static const struct xdg_toplevel_interface toplevel_implementation;

static void copy_text(char *destination,
                      size_t capacity,
                      const char *source)
{
    size_t index = 0;

    if (capacity == 0) {
        return;
    }
    if (source != NULL) {
        while (index + 1 < capacity && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static int protocol_version(uint32_t requested, int maximum)
{
    if (requested > (uint32_t)maximum) {
        return maximum;
    }
    return (int)requested;
}

static struct nb_wayland_surface *find_free_surface(
    struct nb_wayland_server *server)
{
    size_t index;

    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        if (!server->surfaces[index].occupied) {
            return &server->surfaces[index];
        }
    }
    return NULL;
}

static struct nb_wayland_surface *find_surface_by_window(
    struct nb_wayland_server *server,
    nb_window_id window)
{
    size_t index;

    if (window == NB_WINDOW_ID_NONE) {
        return NULL;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *surface = &server->surfaces[index];

        if (surface->occupied && surface->window == window) {
            return surface;
        }
    }
    return NULL;
}

static const struct nb_wayland_surface *find_surface_by_window_const(
    const struct nb_wayland_server *server,
    nb_window_id window)
{
    size_t index;

    if (window == NB_WINDOW_ID_NONE) {
        return NULL;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        const struct nb_wayland_surface *surface =
            &server->surfaces[index];

        if (surface->occupied && surface->window == window) {
            return surface;
        }
    }
    return NULL;
}

static void unmap_surface(struct nb_wayland_surface *surface)
{
    if (surface->window != NB_WINDOW_ID_NONE) {
        (void)nb_shell_destroy_window(surface->server->shell,
                                      surface->window);
        surface->window = NB_WINDOW_ID_NONE;
    }
}

static void remove_frame_resource(struct wl_resource **frames,
                                  size_t *frame_count,
                                  struct wl_resource *resource)
{
    size_t index;

    for (index = 0; index < *frame_count; ++index) {
        if (frames[index] == resource) {
            size_t following;

            for (following = index + 1; following < *frame_count;
                 ++following) {
                frames[following - 1] = frames[following];
            }
            --*frame_count;
            frames[*frame_count] = NULL;
            return;
        }
    }
}

static void frame_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    if (surface == NULL || !surface->occupied) {
        return;
    }
    remove_frame_resource(surface->pending_frames,
                          &surface->pending_frame_count,
                          resource);
    remove_frame_resource(surface->ready_frames,
                          &surface->ready_frame_count,
                          resource);
}

static void destroy_frame_resources(struct nb_wayland_surface *surface)
{
    while (surface->pending_frame_count > 0) {
        wl_resource_destroy(surface->pending_frames[0]);
    }
    while (surface->ready_frame_count > 0) {
        wl_resource_destroy(surface->ready_frames[0]);
    }
}

static void complete_ready_frames(struct nb_wayland_surface *surface,
                                  uint32_t milliseconds)
{
    while (surface->ready_frame_count > 0) {
        struct wl_resource *callback = surface->ready_frames[0];

        wl_callback_send_done(callback, milliseconds);
        wl_resource_destroy(callback);
    }
}

static void move_pending_frames_to_ready(
    struct nb_wayland_surface *surface)
{
    while (surface->pending_frame_count > 0 &&
           surface->ready_frame_count < NB_WAYLAND_MAX_FRAME_CALLBACKS) {
        struct wl_resource *callback = surface->pending_frames[0];

        remove_frame_resource(surface->pending_frames,
                              &surface->pending_frame_count,
                              callback);
        surface->ready_frames[surface->ready_frame_count++] = callback;
    }
}

static void detach_pending_buffer_listener(
    struct nb_wayland_surface *surface)
{
    if (!surface->pending_buffer_listener_attached) {
        return;
    }
    wl_list_remove(&surface->pending_buffer_destroy.link);
    wl_list_init(&surface->pending_buffer_destroy.link);
    surface->pending_buffer_listener_attached = false;
}

static void pending_buffer_destroyed(struct wl_listener *listener,
                                     void *data)
{
    struct nb_wayland_surface *surface =
        (struct nb_wayland_surface *)
            ((char *)listener -
             offsetof(struct nb_wayland_surface,
                      pending_buffer_destroy));

    (void)data;
    surface->pending_buffer_resource = NULL;
    surface->pending_buffer_listener_attached = false;
    wl_list_init(&surface->pending_buffer_destroy.link);
}

static void clear_pending_attach(struct nb_wayland_surface *surface)
{
    detach_pending_buffer_listener(surface);
    surface->pending_buffer_resource = NULL;
    surface->pending_attach = false;
}

static void maybe_release_surface_slot(struct nb_wayland_surface *surface)
{
    if (surface->surface_resource != NULL ||
        surface->xdg_surface_resource != NULL ||
        surface->toplevel_resource != NULL) {
        return;
    }

    unmap_surface(surface);
    clear_pending_attach(surface);
    destroy_frame_resources(surface);
    free(surface->pixels);
    memset(surface, 0, sizeof(*surface));
}

static bool copy_shm_buffer(struct nb_wayland_surface *surface,
                            struct wl_resource *buffer_resource,
                            uint32_t **copied_pixels,
                            int *copied_width,
                            int *copied_height)
{
    struct wl_shm_buffer *buffer =
        wl_shm_buffer_get(buffer_resource);
    uint32_t format;
    int width;
    int height;
    int stride;
    size_t row_bytes;
    size_t pixel_count;
    uint32_t *pixels;
    const unsigned char *source;
    int y;

    if (buffer == NULL) {
        wl_client_post_implementation_error(
            wl_resource_get_client(surface->surface_resource),
            "NixBench currently accepts only wl_shm buffers");
        return false;
    }

    format = wl_shm_buffer_get_format(buffer);
    width = wl_shm_buffer_get_width(buffer);
    height = wl_shm_buffer_get_height(buffer);
    stride = wl_shm_buffer_get_stride(buffer);
    if ((format != WL_SHM_FORMAT_ARGB8888 &&
         format != WL_SHM_FORMAT_XRGB8888) ||
        width <= 0 || height <= 0 || width > INT_MAX / 4 ||
        height > INT_MAX / 4 ||
        stride < width * 4 ||
        (size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height >
            SIZE_MAX / sizeof(uint32_t)) {
        wl_client_post_implementation_error(
            wl_resource_get_client(surface->surface_resource),
            "invalid or unsupported wl_shm buffer");
        return false;
    }

    row_bytes = (size_t)width * sizeof(uint32_t);
    if ((size_t)(height - 1) >
        (SIZE_MAX - row_bytes) / (size_t)stride) {
        wl_client_post_implementation_error(
            wl_resource_get_client(surface->surface_resource),
            "wl_shm buffer address range is too large");
        return false;
    }

    pixel_count = (size_t)width * (size_t)height;
    pixels = malloc(pixel_count * sizeof(*pixels));
    if (pixels == NULL) {
        wl_resource_post_no_memory(surface->surface_resource);
        return false;
    }

    wl_shm_buffer_begin_access(buffer);
    source = wl_shm_buffer_get_data(buffer);
    for (y = 0; y < height; ++y) {
        const unsigned char *row = source + ((size_t)y * (size_t)stride);
        int x;

        for (x = 0; x < width; ++x) {
            uint32_t pixel;

            memcpy(&pixel, row + ((size_t)x * sizeof(pixel)),
                   sizeof(pixel));
            if (format == WL_SHM_FORMAT_XRGB8888) {
                pixel |= UINT32_C(0xff000000);
            }
            pixels[(size_t)y * (size_t)width + (size_t)x] = pixel;
        }
    }
    wl_shm_buffer_end_access(buffer);

    *copied_pixels = pixels;
    *copied_width = width;
    *copied_height = height;
    return true;
}

static void map_surface(struct nb_wayland_surface *surface)
{
    const int cascade =
        (int)(surface->server->next_window_position %
              (unsigned int)NB_WAYLAND_CASCADE_COUNT) *
        NB_WAYLAND_CASCADE;
    struct nb_rect frame;
    const char *title;

    if (surface->toplevel_resource == NULL || surface->pixels == NULL ||
        surface->window != NB_WINDOW_ID_NONE) {
        return;
    }

    frame.x = NB_WAYLAND_INITIAL_X + cascade;
    frame.y = NB_WAYLAND_INITIAL_Y + cascade;
    frame.width = surface->width + (2 * NB_WINDOW_BORDER_WIDTH);
    frame.height = surface->height + (2 * NB_WINDOW_BORDER_WIDTH) +
                   NB_WINDOW_TITLE_HEIGHT + NB_WINDOW_FOOTER_HEIGHT;
    title = surface->title[0] != '\0'
                ? surface->title
                : (surface->app_id[0] != '\0'
                       ? surface->app_id
                       : "Wayland Application");

    surface->window = nb_shell_open_window(surface->server->shell,
                                            title,
                                            frame,
                                            surface->server->menu_source,
                                            surface->server->menu_model);
    if (surface->window != NB_WINDOW_ID_NONE) {
        ++surface->server->next_window_position;
    }
}

static void send_initial_configure(struct nb_wayland_surface *surface)
{
    struct wl_array states;
    uint32_t serial;

    if (surface->toplevel_resource == NULL ||
        surface->xdg_surface_resource == NULL) {
        return;
    }

    wl_array_init(&states);
    xdg_toplevel_send_configure(surface->toplevel_resource,
                                NB_WAYLAND_INITIAL_CONTENT_WIDTH,
                                NB_WAYLAND_INITIAL_CONTENT_HEIGHT,
                                &states);
    wl_array_release(&states);
    serial = wl_display_next_serial(surface->server->display);
    surface->configure_serial = serial;
    surface->configure_sent = true;
    surface->configured = false;
    xdg_surface_send_configure(surface->xdg_surface_resource, serial);
}

static void surface_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    if (surface == NULL || !surface->occupied) {
        return;
    }
    unmap_surface(surface);
    clear_pending_attach(surface);
    destroy_frame_resources(surface);
    free(surface->pixels);
    surface->pixels = NULL;
    surface->surface_resource = NULL;
    maybe_release_surface_slot(surface);
}

static void surface_destroy(struct wl_client *client,
                            struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (surface->xdg_surface_resource != NULL ||
        surface->toplevel_resource != NULL) {
        wl_resource_post_error(resource,
                               WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                               "destroy xdg role objects first");
        return;
    }
    wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *buffer,
                           int32_t x,
                           int32_t y)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    (void)x;
    (void)y;
    if (buffer == NULL) {
        clear_pending_attach(surface);
        surface->pending_attach = true;
        return;
    }
    clear_pending_attach(surface);
    surface->pending_buffer_resource = buffer;
    surface->pending_buffer_destroy.notify = pending_buffer_destroyed;
    wl_resource_add_destroy_listener(buffer,
                                     &surface->pending_buffer_destroy);
    surface->pending_buffer_listener_attached = true;
    surface->pending_attach = true;
}

static void surface_damage(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x,
                           int32_t y,
                           int32_t width,
                           int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void surface_frame(struct wl_client *client,
                          struct wl_resource *resource,
                          uint32_t callback_id)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    struct wl_resource *callback;

    if (surface->pending_frame_count + surface->ready_frame_count >=
        NB_WAYLAND_MAX_FRAME_CALLBACKS) {
        wl_client_post_no_memory(client);
        return;
    }
    callback = wl_resource_create(client,
                                  &wl_callback_interface,
                                  1,
                                  callback_id);
    if (callback == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(callback,
                                   NULL,
                                   surface,
                                   frame_resource_destroyed);
    surface->pending_frames[surface->pending_frame_count++] = callback;
}

static void surface_set_region(struct wl_client *client,
                               struct wl_resource *resource,
                               struct wl_resource *region)
{
    (void)client;
    (void)resource;
    (void)region;
}

static void surface_commit(struct wl_client *client,
                           struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    surface->committed_once = true;
    if (surface->xdg_surface_resource != NULL &&
        surface->toplevel_resource == NULL) {
        wl_resource_post_error(surface->xdg_surface_resource,
                               XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "xdg_surface has no role");
        return;
    }

    if (surface->toplevel_resource != NULL &&
        !surface->configure_sent) {
        if (surface->pending_attach &&
            surface->pending_buffer_resource != NULL) {
            wl_resource_post_error(surface->xdg_surface_resource,
                                   XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                                   "buffer committed before initial "
                                   "configure");
            return;
        }
        clear_pending_attach(surface);
        send_initial_configure(surface);
        move_pending_frames_to_ready(surface);
        return;
    }

    if (surface->pending_attach &&
        surface->pending_buffer_resource != NULL &&
        surface->toplevel_resource != NULL && !surface->configured) {
        wl_resource_post_error(surface->xdg_surface_resource,
                               XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                               "buffer committed before configure ack");
        return;
    }

    if (surface->pending_attach) {
        struct wl_resource *buffer =
            surface->pending_buffer_resource;
        uint32_t *new_pixels = NULL;
        int new_width = 0;
        int new_height = 0;

        if (buffer != NULL &&
            !copy_shm_buffer(surface,
                             buffer,
                             &new_pixels,
                             &new_width,
                             &new_height)) {
            return;
        }

        detach_pending_buffer_listener(surface);
        free(surface->pixels);
        surface->pixels = new_pixels;
        surface->width = new_width;
        surface->height = new_height;
        surface->pending_buffer_resource = NULL;
        surface->pending_attach = false;
        if (buffer != NULL) {
            wl_buffer_send_release(buffer);
        }

        if (surface->pixels == NULL) {
            unmap_surface(surface);
            surface->configure_sent = false;
            surface->configured = false;
            surface->configure_serial = 0;
        } else {
            ++surface->revision;
            if (surface->revision == 0) {
                surface->revision = 1;
            }
            map_surface(surface);
        }
    } else if (surface->pixels != NULL) {
        map_surface(surface);
    }

    move_pending_frames_to_ready(surface);
    (void)client;
}

static void surface_set_buffer_transform(struct wl_client *client,
                                         struct wl_resource *resource,
                                         int32_t transform)
{
    (void)client;
    (void)resource;
    (void)transform;
}

static void surface_set_buffer_scale(struct wl_client *client,
                                     struct wl_resource *resource,
                                     int32_t scale)
{
    if (scale <= 0) {
        wl_resource_post_error(resource,
                               WL_SURFACE_ERROR_INVALID_SCALE,
                               "buffer scale must be positive");
    }
    (void)client;
}

static void surface_offset(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x,
                           int32_t y)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
}

static const struct wl_surface_interface surface_implementation = {
    .destroy = surface_destroy,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame,
    .set_opaque_region = surface_set_region,
    .set_input_region = surface_set_region,
    .commit = surface_commit,
    .set_buffer_transform = surface_set_buffer_transform,
    .set_buffer_scale = surface_set_buffer_scale,
    .damage_buffer = surface_damage,
    .offset = surface_offset
};

static void region_destroy(struct wl_client *client,
                           struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void region_change(struct wl_client *client,
                          struct wl_resource *resource,
                          int32_t x,
                          int32_t y,
                          int32_t width,
                          int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static const struct wl_region_interface region_implementation = {
    .destroy = region_destroy,
    .add = region_change,
    .subtract = region_change
};

static void compositor_create_surface(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t id)
{
    struct nb_wayland_server *server =
        wl_resource_get_user_data(resource);
    struct nb_wayland_surface *surface = find_free_surface(server);
    int version = protocol_version(
        (uint32_t)wl_resource_get_version(resource),
        NB_WAYLAND_COMPOSITOR_VERSION);

    if (surface == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    memset(surface, 0, sizeof(*surface));
    surface->occupied = true;
    surface->server = server;
    surface->window = NB_WINDOW_ID_NONE;
    wl_list_init(&surface->pending_buffer_destroy.link);
    surface->surface_resource =
        wl_resource_create(client, &wl_surface_interface, version, id);
    if (surface->surface_resource == NULL) {
        memset(surface, 0, sizeof(*surface));
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(surface->surface_resource,
                                   &surface_implementation,
                                   surface,
                                   surface_resource_destroyed);
}

static void compositor_create_region(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id)
{
    struct wl_resource *region =
        wl_resource_create(client, &wl_region_interface, 1, id);

    (void)resource;
    if (region == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(region,
                                   &region_implementation,
                                   NULL,
                                   NULL);
}

static const struct wl_compositor_interface compositor_implementation = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region
};

static void bind_compositor(struct wl_client *client,
                            void *data,
                            uint32_t version,
                            uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client,
        &wl_compositor_interface,
        protocol_version(version, NB_WAYLAND_COMPOSITOR_VERSION),
        id);

    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &compositor_implementation,
                                   data,
                                   NULL);
}

static void positioner_destroy(struct wl_client *client,
                               struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void positioner_set_size(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t width,
                                int32_t height)
{
    (void)client;
    (void)resource;
    (void)width;
    (void)height;
}

static void positioner_set_anchor_rect(struct wl_client *client,
                                       struct wl_resource *resource,
                                       int32_t x,
                                       int32_t y,
                                       int32_t width,
                                       int32_t height)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void positioner_set_enum(struct wl_client *client,
                                struct wl_resource *resource,
                                uint32_t value)
{
    (void)client;
    (void)resource;
    (void)value;
}

static void positioner_set_offset(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t x,
                                  int32_t y)
{
    (void)client;
    (void)resource;
    (void)x;
    (void)y;
}

static void positioner_set_reactive(struct wl_client *client,
                                    struct wl_resource *resource)
{
    (void)client;
    (void)resource;
}

static void positioner_set_parent_size(struct wl_client *client,
                                       struct wl_resource *resource,
                                       int32_t width,
                                       int32_t height)
{
    (void)client;
    (void)resource;
    (void)width;
    (void)height;
}

static void positioner_set_parent_configure(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)serial;
}

static const struct xdg_positioner_interface positioner_implementation = {
    .destroy = positioner_destroy,
    .set_size = positioner_set_size,
    .set_anchor_rect = positioner_set_anchor_rect,
    .set_anchor = positioner_set_enum,
    .set_gravity = positioner_set_enum,
    .set_constraint_adjustment = positioner_set_enum,
    .set_offset = positioner_set_offset,
    .set_reactive = positioner_set_reactive,
    .set_parent_size = positioner_set_parent_size,
    .set_parent_configure = positioner_set_parent_configure
};

static void xdg_toplevel_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    if (surface == NULL || !surface->occupied) {
        return;
    }
    unmap_surface(surface);
    clear_pending_attach(surface);
    free(surface->pixels);
    surface->pixels = NULL;
    surface->width = 0;
    surface->height = 0;
    surface->configure_sent = false;
    surface->configured = false;
    surface->configure_serial = 0;
    surface->toplevel_resource = NULL;
    maybe_release_surface_slot(surface);
}

static void xdg_surface_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    if (surface == NULL || !surface->occupied) {
        return;
    }
    surface->xdg_surface_resource = NULL;
    surface->wm_base_resource = NULL;
    maybe_release_surface_slot(surface);
}

static void xdg_surface_destroy(struct wl_client *client,
                                struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (surface->toplevel_resource != NULL) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                               "destroy xdg_toplevel first");
        return;
    }
    wl_resource_destroy(resource);
}

static void xdg_toplevel_destroy(struct wl_client *client,
                                 struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void xdg_toplevel_set_parent(struct wl_client *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *parent)
{
    (void)client;
    (void)resource;
    (void)parent;
}

static void xdg_toplevel_set_title(struct wl_client *client,
                                   struct wl_resource *resource,
                                   const char *title)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    copy_text(surface->title, sizeof(surface->title), title);
}

static void xdg_toplevel_set_app_id(struct wl_client *client,
                                    struct wl_resource *resource,
                                    const char *app_id)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    copy_text(surface->app_id, sizeof(surface->app_id), app_id);
}

static void xdg_toplevel_show_window_menu(
    struct wl_client *client,
    struct wl_resource *resource,
    struct wl_resource *seat,
    uint32_t serial,
    int32_t x,
    int32_t y)
{
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
    (void)x;
    (void)y;
}

static void xdg_toplevel_move(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *seat,
                              uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
}

static void xdg_toplevel_resize(struct wl_client *client,
                                struct wl_resource *resource,
                                struct wl_resource *seat,
                                uint32_t serial,
                                uint32_t edges)
{
    (void)client;
    (void)resource;
    (void)seat;
    (void)serial;
    (void)edges;
}

static void xdg_toplevel_set_size(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t width,
                                  int32_t height)
{
    if (width < 0 || height < 0) {
        wl_resource_post_error(resource,
                               XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                               "negative toplevel size");
    }
    (void)client;
}

static void xdg_toplevel_request_state(struct wl_client *client,
                                       struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (surface->configure_sent && surface->configured) {
        send_initial_configure(surface);
    }
}

static void xdg_toplevel_set_fullscreen(
    struct wl_client *client,
    struct wl_resource *resource,
    struct wl_resource *output)
{
    (void)output;
    xdg_toplevel_request_state(client, resource);
}

static const struct xdg_toplevel_interface toplevel_implementation = {
    .destroy = xdg_toplevel_destroy,
    .set_parent = xdg_toplevel_set_parent,
    .set_title = xdg_toplevel_set_title,
    .set_app_id = xdg_toplevel_set_app_id,
    .show_window_menu = xdg_toplevel_show_window_menu,
    .move = xdg_toplevel_move,
    .resize = xdg_toplevel_resize,
    .set_max_size = xdg_toplevel_set_size,
    .set_min_size = xdg_toplevel_set_size,
    .set_maximized = xdg_toplevel_request_state,
    .unset_maximized = xdg_toplevel_request_state,
    .set_fullscreen = xdg_toplevel_set_fullscreen,
    .unset_fullscreen = xdg_toplevel_request_state,
    .set_minimized = xdg_toplevel_request_state
};

static void xdg_surface_get_toplevel(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    if (surface->toplevel_resource != NULL) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                               "wl_surface already has an xdg role");
        return;
    }
    surface->toplevel_resource = wl_resource_create(
        client,
        &xdg_toplevel_interface,
        wl_resource_get_version(resource),
        id);
    if (surface->toplevel_resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->role_assigned = true;
    surface->configure_sent = false;
    surface->configured = false;
    surface->configure_serial = 0;
    wl_resource_set_implementation(surface->toplevel_resource,
                                   &toplevel_implementation,
                                   surface,
                                   xdg_toplevel_resource_destroyed);
}

static void xdg_surface_get_popup(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id,
                                  struct wl_resource *parent,
                                  struct wl_resource *positioner)
{
    (void)id;
    (void)parent;
    (void)positioner;
    wl_client_post_implementation_error(
        client,
        "NixBench does not implement xdg_popup yet");
    (void)resource;
}

static void xdg_surface_set_window_geometry(
    struct wl_client *client,
    struct wl_resource *resource,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (surface->toplevel_resource == NULL) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "xdg_surface has no role");
        return;
    }
    (void)x;
    (void)y;
    if (width <= 0 || height <= 0) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_INVALID_SIZE,
                               "window geometry must be positive");
    }
}

static void xdg_surface_ack_configure(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t serial)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (surface->toplevel_resource == NULL) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "xdg_surface has no role");
        return;
    }
    if (!surface->configure_sent || surface->configure_serial == 0 ||
        serial != surface->configure_serial) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_INVALID_SERIAL,
                               "unknown configure serial");
        return;
    }
    surface->configured = true;
    surface->configure_serial = 0;
}

static const struct xdg_surface_interface xdg_surface_implementation = {
    .destroy = xdg_surface_destroy,
    .get_toplevel = xdg_surface_get_toplevel,
    .get_popup = xdg_surface_get_popup,
    .set_window_geometry = xdg_surface_set_window_geometry,
    .ack_configure = xdg_surface_ack_configure
};

static void xdg_wm_base_destroy(struct wl_client *client,
                                struct wl_resource *resource)
{
    struct nb_wayland_server *server =
        wl_resource_get_user_data(resource);
    size_t index;

    (void)client;
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        const struct nb_wayland_surface *surface =
            &server->surfaces[index];

        if (surface->occupied &&
            surface->wm_base_resource == resource &&
            surface->xdg_surface_resource != NULL) {
            wl_resource_post_error(
                resource,
                XDG_WM_BASE_ERROR_DEFUNCT_SURFACES,
                "destroy xdg_surface children first");
            return;
        }
    }
    wl_resource_destroy(resource);
}

static void xdg_wm_base_create_positioner(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id)
{
    struct wl_resource *positioner = wl_resource_create(
        client,
        &xdg_positioner_interface,
        wl_resource_get_version(resource),
        id);

    if (positioner == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(positioner,
                                   &positioner_implementation,
                                   NULL,
                                   NULL);
}

static void xdg_wm_base_get_xdg_surface(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    struct wl_resource *surface_resource)
{
    struct nb_wayland_surface *surface;

    if (!wl_resource_instance_of(surface_resource,
                                 &wl_surface_interface,
                                 &surface_implementation)) {
        wl_resource_post_error(resource,
                               XDG_WM_BASE_ERROR_ROLE,
                               "foreign wl_surface");
        return;
    }
    surface = wl_resource_get_user_data(surface_resource);
    if (surface->xdg_surface_resource != NULL) {
        wl_resource_post_error(resource,
                               XDG_WM_BASE_ERROR_ROLE,
                               "wl_surface already has a role");
        return;
    }
    if (!surface->role_assigned &&
        (surface->pending_attach || surface->committed_once ||
         surface->pixels != NULL)) {
        wl_resource_post_error(resource,
                               XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE,
                               "wl_surface already has buffer state");
        return;
    }
    surface->xdg_surface_resource = wl_resource_create(
        client,
        &xdg_surface_interface,
        wl_resource_get_version(resource),
        id);
    if (surface->xdg_surface_resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->wm_base_resource = resource;
    wl_resource_set_implementation(surface->xdg_surface_resource,
                                   &xdg_surface_implementation,
                                   surface,
                                   xdg_surface_resource_destroyed);
}

static void xdg_wm_base_pong(struct wl_client *client,
                             struct wl_resource *resource,
                             uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)serial;
}

static const struct xdg_wm_base_interface xdg_wm_base_implementation = {
    .destroy = xdg_wm_base_destroy,
    .create_positioner = xdg_wm_base_create_positioner,
    .get_xdg_surface = xdg_wm_base_get_xdg_surface,
    .pong = xdg_wm_base_pong
};

static void bind_xdg_wm_base(struct wl_client *client,
                             void *data,
                             uint32_t version,
                             uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client,
        &xdg_wm_base_interface,
        protocol_version(version, NB_WAYLAND_XDG_SHELL_VERSION),
        id);

    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &xdg_wm_base_implementation,
                                   data,
                                   NULL);
}

struct nb_wayland_server *nb_wayland_server_create(
    struct nb_shell *shell,
    nb_menu_source_id menu_source,
    const struct nb_menu_model *menu_model)
{
    struct nb_wayland_server *server;

    if (shell == NULL || menu_source == NB_MENU_SOURCE_NONE ||
        menu_model == NULL) {
        return NULL;
    }

    server = calloc(1, sizeof(*server));
    if (server == NULL) {
        return NULL;
    }
    server->shell = shell;
    server->menu_source = menu_source;
    server->menu_model = menu_model;
    server->display = wl_display_create();
    if (server->display == NULL) {
        free(server);
        return NULL;
    }
    server->event_loop = wl_display_get_event_loop(server->display);
    if (server->event_loop == NULL ||
        wl_display_init_shm(server->display) != 0) {
        wl_display_destroy(server->display);
        free(server);
        return NULL;
    }
    server->compositor_global = wl_global_create(
        server->display,
        &wl_compositor_interface,
        NB_WAYLAND_COMPOSITOR_VERSION,
        server,
        bind_compositor);
    server->xdg_wm_base_global = wl_global_create(
        server->display,
        &xdg_wm_base_interface,
        NB_WAYLAND_XDG_SHELL_VERSION,
        server,
        bind_xdg_wm_base);
    if (server->compositor_global == NULL ||
        server->xdg_wm_base_global == NULL) {
        wl_display_destroy(server->display);
        free(server);
        return NULL;
    }
    return server;
}

void nb_wayland_server_destroy(struct nb_wayland_server *server)
{
    size_t index;

    if (server == NULL) {
        return;
    }
    wl_display_destroy_clients(server->display);
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *surface = &server->surfaces[index];

        if (!surface->occupied) {
            continue;
        }
        unmap_surface(surface);
        clear_pending_attach(surface);
        destroy_frame_resources(surface);
        free(surface->pixels);
        memset(surface, 0, sizeof(*surface));
    }
    wl_display_destroy(server->display);
    free(server);
}

const char *nb_wayland_server_add_socket_auto(
    struct nb_wayland_server *server)
{
    const char *name;

    if (server == NULL) {
        return NULL;
    }
    if (server->display_name[0] != '\0') {
        return server->display_name;
    }
    name = wl_display_add_socket_auto(server->display);
    if (name == NULL || strlen(name) >= sizeof(server->display_name)) {
        return NULL;
    }
    copy_text(server->display_name,
              sizeof(server->display_name),
              name);
    return server->display_name;
}

const char *nb_wayland_server_display_name(
    const struct nb_wayland_server *server)
{
    return server == NULL || server->display_name[0] == '\0'
               ? NULL
               : server->display_name;
}

bool nb_wayland_server_add_client_fd(struct nb_wayland_server *server,
                                     int fd)
{
    return server != NULL && fd >= 0 &&
           wl_client_create(server->display, fd) != NULL;
}

bool nb_wayland_server_dispatch(struct nb_wayland_server *server)
{
    if (server == NULL ||
        wl_event_loop_dispatch(server->event_loop, 0) < 0) {
        return false;
    }
    wl_display_flush_clients(server->display);
    return true;
}

bool nb_wayland_server_owns_window(
    const struct nb_wayland_server *server,
    nb_window_id window)
{
    return server != NULL &&
           find_surface_by_window_const(server, window) != NULL;
}

size_t nb_wayland_server_window_count(
    const struct nb_wayland_server *server)
{
    size_t count = 0;
    size_t index;

    if (server == NULL) {
        return 0;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        if (server->surfaces[index].occupied &&
            server->surfaces[index].window != NB_WINDOW_ID_NONE) {
            ++count;
        }
    }
    return count;
}

nb_window_id nb_wayland_server_window_at(
    const struct nb_wayland_server *server,
    size_t index)
{
    size_t surface_index;

    if (server == NULL) {
        return NB_WINDOW_ID_NONE;
    }
    for (surface_index = 0;
         surface_index < NB_WAYLAND_MAX_SURFACES;
         ++surface_index) {
        const struct nb_wayland_surface *surface =
            &server->surfaces[surface_index];

        if (!surface->occupied ||
            surface->window == NB_WINDOW_ID_NONE) {
            continue;
        }
        if (index == 0) {
            return surface->window;
        }
        --index;
    }
    return NB_WINDOW_ID_NONE;
}

bool nb_wayland_server_surface_snapshot(
    const struct nb_wayland_server *server,
    nb_window_id window,
    struct nb_wayland_surface_snapshot *snapshot)
{
    const struct nb_wayland_surface *surface;

    if (server == NULL || snapshot == NULL) {
        return false;
    }
    surface = find_surface_by_window_const(server, window);
    if (surface == NULL || surface->pixels == NULL ||
        surface->width <= 0 || surface->height <= 0) {
        return false;
    }
    snapshot->pixels = surface->pixels;
    snapshot->width = surface->width;
    snapshot->height = surface->height;
    snapshot->stride = surface->width * (int)sizeof(uint32_t);
    snapshot->revision = surface->revision;
    return true;
}

bool nb_wayland_server_request_close(struct nb_wayland_server *server,
                                     nb_window_id window)
{
    struct nb_wayland_surface *surface;

    if (server == NULL) {
        return false;
    }
    surface = find_surface_by_window(server, window);
    if (surface == NULL || surface->toplevel_resource == NULL) {
        return false;
    }
    xdg_toplevel_send_close(surface->toplevel_resource);
    wl_display_flush_clients(server->display);
    return true;
}

void nb_wayland_server_frame_presented(struct nb_wayland_server *server,
                                       uint32_t milliseconds)
{
    size_t index;

    if (server == NULL) {
        return;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        if (server->surfaces[index].occupied) {
            complete_ready_frames(&server->surfaces[index],
                                  milliseconds);
        }
    }
    wl_display_flush_clients(server->display);
}
