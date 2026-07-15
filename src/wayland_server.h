#ifndef NIXBENCH_WAYLAND_SERVER_H
#define NIXBENCH_WAYLAND_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shell.h"

enum {
    NB_WAYLAND_MAX_SURFACES = NB_DESKTOP_MAX_WINDOWS,
    NB_WAYLAND_DISPLAY_NAME_CAPACITY = 64
};

struct nb_wayland_server;

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
    uint64_t revision;
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

/* Dispatch queued client requests without blocking and flush all clients. */
bool nb_wayland_server_dispatch(struct nb_wayland_server *server);
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

/* Deliver an enabled command from a surface's committed application menu. */
bool nb_wayland_server_dispatch_menu_command(
    struct nb_wayland_server *server,
    nb_window_id window,
    nb_menu_source_id menu_source,
    nb_menu_command command);

#endif
