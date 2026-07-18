#define _POSIX_C_SOURCE 200809L

#include "wayland_server.h"

#ifndef NIXBENCH_HAS_WAYLAND_DECORATION
#define NIXBENCH_HAS_WAYLAND_DECORATION 0
#endif
#ifndef NIXBENCH_HAS_XWAYLAND_SHELL
#define NIXBENCH_HAS_XWAYLAND_SHELL 0
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "nixbench-application-menu-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"
#if NIXBENCH_HAS_WAYLAND_DECORATION
#include "xdg-decoration-unstable-v1-server-protocol.h"
#endif
#if NIXBENCH_HAS_XWAYLAND_SHELL
#include "xwayland-shell-v1-server-protocol.h"
#endif

enum {
    NB_WAYLAND_COMPOSITOR_VERSION = 4,
    NB_WAYLAND_OUTPUT_VERSION = 2,
    NB_WAYLAND_SEAT_VERSION = 5,
    NB_WAYLAND_DATA_DEVICE_MANAGER_VERSION = 1,
    NB_WAYLAND_SUBCOMPOSITOR_VERSION = 1,
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
    NB_WAYLAND_KEYBOARD_REPEAT_DELAY = 600,
    NB_WAYLAND_APPLICATION_MENU_VERSION = 1,
    NB_WAYLAND_CLIPBOARD_MAX_MIME_TYPES = 16,
    NB_WAYLAND_CLIPBOARD_MIME_CAPACITY = 128,
    NB_WAYLAND_CLIPBOARD_MAX_BYTES = 1024 * 1024
};

enum { NB_WAYLAND_DAMAGE_TILE_SIZE = 32 };

enum nb_wayland_surface_role {
    NB_WAYLAND_SURFACE_ROLE_NONE,
    NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL,
    NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL,
    NB_WAYLAND_SURFACE_ROLE_XDG_POPUP,
    NB_WAYLAND_SURFACE_ROLE_SUBSURFACE,
    NB_WAYLAND_SURFACE_ROLE_CURSOR
};

struct nb_wayland_server;
struct nb_wayland_application_menu;
struct nb_wayland_data_source;

enum nb_wayland_selection_kind {
    NB_WAYLAND_SELECTION_NONE,
    NB_WAYLAND_SELECTION_SOURCE,
    NB_WAYLAND_SELECTION_EXTERNAL
};

struct nb_wayland_positioner {
    struct wl_resource *resource;
    int32_t width;
    int32_t height;
    int32_t anchor_x;
    int32_t anchor_y;
    int32_t anchor_width;
    int32_t anchor_height;
    uint32_t anchor;
    uint32_t gravity;
    uint32_t constraint_adjustment;
    int32_t offset_x;
    int32_t offset_y;
    bool size_set;
    bool anchor_rect_set;
};

struct nb_wayland_menu_snapshot {
    struct nb_menu_model model;
    struct nb_menu_spec menus[NB_MENU_MAX_MENUS];
    struct nb_menu_item_spec items[NB_MENU_MAX_MENUS][NB_MENU_MAX_ITEMS];
    char menu_labels[NB_MENU_MAX_MENUS][NB_MENU_TEXT_CAPACITY];
    char item_labels[NB_MENU_MAX_MENUS]
                    [NB_MENU_MAX_ITEMS]
                    [NB_MENU_TEXT_CAPACITY];
};

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

struct nb_wayland_data_device_resource {
    struct nb_wayland_server *server;
    struct wl_resource *resource;
    struct wl_list link;
};

struct nb_wayland_data_source {
    struct nb_wayland_server *server;
    struct wl_resource *resource;
    struct wl_list offers;
    char mime_types[NB_WAYLAND_CLIPBOARD_MAX_MIME_TYPES]
                   [NB_WAYLAND_CLIPBOARD_MIME_CAPACITY];
    size_t mime_type_count;
    bool selection;
};

struct nb_wayland_data_offer {
    struct nb_wayland_server *server;
    struct nb_wayland_data_source *source;
    struct wl_resource *resource;
    struct wl_client *recipient;
    struct wl_list source_link;
    uint64_t external_generation;
};

struct nb_wayland_clipboard_read {
    struct nb_wayland_server *server;
    struct nb_wayland_data_source *source;
    struct wl_event_source *event_source;
    int descriptor;
    char *data;
    size_t size;
};

struct nb_wayland_clipboard_write {
    struct nb_wayland_server *server;
    struct wl_event_source *event_source;
    struct wl_list link;
    int descriptor;
    char *data;
    size_t size;
    size_t offset;
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
    struct wl_resource *popup_resource;
    struct wl_resource *subsurface_resource;
#if NIXBENCH_HAS_XWAYLAND_SHELL
    struct wl_resource *xwayland_surface_resource;
#endif
#if NIXBENCH_HAS_WAYLAND_DECORATION
    struct wl_resource *decoration_resource;
#endif
    struct wl_resource *wm_base_resource;
    struct nb_wayland_surface *popup_parent;
    struct nb_wayland_positioner popup_positioner;
    int popup_x;
    int popup_y;
    uint64_t popup_sequence;
    bool popup_grabbed;
    bool popup_dismissed;
    bool subsurface_synchronized;
    bool window_geometry_set;
    int window_geometry_x;
    int window_geometry_y;
    int window_geometry_width;
    int window_geometry_height;
    struct wl_resource *pending_buffer_resource;
    struct wl_listener pending_buffer_destroy;
    bool pending_buffer_listener_attached;
    bool pending_attach;
    struct nb_damage_region pending_damage;
    bool committed_once;
    uint32_t *pixels;
    uint32_t *composite_pixels;
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
    uint32_t xwayland_window;
    uint64_t pending_xwayland_serial;
    uint64_t xwayland_serial;
    bool pending_xwayland_serial_set;
    char title[NB_WINDOW_TITLE_CAPACITY];
    char app_id[NB_WINDOW_TITLE_CAPACITY];
    struct nb_wayland_application_menu *application_menu;
    struct nb_wayland_menu_snapshot application_menu_snapshots[2];
    unsigned int active_application_menu_snapshot;
    bool application_menu_committed;
    nb_menu_source_id application_menu_source;
};

struct nb_wayland_application_menu {
    struct nb_wayland_server *server;
    struct nb_wayland_surface *surface;
    struct wl_resource *resource;
    struct nb_wayland_menu_snapshot pending;
    bool building;
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
    struct wl_global *data_device_manager_global;
    struct wl_global *subcompositor_global;
#if NIXBENCH_HAS_WAYLAND_DECORATION
    struct wl_global *decoration_manager_global;
#endif
#if NIXBENCH_HAS_XWAYLAND_SHELL
    struct wl_global *xwayland_shell_global;
#endif
    struct wl_global *xdg_wm_base_global;
    struct wl_global *application_menu_global;
    struct wl_list output_resources;
    struct wl_list pointer_resources;
    struct wl_list keyboard_resources;
    struct wl_list data_device_resources;
    struct wl_list clipboard_writes;
    enum nb_wayland_selection_kind selection_kind;
    struct nb_wayland_data_source *selection_source;
    struct nb_wayland_clipboard_read *clipboard_read;
    char *clipboard_text;
    size_t clipboard_text_size;
    uint64_t selection_generation;
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
    bool redraw_pending;
    struct nb_damage_region redraw_damage;
    struct nb_wayland_surface surfaces[NB_WAYLAND_MAX_SURFACES];
    unsigned int next_window_position;
    uint64_t next_popup_sequence;
    struct nb_wayland_xwayland_interface xwayland_interface;
    void *xwayland_context;
    pid_t xwayland_client_pid;
#if NIXBENCH_HAS_XWAYLAND_SHELL
    struct wl_client *xwayland_client;
    struct wl_listener xwayland_client_destroy;
#endif
    char display_name[NB_WAYLAND_DISPLAY_NAME_CAPACITY];
};

static const struct wl_surface_interface surface_implementation;
static const struct wl_output_interface output_implementation;
static const struct wl_pointer_interface pointer_implementation;
static const struct wl_keyboard_interface keyboard_implementation;
static const struct wl_subcompositor_interface subcompositor_implementation;
static const struct wl_subsurface_interface subsurface_implementation;
#if NIXBENCH_HAS_WAYLAND_DECORATION
static const struct zxdg_decoration_manager_v1_interface
    decoration_manager_implementation;
static const struct zxdg_toplevel_decoration_v1_interface
    decoration_implementation;
static void bind_decoration_manager(struct wl_client *client,
                                    void *data,
                                    uint32_t version,
                                    uint32_t id);
#endif
#if NIXBENCH_HAS_XWAYLAND_SHELL
static const struct xwayland_shell_v1_interface
    xwayland_shell_implementation;
static const struct xwayland_surface_v1_interface
    xwayland_surface_implementation;
static void bind_xwayland_shell(struct wl_client *client,
                                void *data,
                                uint32_t version,
                                uint32_t id);
#endif
static const struct xdg_surface_interface xdg_surface_implementation;
static const struct xdg_toplevel_interface toplevel_implementation;
static const struct xdg_popup_interface popup_implementation;
static const struct nixbench_application_menu_manager_v1_interface
    application_menu_manager_implementation;
static const struct nixbench_application_menu_v1_interface
    application_menu_implementation;

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

static bool menu_label_is_valid(const char *label)
{
    size_t length = 0;

    if (label == NULL || label[0] == '\0') {
        return false;
    }
    while (length < NB_MENU_TEXT_CAPACITY && label[length] != '\0') {
        ++length;
    }
    return length < NB_MENU_TEXT_CAPACITY;
}

static void menu_snapshot_reset(struct nb_wayland_menu_snapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->model.menus = snapshot->menus;
}

static bool menu_snapshot_contains_command(
    const struct nb_wayland_menu_snapshot *snapshot,
    nb_menu_command command)
{
    size_t menu_index;

    for (menu_index = 0; menu_index < snapshot->model.menu_count;
         ++menu_index) {
        const struct nb_menu_spec *menu = &snapshot->menus[menu_index];
        size_t item_index;

        for (item_index = 0; item_index < menu->item_count; ++item_index) {
            const struct nb_menu_item_spec *item =
                &snapshot->items[menu_index][item_index];

            if (item->kind == NB_MENU_ITEM_COMMAND &&
                item->command == command) {
                return true;
            }
        }
    }
    return false;
}

static bool menu_snapshot_command_is_actionable(
    const struct nb_wayland_menu_snapshot *snapshot,
    nb_menu_command command)
{
    size_t menu_index;

    for (menu_index = 0; menu_index < snapshot->model.menu_count;
         ++menu_index) {
        const struct nb_menu_spec *menu = &snapshot->menus[menu_index];
        size_t item_index;

        for (item_index = 0; item_index < menu->item_count; ++item_index) {
            const struct nb_menu_item_spec *item =
                &snapshot->items[menu_index][item_index];

            if (item->command == command &&
                nb_menu_item_is_actionable(item)) {
                return true;
            }
        }
    }
    return false;
}

static void menu_snapshot_copy(struct nb_wayland_menu_snapshot *destination,
                               const struct nb_wayland_menu_snapshot *source)
{
    size_t menu_index;

    menu_snapshot_reset(destination);
    destination->model.menu_count = source->model.menu_count;
    for (menu_index = 0; menu_index < source->model.menu_count;
         ++menu_index) {
        const struct nb_menu_spec *source_menu = &source->menus[menu_index];
        struct nb_menu_spec *destination_menu =
            &destination->menus[menu_index];
        size_t item_index;

        memcpy(destination->menu_labels[menu_index],
               source->menu_labels[menu_index],
               NB_MENU_TEXT_CAPACITY);
        destination_menu->label = destination->menu_labels[menu_index];
        destination_menu->items = destination->items[menu_index];
        destination_menu->item_count = source_menu->item_count;
        for (item_index = 0; item_index < source_menu->item_count;
             ++item_index) {
            const struct nb_menu_item_spec *source_item =
                &source->items[menu_index][item_index];
            struct nb_menu_item_spec *destination_item =
                &destination->items[menu_index][item_index];

            *destination_item = *source_item;
            if (source_item->kind == NB_MENU_ITEM_COMMAND) {
                memcpy(destination->item_labels[menu_index][item_index],
                       source->item_labels[menu_index][item_index],
                       NB_MENU_TEXT_CAPACITY);
                destination_item->label =
                    destination->item_labels[menu_index][item_index];
            } else {
                destination_item->label = NULL;
            }
        }
    }
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

static struct nb_wayland_surface *find_surface_by_resource_id(
    struct nb_wayland_server *server,
    uint32_t resource_id)
{
    size_t index;

    if (resource_id == 0) {
        return NULL;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *surface = &server->surfaces[index];

        if (surface->occupied && surface->surface_resource != NULL &&
            wl_resource_get_id(surface->surface_resource) == resource_id) {
            return surface;
        }
    }
    return NULL;
}

static struct nb_wayland_surface *find_surface_by_xwayland_window(
    struct nb_wayland_server *server,
    uint32_t xwindow)
{
    size_t index;

    if (xwindow == 0) {
        return NULL;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *surface = &server->surfaces[index];

        if (surface->occupied &&
            surface->role == NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL &&
            surface->xwayland_window == xwindow) {
            return surface;
        }
    }
    return NULL;
}

static struct nb_wayland_surface *find_surface_by_xwayland_serial(
    struct nb_wayland_server *server,
    uint64_t serial)
{
    size_t index;

    if (serial == 0) {
        return NULL;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *surface = &server->surfaces[index];

        if (surface->occupied &&
            surface->role == NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL &&
            surface->xwayland_serial == serial) {
            return surface;
        }
    }
    return NULL;
}

static bool surface_has_xdg_role(
    const struct nb_wayland_surface *surface)
{
    return surface != NULL &&
           (surface->toplevel_resource != NULL ||
            surface->popup_resource != NULL);
}

static bool surface_has_overlay_role(
    const struct nb_wayland_surface *surface)
{
    return surface != NULL &&
           (surface->popup_resource != NULL ||
            surface->subsurface_resource != NULL);
}

static struct nb_wayland_surface *surface_root_toplevel(
    struct nb_wayland_surface *surface)
{
    size_t depth = 0;

    while (surface != NULL &&
           (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP ||
            surface->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) &&
           depth < NB_WAYLAND_MAX_SURFACES) {
        surface = surface->popup_parent;
        ++depth;
    }
    return surface != NULL &&
                   (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
                    surface->role ==
                        NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL)
               ? surface
               : NULL;
}

static const struct nb_wayland_surface *surface_root_toplevel_const(
    const struct nb_wayland_surface *surface)
{
    size_t depth = 0;

    while (surface != NULL &&
           (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP ||
            surface->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) &&
           depth < NB_WAYLAND_MAX_SURFACES) {
        surface = surface->popup_parent;
        ++depth;
    }
    return surface != NULL &&
                   (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
                    surface->role ==
                        NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL)
               ? surface
               : NULL;
}

static bool surface_root_is_visible(
    const struct nb_wayland_surface *surface)
{
    const struct nb_wayland_surface *root =
        surface_root_toplevel_const(surface);
    const struct nb_window *window;

    if (root == NULL || root->window == NB_WINDOW_ID_NONE) {
        return true;
    }
    window = nb_desktop_find_window(&root->server->shell->desktop,
                                    root->window);
    return window == NULL || (window->visible && !window->minimized);
}

static bool surface_is_mapped(const struct nb_wayland_surface *surface)
{
    if (surface == NULL || surface->pixels == NULL ||
        surface->surface_resource == NULL) {
        return false;
    }
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL) {
        return surface->toplevel_resource != NULL &&
               surface->window != NB_WINDOW_ID_NONE;
    }
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) {
        return surface->xwayland_window != 0 &&
               surface->window != NB_WINDOW_ID_NONE;
    }
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP) {
        return surface->popup_resource != NULL && surface->configured &&
               !surface->popup_dismissed &&
               surface_is_mapped(surface->popup_parent);
    }
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) {
        return surface->subsurface_resource != NULL &&
               surface->popup_parent != NULL &&
               surface_is_mapped(surface->popup_parent);
    }
    return false;
}

static void surface_window_geometry(
    const struct nb_wayland_surface *surface,
    int *x,
    int *y,
    int *width,
    int *height)
{
    *x = surface->window_geometry_set ? surface->window_geometry_x : 0;
    *y = surface->window_geometry_set ? surface->window_geometry_y : 0;
    *width = surface->window_geometry_set
                 ? surface->window_geometry_width
                 : surface->width;
    *height = surface->window_geometry_set
                  ? surface->window_geometry_height
                  : surface->height;
}

