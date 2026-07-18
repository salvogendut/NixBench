#ifndef NIXBENCH_WAYLAND_SERVER_H
#define NIXBENCH_WAYLAND_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "damage_region.h"
#include "shell.h"
#include "theme_atlas.h"

enum {
    NB_WAYLAND_MAX_SURFACES = NB_DESKTOP_MAX_WINDOWS,
    NB_WAYLAND_DISPLAY_NAME_CAPACITY = 64
};

struct nb_wayland_server;

/*
 * Rootless Xwayland creates ordinary wl_surface objects without assigning an
 * xdg-shell role. Its X window manager identifies those surfaces through the
 * WL_SURFACE_ID client message and supplies the corresponding X11 window ID.
 * Configure and close requests then travel back to that unprivileged XWM.
 */
struct nb_wayland_xwayland_interface {
    bool (*configure_window)(void *context,
                             uint32_t xwindow,
                             int width,
                             int height);
    bool (*close_window)(void *context, uint32_t xwindow);
    /* A zero XID clears X11 focus when focus leaves rootless Xwayland. */
    bool (*focus_window)(void *context, uint32_t xwindow);
    /* Claim or release the X11 selections for a Wayland text clipboard. */
    bool (*set_clipboard_owner)(void *context, bool available);
};

/*
 * Stable, host-independent buttons.  The server translates these to the
 * Linux evdev button numbers required by the Wayland wire protocol, so the
 * SDL adapter does not need Linux-only input headers on NetBSD.
 */
enum nb_wayland_pointer_button {
    NB_WAYLAND_POINTER_BUTTON_LEFT,
    NB_WAYLAND_POINTER_BUTTON_MIDDLE,
    NB_WAYLAND_POINTER_BUTTON_RIGHT,
    NB_WAYLAND_POINTER_BUTTON_SIDE,
    NB_WAYLAND_POINTER_BUTTON_EXTRA,
    NB_WAYLAND_POINTER_BUTTON_COUNT
};

/*
 * Pixels are compositor-owned, tightly packed, premultiplied ARGB8888.
 * The borrowed pointer remains valid until the next successful dispatch or
 * until the corresponding surface/server is destroyed.
 */
struct nb_wayland_surface_snapshot {
    const uint32_t *pixels;
    int width;
    int height;
    int stride;
    int geometry_x;
    int geometry_y;
    int geometry_width;
    int geometry_height;
    bool geometry_set;
    uint64_t revision;
};

struct nb_wayland_html_theme_snapshot {
    struct nb_wayland_surface_snapshot surface;
    const struct nb_theme_atlas_layout *layout;
};

/*
 * The shell and fallback menu model are borrowed and must outlive the server.
 * menu_source identifies the fallback model. The caller keeps that ID and the
 * following NB_WAYLAND_MAX_SURFACES IDs unused by other sources and servers
 * for this server's lifetime; creation rejects collisions that already exist.
 */
struct nb_wayland_server *nb_wayland_server_create(
    struct nb_shell *shell,
    nb_menu_source_id menu_source,
    const struct nb_menu_model *menu_model,
    int output_width,
    int output_height);
void nb_wayland_server_destroy(struct nb_wayland_server *server);

/*
 * Enable the private ordinary-user HTML renderer before publishing clients.
 * Strings are copied. The token should be an unguessable per-session value;
 * the selected bundle has already been canonicalized and validated by the
 * caller. Classic sessions do not call this function.
 */
bool nb_wayland_server_enable_html_theme(
    struct nb_wayland_server *server,
    const char *token,
    const char *theme_id,
    const char *theme_directory);
bool nb_wayland_server_html_theme_connected(
    const struct nb_wayland_server *server);
bool nb_wayland_server_html_theme_snapshot(
    const struct nb_wayland_server *server,
    struct nb_wayland_html_theme_snapshot *snapshot);

/*
 * Publish a standard Wayland socket beneath XDG_RUNTIME_DIR. The returned
 * name is borrowed and is NULL on failure. Calling this twice returns the
 * original name.
 */
const char *nb_wayland_server_add_socket_auto(
    struct nb_wayland_server *server);
const char *nb_wayland_server_display_name(
    const struct nb_wayland_server *server);

/*
 * Attach one end of an already-connected stream socket. Ownership of fd is
 * transferred to libwayland on success and remains with the caller on error.
 * This exists for launchers and deterministic in-process protocol tests.
 */
bool nb_wayland_server_add_client_fd(struct nb_wayland_server *server,
                                     int fd);

/* Pollable libwayland event-loop descriptor, or -1 when unavailable. */
int nb_wayland_server_event_descriptor(
    const struct nb_wayland_server *server);

/* Dispatch queued client requests without blocking and flush all clients. */
bool nb_wayland_server_dispatch(struct nb_wayland_server *server);
/*
 * Consume the compositor's visible-damage latch. A true result means that a
 * client request changed content owned by the desktop and the caller should
 * render and present one new frame. Idle dispatch never sets this latch.
 */
bool nb_wayland_server_take_redraw(struct nb_wayland_server *server);
/* Consume redraw and return its desktop damage; unknown changes are full. */
bool nb_wayland_server_take_redraw_damage(
    struct nb_wayland_server *server,
    struct nb_rect *damage);
/* Consume redraw while preserving disjoint desktop damage rectangles. */
bool nb_wayland_server_take_redraw_region(
    struct nb_wayland_server *server,
    struct nb_damage_region *damage);
