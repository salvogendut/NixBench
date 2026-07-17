#ifndef NIXBENCH_XWAYLAND_ROOTLESS_H
#define NIXBENCH_XWAYLAND_ROOTLESS_H

#include <stdbool.h>

struct nb_desktop_runtime;
struct nb_xwayland_rootless;

/*
 * Start one unprivileged, rootless Xwayland server and its minimal XWM.
 * The returned service owns the Xwayland child and the DISPLAY socket.
 */
struct nb_xwayland_rootless *nb_xwayland_rootless_create(
    struct nb_desktop_runtime *desktop,
    const char *xwayland_path);
void nb_xwayland_rootless_destroy(struct nb_xwayland_rootless *service);

/* Drain XWM events without blocking and associate any pending wl_surfaces. */
bool nb_xwayland_rootless_dispatch(struct nb_xwayland_rootless *service);
int nb_xwayland_rootless_event_descriptor(
    const struct nb_xwayland_rootless *service);
const char *nb_xwayland_rootless_display_name(
    const struct nb_xwayland_rootless *service);

#endif