#if NIXBENCH_HAS_WAYLAND_DECORATION
static void send_decoration_configure(struct nb_wayland_surface *surface);
#endif

static void wayland_toplevel_content_size(
    const struct nb_wayland_server *server,
    int *width,
    int *height)
{
    const struct nb_rect viewport = {
        0,
        0,
        server != NULL ? server->output_width : 0,
        server != NULL ? server->output_height : 0
    };
    const struct nb_rect work = nb_menu_work_area(viewport);

    *width = work.width - (2 * NB_WINDOW_BORDER_WIDTH);
    *height = work.height - (2 * NB_WINDOW_BORDER_WIDTH) -
              NB_WINDOW_TITLE_HEIGHT - NB_WINDOW_FOOTER_HEIGHT;
    if (*width < NB_WINDOW_MIN_WIDTH) {
        *width = NB_WINDOW_MIN_WIDTH;
    }
    if (*height < NB_WINDOW_MIN_HEIGHT) {
        *height = NB_WINDOW_MIN_HEIGHT;
    }
}

static void send_toplevel_configure(struct nb_wayland_surface *surface,
                                    int width,
                                    int height)
{
    struct wl_array states;
    uint32_t serial;

    if (surface == NULL || surface->toplevel_resource == NULL) {
        return;
    }
    wl_array_init(&states);
    xdg_toplevel_send_configure(surface->toplevel_resource,
                                width,
                                height,
                                &states);
    wl_array_release(&states);
#if NIXBENCH_HAS_WAYLAND_DECORATION
    send_decoration_configure(surface);
#endif
    serial = wl_display_next_serial(surface->server->display);
    surface->configure_serial = serial;
    surface->configure_sent = true;
    surface->configured = false;
    xdg_surface_send_configure(surface->xdg_surface_resource, serial);
}

static void send_work_area_configure(struct nb_wayland_surface *surface)
{
    int width;
    int height;

    if (surface == NULL) {
        return;
    }
    wayland_toplevel_content_size(surface->server, &width, &height);
    send_toplevel_configure(surface, width, height);
}

#if NIXBENCH_HAS_WAYLAND_DECORATION
static void send_decoration_configure(struct nb_wayland_surface *surface)
{
    if (surface == NULL || surface->decoration_resource == NULL) {
        return;
    }
    zxdg_toplevel_decoration_v1_send_configure(
        surface->decoration_resource,
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
#endif

static bool surface_buffer_origin_in_root(
    const struct nb_wayland_surface *surface,
    int64_t *origin_x,
    int64_t *origin_y)
{
    int parent_geometry_x;
    int parent_geometry_y;
    int parent_geometry_width;
    int parent_geometry_height;
    int surface_geometry_x;
    int surface_geometry_y;
    int surface_geometry_width;
    int surface_geometry_height;

    if (surface == NULL) {
        return false;
    }
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
        surface->role == NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) {
        *origin_x = 0;
        *origin_y = 0;
        return true;
    }
    if (surface->popup_parent == NULL ||
        !surface_buffer_origin_in_root(surface->popup_parent,
                                       origin_x,
                                       origin_y)) {
        return false;
    }
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP) {
        surface_window_geometry(surface->popup_parent,
                                &parent_geometry_x,
                                &parent_geometry_y,
                                &parent_geometry_width,
                                &parent_geometry_height);
        surface_window_geometry(surface,
                                &surface_geometry_x,
                                &surface_geometry_y,
                                &surface_geometry_width,
                                &surface_geometry_height);
        *origin_x += (int64_t)parent_geometry_x + surface->popup_x -
                     surface_geometry_x;
        *origin_y += (int64_t)parent_geometry_y + surface->popup_y -
                     surface_geometry_y;
        (void)parent_geometry_width;
        (void)parent_geometry_height;
        (void)surface_geometry_width;
        (void)surface_geometry_height;
        return true;
    }
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) {
        *origin_x += surface->popup_x;
        *origin_y += surface->popup_y;
        return true;
    }
    return false;
}

static uint32_t blend_premultiplied_argb(uint32_t source,
                                         uint32_t destination)
{
    const uint32_t alpha = source >> 24;
    const uint32_t inverse = UINT32_C(255) - alpha;
    const uint32_t source_red = (source >> 16) & UINT32_C(255);
    const uint32_t source_green = (source >> 8) & UINT32_C(255);
    const uint32_t source_blue = source & UINT32_C(255);
    const uint32_t destination_alpha = destination >> 24;
    const uint32_t destination_red =
        (destination >> 16) & UINT32_C(255);
    const uint32_t destination_green =
        (destination >> 8) & UINT32_C(255);
    const uint32_t destination_blue = destination & UINT32_C(255);
    const uint32_t output_alpha =
        alpha + ((destination_alpha * inverse + 127) / 255);
    const uint32_t output_red =
        source_red + ((destination_red * inverse + 127) / 255);
    const uint32_t output_green =
        source_green + ((destination_green * inverse + 127) / 255);
    const uint32_t output_blue =
        source_blue + ((destination_blue * inverse + 127) / 255);

    return ((output_alpha > 255 ? 255 : output_alpha) << 24) |
           ((output_red > 255 ? 255 : output_red) << 16) |
           ((output_green > 255 ? 255 : output_green) << 8) |
           (output_blue > 255 ? 255 : output_blue);
}

static void composite_overlay(struct nb_wayland_surface *root,
                              const struct nb_wayland_surface *overlay)
{
    int64_t origin_x;
    int64_t origin_y;
    int source_y;

    if (root->composite_pixels == NULL || !surface_is_mapped(overlay) ||
        (overlay->role != NB_WAYLAND_SURFACE_ROLE_XDG_POPUP &&
         overlay->role != NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) ||
        !surface_buffer_origin_in_root(overlay, &origin_x, &origin_y)) {
        return;
    }
    for (source_y = 0; source_y < overlay->height; ++source_y) {
        const int64_t destination_y = origin_y + source_y;
        int source_x;

        if (destination_y < 0 || destination_y >= root->height) {
            continue;
        }
        for (source_x = 0; source_x < overlay->width; ++source_x) {
            const int64_t destination_x = origin_x + source_x;
            size_t destination_index;
            size_t source_index;

            if (destination_x < 0 || destination_x >= root->width) {
                continue;
            }
            destination_index =
                (size_t)destination_y * (size_t)root->width +
                (size_t)destination_x;
            source_index =
                (size_t)source_y * (size_t)overlay->width +
                (size_t)source_x;
            root->composite_pixels[destination_index] =
                blend_premultiplied_argb(
                    overlay->pixels[source_index],
                    root->composite_pixels[destination_index]);
        }
    }
}

static void refresh_toplevel_composite(struct nb_wayland_surface *root)
{
    uint64_t previous_sequence = 0;
    size_t pixel_count;
    bool has_overlay = false;
    size_t index;

    if (root == NULL ||
        root->role != NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL) {
        return;
    }
    free(root->composite_pixels);
    root->composite_pixels = NULL;
    if (root->pixels == NULL || root->width <= 0 || root->height <= 0 ||
        (size_t)root->width > SIZE_MAX / (size_t)root->height ||
        (size_t)root->width * (size_t)root->height >
            SIZE_MAX / sizeof(uint32_t)) {
        return;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        const struct nb_wayland_surface *candidate =
            &root->server->surfaces[index];

        if (candidate->occupied && surface_is_mapped(candidate) &&
            (candidate->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP ||
             candidate->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) &&
            surface_root_toplevel_const(candidate) == root) {
            has_overlay = true;
            break;
        }
    }
    if (!has_overlay) {
        return;
    }
    pixel_count = (size_t)root->width * (size_t)root->height;
    root->composite_pixels =
        malloc(pixel_count * sizeof(*root->composite_pixels));
    if (root->composite_pixels == NULL) {
        return;
    }
    memcpy(root->composite_pixels,
           root->pixels,
           pixel_count * sizeof(*root->composite_pixels));
    for (;;) {
        const struct nb_wayland_surface *next = NULL;

        for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
            const struct nb_wayland_surface *candidate =
                &root->server->surfaces[index];

            if (!candidate->occupied || !surface_is_mapped(candidate) ||
                (candidate->role != NB_WAYLAND_SURFACE_ROLE_XDG_POPUP &&
                 candidate->role != NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) ||
                surface_root_toplevel_const(candidate) != root ||
                candidate->popup_sequence <= previous_sequence ||
                (next != NULL && candidate->popup_sequence >=
                                     next->popup_sequence)) {
                continue;
            }
            next = candidate;
        }
        if (next == NULL) {
            break;
        }
        composite_overlay(root, next);
        previous_sequence = next->popup_sequence;
    }
}

static void advance_surface_revision(struct nb_wayland_surface *surface)
{
    ++surface->revision;
    if (surface->revision == 0) {
        surface->revision = 1;
    }
}

static void mark_redraw_full(struct nb_wayland_server *server)
{
    if (server != NULL) {
        server->redraw_pending = true;
        nb_damage_region_set_full(&server->redraw_damage);
    }
}

static void mark_redraw_rect(struct nb_wayland_server *server,
                             struct nb_rect rect)
{
    if (server == NULL || rect.width <= 0 || rect.height <= 0 ||
        server->output_width <= 0 || server->output_height <= 0) {
        return;
    }
    (void)nb_damage_region_add(
        &server->redraw_damage,
        (struct nb_damage_rect){rect.x, rect.y, rect.width, rect.height},
        server->output_width,
        server->output_height);
    server->redraw_pending = true;
}

static void clear_pending_damage(struct nb_wayland_surface *surface)
{
    nb_damage_region_clear(&surface->pending_damage);
}

static bool surface_damage_to_desktop(
    const struct nb_wayland_surface *surface,
    const struct nb_rect *damage,
    const struct nb_window *window,
    struct nb_rect *desktop_damage)
{
    const struct nb_wayland_surface *root =
        surface_root_toplevel_const(surface);
    const struct nb_rect content = nb_window_content_rect(window);
    int root_geometry_x;
    int root_geometry_y;
    int root_geometry_width;
    int root_geometry_height;
    int64_t origin_x;
    int64_t origin_y;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (root == NULL || damage == NULL || damage->width <= 0 ||
        damage->height <= 0 || desktop_damage == NULL ||
        !surface_buffer_origin_in_root(surface, &origin_x, &origin_y)) {
        return false;
    }
    surface_window_geometry(root,
                            &root_geometry_x,
                            &root_geometry_y,
                            &root_geometry_width,
                            &root_geometry_height);
    left = (int64_t)content.x + origin_x + damage->x - root_geometry_x;
    top = (int64_t)content.y + origin_y + damage->y - root_geometry_y;
    right = left + damage->width;
    bottom = top + damage->height;
    if (left < content.x) {
        left = content.x;
    }
    if (top < content.y) {
        top = content.y;
    }
    if (right > (int64_t)content.x + content.width) {
        right = (int64_t)content.x + content.width;
    }
    if (bottom > (int64_t)content.y + content.height) {
        bottom = (int64_t)content.y + content.height;
    }
    (void)root_geometry_width;
    (void)root_geometry_height;
    if (right <= left || bottom <= top || left < INT_MIN || left > INT_MAX ||
        top < INT_MIN || top > INT_MAX || right - left > INT_MAX ||
        bottom - top > INT_MAX) {
        return false;
    }
    *desktop_damage = (struct nb_rect){(int)left,
                                       (int)top,
                                       (int)(right - left),
                                       (int)(bottom - top)};
    return true;
}

static void surface_tree_damaged(struct nb_wayland_surface *surface,
                                 bool root_revision_already_changed,
                                 const struct nb_rect *damage)
{
    struct nb_wayland_surface *root = surface_root_toplevel(surface);
    const struct nb_window *window;
    struct nb_rect desktop_damage;

    if (root == NULL) {
        return;
    }
    if (!root_revision_already_changed) {
        advance_surface_revision(root);
    }
    refresh_toplevel_composite(root);
    window = root->window != NB_WINDOW_ID_NONE
                 ? nb_desktop_find_window(&root->server->shell->desktop,
                                          root->window)
                 : NULL;
    if (window != NULL && window->visible && !window->minimized) {
        if (surface_damage_to_desktop(surface,
                                      damage,
                                      window,
                                      &desktop_damage)) {
            mark_redraw_rect(root->server, desktop_damage);
        } else {
            mark_redraw_rect(root->server, window->frame);
        }
    } else if (window == NULL) {
        mark_redraw_full(root->server);
    }
}

static void surface_tree_changed(struct nb_wayland_surface *surface,
                                 bool root_revision_already_changed)
{
    surface_tree_damaged(surface, root_revision_already_changed, NULL);
}

static void surface_tree_damage_region(
    struct nb_wayland_surface *surface,
    bool root_revision_already_changed,
    const struct nb_damage_region *damage)
{
    size_t index;

    if (damage == NULL || damage->full || damage->count == 0) {
        surface_tree_damaged(surface,
                             root_revision_already_changed,
                             NULL);
        return;
    }
    for (index = 0; index < damage->count; ++index) {
        const struct nb_damage_rect rect = damage->rects[index];
        const struct nb_rect surface_rect = {
            rect.x, rect.y, rect.width, rect.height
        };

        surface_tree_damaged(surface,
                             root_revision_already_changed || index != 0,
                             &surface_rect);
    }
}

static nb_menu_source_id application_menu_source_for_surface(
    const struct nb_wayland_surface *surface)
{
    const ptrdiff_t index = surface - surface->server->surfaces;

    if (index < 0 || index >= NB_WAYLAND_MAX_SURFACES) {
        return NB_MENU_SOURCE_NONE;
    }
    return surface->server->menu_source + (nb_menu_source_id)index +
           UINT64_C(1);
}