/* Update the single logical output; unchanged sizes are accepted silently. */
bool nb_wayland_server_set_output_size(struct nb_wayland_server *server,
                                       int width,
                                       int height);
/* Complete frame callbacks only after the compositor has presented a frame. */
void nb_wayland_server_frame_presented(struct nb_wayland_server *server,
                                       uint32_t milliseconds);

bool nb_wayland_server_owns_window(
    const struct nb_wayland_server *server,
    nb_window_id window);
size_t nb_wayland_server_window_count(
    const struct nb_wayland_server *server);
nb_window_id nb_wayland_server_window_at(
    const struct nb_wayland_server *server,
    size_t index);
bool nb_wayland_server_surface_snapshot(
    const struct nb_wayland_server *server,
    nb_window_id window,
    struct nb_wayland_surface_snapshot *snapshot);

/*
 * Forward host pointer input after the shell has resolved menus,
 * decorations, and stacking.  hover_window is the explicit Wayland content
 * target, or NB_WINDOW_ID_NONE when the host pointer is not over client
 * content.  During an implicit button grab, motion and releases continue to
 * the grabbed surface regardless of hover_window.  A true return means the
 * input update was accepted; it does not imply that a client was targeted.
 */
bool nb_wayland_server_pointer_motion(struct nb_wayland_server *server,
                                      nb_window_id hover_window,
                                      int desktop_x,
                                      int desktop_y,
                                      uint32_t milliseconds);
bool nb_wayland_server_pointer_button(
    struct nb_wayland_server *server,
    nb_window_id hover_window,
    int desktop_x,
    int desktop_y,
    uint32_t milliseconds,
    enum nb_wayland_pointer_button button,
    bool pressed);
/* Release any held buttons and remove pointer focus. */
void nb_wayland_server_pointer_cancel(struct nb_wayland_server *server,
                                      uint32_t milliseconds);
nb_window_id nb_wayland_server_pointer_grab_window(
    const struct nb_wayland_server *server);

/*
 * Keyboard focus follows the active shell window. Physical keys are named
 * with XKB key names (for example "AC01", "RTRN", or "LFSH") so the SDL
 * adapter remains independent of Linux evdev headers and NetBSD raw codes.
 */
bool nb_wayland_server_keyboard_focus(
    struct nb_wayland_server *server,
    nb_window_id window);
/* Key names absent from the active keymap are accepted and ignored. */
bool nb_wayland_server_keyboard_key(struct nb_wayland_server *server,
                                    const char *xkb_key_name,
                                    uint32_t milliseconds,
                                    bool pressed);
/* Synthesize releases for held keys and remove keyboard focus. */
void nb_wayland_server_keyboard_cancel(struct nb_wayland_server *server,
                                       uint32_t milliseconds);

/* Ask the owning xdg_toplevel to close; the client remains authoritative. */
bool nb_wayland_server_request_close(struct nb_wayland_server *server,
                                     nb_window_id window);
/*
 * Notify the owning xdg_toplevel that its host frame changed size so the
 * client can reconfigure itself instead of being stretched.
 */
bool nb_wayland_server_window_resized(struct nb_wayland_server *server,
                                      nb_window_id window);

/* Deliver an enabled command from a surface's committed application menu. */
bool nb_wayland_server_dispatch_menu_command(
    struct nb_wayland_server *server,
    nb_window_id window,
    nb_menu_source_id menu_source,
    nb_menu_command command);

/* Install or clear the unprivileged rootless-Xwayland control callbacks. */
void nb_wayland_server_set_xwayland_interface(
    struct nb_wayland_server *server,
    const struct nb_wayland_xwayland_interface *interface,
    void *context);
void nb_wayland_server_authorize_xwayland_client(
    struct nb_wayland_server *server,
    pid_t process);

/* Associate an X11 top-level using either Xwayland handshake generation. */
bool nb_wayland_server_associate_xwayland_surface(
    struct nb_wayland_server *server,
    uint32_t surface_resource_id,
    uint32_t xwindow,
    const char *title,
    const char *application_name);
bool nb_wayland_server_associate_xwayland_serial(
    struct nb_wayland_server *server,
    uint64_t surface_serial,
    uint32_t xwindow,
    const char *title,
    const char *application_name);
bool nb_wayland_server_update_xwayland_identity(
    struct nb_wayland_server *server,
    uint32_t xwindow,
    const char *title,
    const char *application_name);
bool nb_wayland_server_set_xwayland_fullscreen(
    struct nb_wayland_server *server,
    uint32_t xwindow,
    bool fullscreen);
bool nb_wayland_server_unmap_xwayland_window(
    struct nb_wayland_server *server,
    uint32_t xwindow);

/*
 * Import bounded UTF-8 text from the unprivileged XWM. The compositor copies
 * data before returning. Clearing affects only an X11-imported selection, so
 * a stale XFixes notification cannot discard a newer Wayland-owned clipboard.
 */
bool nb_wayland_server_set_external_clipboard_text(
    struct nb_wayland_server *server,
    const char *text,
    size_t size);
void nb_wayland_server_clear_external_clipboard(
    struct nb_wayland_server *server);

/* Borrow the cached text used while NixBench owns the X11 selections. */
bool nb_wayland_server_clipboard_text(
    const struct nb_wayland_server *server,
    const char **text,
    size_t *size);

#endif
