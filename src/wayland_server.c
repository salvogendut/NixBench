#define _POSIX_C_SOURCE 200809L

#include "wayland_server.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-server-protocol.h"

enum {
    NB_WAYLAND_COMPOSITOR_VERSION = 4,
    NB_WAYLAND_OUTPUT_VERSION = 2,
    NB_WAYLAND_SEAT_VERSION = 5,
    NB_WAYLAND_XDG_SHELL_VERSION = 1,
    NB_WAYLAND_MAX_FRAME_CALLBACKS = 16,
    NB_WAYLAND_INITIAL_CONTENT_WIDTH = 560,
    NB_WAYLAND_INITIAL_CONTENT_HEIGHT = 300,
    NB_WAYLAND_INITIAL_X = 120,
    NB_WAYLAND_INITIAL_Y = 78,
    NB_WAYLAND_CASCADE = 28,
    NB_WAYLAND_CASCADE_COUNT = 8,
    NB_WAYLAND_DEFAULT_REFRESH_MILLIHERTZ = 60000,
    NB_WAYLAND_KEY_CAPACITY = 256,
    NB_WAYLAND_KEYBOARD_REPEAT_RATE = 25,
    NB_WAYLAND_KEYBOARD_REPEAT_DELAY = 600
};

enum nb_wayland_surface_role {
    NB_WAYLAND_SURFACE_ROLE_NONE,
    NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL,
    NB_WAYLAND_SURFACE_ROLE_CURSOR
};

struct nb_wayland_server;

struct nb_wayland_pointer_resource {
    struct nb_wayland_server *server;
    struct wl_resource *resource;
    struct wl_list link;
};

struct nb_wayland_keyboard_resource {
    struct nb_wayland_server *server;
    struct wl_resource *resource;
    struct wl_list link;
};

struct nb_wayland_output_resource {
    struct nb_wayland_server *server;
    struct wl_resource *resource;
    struct wl_list link;
};

struct nb_wayland_surface {
    bool occupied;
    enum nb_wayland_surface_role role;
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
    struct wl_global *output_global;
    struct wl_global *seat_global;
    struct wl_global *xdg_wm_base_global;
    struct wl_list output_resources;
    struct wl_list pointer_resources;
    struct wl_list keyboard_resources;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    char *keymap_text;
    size_t keymap_size;
    struct nb_wayland_surface *keyboard_focus;
    bool keyboard_keys[NB_WAYLAND_KEY_CAPACITY];
    uint32_t keyboard_mods_depressed;
    uint32_t keyboard_mods_latched;
    uint32_t keyboard_mods_locked;
    uint32_t keyboard_group;
    struct nb_wayland_surface *pointer_focus;
    struct nb_wayland_surface *pointer_grab;
    struct nb_wayland_surface *pointer_cursor;
    struct wl_client *pointer_enter_client;
    uint32_t pointer_enter_serial;
    uint32_t pointer_buttons;
    int pointer_x;
    int pointer_y;
    uint32_t pointer_time;
    bool pointer_position_valid;
    int output_width;
    int output_height;
    int output_refresh_millihertz;
    bool destroying;
    struct nb_wayland_surface surfaces[NB_WAYLAND_MAX_SURFACES];
    unsigned int next_window_position;
    char display_name[NB_WAYLAND_DISPLAY_NAME_CAPACITY];
};

static const struct wl_surface_interface surface_implementation;
static const struct wl_output_interface output_implementation;
static const struct wl_pointer_interface pointer_implementation;
static const struct wl_keyboard_interface keyboard_implementation;
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

static bool output_resource_belongs_to_client(
    const struct nb_wayland_output_resource *output,
    const struct wl_client *client)
{
    return output->resource != NULL &&
           wl_resource_get_client(output->resource) == client;
}

static void output_send_state(
    const struct nb_wayland_server *server,
    struct wl_resource *resource,
    bool include_geometry)
{
    if (include_geometry) {
        wl_output_send_geometry(resource,
                                0,
                                0,
                                0,
                                0,
                                WL_OUTPUT_SUBPIXEL_UNKNOWN,
                                "NixBench",
                                "Hosted output",
                                WL_OUTPUT_TRANSFORM_NORMAL);
    }
    wl_output_send_mode(resource,
                        WL_OUTPUT_MODE_CURRENT |
                            WL_OUTPUT_MODE_PREFERRED,
                        server->output_width,
                        server->output_height,
                        server->output_refresh_millihertz);
    if (wl_resource_get_version(resource) >=
        WL_OUTPUT_SCALE_SINCE_VERSION) {
        wl_output_send_scale(resource, 1);
    }
    if (wl_resource_get_version(resource) >=
        WL_OUTPUT_DONE_SINCE_VERSION) {
        wl_output_send_done(resource);
    }
}

static void surface_send_output_membership(
    struct nb_wayland_surface *surface,
    bool entered)
{
    struct nb_wayland_server *server = surface->server;
    struct nb_wayland_output_resource *output;
    struct wl_client *client;

    if (server->destroying || surface->surface_resource == NULL) {
        return;
    }
    client = wl_resource_get_client(surface->surface_resource);
    wl_list_for_each(output, &server->output_resources, link) {
        if (!output_resource_belongs_to_client(output, client)) {
            continue;
        }
        if (entered) {
            wl_surface_send_enter(surface->surface_resource,
                                  output->resource);
        } else {
            wl_surface_send_leave(surface->surface_resource,
                                  output->resource);
        }
    }
}