static bool bind_surface_menu(struct nb_wayland_surface *surface,
                              nb_menu_source_id source,
                              const struct nb_menu_model *model)
{
    if (surface->window == NB_WINDOW_ID_NONE) {
        return true;
    }
    return nb_shell_update_window_menu(surface->server->shell,
                                       surface->window,
                                       source,
                                       model);
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

static void data_device_send_selection_to_client(
    struct nb_wayland_server *server,
    struct wl_client *client);

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
    uint32_t old_xwindow;
    uint32_t new_xwindow;

    if (new_focus != NULL &&
        (new_focus->window == NB_WINDOW_ID_NONE ||
         new_focus->surface_resource == NULL || new_focus->pixels == NULL)) {
        new_focus = NULL;
    }
    if (old_focus == new_focus) {
        return true;
    }
    old_xwindow = old_focus != NULL ? old_focus->xwayland_window : 0;
    new_xwindow = new_focus != NULL ? new_focus->xwayland_window : 0;
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
        if (old_client != new_client) {
            data_device_send_selection_to_client(server, new_client);
        }
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
    if ((old_xwindow != 0 || new_xwindow != 0) &&
        server->xwayland_interface.focus_window != NULL &&
        !server->xwayland_interface.focus_window(server->xwayland_context,
                                                 new_xwindow)) {
        return false;
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
    const struct nb_wayland_surface *root;
    const struct nb_window *window;
    struct nb_rect content;
    wl_fixed_t root_x;
    wl_fixed_t root_y;
    int64_t origin_x;
    int64_t origin_y;
    int geometry_x;
    int geometry_y;
    int geometry_width;
    int geometry_height;

    if (surface == NULL || surface->width <= 0 || surface->height <= 0 ||
        surface->surface_resource == NULL || !surface_is_mapped(surface)) {
        return false;
    }
    root = surface_root_toplevel_const(surface);
    if (root == NULL || root->window == NB_WINDOW_ID_NONE) {
        return false;
    }
    window = nb_desktop_find_window(&surface->server->shell->desktop,
                                    root->window);
    if (window == NULL) {
        return false;
    }
    content = nb_window_content_rect(window);
    if (content.width <= 0 || content.height <= 0) {
        return false;
    }
    surface_window_geometry(root,
                            &geometry_x,
                            &geometry_y,
                            &geometry_width,
                            &geometry_height);
    if (geometry_width <= 0 || geometry_height <= 0) {
        geometry_x = 0;
        geometry_y = 0;
        geometry_width = root->width;
        geometry_height = root->height;
    }
    root_x = pointer_fixed_coordinate(desktop_x,
                                      content.x,
                                      content.width,
                                      geometry_width) +
             wl_fixed_from_int(geometry_x);
    root_y = pointer_fixed_coordinate(desktop_y,
                                      content.y,
                                      content.height,
                                      geometry_height) +
             wl_fixed_from_int(geometry_y);
    if (surface == root) {
        *surface_x = root_x;
        *surface_y = root_y;
        return true;
    }
    if (!surface_buffer_origin_in_root(surface, &origin_x, &origin_y) ||
        origin_x < INT32_MIN / 256 || origin_x > INT32_MAX / 256 ||
        origin_y < INT32_MIN / 256 || origin_y > INT32_MAX / 256) {
        return false;
    }
    *surface_x = root_x - wl_fixed_from_int((int)origin_x);
    *surface_y = root_y - wl_fixed_from_int((int)origin_y);
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

static void dismiss_overlay_descendants(
    struct nb_wayland_surface *parent,
    bool send_client_events);

static void unmap_surface(struct nb_wayland_surface *surface,
                          bool send_client_events)
{
    dismiss_overlay_descendants(surface, send_client_events);
    if (surface->window != NB_WINDOW_ID_NONE) {
        if (send_client_events) {
            surface_send_output_membership(surface, false);
        }
        clear_surface_keyboard_state(surface, send_client_events);
        clear_surface_pointer_state(surface, send_client_events);
        (void)nb_shell_destroy_window(surface->server->shell,
                                      surface->window);
        surface->window = NB_WINDOW_ID_NONE;
        mark_redraw_full(surface->server);
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

static void dismiss_overlay_descendants(
    struct nb_wayland_surface *parent,
    bool send_client_events)
{
    struct nb_wayland_server *server;
    size_t index;

    if (parent == NULL || parent->server == NULL) {
        return;
    }
    server = parent->server;
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *child = &server->surfaces[index];
        struct nb_wayland_surface *root;
        bool was_mapped;

        if (!child->occupied ||
            child->popup_parent != parent ||
            (child->role != NB_WAYLAND_SURFACE_ROLE_XDG_POPUP &&
             child->role != NB_WAYLAND_SURFACE_ROLE_SUBSURFACE)) {
            continue;
        }
        root = surface_root_toplevel(child);
        was_mapped = surface_is_mapped(child);
        dismiss_overlay_descendants(child, send_client_events);
        if (child->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP) {
            if (!child->popup_dismissed) {
                child->popup_dismissed = true;
                if (send_client_events && !server->destroying) {
                    xdg_popup_send_popup_done(child->popup_resource);
                }
            }
        }
        clear_surface_keyboard_state(child, send_client_events);
        clear_surface_pointer_state(child, send_client_events);
        clear_pending_attach(child);
        destroy_frame_resources(child);
        if (was_mapped && send_client_events) {
            surface_send_output_membership(child, false);
        }
        free(child->pixels);
        free(child->composite_pixels);
        child->pixels = NULL;
        child->composite_pixels = NULL;
        child->width = 0;
        child->height = 0;
        child->configure_sent = false;
        child->configured = false;
        child->configure_serial = 0;
        child->popup_parent = NULL;
        child->popup_grabbed = false;
        if (child->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) {
            child->subsurface_synchronized = true;
        }
        if (root != NULL) {
            surface_tree_changed(root, false);
        }
    }
}

static void maybe_release_surface_slot(struct nb_wayland_surface *surface)
{
    if (surface->surface_resource != NULL ||
        surface->xdg_surface_resource != NULL ||
        surface->toplevel_resource != NULL ||
#if NIXBENCH_HAS_XWAYLAND_SHELL
        surface->xwayland_surface_resource != NULL ||
#endif
        surface_has_overlay_role(surface)) {
        return;
    }

    clear_surface_pointer_state(surface, false);
    unmap_surface(surface, false);
    clear_pending_attach(surface);
    destroy_frame_resources(surface);
    free(surface->pixels);
    free(surface->composite_pixels);
    memset(surface, 0, sizeof(*surface));
}

static bool copy_shm_buffer(struct nb_wayland_surface *surface,
                            struct wl_resource *buffer_resource,
                            const struct nb_damage_region *requested_damage,
                            uint32_t **copied_pixels,
                            int *copied_width,
                            int *copied_height,
                            struct nb_damage_region *copied_damage,
                            bool *pixels_changed)
{
    struct wl_shm_buffer *buffer =
        wl_shm_buffer_get(buffer_resource);
    uint32_t format;
    int width;
    int height;
    int stride;
    struct nb_damage_region damage;
    struct nb_damage_region changed_damage;
    size_t row_bytes;
    size_t pixel_count;
    uint32_t *pixels;
    const unsigned char *source;
    unsigned char *changed_tiles = NULL;
    size_t damage_index;
    size_t tile_columns = 0;
    size_t tile_rows = 0;
    size_t tile_count = 0;
    bool new_allocation;

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

    new_allocation = surface->pixels == NULL || surface->width != width ||
                     surface->height != height;
    if (new_allocation) {
        pixel_count = (size_t)width * (size_t)height;
        pixels = malloc(pixel_count * sizeof(*pixels));
        if (pixels == NULL) {
            wl_resource_post_no_memory(surface->surface_resource);
            return false;
        }
        nb_damage_region_clear(&damage);
        (void)nb_damage_region_add(
            &damage,
            (struct nb_damage_rect){0, 0, width, height},
            width,
            height);
    } else {
        pixels = surface->pixels;
        nb_damage_region_clear(&damage);
        if (requested_damage != NULL && requested_damage->full) {
            (void)nb_damage_region_add(
                &damage,
                (struct nb_damage_rect){0, 0, width, height},
                width,
                height);
        } else if (requested_damage != NULL) {
            for (damage_index = 0;
                 damage_index < requested_damage->count;
                 ++damage_index) {
                (void)nb_damage_region_add(
                    &damage,
                    requested_damage->rects[damage_index],
                    width,
                    height);
            }
        }
    }

    nb_damage_region_clear(&changed_damage);
    if (!new_allocation && damage.count != 0) {
        tile_columns = ((size_t)width + NB_WAYLAND_DAMAGE_TILE_SIZE - 1U) /
                       NB_WAYLAND_DAMAGE_TILE_SIZE;
        tile_rows = ((size_t)height + NB_WAYLAND_DAMAGE_TILE_SIZE - 1U) /
                    NB_WAYLAND_DAMAGE_TILE_SIZE;
        if (tile_columns == 0 || tile_rows == 0 ||
            tile_columns > SIZE_MAX / tile_rows) {
            wl_client_post_implementation_error(
                wl_resource_get_client(surface->surface_resource),
                "wl_shm damage tile grid is too large");
            return false;
        }
        tile_count = tile_columns * tile_rows;
        changed_tiles = calloc(tile_count, sizeof(*changed_tiles));
        if (changed_tiles == NULL) {
            wl_resource_post_no_memory(surface->surface_resource);
            return false;
        }
    }
    if (damage.count != 0) {
        wl_shm_buffer_begin_access(buffer);
        source = wl_shm_buffer_get_data(buffer);
    }
    for (damage_index = 0; damage_index < damage.count; ++damage_index) {
        const struct nb_damage_rect rect = damage.rects[damage_index];
        int y;

        for (y = rect.y; y < rect.y + rect.height; ++y) {
            const unsigned char *row =
                source + ((size_t)y * (size_t)stride);
            int x;

            for (x = rect.x; x < rect.x + rect.width; ++x) {
                uint32_t pixel;
                const size_t destination_index =
                    (size_t)y * (size_t)width + (size_t)x;

                memcpy(&pixel,
                       row + ((size_t)x * sizeof(pixel)),
                       sizeof(pixel));
                if (format == WL_SHM_FORMAT_XRGB8888) {
                    pixel |= UINT32_C(0xff000000);
                }
                if (new_allocation || pixels[destination_index] != pixel) {
                    pixels[destination_index] = pixel;
                    if (changed_tiles != NULL) {
                        const size_t tile_x =
                            (size_t)x / NB_WAYLAND_DAMAGE_TILE_SIZE;
                        const size_t tile_y =
                            (size_t)y / NB_WAYLAND_DAMAGE_TILE_SIZE;

                        changed_tiles[tile_y * tile_columns + tile_x] = 1;
                    }
                }
            }
        }
    }
    if (damage.count != 0) {
        wl_shm_buffer_end_access(buffer);
    }

    if (new_allocation && damage.count != 0) {
        (void)nb_damage_region_add(
            &changed_damage,
            (struct nb_damage_rect){0, 0, width, height},
            width,
            height);
    } else if (changed_tiles != NULL) {
        size_t tile_y;

        for (tile_y = 0; tile_y < tile_rows; ++tile_y) {
            size_t tile_x = 0;

            while (tile_x < tile_columns) {
                size_t first_tile;
                int x;
                int y;
                int right;
                int bottom;

                while (tile_x < tile_columns &&
                       changed_tiles[tile_y * tile_columns + tile_x] == 0) {
                    ++tile_x;
                }
                if (tile_x == tile_columns) {
                    break;
                }
                first_tile = tile_x;
                while (tile_x < tile_columns &&
                       changed_tiles[tile_y * tile_columns + tile_x] != 0) {
                    ++tile_x;
                }
                x = (int)(first_tile * NB_WAYLAND_DAMAGE_TILE_SIZE);
                y = (int)(tile_y * NB_WAYLAND_DAMAGE_TILE_SIZE);
                right = (int)(tile_x * NB_WAYLAND_DAMAGE_TILE_SIZE);
                bottom = y + NB_WAYLAND_DAMAGE_TILE_SIZE;
                if (right > width) {
                    right = width;
                }
                if (bottom > height) {
                    bottom = height;
                }
                (void)nb_damage_region_add(
                    &changed_damage,
                    (struct nb_damage_rect){x,
                                             y,
                                             right - x,
                                             bottom - y},
                    width,
                    height);
            }
        }
    }
    free(changed_tiles);

    *copied_pixels = pixels;
    *copied_width = width;
    *copied_height = height;
    /*
     * A frame callback still needs one presentation when a client damages
     * pixels whose visible values did not change. Keep that no-op redraw
     * microscopic rather than reusing the client's broad declaration.
     */
    if (changed_damage.count == 0 && damage.count != 0) {
        (void)nb_damage_region_add(
            &changed_damage,
            (struct nb_damage_rect){damage.rects[0].x,
                                     damage.rects[0].y,
                                     1,
                                     1},
            width,
            height);
    }

    *copied_damage = changed_damage;
    *pixels_changed = changed_damage.count != 0;
    return true;
}

static void map_surface(struct nb_wayland_surface *surface)
{
    const int cascade =
        (int)(surface->server->next_window_position %
              (unsigned int)NB_WAYLAND_CASCADE_COUNT) *
        NB_WAYLAND_CASCADE;
    int geometry_x;
    int geometry_y;
    int geometry_width;
    int geometry_height;
    struct nb_rect frame;
    const char *title;

    if ((surface->role != NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL &&
         surface->role != NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) ||
        surface->pixels == NULL ||
        surface->window != NB_WINDOW_ID_NONE) {
        return;
    }

    surface_window_geometry(surface,
                            &geometry_x,
                            &geometry_y,
                            &geometry_width,
                            &geometry_height);
    frame.x = NB_WAYLAND_INITIAL_X + cascade;
    frame.y = NB_WAYLAND_INITIAL_Y + cascade;
    frame.width = geometry_width + (2 * NB_WINDOW_BORDER_WIDTH);
    frame.height = geometry_height + (2 * NB_WINDOW_BORDER_WIDTH) +
                   NB_WINDOW_TITLE_HEIGHT + NB_WINDOW_FOOTER_HEIGHT;
    title = surface->title[0] != '\0'
                ? surface->title
                : (surface->app_id[0] != '\0'
                       ? surface->app_id
                       : (surface->role ==
                                  NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL
                              ? "X11 Application"
                              : "Wayland Application"));

    surface->window = nb_shell_open_window(
        surface->server->shell,
        title,
        frame,
        surface->application_menu_committed
            ? surface->application_menu_source
            : surface->server->menu_source,
        surface->application_menu_committed
            ? &surface->application_menu_snapshots[
                  surface->active_application_menu_snapshot].model
            : surface->server->menu_model);
    if (surface->window != NB_WINDOW_ID_NONE) {
        const struct nb_rect viewport = {
            0,
            0,
            surface->server->output_width,
            surface->server->output_height
        };

        ++surface->server->next_window_position;
        (void)nb_shell_clamp_windows(surface->server->shell, viewport);
        surface_send_output_membership(surface, true);
        mark_redraw_full(surface->server);
    }
}

static void send_initial_configure(struct nb_wayland_surface *surface)
{
    uint32_t serial;

    if (surface->xdg_surface_resource == NULL) {
        return;
    }
    if (surface->toplevel_resource != NULL) {
        /* A zero size lets the client commit its preferred initial size. */
        send_toplevel_configure(surface, 0, 0);
    } else if (surface->popup_resource != NULL &&
               !surface->popup_dismissed) {
        xdg_popup_send_configure(
            surface->popup_resource,
            surface->popup_x,
            surface->popup_y,
            surface->popup_positioner.width,
            surface->popup_positioner.height);
        serial = wl_display_next_serial(surface->server->display);
        surface->configure_serial = serial;
        surface->configure_sent = true;
        surface->configured = false;
        xdg_surface_send_configure(surface->xdg_surface_resource, serial);
    } else {
        return;
    }
}

static void detach_surface_application_menu(
    struct nb_wayland_surface *surface)
{
    if (surface->application_menu != NULL &&
        surface->application_menu->surface == surface) {
        surface->application_menu->surface = NULL;
    }
    surface->application_menu = NULL;
    surface->application_menu_committed = false;
    surface->active_application_menu_snapshot = 0;
    menu_snapshot_reset(&surface->application_menu_snapshots[0]);
    menu_snapshot_reset(&surface->application_menu_snapshots[1]);
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
    free(surface->composite_pixels);
    surface->pixels = NULL;
    surface->composite_pixels = NULL;
    detach_surface_application_menu(surface);
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
        surface->toplevel_resource != NULL ||
        surface_has_overlay_role(surface)) {
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
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    int64_t right;
    int64_t bottom;
    struct nb_damage_rect damage;

    (void)client;
    if (surface == NULL || width <= 0 || height <= 0) {
        return;
    }
    right = (int64_t)x + width;
    bottom = (int64_t)y + height;
    if (right <= 0 || bottom <= 0) {
        return;
    }
    if (right > INT_MAX) {
        right = INT_MAX;
    }
    if (bottom > INT_MAX) {
        bottom = INT_MAX;
    }
    damage.x = x < 0 ? 0 : x;
    damage.y = y < 0 ? 0 : y;
    damage.width = (int)right - damage.x;
    damage.height = (int)bottom - damage.y;
    (void)nb_damage_region_add(&surface->pending_damage,
                               damage,
                               INT_MAX,
                               INT_MAX);
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
    if (surface->pending_xwayland_serial_set) {
        surface->xwayland_serial = surface->pending_xwayland_serial;
        surface->pending_xwayland_serial = 0;
        surface->pending_xwayland_serial_set = false;
    }
    if (surface->xdg_surface_resource != NULL &&
        !surface_has_xdg_role(surface)) {
        wl_resource_post_error(surface->xdg_surface_resource,
                               XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "xdg_surface has no role");
        return;
    }
    if (surface->popup_resource != NULL &&
        (surface->popup_dismissed ||
         !surface_is_mapped(surface->popup_parent))) {
        if (!surface->popup_dismissed) {
            surface->popup_dismissed = true;
            xdg_popup_send_popup_done(surface->popup_resource);
        }
        clear_pending_attach(surface);
        clear_pending_damage(surface);
        move_pending_frames_to_ready(surface);
        return;
    }

    if (surface_has_xdg_role(surface) && !surface->configure_sent) {
        if (surface->pending_attach &&
            surface->pending_buffer_resource != NULL) {
            wl_resource_post_error(surface->xdg_surface_resource,
                                   XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                                   "buffer committed before initial "
                                   "configure");
            return;
        }
        clear_pending_attach(surface);
        clear_pending_damage(surface);
        send_initial_configure(surface);
        move_pending_frames_to_ready(surface);
        return;
    }

    if (surface->pending_attach &&
        surface->pending_buffer_resource != NULL &&
        surface_has_xdg_role(surface) && !surface->configured) {
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
        struct nb_damage_region copied_damage;
        bool pixels_changed = false;
        const bool was_mapped = surface_is_mapped(surface);

        if (buffer != NULL &&
            !copy_shm_buffer(surface,
                             buffer,
                             &surface->pending_damage,
                             &new_pixels,
                             &new_width,
                             &new_height,
                             &copied_damage,
                             &pixels_changed)) {
            return;
        }

        if (buffer == NULL && was_mapped) {
            dismiss_overlay_descendants(surface, true);
        }

        detach_pending_buffer_listener(surface);
        if (surface->pixels != new_pixels) {
            free(surface->pixels);
        }
        surface->pixels = new_pixels;
        surface->width = new_width;
        surface->height = new_height;
        surface->pending_buffer_resource = NULL;
        surface->pending_attach = false;
        clear_pending_damage(surface);
        if (buffer != NULL) {
            wl_buffer_send_release(buffer);
        }

        if (surface->pixels == NULL) {
            if (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
                surface->role ==
                    NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) {
                unmap_surface(surface, true);
                surface_tree_changed(surface, false);
            } else if (was_mapped) {
                surface_send_output_membership(surface, false);
                surface_tree_changed(surface, false);
            }
            surface->configure_sent = false;
            surface->configured = false;
            surface->configure_serial = 0;
        } else if (pixels_changed) {
            advance_surface_revision(surface);
            if (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
                surface->role ==
                    NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) {
                map_surface(surface);
                surface_tree_damage_region(surface, true, &copied_damage);
            } else {
                if (!was_mapped) {
                    surface_send_output_membership(surface, true);
                }
                surface_tree_damage_region(surface, false, &copied_damage);
            }
        } else if (surface->role == NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
                   surface->role ==
                       NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) {
            map_surface(surface);
        }
    } else if (surface->pixels != NULL &&
               surface->toplevel_resource != NULL) {
        map_surface(surface);
    }

    clear_pending_damage(surface);
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
    surface->application_menu_source =
        application_menu_source_for_surface(surface);
    menu_snapshot_reset(&surface->application_menu_snapshots[0]);
    menu_snapshot_reset(&surface->application_menu_snapshots[1]);
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

static void bind_subcompositor(struct wl_client *client,
                               void *data,
                               uint32_t version,
                               uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client,
        &wl_subcompositor_interface,
        protocol_version(version, NB_WAYLAND_SUBCOMPOSITOR_VERSION),
        id);

    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &subcompositor_implementation,
                                   data,
                                   NULL);
}

static void subsurface_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    struct nb_wayland_surface *root;

    if (surface == NULL || !surface->occupied) {
        return;
    }
    root = surface_root_toplevel(surface);
    dismiss_overlay_descendants(surface, false);
    clear_surface_pointer_state(surface, false);
    clear_surface_keyboard_state(surface, false);
    clear_pending_attach(surface);
    destroy_frame_resources(surface);
    if (surface->pixels != NULL) {
        surface_send_output_membership(surface, false);
    }
    free(surface->pixels);
    free(surface->composite_pixels);
    surface->pixels = NULL;
    surface->composite_pixels = NULL;
    surface->width = 0;
    surface->height = 0;
    surface->configure_sent = false;
    surface->configured = false;
    surface->configure_serial = 0;
    surface->subsurface_resource = NULL;
    surface->popup_parent = NULL;
    surface->subsurface_synchronized = true;
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) {
        surface->role = NB_WAYLAND_SURFACE_ROLE_NONE;
    }
    if (root != NULL) {
        surface_tree_changed(root, false);
    }
    maybe_release_surface_slot(surface);
}

