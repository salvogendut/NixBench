#ifndef NIXBENCH_BACKEND_PROBE_DRM_INTERNAL_H
#define NIXBENCH_BACKEND_PROBE_DRM_INTERNAL_H

#include "backend_probe.h"

#include <stdbool.h>

/*
 * Injectable operations keep the descriptor-lifecycle policy testable on
 * machines without a DRM subsystem.  Operations return zero on success and
 * negative errno values on failure, except open_card(), which returns either
 * a non-negative descriptor or a negative errno value.
 */
struct nb_backend_probe_drm_operations {
    int (*open_card)(void *opaque, const char *path, bool read_write);
    int (*close_card)(void *opaque, int descriptor);
    int (*is_master)(void *opaque, int descriptor, bool *is_master);
    int (*drop_master)(void *opaque, int descriptor);
    void (*collect_version)(void *opaque,
                            int descriptor,
                            struct nb_backend_probe_drm_card *card);
    void (*collect_capabilities)(void *opaque,
                                 int descriptor,
                                 struct nb_backend_probe_drm_card *card);
    void (*collect_resources)(void *opaque,
                              int descriptor,
                              struct nb_backend_probe_drm_card *card);
};

bool nb_backend_probe_drm_query_supported(void);

void nb_backend_probe_drm_collect_card(
    struct nb_backend_probe_drm_card *card);

void nb_backend_probe_drm_collect_card_with_operations(
    struct nb_backend_probe_drm_card *card,
    const struct nb_backend_probe_drm_operations *operations,
    void *opaque);

#endif