static void output_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_output_resource *output =
        wl_resource_get_user_data(resource);

    if (output == NULL) {
        return;
    }
    output->resource = NULL;
    wl_list_remove(&output->link);
    wl_list_init(&output->link);
    free(output);
}

static void output_release(struct wl_client *client,
                           struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_output_interface output_implementation = {
    .release = output_release
};

static void bind_output(struct wl_client *client,
                        void *data,
                        uint32_t version,
                        uint32_t id)
{
    struct nb_wayland_server *server = data;
    struct nb_wayland_output_resource *output =
        calloc(1, sizeof(*output));
    size_t index;

    if (output == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    output->server = server;
    output->resource = wl_resource_create(
        client,
        &wl_output_interface,
        protocol_version(version, NB_WAYLAND_OUTPUT_VERSION),
        id);
    if (output->resource == NULL) {
        free(output);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_insert(&server->output_resources, &output->link);
    wl_resource_set_implementation(output->resource,
                                   &output_implementation,
                                   output,
                                   output_resource_destroyed);
    output_send_state(server, output->resource, true);

    /* A late output binding still needs membership for mapped surfaces. */
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *surface = &server->surfaces[index];

        if (surface->occupied && surface->window != NB_WINDOW_ID_NONE &&
            surface->surface_resource != NULL &&
            wl_resource_get_client(surface->surface_resource) == client) {
            wl_surface_send_enter(surface->surface_resource,
                                  output->resource);
        }
    }
}

static bool keyboard_resource_belongs_to_client(
    const struct nb_wayland_keyboard_resource *keyboard,
    const struct wl_client *client)
{
    return keyboard->resource != NULL &&
           wl_resource_get_client(keyboard->resource) == client;
}

static void keyboard_read_modifiers(struct nb_wayland_server *server,
                                    uint32_t *depressed,
                                    uint32_t *latched,
                                    uint32_t *locked,
                                    uint32_t *group)
{
    *depressed = (uint32_t)xkb_state_serialize_mods(
        server->xkb_state, XKB_STATE_MODS_DEPRESSED);
    *latched = (uint32_t)xkb_state_serialize_mods(
        server->xkb_state, XKB_STATE_MODS_LATCHED);
    *locked = (uint32_t)xkb_state_serialize_mods(
        server->xkb_state, XKB_STATE_MODS_LOCKED);
    *group = (uint32_t)xkb_state_serialize_layout(
        server->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
}

static void keyboard_store_modifiers(struct nb_wayland_server *server)
{
    keyboard_read_modifiers(server,
                            &server->keyboard_mods_depressed,
                            &server->keyboard_mods_latched,
                            &server->keyboard_mods_locked,
                            &server->keyboard_group);
}

static bool keyboard_build_pressed_array(
    const struct nb_wayland_server *server,
    struct wl_array *keys)
{
    size_t index;

    wl_array_init(keys);
    for (index = 0; index < NB_WAYLAND_KEY_CAPACITY; ++index) {
        uint32_t *entry;

        if (!server->keyboard_keys[index]) {
            continue;
        }
        entry = wl_array_add(keys, sizeof(*entry));
        if (entry == NULL) {
            wl_array_release(keys);
            return false;
        }
        *entry = (uint32_t)index;
    }
    return true;
}

static void keyboard_send_modifiers_to_client(
    struct nb_wayland_server *server,
    struct wl_client *client,
    uint32_t serial)
{
    struct nb_wayland_keyboard_resource *keyboard;

    if (client == NULL) {
        return;
    }
    wl_list_for_each(keyboard, &server->keyboard_resources, link) {
        if (keyboard_resource_belongs_to_client(keyboard, client)) {
            wl_keyboard_send_modifiers(keyboard->resource,
                                       serial,
                                       server->keyboard_mods_depressed,
                                       server->keyboard_mods_latched,
                                       server->keyboard_mods_locked,
                                       server->keyboard_group);
        }
    }
}

static bool keyboard_change_focus(struct nb_wayland_server *server,
                                  struct nb_wayland_surface *new_focus)
{
    struct nb_wayland_surface *old_focus = server->keyboard_focus;
    struct wl_client *old_client = NULL;
    struct wl_client *new_client = NULL;
    struct nb_wayland_keyboard_resource *keyboard;
    struct wl_array keys;
    uint32_t leave_serial = 0;
    uint32_t enter_serial = 0;

    if (new_focus != NULL &&
        (new_focus->window == NB_WINDOW_ID_NONE ||
         new_focus->surface_resource == NULL || new_focus->pixels == NULL)) {
        new_focus = NULL;
    }
    if (old_focus == new_focus) {
        return true;
    }
    if (server->destroying) {
        server->keyboard_focus = new_focus;
        return true;
    }
    if (new_focus != NULL &&
        !keyboard_build_pressed_array(server, &keys)) {
        wl_client_post_no_memory(
            wl_resource_get_client(new_focus->surface_resource));
        return false;
    }

    if (old_focus != NULL && old_focus->surface_resource != NULL) {
        old_client = wl_resource_get_client(old_focus->surface_resource);
        leave_serial = wl_display_next_serial(server->display);
    }
    if (new_focus != NULL) {
        new_client = wl_resource_get_client(new_focus->surface_resource);
        enter_serial = wl_display_next_serial(server->display);
    }
    wl_list_for_each(keyboard, &server->keyboard_resources, link) {
        if (old_client != NULL &&
            keyboard_resource_belongs_to_client(keyboard, old_client)) {
            wl_keyboard_send_leave(keyboard->resource,
                                   leave_serial,
                                   old_focus->surface_resource);
        }
    }

    server->keyboard_focus = new_focus;
    if (new_focus != NULL) {
        wl_list_for_each(keyboard, &server->keyboard_resources, link) {
            if (keyboard_resource_belongs_to_client(keyboard, new_client)) {
                wl_keyboard_send_enter(keyboard->resource,
                                       enter_serial,
                                       new_focus->surface_resource,
                                       &keys);
            }
        }
        keyboard_send_modifiers_to_client(server,
                                          new_client,
                                          enter_serial);
        wl_array_release(&keys);
    }
    return true;
}

static void clear_surface_keyboard_state(
    struct nb_wayland_surface *surface,
    bool send_events)
{
    struct nb_wayland_server *server = surface->server;

    if (server->keyboard_focus != surface) {
        return;
    }
    if (send_events) {
        (void)keyboard_change_focus(server, NULL);
    } else {
        server->keyboard_focus = NULL;
    }
}

static bool initialize_keyboard_state(struct nb_wayland_server *server)
{
    server->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (server->xkb_context == NULL) {
        return false;
    }
    server->xkb_keymap = xkb_keymap_new_from_names(
        server->xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (server->xkb_keymap == NULL) {
        return false;
    }
    server->xkb_state = xkb_state_new(server->xkb_keymap);
    if (server->xkb_state == NULL) {
        return false;
    }
    server->keymap_text = xkb_keymap_get_as_string(
        server->xkb_keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (server->keymap_text == NULL) {
        return false;
    }
    server->keymap_size = strlen(server->keymap_text) + 1;
    if (server->keymap_size > UINT32_MAX) {
        return false;
    }
    keyboard_store_modifiers(server);
    return true;
}

static void destroy_keyboard_state(struct nb_wayland_server *server)
{
    free(server->keymap_text);
    server->keymap_text = NULL;
    if (server->xkb_state != NULL) {
        xkb_state_unref(server->xkb_state);
    }
    server->xkb_state = NULL;
    if (server->xkb_keymap != NULL) {
        xkb_keymap_unref(server->xkb_keymap);
    }
    server->xkb_keymap = NULL;
    if (server->xkb_context != NULL) {
        xkb_context_unref(server->xkb_context);
    }
    server->xkb_context = NULL;
}

static wl_fixed_t pointer_fixed_coordinate(int desktop_coordinate,
                                           int content_origin,
                                           int content_extent,
                                           int surface_extent)
{
    double coordinate;
    const double maximum = (double)INT32_MAX / 256.0;
    const double minimum = (double)INT32_MIN / 256.0;

    if (content_extent <= 0 || surface_extent <= 0) {
        return 0;
    }
    coordinate = ((double)desktop_coordinate - (double)content_origin) *
                 (double)surface_extent / (double)content_extent;
    if (coordinate >= maximum) {
        return INT32_MAX;
    }
    if (coordinate <= minimum) {
        return INT32_MIN;
    }
    return wl_fixed_from_double(coordinate);
}

static bool pointer_surface_coordinates(
    const struct nb_wayland_surface *surface,
    int desktop_x,
    int desktop_y,
    wl_fixed_t *surface_x,
    wl_fixed_t *surface_y)
{
    const struct nb_window *window;
    struct nb_rect content;

    if (surface == NULL || surface->window == NB_WINDOW_ID_NONE ||
        surface->width <= 0 || surface->height <= 0 ||
        surface->surface_resource == NULL) {
        return false;
    }
    window = nb_desktop_find_window(&surface->server->shell->desktop,
                                    surface->window);
    if (window == NULL) {
        return false;
    }
    content = nb_window_content_rect(window);
    if (content.width <= 0 || content.height <= 0) {
        return false;
    }
    *surface_x = pointer_fixed_coordinate(desktop_x,
                                          content.x,
                                          content.width,
                                          surface->width);
    *surface_y = pointer_fixed_coordinate(desktop_y,
                                          content.y,
                                          content.height,
                                          surface->height);
    return true;
}

static bool pointer_resource_belongs_to_client(
    const struct nb_wayland_pointer_resource *pointer,
    const struct wl_client *client)
{
    return pointer->resource != NULL &&
           wl_resource_get_client(pointer->resource) == client;
}

static void pointer_send_frames_to_client(
    struct nb_wayland_server *server,
    struct wl_client *client)
{
    struct nb_wayland_pointer_resource *pointer;

    if (client == NULL) {
        return;
    }
    wl_list_for_each(pointer, &server->pointer_resources, link) {
        if (pointer_resource_belongs_to_client(pointer, client) &&
            wl_resource_get_version(pointer->resource) >=
                WL_POINTER_FRAME_SINCE_VERSION) {
            wl_pointer_send_frame(pointer->resource);
        }
    }
}

static void pointer_change_focus(struct nb_wayland_server *server,
                                 struct nb_wayland_surface *new_focus)
{
    struct nb_wayland_surface *old_focus = server->pointer_focus;
    struct wl_client *old_client = NULL;
    struct wl_client *new_client = NULL;
    struct nb_wayland_pointer_resource *pointer;
    wl_fixed_t surface_x = 0;
    wl_fixed_t surface_y = 0;
    uint32_t leave_serial = 0;
    uint32_t enter_serial = 0;

    if (new_focus != NULL &&
        !pointer_surface_coordinates(new_focus,
                                     server->pointer_x,
                                     server->pointer_y,
                                     &surface_x,
                                     &surface_y)) {
        new_focus = NULL;
    }
    if (old_focus == new_focus) {
        return;
    }
    if (server->destroying) {
        server->pointer_focus = new_focus;
        server->pointer_cursor = NULL;
        server->pointer_enter_client = NULL;
        return;
    }

    if (old_focus != NULL && old_focus->surface_resource != NULL) {
        old_client = wl_resource_get_client(old_focus->surface_resource);
        leave_serial = wl_display_next_serial(server->display);
    }
    if (new_focus != NULL && new_focus->surface_resource != NULL) {
        new_client = wl_resource_get_client(new_focus->surface_resource);
        enter_serial = wl_display_next_serial(server->display);
    }

    wl_list_for_each(pointer, &server->pointer_resources, link) {
        if (old_client != NULL &&
            pointer_resource_belongs_to_client(pointer, old_client)) {
            wl_pointer_send_leave(pointer->resource,
                                  leave_serial,
                                  old_focus->surface_resource);
        }
    }

    server->pointer_focus = new_focus;
    server->pointer_cursor = NULL;
    server->pointer_enter_client = new_client;
    server->pointer_enter_serial = enter_serial;

    wl_list_for_each(pointer, &server->pointer_resources, link) {
        if (new_client != NULL &&
            pointer_resource_belongs_to_client(pointer, new_client)) {
            wl_pointer_send_enter(pointer->resource,
                                  enter_serial,
                                  new_focus->surface_resource,
                                  surface_x,
                                  surface_y);
        }
    }

    if (old_client != NULL && old_client == new_client) {
        pointer_send_frames_to_client(server, old_client);
    } else {
        pointer_send_frames_to_client(server, old_client);
        pointer_send_frames_to_client(server, new_client);
    }
}

static uint32_t pointer_protocol_button(
    enum nb_wayland_pointer_button button)
{
    /* Linux evdev values mandated by the Wayland core protocol. */
    switch (button) {
    case NB_WAYLAND_POINTER_BUTTON_LEFT:
        return UINT32_C(0x110);
    case NB_WAYLAND_POINTER_BUTTON_MIDDLE:
        return UINT32_C(0x112);
    case NB_WAYLAND_POINTER_BUTTON_RIGHT:
        return UINT32_C(0x111);
    case NB_WAYLAND_POINTER_BUTTON_SIDE:
        return UINT32_C(0x113);
    case NB_WAYLAND_POINTER_BUTTON_EXTRA:
        return UINT32_C(0x114);
    case NB_WAYLAND_POINTER_BUTTON_COUNT:
        break;
    }
    return 0;
}

static void pointer_send_button(struct nb_wayland_server *server,
                                struct nb_wayland_surface *surface,
                                uint32_t milliseconds,
                                enum nb_wayland_pointer_button button,
                                uint32_t state)
{
    struct nb_wayland_pointer_resource *pointer;
    struct wl_client *client;
    const uint32_t serial = wl_display_next_serial(server->display);
    const uint32_t protocol_button = pointer_protocol_button(button);

    if (surface == NULL || surface->surface_resource == NULL ||
        protocol_button == 0) {
        return;
    }
    client = wl_resource_get_client(surface->surface_resource);
    wl_list_for_each(pointer, &server->pointer_resources, link) {
        if (pointer_resource_belongs_to_client(pointer, client)) {
            wl_pointer_send_button(pointer->resource,
                                   serial,
                                   milliseconds,
                                   protocol_button,
                                   state);
        }
    }
    pointer_send_frames_to_client(server, client);
}

static void pointer_cancel_internal(struct nb_wayland_server *server,
                                    uint32_t milliseconds,
                                    bool send_events)
{
    struct nb_wayland_surface *target = server->pointer_grab != NULL
                                            ? server->pointer_grab
                                            : server->pointer_focus;
    unsigned int index;

    if (send_events && !server->destroying && target != NULL) {
        for (index = 0;
             index < (unsigned int)NB_WAYLAND_POINTER_BUTTON_COUNT;
             ++index) {
            const uint32_t mask = UINT32_C(1) << index;

            if ((server->pointer_buttons & mask) != 0) {
                pointer_send_button(
                    server,
                    target,
                    milliseconds,
                    (enum nb_wayland_pointer_button)index,
                    WL_POINTER_BUTTON_STATE_RELEASED);
            }
        }
    }
    server->pointer_buttons = 0;
    server->pointer_grab = NULL;
    if (send_events && !server->destroying) {
        pointer_change_focus(server, NULL);
    } else {
        server->pointer_focus = NULL;
        server->pointer_cursor = NULL;
        server->pointer_enter_client = NULL;
    }
}

static void clear_surface_pointer_state(struct nb_wayland_surface *surface,
                                        bool send_events)
{
    struct nb_wayland_server *server = surface->server;

    if (server->pointer_cursor == surface) {
        server->pointer_cursor = NULL;
    }
    if (server->pointer_focus != surface &&
        server->pointer_grab != surface) {
        return;
    }
    pointer_cancel_internal(server, server->pointer_time, send_events);
}

static void unmap_surface(struct nb_wayland_surface *surface,
                          bool send_client_events)
{
    if (surface->window != NB_WINDOW_ID_NONE) {
        if (send_client_events) {
            surface_send_output_membership(surface, false);
        }
        clear_surface_keyboard_state(surface, send_client_events);
        clear_surface_pointer_state(surface, send_client_events);
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

    clear_surface_pointer_state(surface, false);
    unmap_surface(surface, false);
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
        surface_send_output_membership(surface, true);
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
    clear_surface_pointer_state(surface, false);
    unmap_surface(surface, false);
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
    unmap_surface(surface, true);
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
            unmap_surface(surface, true);
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

static void pointer_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_pointer_resource *pointer =
        wl_resource_get_user_data(resource);

    if (pointer == NULL) {
        return;
    }
    pointer->resource = NULL;
    wl_list_remove(&pointer->link);
    wl_list_init(&pointer->link);
    free(pointer);
}

static void pointer_set_cursor(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t serial,
                               struct wl_resource *surface_resource,
                               int32_t hotspot_x,
                               int32_t hotspot_y)
{
    struct nb_wayland_pointer_resource *pointer =
        wl_resource_get_user_data(resource);
    struct nb_wayland_server *server = pointer->server;
    struct nb_wayland_surface *surface = NULL;

    (void)hotspot_x;
    (void)hotspot_y;
    if (server->pointer_enter_client != client ||
        server->pointer_enter_serial != serial) {
        return;
    }
    if (surface_resource != NULL) {
        if (!wl_resource_instance_of(surface_resource,
                                     &wl_surface_interface,
                                     &surface_implementation)) {
            wl_resource_post_error(resource,
                                   WL_POINTER_ERROR_ROLE,
                                   "invalid cursor surface");
            return;
        }
        surface = wl_resource_get_user_data(surface_resource);
        if (surface == NULL || surface->server != server ||
            wl_resource_get_client(surface_resource) != client) {
            wl_resource_post_error(resource,
                                   WL_POINTER_ERROR_ROLE,
                                   "foreign cursor surface");
            return;
        }
        if (surface->role != NB_WAYLAND_SURFACE_ROLE_NONE &&
            surface->role != NB_WAYLAND_SURFACE_ROLE_CURSOR) {
            wl_resource_post_error(resource,
                                   WL_POINTER_ERROR_ROLE,
                                   "wl_surface already has another role");
            return;
        }
        surface->role = NB_WAYLAND_SURFACE_ROLE_CURSOR;
    }
    server->pointer_cursor = surface;
}

static void pointer_release(struct wl_client *client,
                            struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_pointer_interface pointer_implementation = {
    .set_cursor = pointer_set_cursor,
    .release = pointer_release
};

static void seat_get_pointer(struct wl_client *client,
                             struct wl_resource *resource,
                             uint32_t id)
{
    struct nb_wayland_server *server =
        wl_resource_get_user_data(resource);
    struct nb_wayland_pointer_resource *pointer =
        calloc(1, sizeof(*pointer));

    if (pointer == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    pointer->server = server;
    pointer->resource = wl_resource_create(client,
                                           &wl_pointer_interface,
                                           wl_resource_get_version(resource),
                                           id);
    if (pointer->resource == NULL) {
        free(pointer);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_insert(&server->pointer_resources, &pointer->link);
    wl_resource_set_implementation(pointer->resource,
                                   &pointer_implementation,
                                   pointer,
                                   pointer_resource_destroyed);

    if (server->pointer_position_valid &&
        server->pointer_focus != NULL &&
        server->pointer_focus->surface_resource != NULL &&
        wl_resource_get_client(
            server->pointer_focus->surface_resource) == client) {
        wl_fixed_t surface_x;
        wl_fixed_t surface_y;

        if (pointer_surface_coordinates(server->pointer_focus,
                                        server->pointer_x,
                                        server->pointer_y,
                                        &surface_x,
                                        &surface_y)) {
            const uint32_t serial =
                wl_display_next_serial(server->display);

            server->pointer_enter_client = client;
            server->pointer_enter_serial = serial;
            wl_pointer_send_enter(pointer->resource,
                                  serial,
                                  server->pointer_focus->surface_resource,
                                  surface_x,
                                  surface_y);
            if (wl_resource_get_version(pointer->resource) >=
                WL_POINTER_FRAME_SINCE_VERSION) {
                wl_pointer_send_frame(pointer->resource);
            }
        }
    }
}

static int create_keymap_file(const struct nb_wayland_server *server)
{
    char path[] = "/tmp/nixbench-keymap-XXXXXX";
    size_t written = 0;
    int fd = mkstemp(path);

    if (fd < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || unlink(path) != 0) {
        goto error;
    }
    while (written < server->keymap_size) {
        const ssize_t result = write(fd,
                                     server->keymap_text + written,
                                     server->keymap_size - written);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            goto error;
        }
        if (result == 0) {
            errno = EIO;
            goto error;
        }
        written += (size_t)result;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        goto error;
    }
    return fd;

error:
    {
        const int saved_errno = errno;

        (void)unlink(path);
        (void)close(fd);
        errno = saved_errno;
    }
    return -1;
}

static void keyboard_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_keyboard_resource *keyboard =
        wl_resource_get_user_data(resource);

    if (keyboard == NULL) {
        return;
    }
    keyboard->resource = NULL;
    wl_list_remove(&keyboard->link);
    wl_list_init(&keyboard->link);
    free(keyboard);
}

static void keyboard_release(struct wl_client *client,
                             struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface keyboard_implementation = {
    .release = keyboard_release
};

static void seat_get_keyboard(struct wl_client *client,
                              struct wl_resource *resource,
                              uint32_t id)
{
    struct nb_wayland_server *server =
        wl_resource_get_user_data(resource);
    struct nb_wayland_keyboard_resource *keyboard =
        calloc(1, sizeof(*keyboard));
    int keymap_fd;

    if (keyboard == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    keyboard->server = server;
    keyboard->resource = wl_resource_create(client,
                                             &wl_keyboard_interface,
                                             wl_resource_get_version(resource),
                                             id);
    if (keyboard->resource == NULL) {
        free(keyboard);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_insert(&server->keyboard_resources, &keyboard->link);
    wl_resource_set_implementation(keyboard->resource,
                                   &keyboard_implementation,
                                   keyboard,
                                   keyboard_resource_destroyed);

    keymap_fd = create_keymap_file(server);
    if (keymap_fd < 0) {
        wl_client_post_implementation_error(
            client, "NixBench could not publish its keyboard keymap");
        return;
    }
    wl_keyboard_send_keymap(keyboard->resource,
                            WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                            keymap_fd,
                            (uint32_t)server->keymap_size);
    (void)close(keymap_fd);
    if (wl_resource_get_version(keyboard->resource) >=
        WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
        wl_keyboard_send_repeat_info(
            keyboard->resource,
            NB_WAYLAND_KEYBOARD_REPEAT_RATE,
            NB_WAYLAND_KEYBOARD_REPEAT_DELAY);
    }

    if (server->keyboard_focus != NULL &&
        server->keyboard_focus->surface_resource != NULL &&
        wl_resource_get_client(
            server->keyboard_focus->surface_resource) == client) {
        struct wl_array keys;
        const uint32_t serial = wl_display_next_serial(server->display);

        if (!keyboard_build_pressed_array(server, &keys)) {
            wl_client_post_no_memory(client);
            return;
        }
        wl_keyboard_send_enter(keyboard->resource,
                               serial,
                               server->keyboard_focus->surface_resource,
                               &keys);
        wl_keyboard_send_modifiers(keyboard->resource,
                                   serial,
                                   server->keyboard_mods_depressed,
                                   server->keyboard_mods_latched,
                                   server->keyboard_mods_locked,
                                   server->keyboard_group);
        wl_array_release(&keys);
    }
}

static void seat_get_touch(struct wl_client *client,
                           struct wl_resource *resource,
                           uint32_t id)
{
    (void)client;
    (void)id;
    wl_resource_post_error(resource,
                           WL_SEAT_ERROR_MISSING_CAPABILITY,
                           "NixBench seat has no touch capability");
}

static void seat_release(struct wl_client *client,
                         struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct wl_seat_interface seat_implementation = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = seat_release
};

static void bind_seat(struct wl_client *client,
                      void *data,
                      uint32_t version,
                      uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client,
        &wl_seat_interface,
        protocol_version(version, NB_WAYLAND_SEAT_VERSION),
        id);

    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &seat_implementation,
                                   data,
                                   NULL);
    if (wl_resource_get_version(resource) >= WL_SEAT_NAME_SINCE_VERSION) {
        wl_seat_send_name(resource, "nixbench-seat0");
    }
    wl_seat_send_capabilities(resource,
                              WL_SEAT_CAPABILITY_POINTER |
                                  WL_SEAT_CAPABILITY_KEYBOARD);
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
    unmap_surface(surface, false);
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
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    unmap_surface(surface, true);
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

    if (surface->toplevel_resource != NULL ||
        (surface->role != NB_WAYLAND_SURFACE_ROLE_NONE &&
         surface->role != NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL)) {
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
    surface->role = NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL;
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
    if (surface->role != NB_WAYLAND_SURFACE_ROLE_NONE) {
        wl_resource_post_error(resource,
                               XDG_WM_BASE_ERROR_ROLE,
                               "wl_surface already has a role");
        return;
    }
    if (surface->pending_attach || surface->committed_once ||
        surface->pixels != NULL) {
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
    const struct nb_menu_model *menu_model,
    int output_width,
    int output_height)
{
    struct nb_wayland_server *server;

    if (shell == NULL || menu_source == NB_MENU_SOURCE_NONE ||
        menu_model == NULL || output_width <= 0 || output_height <= 0) {
        return NULL;
    }

    server = calloc(1, sizeof(*server));
    if (server == NULL) {
        return NULL;
    }
    server->shell = shell;
    server->menu_source = menu_source;
    server->menu_model = menu_model;
    server->output_width = output_width;
    server->output_height = output_height;
    server->output_refresh_millihertz =
        NB_WAYLAND_DEFAULT_REFRESH_MILLIHERTZ;
    wl_list_init(&server->output_resources);
    wl_list_init(&server->pointer_resources);
    wl_list_init(&server->keyboard_resources);
    if (!initialize_keyboard_state(server)) {
        destroy_keyboard_state(server);
        free(server);
        return NULL;
    }
    server->display = wl_display_create();
    if (server->display == NULL) {
        destroy_keyboard_state(server);
        free(server);
        return NULL;
    }
    server->event_loop = wl_display_get_event_loop(server->display);
    if (server->event_loop == NULL ||
        wl_display_init_shm(server->display) != 0) {
        wl_display_destroy(server->display);
        destroy_keyboard_state(server);
        free(server);
        return NULL;
    }
    server->compositor_global = wl_global_create(
        server->display,
        &wl_compositor_interface,
        NB_WAYLAND_COMPOSITOR_VERSION,
        server,
        bind_compositor);
    server->output_global = wl_global_create(server->display,
                                             &wl_output_interface,
                                             NB_WAYLAND_OUTPUT_VERSION,
                                             server,
                                             bind_output);
    server->seat_global = wl_global_create(server->display,
                                           &wl_seat_interface,
                                           NB_WAYLAND_SEAT_VERSION,
                                           server,
                                           bind_seat);
    server->xdg_wm_base_global = wl_global_create(
        server->display,
        &xdg_wm_base_interface,
        NB_WAYLAND_XDG_SHELL_VERSION,
        server,
        bind_xdg_wm_base);
    if (server->compositor_global == NULL ||
        server->output_global == NULL ||
        server->seat_global == NULL ||
        server->xdg_wm_base_global == NULL) {
        wl_display_destroy(server->display);
        destroy_keyboard_state(server);
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
    server->destroying = true;
    pointer_cancel_internal(server, server->pointer_time, false);
    wl_display_destroy_clients(server->display);
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *surface = &server->surfaces[index];

        if (!surface->occupied) {
            continue;
        }
        unmap_surface(surface, false);
        clear_pending_attach(surface);
        destroy_frame_resources(surface);
        free(surface->pixels);
        memset(surface, 0, sizeof(*surface));
    }
    wl_display_destroy(server->display);
    destroy_keyboard_state(server);
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

bool nb_wayland_server_set_output_size(struct nb_wayland_server *server,
                                       int width,
                                       int height)
{
    struct nb_wayland_output_resource *output;

    if (server == NULL || server->destroying || width <= 0 || height <= 0) {
        return false;
    }
    if (server->output_width == width && server->output_height == height) {
        return true;
    }
    server->output_width = width;
    server->output_height = height;
    wl_list_for_each(output, &server->output_resources, link) {
        wl_output_send_mode(output->resource,
                            WL_OUTPUT_MODE_CURRENT |
                                WL_OUTPUT_MODE_PREFERRED,
                            width,
                            height,
                            server->output_refresh_millihertz);
        if (wl_resource_get_version(output->resource) >=
            WL_OUTPUT_DONE_SINCE_VERSION) {
            wl_output_send_done(output->resource);
        }
    }
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

static struct nb_wayland_surface *pointer_hover_surface(
    struct nb_wayland_server *server,
    nb_window_id hover_window)
{
    struct nb_wayland_surface *surface =
        find_surface_by_window(server, hover_window);

    if (surface == NULL ||
        surface->role != NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
        surface->surface_resource == NULL || surface->pixels == NULL) {
        return NULL;
    }
    return surface;
}

bool nb_wayland_server_pointer_motion(struct nb_wayland_server *server,
                                      nb_window_id hover_window,
                                      int desktop_x,
                                      int desktop_y,
                                      uint32_t milliseconds)
{
    struct nb_wayland_surface *target;
    bool focus_changed;

    if (server == NULL || server->destroying) {
        return false;
    }
    server->pointer_x = desktop_x;
    server->pointer_y = desktop_y;
    server->pointer_time = milliseconds;
    server->pointer_position_valid = true;
    target = server->pointer_grab != NULL
                 ? server->pointer_grab
                 : pointer_hover_surface(server, hover_window);
    focus_changed = server->pointer_focus != target;
    pointer_change_focus(server, target);

    if (!focus_changed && target != NULL) {
        struct nb_wayland_pointer_resource *pointer;
        struct wl_client *client =
            wl_resource_get_client(target->surface_resource);
        wl_fixed_t surface_x;
        wl_fixed_t surface_y;

        if (!pointer_surface_coordinates(target,
                                         desktop_x,
                                         desktop_y,
                                         &surface_x,
                                         &surface_y)) {
            pointer_change_focus(server, NULL);
            return false;
        }
        wl_list_for_each(pointer, &server->pointer_resources, link) {
            if (pointer_resource_belongs_to_client(pointer, client)) {
                wl_pointer_send_motion(pointer->resource,
                                       milliseconds,
                                       surface_x,
                                       surface_y);
            }
        }
        pointer_send_frames_to_client(server, client);
    }
    return true;
}

bool nb_wayland_server_pointer_button(
    struct nb_wayland_server *server,
    nb_window_id hover_window,
    int desktop_x,
    int desktop_y,
    uint32_t milliseconds,
    enum nb_wayland_pointer_button button,
    bool pressed)
{
    struct nb_wayland_surface *target;
    unsigned int button_index;
    uint32_t mask;

    if (server == NULL || server->destroying ||
        pointer_protocol_button(button) == 0 ||
        button < NB_WAYLAND_POINTER_BUTTON_LEFT ||
        button >= NB_WAYLAND_POINTER_BUTTON_COUNT) {
        return false;
    }
    button_index = (unsigned int)button;
    mask = UINT32_C(1) << button_index;
    server->pointer_x = desktop_x;
    server->pointer_y = desktop_y;
    server->pointer_time = milliseconds;
    server->pointer_position_valid = true;

    if (pressed) {
        target = server->pointer_grab != NULL
                     ? server->pointer_grab
                     : pointer_hover_surface(server, hover_window);
        if ((server->pointer_buttons & mask) != 0) {
            return true;
        }
        if (server->pointer_buttons == 0) {
            pointer_change_focus(server, target);
            target = server->pointer_focus;
        }
        if (target == NULL) {
            return true;
        }
        if (server->pointer_buttons == 0) {
            server->pointer_grab = target;
        }
        server->pointer_buttons |= mask;
        pointer_send_button(server,
                            target,
                            milliseconds,
                            button,
                            WL_POINTER_BUTTON_STATE_PRESSED);
        return true;
    }

    if ((server->pointer_buttons & mask) == 0) {
        return true;
    }
    target = server->pointer_grab != NULL
                 ? server->pointer_grab
                 : server->pointer_focus;
    pointer_send_button(server,
                        target,
                        milliseconds,
                        button,
                        WL_POINTER_BUTTON_STATE_RELEASED);
    server->pointer_buttons &= ~mask;
    if (server->pointer_buttons == 0) {
        server->pointer_grab = NULL;
        pointer_change_focus(server,
                             pointer_hover_surface(server,
                                                   hover_window));
    }
    return true;
}

void nb_wayland_server_pointer_cancel(struct nb_wayland_server *server,
                                      uint32_t milliseconds)
{
    if (server == NULL || server->destroying) {
        return;
    }
    server->pointer_time = milliseconds;
    pointer_cancel_internal(server, milliseconds, true);
}

nb_window_id nb_wayland_server_pointer_grab_window(
    const struct nb_wayland_server *server)
{
    if (server == NULL || server->pointer_grab == NULL ||
        server->pointer_buttons == 0) {
        return NB_WINDOW_ID_NONE;
    }
    return server->pointer_grab->window;
}

bool nb_wayland_server_keyboard_focus(
    struct nb_wayland_server *server,
    nb_window_id window)
{
    struct nb_wayland_surface *surface;

    if (server == NULL || server->destroying) {
        return false;
    }
    surface = find_surface_by_window(server, window);
    if (surface != NULL &&
        (surface->role != NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
         surface->pixels == NULL)) {
        surface = NULL;
    }
    return keyboard_change_focus(server, surface);
}

static bool keyboard_wire_keycode(
    const struct nb_wayland_server *server,
    const char *xkb_key_name,
    uint32_t *wire_keycode)
{
    xkb_keycode_t xkb_keycode;

    if (xkb_key_name == NULL || xkb_key_name[0] == '\0') {
        return false;
    }
    xkb_keycode = xkb_keymap_key_by_name(server->xkb_keymap,
                                         xkb_key_name);
    if (xkb_keycode == XKB_KEYCODE_INVALID || xkb_keycode < 8 ||
        xkb_keycode - 8 >= NB_WAYLAND_KEY_CAPACITY) {
        return false;
    }
    *wire_keycode = (uint32_t)(xkb_keycode - 8);
    return true;
}

static void keyboard_send_key_to_focus(
    struct nb_wayland_server *server,
    uint32_t serial,
    uint32_t milliseconds,
    uint32_t wire_keycode,
    uint32_t state)
{
    struct nb_wayland_keyboard_resource *keyboard;
    struct wl_client *client;

    if (server->keyboard_focus == NULL ||
        server->keyboard_focus->surface_resource == NULL) {
        return;
    }
    client = wl_resource_get_client(
        server->keyboard_focus->surface_resource);
    wl_list_for_each(keyboard, &server->keyboard_resources, link) {
        if (keyboard_resource_belongs_to_client(keyboard, client)) {
            wl_keyboard_send_key(keyboard->resource,
                                 serial,
                                 milliseconds,
                                 wire_keycode,
                                 state);
        }
    }
}

static void keyboard_update_wire_key(
    struct nb_wayland_server *server,
    uint32_t wire_keycode,
    uint32_t milliseconds,
    bool pressed)
{
    struct wl_client *focused_client = NULL;
    uint32_t depressed;
    uint32_t latched;
    uint32_t locked;
    uint32_t group;
    const uint32_t serial = wl_display_next_serial(server->display);

    keyboard_send_key_to_focus(
        server,
        serial,
        milliseconds,
        wire_keycode,
        pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                : WL_KEYBOARD_KEY_STATE_RELEASED);
    server->keyboard_keys[wire_keycode] = pressed;
    (void)xkb_state_update_key(
        server->xkb_state,
        (xkb_keycode_t)(wire_keycode + 8),
        pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    keyboard_read_modifiers(server,
                            &depressed,
                            &latched,
                            &locked,
                            &group);
    if (depressed == server->keyboard_mods_depressed &&
        latched == server->keyboard_mods_latched &&
        locked == server->keyboard_mods_locked &&
        group == server->keyboard_group) {
        return;
    }
    server->keyboard_mods_depressed = depressed;
    server->keyboard_mods_latched = latched;
    server->keyboard_mods_locked = locked;
    server->keyboard_group = group;
    if (server->keyboard_focus != NULL &&
        server->keyboard_focus->surface_resource != NULL) {
        focused_client = wl_resource_get_client(
            server->keyboard_focus->surface_resource);
    }
    keyboard_send_modifiers_to_client(server, focused_client, serial);
}

bool nb_wayland_server_keyboard_key(struct nb_wayland_server *server,
                                    const char *xkb_key_name,
                                    uint32_t milliseconds,
                                    bool pressed)
{
    uint32_t wire_keycode;

    if (server == NULL || server->destroying || xkb_key_name == NULL) {
        return false;
    }
    if (!keyboard_wire_keycode(server, xkb_key_name, &wire_keycode)) {
        return true;
    }
    if (server->keyboard_keys[wire_keycode] == pressed) {
        return true;
    }
    keyboard_update_wire_key(server,
                             wire_keycode,
                             milliseconds,
                             pressed);
    return true;
}

void nb_wayland_server_keyboard_cancel(struct nb_wayland_server *server,
                                       uint32_t milliseconds)
{
    size_t index;

    if (server == NULL || server->destroying) {
        return;
    }
    for (index = 0; index < NB_WAYLAND_KEY_CAPACITY; ++index) {
        if (server->keyboard_keys[index]) {
            keyboard_update_wire_key(server,
                                     (uint32_t)index,
                                     milliseconds,
                                     false);
        }
    }
    (void)keyboard_change_focus(server, NULL);
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