static void subcompositor_destroy(struct wl_client *client,
                                  struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void subsurface_destroy(struct wl_client *client,
                               struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    wl_resource_destroy(resource);
    (void)surface;
}

static void subsurface_set_position(struct wl_client *client,
                                    struct wl_resource *resource,
                                    int32_t x,
                                    int32_t y)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    struct nb_wayland_surface *root = surface_root_toplevel(surface);

    (void)client;
    if (surface->role != NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) {
        return;
    }
    surface->popup_x = x;
    surface->popup_y = y;
    if (root != NULL) {
        surface_tree_changed(surface, false);
    }
}

static void subsurface_place_above(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *sibling)
{
    (void)client;
    (void)resource;
    (void)sibling;
}

static void subsurface_place_below(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *sibling)
{
    (void)client;
    (void)resource;
    (void)sibling;
}

static void subsurface_set_sync(struct wl_client *client,
                               struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) {
        surface->subsurface_synchronized = true;
    }
}

static void subsurface_set_desync(struct wl_client *client,
                                  struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) {
        surface->subsurface_synchronized = false;
    }
}

static const struct wl_subsurface_interface subsurface_implementation = {
    .destroy = subsurface_destroy,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync
};

static void subcompositor_get_subsurface(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t id,
                                         struct wl_resource *surface,
                                         struct wl_resource *parent)
{
    struct nb_wayland_server *server =
        wl_resource_get_user_data(resource);
    struct nb_wayland_surface *child;
    struct nb_wayland_surface *parent_surface;

