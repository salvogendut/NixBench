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

/* The shell and menu model are borrowed and must outlive the server. */
struct nb_wayland_server *nb_wayland_server_create(
    struct nb_shell *shell,
    nb_menu_source_id menu_source,
    const struct nb_menu_model *menu_model);
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

/* Ask the owning xdg_toplevel to close; the client remains authoritative. */
bool nb_wayland_server_request_close(struct nb_wayland_server *server,
                                     nb_window_id window);

#endif