    if (!wl_resource_instance_of(surface,
                                 &wl_surface_interface,
                                 &surface_implementation) ||
        !wl_resource_instance_of(parent,
                                 &wl_surface_interface,
                                 &surface_implementation)) {
        wl_resource_post_error(resource,
                               WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "subsurface uses a foreign surface");
        return;
    }
    child = wl_resource_get_user_data(surface);
    parent_surface = wl_resource_get_user_data(parent);
    if (child == NULL || parent_surface == NULL ||
        child->server != server || parent_surface->server != server ||
        wl_resource_get_client(surface) != client ||
        wl_resource_get_client(parent) != client || child == parent_surface ||
        (child->role != NB_WAYLAND_SURFACE_ROLE_NONE &&
         child->role != NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) ||
        child->surface_resource != surface || parent_surface->surface_resource != parent ||
        surface_has_xdg_role(child) || child->subsurface_resource != NULL) {
        wl_resource_post_error(resource,
                               WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "subsurface surface is invalid");
        return;
    }
    child->subsurface_resource = wl_resource_create(
        client,
        &wl_subsurface_interface,
        wl_resource_get_version(resource),
        id);
    if (child->subsurface_resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    child->role = NB_WAYLAND_SURFACE_ROLE_SUBSURFACE;
    child->popup_parent = parent_surface;
    child->popup_x = 0;
    child->popup_y = 0;
    child->popup_sequence = ++server->next_popup_sequence;
    if (server->next_popup_sequence == 0) {
        server->next_popup_sequence = 1;
    }
    child->subsurface_synchronized = true;
    wl_resource_set_implementation(child->subsurface_resource,
                                   &subsurface_implementation,
                                   child,
                                   subsurface_resource_destroyed);
}

static const struct wl_subcompositor_interface
    subcompositor_implementation = {
        .destroy = subcompositor_destroy,
        .get_subsurface = subcompositor_get_subsurface
    };

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

static bool clipboard_set_descriptor_flags(int descriptor,
                                           bool nonblocking)
{
    int flags = fcntl(descriptor, F_GETFD);

    if (flags < 0 ||
        fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0) {
        return false;
    }
    if (!nonblocking) {
        return true;
    }
    flags = fcntl(descriptor, F_GETFL);
    return flags >= 0 &&
           fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

static ssize_t clipboard_write_without_sigpipe(int descriptor,
                                               const void *bytes,
                                               size_t size)
{
    const struct timespec no_wait = {0, 0};
    sigset_t blocked;
    sigset_t previous;
    sigset_t pending;
    ssize_t count;
    int saved_error;
    int was_pending;

    if (sigemptyset(&blocked) != 0 ||
        sigaddset(&blocked, SIGPIPE) != 0 ||
        sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        return -1;
    }
    if (sigpending(&pending) != 0 ||
        (was_pending = sigismember(&pending, SIGPIPE)) < 0) {
        saved_error = errno;
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        errno = saved_error;
        return -1;
    }
    count = write(descriptor, bytes, size);
    saved_error = errno;
    if (count < 0 && saved_error == EPIPE && was_pending == 0) {
        int result;

        do {
            result = sigtimedwait(&blocked, NULL, &no_wait);
        } while (result < 0 && errno == EINTR);
    }
    if (sigprocmask(SIG_SETMASK, &previous, NULL) != 0) {
        return -1;
    }
    errno = saved_error;
    return count;
}

static bool clipboard_mime_is_external_text(const char *mime_type)
{
    return mime_type != NULL &&
           (strcmp(mime_type, "text/plain;charset=utf-8") == 0 ||
            strcmp(mime_type, "text/plain;charset=UTF-8") == 0 ||
            strcmp(mime_type, "text/plain") == 0 ||
            strcmp(mime_type, "UTF8_STRING") == 0 ||
            strcmp(mime_type, "STRING") == 0);
}

static bool data_source_offers_mime(
    const struct nb_wayland_data_source *source,
    const char *mime_type)
{
    size_t index;

    if (source == NULL || mime_type == NULL) {
        return false;
    }
    for (index = 0; index < source->mime_type_count; ++index) {
        if (strcmp(source->mime_types[index], mime_type) == 0) {
            return true;
        }
    }
    return false;
}

static const char *data_source_preferred_text_mime(
    const struct nb_wayland_data_source *source)
{
    static const char *const preferred[] = {
        "text/plain;charset=utf-8",
        "text/plain;charset=UTF-8",
        "UTF8_STRING",
        "text/plain",
        "STRING"
    };
    size_t index;

    for (index = 0; index < sizeof(preferred) / sizeof(preferred[0]);
         ++index) {
        if (data_source_offers_mime(source, preferred[index])) {
            return preferred[index];
        }
    }
    return NULL;
}

static void clipboard_write_destroy(
    struct nb_wayland_clipboard_write *transfer)
{
    if (transfer == NULL) {
        return;
    }
    if (transfer->event_source != NULL) {
        wl_event_source_remove(transfer->event_source);
        transfer->event_source = NULL;
    }
    if (transfer->descriptor >= 0) {
        (void)close(transfer->descriptor);
        transfer->descriptor = -1;
    }
    wl_list_remove(&transfer->link);
    wl_list_init(&transfer->link);
    free(transfer->data);
    free(transfer);
}

static int clipboard_write_ready(int descriptor,
                                 uint32_t mask,
                                 void *data)
{
    struct nb_wayland_clipboard_write *transfer = data;

    if ((mask & WL_EVENT_ERROR) != 0) {
        clipboard_write_destroy(transfer);
        return 0;
    }
    while (transfer->offset < transfer->size) {
        ssize_t count = clipboard_write_without_sigpipe(
            descriptor,
            transfer->data + transfer->offset,
            transfer->size - transfer->offset);

        if (count > 0) {
            transfer->offset += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        clipboard_write_destroy(transfer);
        return 0;
    }
    clipboard_write_destroy(transfer);
    return 0;
}

static bool clipboard_start_write(struct nb_wayland_server *server,
                                  int descriptor,
                                  const char *data,
                                  size_t size)
{
    struct nb_wayland_clipboard_write *transfer;

    if (descriptor < 0 || data == NULL ||
        size > NB_WAYLAND_CLIPBOARD_MAX_BYTES ||
        !clipboard_set_descriptor_flags(descriptor, true)) {
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        return false;
    }
    transfer = calloc(1, sizeof(*transfer));
    if (transfer == NULL) {
        (void)close(descriptor);
        return false;
    }
    transfer->data = malloc(size == 0 ? 1 : size);
    if (transfer->data == NULL) {
        (void)close(descriptor);
        free(transfer);
        return false;
    }
    if (size != 0) {
        memcpy(transfer->data, data, size);
    }
    transfer->server = server;
    transfer->descriptor = descriptor;
    transfer->size = size;
    wl_list_init(&transfer->link);
    wl_list_insert(&server->clipboard_writes, &transfer->link);
    transfer->event_source = wl_event_loop_add_fd(
        server->event_loop,
        descriptor,
        WL_EVENT_WRITABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
        clipboard_write_ready,
        transfer);
    if (transfer->event_source == NULL) {
        clipboard_write_destroy(transfer);
        return false;
    }
    if (size == 0) {
        clipboard_write_destroy(transfer);
    }
    return true;
}

static void data_offer_accept(struct wl_client *client,
                              struct wl_resource *resource,
                              uint32_t serial,
                              const char *mime_type)
{
    (void)client;
    (void)resource;
    (void)serial;
    (void)mime_type;
}

static void data_offer_receive(struct wl_client *client,
                               struct wl_resource *resource,
                               const char *mime_type,
                               int32_t descriptor)
{
    struct nb_wayland_data_offer *offer =
        wl_resource_get_user_data(resource);

    (void)client;
    if (offer == NULL || descriptor < 0) {
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        return;
    }
    if (offer->source != NULL && offer->source->resource != NULL &&
        data_source_offers_mime(offer->source, mime_type)) {
        wl_data_source_send_send(offer->source->resource,
                                 mime_type,
                                 descriptor);
        (void)close(descriptor);
        return;
    }
    if (offer->source == NULL &&
        offer->external_generation != 0 &&
        offer->server->selection_kind == NB_WAYLAND_SELECTION_EXTERNAL &&
        offer->external_generation ==
            offer->server->selection_generation &&
        clipboard_mime_is_external_text(mime_type) &&
        offer->server->clipboard_text != NULL) {
        (void)clipboard_start_write(offer->server,
                                    descriptor,
                                    offer->server->clipboard_text,
                                    offer->server->clipboard_text_size);
        return;
    }
    (void)close(descriptor);
}

static void data_offer_destroy(struct wl_client *client,
                               struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void data_offer_finish(struct wl_client *client,
                              struct wl_resource *resource)
{
    (void)client;
    (void)resource;
}

static void data_offer_set_actions(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t dnd_actions,
                                   uint32_t preferred_action)
{
    (void)client;
    (void)resource;
    (void)dnd_actions;
    (void)preferred_action;
}

static const struct wl_data_offer_interface data_offer_implementation = {
    .accept = data_offer_accept,
    .receive = data_offer_receive,
    .destroy = data_offer_destroy,
    .finish = data_offer_finish,
    .set_actions = data_offer_set_actions
};

static void data_offer_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_data_offer *offer =
        wl_resource_get_user_data(resource);

    if (offer == NULL) {
        return;
    }
    offer->resource = NULL;
    if (!wl_list_empty(&offer->source_link)) {
        wl_list_remove(&offer->source_link);
        wl_list_init(&offer->source_link);
    }
    free(offer);
}

static void data_source_invalidate_offers(
    struct nb_wayland_data_source *source)
{
    struct nb_wayland_data_offer *offer;
    struct nb_wayland_data_offer *temporary;

    wl_list_for_each_safe(offer, temporary, &source->offers, source_link) {
        wl_list_remove(&offer->source_link);
        wl_list_init(&offer->source_link);
        offer->source = NULL;
    }
}

static void clipboard_notify_xwayland(struct nb_wayland_server *server,
                                      bool available)
{
    if (!server->destroying &&
        server->xwayland_interface.set_clipboard_owner != NULL &&
        !server->xwayland_interface.set_clipboard_owner(
            server->xwayland_context,
            available)) {
        fprintf(stderr,
                "Rootless Xwayland could not %s the text clipboard\n",
                available ? "publish" : "release");
    }
}

static void clipboard_read_finish(
    struct nb_wayland_clipboard_read *transfer,
    bool success)
{
    struct nb_wayland_server *server;

    if (transfer == NULL) {
        return;
    }
    server = transfer->server;
    if (transfer->event_source != NULL) {
        wl_event_source_remove(transfer->event_source);
        transfer->event_source = NULL;
    }
    if (transfer->descriptor >= 0) {
        (void)close(transfer->descriptor);
        transfer->descriptor = -1;
    }
    if (server->clipboard_read == transfer) {
        server->clipboard_read = NULL;
    }
    if (success &&
        server->selection_kind == NB_WAYLAND_SELECTION_SOURCE &&
        server->selection_source == transfer->source &&
        transfer->source != NULL && transfer->source->resource != NULL) {
        free(server->clipboard_text);
        transfer->data[transfer->size] = '\0';
        server->clipboard_text = transfer->data;
        server->clipboard_text_size = transfer->size;
        transfer->data = NULL;
        clipboard_notify_xwayland(server, true);
    }
    free(transfer->data);
    free(transfer);
}

static int clipboard_read_ready(int descriptor,
                                uint32_t mask,
                                void *data)
{
    struct nb_wayland_clipboard_read *transfer = data;

    if ((mask & WL_EVENT_ERROR) != 0) {
        clipboard_read_finish(transfer, false);
        return 0;
    }
    for (;;) {
        const size_t remaining =
            NB_WAYLAND_CLIPBOARD_MAX_BYTES + 1 - transfer->size;
        ssize_t count;

        if (remaining == 0) {
            clipboard_read_finish(transfer, false);
            return 0;
        }
        count = read(descriptor,
                     transfer->data + transfer->size,
                     remaining);
        if (count > 0) {
            transfer->size += (size_t)count;
            if (transfer->size > NB_WAYLAND_CLIPBOARD_MAX_BYTES) {
                clipboard_read_finish(transfer, false);
                return 0;
            }
            continue;
        }
        if (count == 0) {
            clipboard_read_finish(transfer, true);
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        clipboard_read_finish(transfer, false);
        return 0;
    }
}

static void clipboard_cancel_read(struct nb_wayland_server *server)
{
    if (server->clipboard_read != NULL) {
        clipboard_read_finish(server->clipboard_read, false);
    }
}

static bool clipboard_start_read(
    struct nb_wayland_server *server,
    struct nb_wayland_data_source *source)
{
    struct nb_wayland_clipboard_read *transfer;
    const char *mime_type = data_source_preferred_text_mime(source);
    int descriptors[2] = {-1, -1};

    if (mime_type == NULL || source->resource == NULL ||
        pipe(descriptors) != 0 ||
        !clipboard_set_descriptor_flags(descriptors[0], true) ||
        !clipboard_set_descriptor_flags(descriptors[1], false)) {
        if (descriptors[0] >= 0) {
            (void)close(descriptors[0]);
        }
        if (descriptors[1] >= 0) {
            (void)close(descriptors[1]);
        }
        return false;
    }
    transfer = calloc(1, sizeof(*transfer));
    if (transfer == NULL) {
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        return false;
    }
    transfer->data = malloc(NB_WAYLAND_CLIPBOARD_MAX_BYTES + 1);
    if (transfer->data == NULL) {
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        free(transfer);
        return false;
    }
    transfer->server = server;
    transfer->source = source;
    transfer->descriptor = descriptors[0];
    transfer->event_source = wl_event_loop_add_fd(
        server->event_loop,
        descriptors[0],
        WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
        clipboard_read_ready,
        transfer);
    if (transfer->event_source == NULL) {
        (void)close(descriptors[1]);
        clipboard_read_finish(transfer, false);
        return false;
    }
    server->clipboard_read = transfer;
    wl_data_source_send_send(source->resource,
                             mime_type,
                             descriptors[1]);
    (void)close(descriptors[1]);
    return true;
}

static void data_source_offer(struct wl_client *client,
                              struct wl_resource *resource,
                              const char *mime_type)
{
    struct nb_wayland_data_source *source =
        wl_resource_get_user_data(resource);
    size_t length;

    if (source == NULL || mime_type == NULL || source->selection) {
        wl_client_post_implementation_error(
            client, "invalid wl_data_source MIME offer");
        return;
    }
    length = strlen(mime_type);
    if (length == 0 ||
        length >= NB_WAYLAND_CLIPBOARD_MIME_CAPACITY ||
        source->mime_type_count >=
            NB_WAYLAND_CLIPBOARD_MAX_MIME_TYPES) {
        wl_client_post_implementation_error(
            client, "wl_data_source MIME offer exceeds NixBench limits");
        return;
    }
    if (data_source_offers_mime(source, mime_type)) {
        return;
    }
    memcpy(source->mime_types[source->mime_type_count],
           mime_type,
           length + 1);
    ++source->mime_type_count;
}

static void data_device_send_selection_to_client(
    struct nb_wayland_server *server,
    struct wl_client *client);

static void selection_clear(struct nb_wayland_server *server,
                            bool cancel_source,
                            bool notify_client,
                            bool release_xwayland)
{
    struct nb_wayland_data_source *source = server->selection_source;
    struct wl_client *focused_client = NULL;

    clipboard_cancel_read(server);
    if (release_xwayland &&
        server->selection_kind == NB_WAYLAND_SELECTION_SOURCE) {
        clipboard_notify_xwayland(server, false);
    }
    if (source != NULL) {
        data_source_invalidate_offers(source);
        source->selection = false;
        if (cancel_source && source->resource != NULL) {
            wl_data_source_send_cancelled(source->resource);
        }
    }
    server->selection_source = NULL;
    server->selection_kind = NB_WAYLAND_SELECTION_NONE;
    ++server->selection_generation;
    free(server->clipboard_text);
    server->clipboard_text = NULL;
    server->clipboard_text_size = 0;
    if (notify_client && !server->destroying &&
        server->keyboard_focus != NULL &&
        server->keyboard_focus->surface_resource != NULL) {
        focused_client = wl_resource_get_client(
            server->keyboard_focus->surface_resource);
        data_device_send_selection_to_client(server, focused_client);
    }
}

static void data_source_destroy(struct wl_client *client,
                                struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void data_source_set_actions(struct wl_client *client,
                                    struct wl_resource *resource,
                                    uint32_t dnd_actions)
{
    (void)client;
    (void)resource;
    (void)dnd_actions;
}

static const struct wl_data_source_interface data_source_implementation = {
    .offer = data_source_offer,
    .destroy = data_source_destroy,
    .set_actions = data_source_set_actions
};

static void data_source_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_data_source *source =
        wl_resource_get_user_data(resource);

    if (source == NULL) {
        return;
    }
    source->resource = NULL;
    if (source->server->selection_source == source) {
        selection_clear(source->server,
                        false,
                        !source->server->destroying,
                        !source->server->destroying);
    }
    data_source_invalidate_offers(source);
    free(source);
}

static void cancel_unused_data_source(struct wl_resource *resource)
{
    if (resource != NULL &&
        wl_resource_instance_of(resource,
                                &wl_data_source_interface,
                                &data_source_implementation)) {
        struct nb_wayland_data_source *source =
            wl_resource_get_user_data(resource);

        if (source != NULL && !source->selection &&
            source->resource != NULL) {
            wl_data_source_send_cancelled(source->resource);
        }
    }
}

static void data_device_start_drag(struct wl_client *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *source,
                                   struct wl_resource *origin,
                                   struct wl_resource *icon,
                                   uint32_t serial)
{
    (void)client;
    (void)resource;
    (void)origin;
    (void)icon;
    (void)serial;
    cancel_unused_data_source(source);
}

static bool data_device_send_selection(
    struct nb_wayland_data_device_resource *device)
{
    struct nb_wayland_server *server = device->server;
    struct nb_wayland_data_offer *offer;
    struct wl_client *client;
    size_t index;

    if (device->resource == NULL) {
        return false;
    }
    if (server->selection_kind == NB_WAYLAND_SELECTION_NONE ||
        (server->selection_kind == NB_WAYLAND_SELECTION_SOURCE &&
         (server->selection_source == NULL ||
          server->selection_source->resource == NULL)) ||
        (server->selection_kind == NB_WAYLAND_SELECTION_EXTERNAL &&
         server->clipboard_text == NULL)) {
        wl_data_device_send_selection(device->resource, NULL);
        return true;
    }
    offer = calloc(1, sizeof(*offer));
    if (offer == NULL) {
        wl_client_post_no_memory(
            wl_resource_get_client(device->resource));
        return false;
    }
    client = wl_resource_get_client(device->resource);
    offer->server = server;
    offer->recipient = client;
    offer->resource = wl_resource_create(
        client,
        &wl_data_offer_interface,
        wl_resource_get_version(device->resource),
        0);
    wl_list_init(&offer->source_link);
    if (offer->resource == NULL) {
        free(offer);
        wl_client_post_no_memory(client);
        return false;
    }
    if (server->selection_kind == NB_WAYLAND_SELECTION_SOURCE) {
        offer->source = server->selection_source;
        wl_list_insert(&offer->source->offers, &offer->source_link);
    } else {
        offer->external_generation = server->selection_generation;
    }
    wl_resource_set_implementation(offer->resource,
                                   &data_offer_implementation,
                                   offer,
                                   data_offer_resource_destroyed);
    wl_data_device_send_data_offer(device->resource, offer->resource);
    if (offer->source != NULL) {
        for (index = 0; index < offer->source->mime_type_count; ++index) {
            wl_data_offer_send_offer(
                offer->resource,
                offer->source->mime_types[index]);
        }
    } else {
        wl_data_offer_send_offer(offer->resource,
                                 "text/plain;charset=utf-8");
        wl_data_offer_send_offer(offer->resource, "text/plain");
        wl_data_offer_send_offer(offer->resource, "UTF8_STRING");
    }
    wl_data_device_send_selection(device->resource, offer->resource);
    return true;
}

static void data_device_send_selection_to_client(
    struct nb_wayland_server *server,
    struct wl_client *client)
{
    struct nb_wayland_data_device_resource *device;

    if (client == NULL) {
        return;
    }
    wl_list_for_each(device, &server->data_device_resources, link) {
        if (device->resource != NULL &&
            wl_resource_get_client(device->resource) == client) {
            (void)data_device_send_selection(device);
        }
    }
}

static void data_device_set_selection(struct wl_client *client,
                                      struct wl_resource *resource,
                                      struct wl_resource *source,
                                      uint32_t serial)
{
    struct nb_wayland_data_device_resource *device =
        wl_resource_get_user_data(resource);
    struct nb_wayland_server *server;
    struct nb_wayland_data_source *data_source = NULL;
    struct wl_client *focused_client = NULL;

    (void)serial;
    if (device == NULL || device->server == NULL) {
        return;
    }
    server = device->server;
    if (source != NULL) {
        if (!wl_resource_instance_of(source,
                                     &wl_data_source_interface,
                                     &data_source_implementation) ||
            wl_resource_get_client(source) != client) {
            wl_client_post_implementation_error(
                client, "wl_data_device received a foreign data source");
            return;
        }
        data_source = wl_resource_get_user_data(source);
    }
    if (server->keyboard_focus != NULL &&
        server->keyboard_focus->surface_resource != NULL) {
        focused_client = wl_resource_get_client(
            server->keyboard_focus->surface_resource);
    }
    if (focused_client != client) {
        cancel_unused_data_source(source);
        return;
    }
    if (data_source != NULL &&
        server->selection_kind == NB_WAYLAND_SELECTION_SOURCE &&
        server->selection_source == data_source) {
        return;
    }
    selection_clear(server, true, false, true);
    if (data_source != NULL) {
        server->selection_kind = NB_WAYLAND_SELECTION_SOURCE;
        server->selection_source = data_source;
        data_source->selection = true;
        ++server->selection_generation;
    }
    data_device_send_selection_to_client(server, focused_client);
    if (data_source != NULL) {
        (void)clipboard_start_read(server, data_source);
    }
}

static void data_device_release(struct wl_client *client,
                                struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void data_device_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_data_device_resource *device =
        wl_resource_get_user_data(resource);

    if (device == NULL) {
        return;
    }
    device->resource = NULL;
    wl_list_remove(&device->link);
    wl_list_init(&device->link);
    free(device);
}

static const struct wl_data_device_interface data_device_implementation = {
    .start_drag = data_device_start_drag,
    .set_selection = data_device_set_selection,
    .release = data_device_release
};

static void data_device_manager_create_data_source(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id)
{
    struct nb_wayland_server *server =
        wl_resource_get_user_data(resource);
    struct nb_wayland_data_source *source;

    source = calloc(1, sizeof(*source));
    if (source == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    source->server = server;
    source->resource = wl_resource_create(
        client,
        &wl_data_source_interface,
        wl_resource_get_version(resource),
        id);

    if (source->resource == NULL) {
        free(source);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_init(&source->offers);
    wl_resource_set_implementation(source->resource,
                                   &data_source_implementation,
                                   source,
                                   data_source_resource_destroyed);
}

static void data_device_manager_get_data_device(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    struct wl_resource *seat)
{
    struct nb_wayland_server *server =
        wl_resource_get_user_data(resource);
    struct nb_wayland_data_device_resource *device;

    if (!wl_resource_instance_of(seat,
                                 &wl_seat_interface,
                                 &seat_implementation) ||
        wl_resource_get_user_data(seat) != server) {
        wl_client_post_implementation_error(
            client, "wl_data_device requested for a foreign seat");
        return;
    }
    device = calloc(1, sizeof(*device));
    if (device == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    device->server = server;
    device->resource = wl_resource_create(client,
                                          &wl_data_device_interface,
                                          wl_resource_get_version(resource),
                                          id);
    if (device->resource == NULL) {
        free(device);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_insert(&server->data_device_resources, &device->link);
    wl_resource_set_implementation(device->resource,
                                   &data_device_implementation,
                                   device,
                                   data_device_resource_destroyed);
    if (server->keyboard_focus != NULL &&
        server->keyboard_focus->surface_resource != NULL &&
        wl_resource_get_client(server->keyboard_focus->surface_resource) ==
            client) {
        (void)data_device_send_selection(device);
    }
}

static const struct wl_data_device_manager_interface
    data_device_manager_implementation = {
        .create_data_source = data_device_manager_create_data_source,
        .get_data_device = data_device_manager_get_data_device
    };

static void bind_data_device_manager(struct wl_client *client,
                                     void *data,
                                     uint32_t version,
                                     uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client,
        &wl_data_device_manager_interface,
        protocol_version(version, NB_WAYLAND_DATA_DEVICE_MANAGER_VERSION),
        id);

    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &data_device_manager_implementation,
                                   data,
                                   NULL);
}

static void positioner_destroy(struct wl_client *client,
                               struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void positioner_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_positioner *positioner =
        wl_resource_get_user_data(resource);

    if (positioner == NULL) {
        return;
    }
    positioner->resource = NULL;
    free(positioner);
}

static void positioner_set_size(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t width,
                                int32_t height)
{
    struct nb_wayland_positioner *positioner =
        wl_resource_get_user_data(resource);

    (void)client;
    if (width <= 0 || height <= 0) {
        wl_resource_post_error(resource,
                               XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "popup size must be positive");
        return;
    }
    positioner->width = width;
    positioner->height = height;
    positioner->size_set = true;
}

static void positioner_set_anchor_rect(struct wl_client *client,
                                       struct wl_resource *resource,
                                       int32_t x,
                                       int32_t y,
                                       int32_t width,
                                       int32_t height)
{
    struct nb_wayland_positioner *positioner =
        wl_resource_get_user_data(resource);

    (void)client;
    if (width < 0 || height < 0) {
        wl_resource_post_error(resource,
                               XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "popup anchor rectangle cannot be negative");
        return;
    }
    positioner->anchor_x = x;
    positioner->anchor_y = y;
    positioner->anchor_width = width;
    positioner->anchor_height = height;
    positioner->anchor_rect_set = width > 0 && height > 0;
}

static bool positioner_enum_is_valid(uint32_t value)
{
    return value <= XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
}

static void positioner_set_anchor(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t anchor)
{
    struct nb_wayland_positioner *positioner =
        wl_resource_get_user_data(resource);

    (void)client;
    if (!positioner_enum_is_valid(anchor)) {
        wl_resource_post_error(resource,
                               XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "invalid popup anchor");
        return;
    }
    positioner->anchor = anchor;
}

static void positioner_set_gravity(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t gravity)
{
    struct nb_wayland_positioner *positioner =
        wl_resource_get_user_data(resource);

    (void)client;
    if (!positioner_enum_is_valid(gravity)) {
        wl_resource_post_error(resource,
                               XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "invalid popup gravity");
        return;
    }
    positioner->gravity = gravity;
}

static void positioner_set_constraint_adjustment(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t adjustment)
{
    struct nb_wayland_positioner *positioner =
        wl_resource_get_user_data(resource);
    const uint32_t valid =
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y;

    (void)client;
    if ((adjustment & ~valid) != 0) {
        wl_resource_post_error(resource,
                               XDG_POSITIONER_ERROR_INVALID_INPUT,
                               "invalid popup constraint adjustment");
        return;
    }
    positioner->constraint_adjustment = adjustment;
}

static void positioner_set_offset(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t x,
                                  int32_t y)
{
    struct nb_wayland_positioner *positioner =
        wl_resource_get_user_data(resource);

    (void)client;
    positioner->offset_x = x;
    positioner->offset_y = y;
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
    .set_anchor = positioner_set_anchor,
    .set_gravity = positioner_set_gravity,
    .set_constraint_adjustment = positioner_set_constraint_adjustment,
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
    free(surface->composite_pixels);
    surface->pixels = NULL;
    surface->composite_pixels = NULL;
    surface->width = 0;
    surface->height = 0;
#if NIXBENCH_HAS_WAYLAND_DECORATION
    if (surface->decoration_resource != NULL) {
        wl_resource_destroy(surface->decoration_resource);
    }
#endif
    surface->configure_sent = false;
    surface->configured = false;
    surface->configure_serial = 0;
    surface->toplevel_resource = NULL;
    detach_surface_application_menu(surface);
    maybe_release_surface_slot(surface);
}

#if NIXBENCH_HAS_WAYLAND_DECORATION
static void decoration_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    if (surface == NULL || !surface->occupied) {
        return;
    }
    surface->decoration_resource = NULL;
}

static void decoration_destroy(struct wl_client *client,
                              struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void decoration_set_mode(struct wl_client *client,
                                struct wl_resource *resource,
                                uint32_t mode)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE &&
        mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
        wl_resource_post_error(resource,
                               ZXDG_TOPLEVEL_DECORATION_V1_ERROR_INVALID_MODE,
                               "invalid decoration mode");
        return;
    }
    send_decoration_configure(surface);
}

static void decoration_unset_mode(struct wl_client *client,
                                  struct wl_resource *resource)
{
    (void)client;
    decoration_set_mode(client,
                        resource,
                        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_interface
    decoration_implementation = {
        .destroy = decoration_destroy,
        .set_mode = decoration_set_mode,
        .unset_mode = decoration_unset_mode
    };

static void decoration_manager_destroy(struct wl_client *client,
                                       struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void decoration_manager_get_toplevel_decoration(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    struct wl_resource *toplevel)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(toplevel);

    (void)resource;
    if (surface->decoration_resource != NULL) {
        wl_resource_post_error(
            toplevel,
            ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED,
            "xdg_toplevel already has a decoration object");
        return;
    }
    surface->decoration_resource = wl_resource_create(
        client,
        &zxdg_toplevel_decoration_v1_interface,
        wl_resource_get_version(toplevel),
        id);
    if (surface->decoration_resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(surface->decoration_resource,
                                   &decoration_implementation,
                                   surface,
                                   decoration_resource_destroyed);
    send_decoration_configure(surface);
}

static const struct zxdg_decoration_manager_v1_interface
    decoration_manager_implementation = {
        .destroy = decoration_manager_destroy,
        .get_toplevel_decoration =
            decoration_manager_get_toplevel_decoration
    };

static void bind_decoration_manager(struct wl_client *client,
                                    void *data,
                                    uint32_t version,
                                    uint32_t id)
{
    struct nb_wayland_server *server = data;
    struct wl_resource *resource =
        wl_resource_create(client,
                           &zxdg_decoration_manager_v1_interface,
                           version > 2 ? 2 : version,
                           id);

    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &decoration_manager_implementation,
                                   server,
                                   NULL);
}
#endif

static bool anchor_is_left(uint32_t anchor)
{
    return anchor == XDG_POSITIONER_ANCHOR_LEFT ||
           anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
           anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
}

static bool anchor_is_right(uint32_t anchor)
{
    return anchor == XDG_POSITIONER_ANCHOR_RIGHT ||
           anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT ||
           anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
}

static bool anchor_is_top(uint32_t anchor)
{
    return anchor == XDG_POSITIONER_ANCHOR_TOP ||
           anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
           anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT;
}

static bool anchor_is_bottom(uint32_t anchor)
{
    return anchor == XDG_POSITIONER_ANCHOR_BOTTOM ||
           anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT ||
           anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
}

static bool positioner_calculate_geometry(
    const struct nb_wayland_positioner *positioner,
    const struct nb_wayland_surface *parent,
    int *popup_x,
    int *popup_y)
{
    int parent_x;
    int parent_y;
    int parent_width;
    int parent_height;
    int64_t anchor_x;
    int64_t anchor_y;
    int64_t x;
    int64_t y;

    if (positioner == NULL || parent == NULL ||
        !positioner->size_set || !positioner->anchor_rect_set) {
        return false;
    }
    surface_window_geometry(parent,
                            &parent_x,
                            &parent_y,
                            &parent_width,
                            &parent_height);
    if (parent_width <= 0 || parent_height <= 0 ||
        positioner->anchor_x < 0 || positioner->anchor_y < 0 ||
        (int64_t)positioner->anchor_x + positioner->anchor_width >
            parent_width ||
        (int64_t)positioner->anchor_y + positioner->anchor_height >
            parent_height) {
        return false;
    }
    if (anchor_is_left(positioner->anchor)) {
        anchor_x = positioner->anchor_x;
    } else if (anchor_is_right(positioner->anchor)) {
        anchor_x = (int64_t)positioner->anchor_x +
                   positioner->anchor_width;
    } else {
        anchor_x = (int64_t)positioner->anchor_x +
                   positioner->anchor_width / 2;
    }
    if (anchor_is_top(positioner->anchor)) {
        anchor_y = positioner->anchor_y;
    } else if (anchor_is_bottom(positioner->anchor)) {
        anchor_y = (int64_t)positioner->anchor_y +
                   positioner->anchor_height;
    } else {
        anchor_y = (int64_t)positioner->anchor_y +
                   positioner->anchor_height / 2;
    }
    if (anchor_is_left(positioner->gravity)) {
        x = anchor_x - positioner->width;
    } else if (anchor_is_right(positioner->gravity)) {
        x = anchor_x;
    } else {
        x = anchor_x - positioner->width / 2;
    }
    if (anchor_is_top(positioner->gravity)) {
        y = anchor_y - positioner->height;
    } else if (anchor_is_bottom(positioner->gravity)) {
        y = anchor_y;
    } else {
        y = anchor_y - positioner->height / 2;
    }
    x += positioner->offset_x;
    y += positioner->offset_y;
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) {
        return false;
    }
    *popup_x = (int)x;
    *popup_y = (int)y;
    (void)parent_x;
    (void)parent_y;
    return true;
}

static bool popup_has_live_child(
    const struct nb_wayland_surface *parent,
    const struct nb_wayland_surface *ignored_child)
{
    size_t index;

    if (parent == NULL || parent->server == NULL) {
        return false;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        const struct nb_wayland_surface *candidate =
            &parent->server->surfaces[index];

        if (candidate != ignored_child && candidate->occupied &&
            candidate->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP &&
            candidate->popup_resource != NULL &&
            candidate->popup_parent == parent) {
            return true;
        }
    }
    return false;
}

static bool toplevel_has_other_popup_grab(
    const struct nb_wayland_surface *root,
    const struct nb_wayland_surface *ignored_popup)
{
    size_t index;

    if (root == NULL || root->server == NULL) {
        return false;
    }
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        const struct nb_wayland_surface *candidate =
            &root->server->surfaces[index];

        if (candidate != ignored_popup && candidate->occupied &&
            candidate->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP &&
            candidate->popup_resource != NULL &&
            candidate->popup_grabbed && !candidate->popup_dismissed &&
            surface_root_toplevel_const(candidate) == root) {
            return true;
        }
    }
    return false;
}

static void popup_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    struct nb_wayland_surface *root;

    if (surface == NULL || !surface->occupied) {
        return;
    }
    root = surface_root_toplevel(surface);
    dismiss_overlay_descendants(surface, false);
    clear_surface_pointer_state(surface, false);
    clear_surface_keyboard_state(surface, false);
    clear_pending_attach(surface);
    destroy_frame_resources(surface);
    if (surface->pixels != NULL) {
        surface_send_output_membership(surface, false);
    }
    free(surface->pixels);
    free(surface->composite_pixels);
    surface->pixels = NULL;
    surface->composite_pixels = NULL;
    surface->width = 0;
    surface->height = 0;
    surface->configure_sent = false;
    surface->configured = false;
    surface->configure_serial = 0;
    surface->popup_resource = NULL;
    surface->popup_parent = NULL;
    surface->popup_grabbed = false;
    surface->popup_dismissed = false;
    if (root != NULL) {
        surface_tree_changed(root, false);
    }
    maybe_release_surface_slot(surface);
}

static void popup_destroy(struct wl_client *client,
                          struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (popup_has_live_child(surface, NULL)) {
        wl_resource_post_error(
            surface->wm_base_resource,
            XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
            "destroy the topmost child popup first");
        return;
    }
    wl_resource_destroy(resource);
}

static void popup_grab(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *seat,
                       uint32_t serial)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    struct nb_wayland_surface *parent = surface->popup_parent;
    struct nb_wayland_surface *root =
        surface_root_toplevel(surface);

    (void)client;
    (void)serial;
    if (surface->popup_dismissed || parent == NULL ||
        surface->configure_sent || surface->pixels != NULL) {
        wl_resource_post_error(resource,
                               XDG_POPUP_ERROR_INVALID_GRAB,
                               "popup grab requested in an invalid state");
        return;
    }
    if (!wl_resource_instance_of(seat,
                                 &wl_seat_interface,
                                 &seat_implementation) ||
        wl_resource_get_user_data(seat) != surface->server) {
        wl_resource_post_error(resource,
                               XDG_POPUP_ERROR_INVALID_GRAB,
                               "popup grab uses a foreign seat");
        return;
    }
    if ((parent->role == NB_WAYLAND_SURFACE_ROLE_XDG_POPUP &&
         (!parent->popup_grabbed || parent->popup_dismissed ||
          popup_has_live_child(parent, surface))) ||
        (parent->role == NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL &&
         toplevel_has_other_popup_grab(root, surface))) {
        wl_resource_post_error(
            surface->wm_base_resource,
            XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
            "popup parent is not the topmost popup grab");
        return;
    }
    surface->popup_grabbed = true;
}

static void popup_reposition(struct wl_client *client,
                             struct wl_resource *resource,
                             struct wl_resource *positioner_resource,
                             uint32_t token)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    struct nb_wayland_positioner *positioner;
    int popup_x;
    int popup_y;

    (void)client;
    if (!wl_resource_instance_of(positioner_resource,
                                 &xdg_positioner_interface,
                                 &positioner_implementation)) {
        wl_resource_post_error(surface->wm_base_resource,
                               XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                               "foreign popup positioner");
        return;
    }
    positioner = wl_resource_get_user_data(positioner_resource);
    if (!positioner_calculate_geometry(positioner,
                                       surface->popup_parent,
                                       &popup_x,
                                       &popup_y)) {
        wl_resource_post_error(surface->wm_base_resource,
                               XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                               "incomplete popup positioner");
        return;
    }
    surface->popup_positioner = *positioner;
    surface->popup_positioner.resource = NULL;
    surface->popup_x = popup_x;
    surface->popup_y = popup_y;
    xdg_popup_send_repositioned(resource, token);
    xdg_popup_send_configure(resource,
                             popup_x,
                             popup_y,
                             positioner->width,
                             positioner->height);
    surface->configure_serial =
        wl_display_next_serial(surface->server->display);
    surface->configure_sent = true;
    surface->configured = false;
    xdg_surface_send_configure(surface->xdg_surface_resource,
                               surface->configure_serial);
}

static const struct xdg_popup_interface popup_implementation = {
    .destroy = popup_destroy,
    .grab = popup_grab,
    .reposition = popup_reposition
};

static void xdg_surface_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    if (surface == NULL || !surface->occupied) {
        return;
    }
#if NIXBENCH_HAS_WAYLAND_DECORATION
    if (surface->decoration_resource != NULL) {
        wl_resource_destroy(surface->decoration_resource);
    }
#endif
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
    if (surface_has_xdg_role(surface)) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                               "destroy the xdg role object first");
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
#if NIXBENCH_HAS_WAYLAND_DECORATION
    if (surface->decoration_resource != NULL) {
        wl_resource_destroy(surface->decoration_resource);
    }
#endif
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
        send_work_area_configure(surface);
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
                                  struct wl_resource *positioner_resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    struct nb_wayland_surface *parent_surface;
    struct nb_wayland_positioner *positioner;
    int popup_x;
    int popup_y;

    if (surface->popup_resource != NULL ||
        (surface->role != NB_WAYLAND_SURFACE_ROLE_NONE &&
         surface->role != NB_WAYLAND_SURFACE_ROLE_XDG_POPUP)) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                               "wl_surface already has an xdg role object");
        return;
    }
    if (parent == NULL ||
        !wl_resource_instance_of(parent,
                                 &xdg_surface_interface,
                                 &xdg_surface_implementation)) {
        wl_resource_post_error(surface->wm_base_resource,
                               XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                               "popup parent is not a local xdg_surface");
        return;
    }
    parent_surface = wl_resource_get_user_data(parent);
    if (parent_surface == NULL ||
        parent_surface->server != surface->server ||
        parent_surface->xdg_surface_resource != parent ||
        !surface_has_xdg_role(parent_surface) ||
        !surface_is_mapped(parent_surface) ||
        wl_resource_get_client(parent_surface->surface_resource) != client) {
        wl_resource_post_error(surface->wm_base_resource,
                               XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                               "popup parent is not mapped");
        return;
    }
    if (!wl_resource_instance_of(positioner_resource,
                                 &xdg_positioner_interface,
                                 &positioner_implementation)) {
        wl_resource_post_error(surface->wm_base_resource,
                               XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                               "popup positioner is not local");
        return;
    }
    positioner = wl_resource_get_user_data(positioner_resource);
    if (!positioner_calculate_geometry(positioner,
                                       parent_surface,
                                       &popup_x,
                                       &popup_y)) {
        wl_resource_post_error(surface->wm_base_resource,
                               XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                               "popup positioner is incomplete or outside "
                               "its parent");
        return;
    }
    surface->popup_resource = wl_resource_create(
        client,
        &xdg_popup_interface,
        wl_resource_get_version(resource),
        id);
    if (surface->popup_resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->role = NB_WAYLAND_SURFACE_ROLE_XDG_POPUP;
    surface->popup_parent = parent_surface;
    surface->popup_positioner = *positioner;
    surface->popup_positioner.resource = NULL;
    surface->popup_x = popup_x;
    surface->popup_y = popup_y;
    ++surface->server->next_popup_sequence;
    if (surface->server->next_popup_sequence == 0) {
        surface->server->next_popup_sequence = 1;
    }
    surface->popup_sequence = surface->server->next_popup_sequence;
    wl_resource_set_implementation(surface->popup_resource,
                                   &popup_implementation,
                                   surface,
                                   popup_resource_destroyed);
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
    if (!surface_has_xdg_role(surface)) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                               "xdg_surface has no role");
        return;
    }
    if (width <= 0 || height <= 0) {
        wl_resource_post_error(resource,
                               XDG_SURFACE_ERROR_INVALID_SIZE,
                               "window geometry must be positive");
        return;
    }
    surface->window_geometry_set = true;
    surface->window_geometry_x = x;
    surface->window_geometry_y = y;
    surface->window_geometry_width = width;
    surface->window_geometry_height = height;
    if (surface_is_mapped(surface)) {
        surface_tree_changed(surface, false);
    }
}

static void xdg_surface_ack_configure(struct wl_client *client,
                                      struct wl_resource *resource,
                                      uint32_t serial)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    (void)client;
    if (!surface_has_xdg_role(surface)) {
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
    struct nb_wayland_positioner *positioner =
        calloc(1, sizeof(*positioner));

    if (positioner == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    positioner->resource = wl_resource_create(
        client,
        &xdg_positioner_interface,
        wl_resource_get_version(resource),
        id);

    if (positioner->resource == NULL) {
        free(positioner);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(positioner->resource,
                                   &positioner_implementation,
                                   positioner,
                                   positioner_resource_destroyed);
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

static void application_menu_resource_destroyed(struct wl_resource *resource)
{
    struct nb_wayland_application_menu *menu =
        wl_resource_get_user_data(resource);

    if (menu == NULL) {
        return;
    }
    if (menu->surface != NULL &&
        menu->surface->application_menu == menu) {
        struct nb_wayland_surface *surface = menu->surface;
        bool fallback_bound = surface->server->destroying;

        if (!fallback_bound) {
            fallback_bound = bind_surface_menu(surface,
                                               surface->server->menu_source,
                                               surface->server->menu_model);
        }
        surface->application_menu = NULL;
        if (fallback_bound) {
            surface->application_menu_committed = false;
            surface->active_application_menu_snapshot = 0;
            menu_snapshot_reset(&surface->application_menu_snapshots[0]);
            menu_snapshot_reset(&surface->application_menu_snapshots[1]);
            if (surface->window != NB_WINDOW_ID_NONE) {
                mark_redraw_full(surface->server);
            }
        } else {
            surface->application_menu_committed = false;
            wl_client_post_implementation_error(
                wl_resource_get_client(resource),
                "could not restore the fallback application menu");
        }
    }
    menu->surface = NULL;
    menu->resource = NULL;
    free(menu);
}

static bool application_menu_can_build(
    struct nb_wayland_application_menu *menu)
{
    if (menu->surface == NULL || !menu->surface->occupied) {
        wl_resource_post_error(
            menu->resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_DEFUNCT_SURFACE,
            "the application-menu surface is defunct");
        return false;
    }
    if (!menu->building) {
        wl_resource_post_error(
            menu->resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_INVALID_STATE,
            "reset is required before editing a committed menu");
        return false;
    }
    return true;
}

static void application_menu_destroy(struct wl_client *client,
                                     struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void application_menu_reset(struct wl_client *client,
                                   struct wl_resource *resource)
{
    struct nb_wayland_application_menu *menu =
        wl_resource_get_user_data(resource);

    (void)client;
    if (menu->surface == NULL || !menu->surface->occupied) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_DEFUNCT_SURFACE,
            "the application-menu surface is defunct");
        return;
    }
    menu_snapshot_reset(&menu->pending);
    menu->building = true;
}

static void application_menu_append_menu(struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *label)
{
    struct nb_wayland_application_menu *menu =
        wl_resource_get_user_data(resource);
    struct nb_wayland_menu_snapshot *pending = &menu->pending;
    size_t index;

    (void)client;
    if (!application_menu_can_build(menu)) {
        return;
    }
    if (!menu_label_is_valid(label)) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_INVALID_LABEL,
            "application-menu label is empty or too long");
        return;
    }
    if (pending->model.menu_count >= NB_MENU_MAX_MENUS) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_LIMIT_EXCEEDED,
            "too many top-level application menus");
        return;
    }

    index = pending->model.menu_count++;
    copy_text(pending->menu_labels[index],
              sizeof(pending->menu_labels[index]),
              label);
    pending->menus[index].label = pending->menu_labels[index];
    pending->menus[index].items = pending->items[index];
    pending->menus[index].item_count = 0;
}

static void application_menu_append_item(struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *label,
                                         uint32_t command,
                                         uint32_t flags)
{
    struct nb_wayland_application_menu *menu =
        wl_resource_get_user_data(resource);
    struct nb_wayland_menu_snapshot *pending = &menu->pending;
    const uint32_t known_flags =
        NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED |
        NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_CHECKED;
    struct nb_menu_spec *current;
    struct nb_menu_item_spec *item;
    size_t menu_index;
    size_t item_index;

    (void)client;
    if (!application_menu_can_build(menu)) {
        return;
    }
    if (pending->model.menu_count == 0) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_INVALID_STATE,
            "an item requires a preceding top-level menu");
        return;
    }
    if (!menu_label_is_valid(label)) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_INVALID_LABEL,
            "application-menu item label is empty or too long");
        return;
    }
    if ((flags & ~known_flags) != 0) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_INVALID_FLAGS,
            "unknown application-menu item flags 0x%x",
            flags);
        return;
    }
    if (command == NB_MENU_COMMAND_NONE ||
        menu_snapshot_contains_command(pending, command)) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_INVALID_COMMAND,
            "application-menu command %u is zero or duplicated",
            command);
        return;
    }

    menu_index = pending->model.menu_count - 1;
    current = &pending->menus[menu_index];
    if (current->item_count >= NB_MENU_MAX_ITEMS) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_LIMIT_EXCEEDED,
            "too many items in an application menu");
        return;
    }
    item_index = current->item_count++;
    copy_text(pending->item_labels[menu_index][item_index],
              sizeof(pending->item_labels[menu_index][item_index]),
              label);
    item = &pending->items[menu_index][item_index];
    item->label = pending->item_labels[menu_index][item_index];
    item->command = command;
    item->kind = NB_MENU_ITEM_COMMAND;
    item->enabled =
        (flags & NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED) != 0;
    item->checked =
        (flags & NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_CHECKED) != 0;
}

static void application_menu_append_separator(struct wl_client *client,
                                              struct wl_resource *resource)
{
    struct nb_wayland_application_menu *menu =
        wl_resource_get_user_data(resource);
    struct nb_wayland_menu_snapshot *pending = &menu->pending;
    struct nb_menu_spec *current;
    struct nb_menu_item_spec *item;
    size_t menu_index;

    (void)client;
    if (!application_menu_can_build(menu)) {
        return;
    }
    if (pending->model.menu_count == 0) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_INVALID_STATE,
            "a separator requires a preceding top-level menu");
        return;
    }
    menu_index = pending->model.menu_count - 1;
    current = &pending->menus[menu_index];
    if (current->item_count >= NB_MENU_MAX_ITEMS) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_LIMIT_EXCEEDED,
            "too many items in an application menu");
        return;
    }
    item = &pending->items[menu_index][current->item_count++];
    item->label = NULL;
    item->command = NB_MENU_COMMAND_NONE;
    item->kind = NB_MENU_ITEM_SEPARATOR;
    item->enabled = false;
    item->checked = false;
}

static void application_menu_commit(struct wl_client *client,
                                    struct wl_resource *resource)
{
    struct nb_wayland_application_menu *menu =
        wl_resource_get_user_data(resource);
    struct nb_wayland_surface *surface;
    struct nb_wayland_menu_snapshot *candidate;
    unsigned int candidate_index;

    (void)client;
    if (!application_menu_can_build(menu)) {
        return;
    }
    if (menu->pending.model.menu_count == 0) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_V1_ERROR_INVALID_STATE,
            "cannot commit an empty application menu");
        return;
    }

    surface = menu->surface;
    candidate_index = surface->active_application_menu_snapshot ^ 1U;
    candidate = &surface->application_menu_snapshots[candidate_index];
    menu_snapshot_copy(candidate, &menu->pending);
    if (!bind_surface_menu(surface,
                           surface->application_menu_source,
                           &candidate->model)) {
        menu_snapshot_reset(candidate);
        wl_client_post_implementation_error(
            wl_resource_get_client(resource),
            "could not publish the application menu in the shell");
        return;
    }
    surface->active_application_menu_snapshot = candidate_index;
    surface->application_menu_committed = true;
    menu->building = false;
    if (surface->window != NB_WINDOW_ID_NONE) {
        mark_redraw_full(surface->server);
    }
}

static const struct nixbench_application_menu_v1_interface
application_menu_implementation = {
    .destroy = application_menu_destroy,
    .reset = application_menu_reset,
    .append_menu = application_menu_append_menu,
    .append_item = application_menu_append_item,
    .append_separator = application_menu_append_separator,
    .commit = application_menu_commit
};

static void application_menu_manager_destroy(struct wl_client *client,
                                             struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void application_menu_manager_get_menu(
    struct wl_client *client,
    struct wl_resource *resource,
    uint32_t id,
    struct wl_resource *surface_resource)
{
    struct nb_wayland_server *server = wl_resource_get_user_data(resource);
    struct nb_wayland_surface *surface;
    struct nb_wayland_application_menu *menu;

    if (!wl_resource_instance_of(surface_resource,
                                 &wl_surface_interface,
                                 &surface_implementation) ||
        wl_resource_get_client(surface_resource) != client) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_MANAGER_V1_ERROR_FOREIGN_SURFACE,
            "application menu requires a wl_surface owned by this client");
        return;
    }
    surface = wl_resource_get_user_data(surface_resource);
    if (surface == NULL || surface->server != server) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_MANAGER_V1_ERROR_FOREIGN_SURFACE,
            "application menu requires a surface from this compositor");
        return;
    }
    if (surface->role != NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL ||
        surface->toplevel_resource == NULL) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_MANAGER_V1_ERROR_INVALID_ROLE,
            "application menu requires a live xdg_toplevel role");
        return;
    }
    if (surface->application_menu != NULL) {
        wl_resource_post_error(
            resource,
            NIXBENCH_APPLICATION_MENU_MANAGER_V1_ERROR_ALREADY_EXISTS,
            "the surface already has an application menu");
        return;
    }

    menu = calloc(1, sizeof(*menu));
    if (menu == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    menu->resource = wl_resource_create(
        client,
        &nixbench_application_menu_v1_interface,
        wl_resource_get_version(resource),
        id);
    if (menu->resource == NULL) {
        free(menu);
        wl_client_post_no_memory(client);
        return;
    }
    menu->server = server;
    menu->surface = surface;
    menu->building = true;
    menu_snapshot_reset(&menu->pending);
    surface->application_menu = menu;
    wl_resource_set_implementation(menu->resource,
                                   &application_menu_implementation,
                                   menu,
                                   application_menu_resource_destroyed);
}

static const struct nixbench_application_menu_manager_v1_interface
application_menu_manager_implementation = {
    .destroy = application_menu_manager_destroy,
    .get_menu = application_menu_manager_get_menu
};

static void bind_application_menu_manager(struct wl_client *client,
                                          void *data,
                                          uint32_t version,
                                          uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(
        client,
        &nixbench_application_menu_manager_v1_interface,
        protocol_version(version, NB_WAYLAND_APPLICATION_MENU_VERSION),
        id);

    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &application_menu_manager_implementation,
                                   data,
                                   NULL);
}

#if NIXBENCH_HAS_XWAYLAND_SHELL
static void xwayland_surface_resource_destroyed(
    struct wl_resource *resource)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);

    if (surface == NULL || !surface->occupied) {
        return;
    }
    surface->xwayland_surface_resource = NULL;
    maybe_release_surface_slot(surface);
}

static void xwayland_surface_set_serial(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t serial_lo,
                                        uint32_t serial_hi)
{
    struct nb_wayland_surface *surface =
        wl_resource_get_user_data(resource);
    const uint64_t serial = (uint64_t)serial_lo |
                            ((uint64_t)serial_hi << 32U);

    (void)client;
    if (serial == 0) {
        wl_resource_post_error(
            resource,
            XWAYLAND_SURFACE_V1_ERROR_INVALID_SERIAL,
            "xwayland surface serial must be non-zero");
        return;
    }
    if (surface->xwayland_serial != 0) {
        wl_resource_post_error(
            resource,
            XWAYLAND_SURFACE_V1_ERROR_ALREADY_ASSOCIATED,
            "xwayland surface serial was already committed");
        return;
    }
    surface->pending_xwayland_serial = serial;
    surface->pending_xwayland_serial_set = true;
}

static void xwayland_surface_destroy(struct wl_client *client,
                                     struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static const struct xwayland_surface_v1_interface
xwayland_surface_implementation = {
    .set_serial = xwayland_surface_set_serial,
    .destroy = xwayland_surface_destroy
};

static void xwayland_shell_destroy(struct wl_client *client,
                                   struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void xwayland_shell_get_surface(struct wl_client *client,
                                       struct wl_resource *resource,
                                       uint32_t id,
                                       struct wl_resource *surface_resource)
{
    struct nb_wayland_surface *surface;
    struct wl_resource *role_resource;

    if (surface_resource == NULL ||
        wl_resource_get_client(surface_resource) != client ||
        !wl_resource_instance_of(surface_resource,
                                 &wl_surface_interface,
                                 &surface_implementation)) {
        wl_resource_post_error(resource,
                               XWAYLAND_SHELL_V1_ERROR_ROLE,
                               "invalid wl_surface for xwayland role");
        return;
    }
    surface = wl_resource_get_user_data(surface_resource);
    if (surface == NULL ||
        surface->role != NB_WAYLAND_SURFACE_ROLE_NONE) {
        wl_resource_post_error(resource,
                               XWAYLAND_SHELL_V1_ERROR_ROLE,
                               "wl_surface already has a role");
        return;
    }
    role_resource = wl_resource_create(client,
                                       &xwayland_surface_v1_interface,
                                       1,
                                       id);
    if (role_resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->role = NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL;
    surface->xwayland_surface_resource = role_resource;
    wl_resource_set_implementation(role_resource,
                                   &xwayland_surface_implementation,
                                   surface,
                                   xwayland_surface_resource_destroyed);
}

static const struct xwayland_shell_v1_interface
xwayland_shell_implementation = {
    .destroy = xwayland_shell_destroy,
    .get_xwayland_surface = xwayland_shell_get_surface
};

static void xwayland_client_destroyed(struct wl_listener *listener,
                                      void *data)
{
    struct nb_wayland_server *server =
        wl_container_of(listener, server, xwayland_client_destroy);

    (void)data;
    server->xwayland_client = NULL;
    server->xwayland_client_pid = 0;
}

static void bind_xwayland_shell(struct wl_client *client,
                                void *data,
                                uint32_t version,
                                uint32_t id)
{
    struct nb_wayland_server *server = data;
    struct wl_resource *resource;
    pid_t process = -1;
    uid_t user = (uid_t)-1;
    gid_t group = (gid_t)-1;
    bool authorized;

    wl_client_get_credentials(client, &process, &user, &group);
    resource = wl_resource_create(client,
                                  &xwayland_shell_v1_interface,
                                  protocol_version(version, 1),
                                  id);
    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource,
                                   &xwayland_shell_implementation,
                                   server,
                                   NULL);
    authorized = server->xwayland_client_pid > 0 &&
                 (server->xwayland_client == client ||
                  (server->xwayland_client == NULL &&
                   ((process > 0 &&
                     process == server->xwayland_client_pid) ||
                    /*
                     * NetBSD's local-socket credentials do not expose a
                     * peer PID (and some libwayland builds also leave the
                     * UID unset). The Wayland socket lives in a private
                     * per-session directory, so consume the authorization
                     * for its first binder and lock subsequent binds to that
                     * exact wl_client.
                     */
                    process <= 0)));
    if (!authorized) {
        wl_resource_post_error(resource,
                               XWAYLAND_SHELL_V1_ERROR_ROLE,
                               "client is not the authorized Xwayland server");
        return;
    }
    if (server->xwayland_client == NULL) {
        server->xwayland_client = client;
        server->xwayland_client_destroy.notify =
            xwayland_client_destroyed;
        wl_client_add_destroy_listener(
            client,
            &server->xwayland_client_destroy);
    }
    (void)user;
    (void)group;
}
#endif

static bool menu_source_range_is_available(
    const struct nb_shell *shell,
    nb_menu_source_id first)
{
    const nb_menu_source_id last =
        first + (nb_menu_source_id)NB_WAYLAND_MAX_SURFACES;
    size_t index;

    if (shell->desktop_menu_source >= first &&
        shell->desktop_menu_source <= last) {
        return false;
    }
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        const struct nb_shell_menu_binding *binding =
            &shell->menu_bindings[index];

        if (binding->window != NB_WINDOW_ID_NONE &&
            binding->menu_source >= first &&
            binding->menu_source <= last) {
            return false;
        }
    }
    return true;
}

struct nb_wayland_server *nb_wayland_server_create(
    struct nb_shell *shell,
    nb_menu_source_id menu_source,
    const struct nb_menu_model *menu_model,
    int output_width,
    int output_height)
{
    struct nb_wayland_server *server;
#if NIXBENCH_HAS_XWAYLAND_SHELL
    const char *legacy_xwayland_association =
        getenv("NIXBENCH_XWAYLAND_LEGACY_ASSOCIATION");
    const bool advertise_xwayland_shell =
        legacy_xwayland_association == NULL ||
        strcmp(legacy_xwayland_association, "1") != 0;
#endif

    if (shell == NULL || menu_source == NB_MENU_SOURCE_NONE ||
        menu_source > UINT64_MAX - NB_WAYLAND_MAX_SURFACES ||
        menu_model == NULL || output_width <= 0 || output_height <= 0 ||
        !menu_source_range_is_available(shell, menu_source)) {
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
    wl_list_init(&server->data_device_resources);
    wl_list_init(&server->clipboard_writes);
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
    server->subcompositor_global = wl_global_create(
        server->display,
        &wl_subcompositor_interface,
        NB_WAYLAND_SUBCOMPOSITOR_VERSION,
        server,
        bind_subcompositor);
#if NIXBENCH_HAS_WAYLAND_DECORATION
    server->decoration_manager_global = wl_global_create(
        server->display,
        &zxdg_decoration_manager_v1_interface,
        1,
        server,
        bind_decoration_manager);
#endif
#if NIXBENCH_HAS_XWAYLAND_SHELL
    if (advertise_xwayland_shell) {
        server->xwayland_shell_global = wl_global_create(
            server->display,
            &xwayland_shell_v1_interface,
            1,
            server,
            bind_xwayland_shell);
    }
#endif
    server->seat_global = wl_global_create(server->display,
                                           &wl_seat_interface,
                                           NB_WAYLAND_SEAT_VERSION,
                                           server,
                                           bind_seat);
    server->data_device_manager_global = wl_global_create(
        server->display,
        &wl_data_device_manager_interface,
        NB_WAYLAND_DATA_DEVICE_MANAGER_VERSION,
        server,
        bind_data_device_manager);
    server->xdg_wm_base_global = wl_global_create(
        server->display,
        &xdg_wm_base_interface,
        NB_WAYLAND_XDG_SHELL_VERSION,
        server,
        bind_xdg_wm_base);
    server->application_menu_global = wl_global_create(
        server->display,
        &nixbench_application_menu_manager_v1_interface,
        NB_WAYLAND_APPLICATION_MENU_VERSION,
        server,
        bind_application_menu_manager);
    if (server->compositor_global == NULL ||
        server->output_global == NULL ||
        server->seat_global == NULL ||
        server->data_device_manager_global == NULL ||
        server->xdg_wm_base_global == NULL ||
#if NIXBENCH_HAS_XWAYLAND_SHELL
        (advertise_xwayland_shell && server->xwayland_shell_global == NULL) ||
#endif
        server->application_menu_global == NULL) {
        wl_display_destroy(server->display);
        destroy_keyboard_state(server);
        free(server);
        return NULL;
    }
    return server;
}

void nb_wayland_server_destroy(struct nb_wayland_server *server)
{
    struct nb_wayland_clipboard_write *transfer;
    struct nb_wayland_clipboard_write *temporary;
    size_t index;

    if (server == NULL) {
        return;
    }
    server->destroying = true;
    selection_clear(server, false, false, false);
    wl_list_for_each_safe(transfer,
                          temporary,
                          &server->clipboard_writes,
                          link) {
        clipboard_write_destroy(transfer);
    }
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
        free(surface->composite_pixels);
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

int nb_wayland_server_event_descriptor(
    const struct nb_wayland_server *server)
{
    return server != NULL && server->event_loop != NULL
               ? wl_event_loop_get_fd(server->event_loop)
               : -1;
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

bool nb_wayland_server_take_redraw(struct nb_wayland_server *server)
{
    return nb_wayland_server_take_redraw_region(server, NULL);
}

bool nb_wayland_server_take_redraw_damage(
    struct nb_wayland_server *server,
    struct nb_rect *damage)
{
    struct nb_damage_region region;
    struct nb_damage_rect bounds;

    if (!nb_wayland_server_take_redraw_region(server, &region)) {
        return false;
    }
    if (damage != NULL &&
        nb_damage_region_bounds(&region,
                                server->output_width,
                                server->output_height,
                                &bounds)) {
        *damage = (struct nb_rect){bounds.x,
                                   bounds.y,
                                   bounds.width,
                                   bounds.height};
    }
    return true;
}

bool nb_wayland_server_take_redraw_region(
    struct nb_wayland_server *server,
    struct nb_damage_region *damage)
{
    bool redraw;

    if (server == NULL) {
        return false;
    }
    redraw = server->redraw_pending;
    if (redraw && damage != NULL) {
        *damage = server->redraw_damage;
    }
    server->redraw_pending = false;
    nb_damage_region_clear(&server->redraw_damage);
    return redraw;
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
    mark_redraw_full(server);
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
    snapshot->pixels = surface->composite_pixels != NULL
                           ? surface->composite_pixels
                           : surface->pixels;
    snapshot->width = surface->width;
    snapshot->height = surface->height;
    snapshot->stride = surface->width * (int)sizeof(uint32_t);
    snapshot->geometry_x = surface->window_geometry_set
                               ? surface->window_geometry_x
                               : 0;
    snapshot->geometry_y = surface->window_geometry_set
                               ? surface->window_geometry_y
                               : 0;
    snapshot->geometry_width = surface->window_geometry_set
                                   ? surface->window_geometry_width
                                   : surface->width;
    snapshot->geometry_height = surface->window_geometry_set
                                    ? surface->window_geometry_height
                                    : surface->height;
    snapshot->geometry_set = surface->window_geometry_set;
    snapshot->revision = surface->revision;
    return true;
}

bool nb_wayland_server_dispatch_menu_command(
    struct nb_wayland_server *server,
    nb_window_id window,
    nb_menu_source_id menu_source,
    nb_menu_command command)
{
    struct nb_wayland_surface *surface;

    if (server == NULL || server->destroying ||
        menu_source == NB_MENU_SOURCE_NONE ||
        command == NB_MENU_COMMAND_NONE) {
        return false;
    }
    surface = find_surface_by_window(server, window);
    if (surface == NULL || !surface->application_menu_committed ||
        surface->application_menu_source != menu_source ||
        surface->application_menu == NULL ||
        surface->application_menu->resource == NULL ||
        !menu_snapshot_command_is_actionable(
            &surface->application_menu_snapshots[
                surface->active_application_menu_snapshot],
            command)) {
        return false;
    }
    nixbench_application_menu_v1_send_command(
        surface->application_menu->resource,
        command);
    return true;
}

void nb_wayland_server_set_xwayland_interface(
    struct nb_wayland_server *server,
    const struct nb_wayland_xwayland_interface *interface,
    void *context)
{
    if (server == NULL || server->destroying) {
        return;
    }
    memset(&server->xwayland_interface,
           0,
           sizeof(server->xwayland_interface));
    server->xwayland_context = NULL;
    if (interface != NULL) {
        server->xwayland_interface = *interface;
        server->xwayland_context = context;
        if (server->selection_kind == NB_WAYLAND_SELECTION_SOURCE &&
            server->selection_source != NULL &&
            server->clipboard_text != NULL) {
            clipboard_notify_xwayland(server, true);
        }
    }
}

void nb_wayland_server_authorize_xwayland_client(
    struct nb_wayland_server *server,
    pid_t process)
{
    if (server == NULL || server->destroying || process < 0) {
        return;
    }
    server->xwayland_client_pid = process;
}

static bool associate_xwayland_surface(
    struct nb_wayland_server *server,
    struct nb_wayland_surface *surface,
    uint32_t xwindow,
    const char *title,
    const char *application_name)
{
    struct nb_wayland_surface *existing =
        find_surface_by_xwayland_window(server, xwindow);

    if (surface == NULL ||
        (existing != NULL && existing != surface) ||
        (surface->role != NB_WAYLAND_SURFACE_ROLE_NONE &&
         surface->role != NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) ||
        surface->xdg_surface_resource != NULL ||
        surface->subsurface_resource != NULL ||
        (surface->xwayland_window != 0 &&
         surface->xwayland_window != xwindow)) {
        return false;
    }
    surface->role = NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL;
    surface->xwayland_window = xwindow;
    copy_text(surface->title,
              sizeof(surface->title),
              title != NULL && title[0] != '\0'
                  ? title
                  : "X11 Application");
    copy_text(surface->app_id,
              sizeof(surface->app_id),
              application_name != NULL && application_name[0] != '\0'
                  ? application_name
                  : surface->title);
    if (surface->window == NB_WINDOW_ID_NONE) {
        map_surface(surface);
    } else if (!nb_desktop_set_window_title(&server->shell->desktop,
                                            surface->window,
                                            surface->title)) {
        return false;
    }
    if (surface->window != NB_WINDOW_ID_NONE &&
        !nb_shell_set_window_menu_label(server->shell,
                                        surface->window,
                                        surface->app_id)) {
        return false;
    }
    mark_redraw_full(server);
    return true;
}

bool nb_wayland_server_associate_xwayland_surface(
    struct nb_wayland_server *server,
    uint32_t surface_resource_id,
    uint32_t xwindow,
    const char *title,
    const char *application_name)
{
    struct nb_wayland_surface *surface;

    if (server == NULL || server->destroying ||
        surface_resource_id == 0 || xwindow == 0) {
        return false;
    }
    surface = find_surface_by_resource_id(server, surface_resource_id);
    return associate_xwayland_surface(server,
                                      surface,
                                      xwindow,
                                      title,
                                      application_name);
}

bool nb_wayland_server_associate_xwayland_serial(
    struct nb_wayland_server *server,
    uint64_t surface_serial,
    uint32_t xwindow,
    const char *title,
    const char *application_name)
{
    struct nb_wayland_surface *surface;

    if (server == NULL || server->destroying ||
        surface_serial == 0 || xwindow == 0) {
        return false;
    }
    surface = find_surface_by_xwayland_serial(server, surface_serial);
    return associate_xwayland_surface(server,
                                      surface,
                                      xwindow,
                                      title,
                                      application_name);
}

bool nb_wayland_server_update_xwayland_identity(
    struct nb_wayland_server *server,
    uint32_t xwindow,
    const char *title,
    const char *application_name)
{
    struct nb_wayland_surface *surface;

    if (server == NULL || server->destroying || xwindow == 0) {
        return false;
    }
    surface = find_surface_by_xwayland_window(server, xwindow);
    if (surface == NULL) {
        return false;
    }
    copy_text(surface->title,
              sizeof(surface->title),
              title != NULL && title[0] != '\0'
                  ? title
                  : "X11 Application");
    copy_text(surface->app_id,
              sizeof(surface->app_id),
              application_name != NULL && application_name[0] != '\0'
                  ? application_name
                  : surface->title);
    if (surface->window != NB_WINDOW_ID_NONE &&
        (!nb_desktop_set_window_title(&server->shell->desktop,
                                      surface->window,
                                      surface->title) ||
         !nb_shell_set_window_menu_label(server->shell,
                                         surface->window,
                                         surface->app_id))) {
        return false;
    }
    mark_redraw_full(server);
    return true;
}

bool nb_wayland_server_set_xwayland_fullscreen(
    struct nb_wayland_server *server,
    uint32_t xwindow,
    bool fullscreen)
{
    struct nb_wayland_surface *surface;
    const struct nb_rect viewport = {
        0,
        0,
        server != NULL ? server->output_width : 0,
        server != NULL ? server->output_height : 0
    };

    if (server == NULL || server->destroying || xwindow == 0) {
        return false;
    }
    surface = find_surface_by_xwayland_window(server, xwindow);
    if (surface == NULL || surface->window == NB_WINDOW_ID_NONE) {
        return false;
    }
    nb_shell_pointer_cancel(server->shell);
    if (!nb_desktop_set_window_fullscreen(&server->shell->desktop,
                                          surface->window,
                                          fullscreen,
                                          viewport) ||
        !nb_wayland_server_window_resized(server, surface->window)) {
        return false;
    }
    mark_redraw_full(server);
    return true;
}

bool nb_wayland_server_unmap_xwayland_window(
    struct nb_wayland_server *server,
    uint32_t xwindow)
{
    struct nb_wayland_surface *surface;

    if (server == NULL || server->destroying || xwindow == 0) {
        return false;
    }
    surface = find_surface_by_xwayland_window(server, xwindow);
    if (surface == NULL) {
        return false;
    }
    unmap_surface(surface, true);
    return true;
}

bool nb_wayland_server_set_external_clipboard_text(
    struct nb_wayland_server *server,
    const char *text,
    size_t size)
{
    char *copy;
    struct wl_client *focused_client = NULL;

    if (server == NULL || server->destroying || text == NULL ||
        size > NB_WAYLAND_CLIPBOARD_MAX_BYTES) {
        return false;
    }
    copy = malloc(size + 1);
    if (copy == NULL) {
        return false;
    }
    if (size != 0) {
        memcpy(copy, text, size);
    }
    copy[size] = '\0';
    selection_clear(server, true, false, false);
    server->selection_kind = NB_WAYLAND_SELECTION_EXTERNAL;
    server->clipboard_text = copy;
    server->clipboard_text_size = size;
    ++server->selection_generation;
    if (server->keyboard_focus != NULL &&
        server->keyboard_focus->surface_resource != NULL) {
        focused_client = wl_resource_get_client(
            server->keyboard_focus->surface_resource);
        data_device_send_selection_to_client(server, focused_client);
    }
    return true;
}

void nb_wayland_server_clear_external_clipboard(
    struct nb_wayland_server *server)
{
    if (server != NULL && !server->destroying &&
        server->selection_kind == NB_WAYLAND_SELECTION_EXTERNAL) {
        selection_clear(server, false, true, false);
    }
}

bool nb_wayland_server_clipboard_text(
    const struct nb_wayland_server *server,
    const char **text,
    size_t *size)
{
    if (server == NULL || text == NULL || size == NULL ||
        server->selection_kind != NB_WAYLAND_SELECTION_SOURCE ||
        server->selection_source == NULL ||
        server->clipboard_text == NULL) {
        return false;
    }
    *text = server->clipboard_text;
    *size = server->clipboard_text_size;
    return true;
}

static struct nb_wayland_surface *pointer_hover_surface(
    struct nb_wayland_server *server,
    nb_window_id hover_window,
    int desktop_x,
    int desktop_y)
{
    struct nb_wayland_surface *root =
        find_surface_by_window(server, hover_window);
    struct nb_wayland_surface *target;
    uint64_t target_sequence = 0;
    size_t index;

    if (root == NULL ||
        (root->role != NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL &&
         root->role != NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) ||
        root->surface_resource == NULL || root->pixels == NULL) {
        return NULL;
    }
    target = root;
    for (index = 0; index < NB_WAYLAND_MAX_SURFACES; ++index) {
        struct nb_wayland_surface *candidate = &server->surfaces[index];
        wl_fixed_t candidate_x;
        wl_fixed_t candidate_y;

        if (!candidate->occupied || !surface_is_mapped(candidate) ||
            (candidate->role != NB_WAYLAND_SURFACE_ROLE_XDG_POPUP &&
             candidate->role != NB_WAYLAND_SURFACE_ROLE_SUBSURFACE) ||
            surface_root_toplevel(candidate) != root ||
            candidate->popup_sequence < target_sequence ||
            !pointer_surface_coordinates(candidate,
                                         desktop_x,
                                         desktop_y,
                                         &candidate_x,
                                         &candidate_y) ||
            candidate_x < 0 || candidate_y < 0 ||
            wl_fixed_to_double(candidate_x) >= candidate->width ||
            wl_fixed_to_double(candidate_y) >= candidate->height) {
            continue;
        }
        target = candidate;
        target_sequence = candidate->popup_sequence;
    }
    return target;
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
                 : pointer_hover_surface(server,
                                         hover_window,
                                         desktop_x,
                                         desktop_y);
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
                     : pointer_hover_surface(server,
                                             hover_window,
                                             desktop_x,
                                             desktop_y);
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
                                                   hover_window,
                                                   desktop_x,
                                                   desktop_y));
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
    const struct nb_wayland_surface *root;

    if (server == NULL || server->pointer_grab == NULL ||
        server->pointer_buttons == 0) {
        return NB_WINDOW_ID_NONE;
    }
    root = surface_root_toplevel_const(server->pointer_grab);
    return root != NULL ? root->window : NB_WINDOW_ID_NONE;
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
        ((surface->role != NB_WAYLAND_SURFACE_ROLE_XDG_TOPLEVEL &&
          surface->role != NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) ||
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
    if (surface == NULL) {
        return false;
    }
    if (surface->toplevel_resource != NULL) {
        xdg_toplevel_send_close(surface->toplevel_resource);
        wl_display_flush_clients(server->display);
        return true;
    }
    return surface->role == NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL &&
           server->xwayland_interface.close_window != NULL &&
           server->xwayland_interface.close_window(
               server->xwayland_context,
               surface->xwayland_window);
}

bool nb_wayland_server_window_resized(struct nb_wayland_server *server,
                                      nb_window_id window)
{
    struct nb_rect content;
    const struct nb_window *host_window;
    struct nb_wayland_surface *surface;
    int desired_width;
    int desired_height;

    if (server == NULL || server->destroying) {
        return false;
    }
    surface = find_surface_by_window(server, window);
    if (surface == NULL ||
        (surface->toplevel_resource == NULL &&
         surface->role != NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL)) {
        return false;
    }
    host_window = nb_desktop_find_window(&server->shell->desktop, window);
    if (host_window == NULL) {
        return false;
    }
    content = nb_window_content_rect(host_window);
    desired_width = content.width;
    desired_height = content.height;
    if (desired_width < NB_WINDOW_MIN_WIDTH) {
        desired_width = NB_WINDOW_MIN_WIDTH;
    }
    if (desired_height < NB_WINDOW_MIN_HEIGHT) {
        desired_height = NB_WINDOW_MIN_HEIGHT;
    }
    if (desired_width == surface->width && desired_height == surface->height) {
        return true;
    }
    if (surface->role == NB_WAYLAND_SURFACE_ROLE_XWAYLAND_TOPLEVEL) {
        if (server->xwayland_interface.configure_window == NULL ||
            !server->xwayland_interface.configure_window(
                server->xwayland_context,
                surface->xwayland_window,
                desired_width,
                desired_height)) {
            return false;
        }
    } else {
        send_toplevel_configure(surface, desired_width, desired_height);
        wl_display_flush_clients(server->display);
    }
    mark_redraw_full(server);
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
        if (server->surfaces[index].occupied &&
            surface_root_is_visible(&server->surfaces[index])) {
            complete_ready_frames(&server->surfaces[index],
                                  milliseconds);
        }
    }
    wl_display_flush_clients(server->display);
}
