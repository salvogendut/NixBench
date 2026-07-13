#ifndef NIXBENCH_BACKEND_PROBE_H
#define NIXBENCH_BACKEND_PROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_BACKEND_PROBE_PATH_CAPACITY = 256,
    NB_BACKEND_PROBE_DRIVER_CAPACITY = 16,
    NB_BACKEND_PROBE_DRIVER_NAME_CAPACITY = 32,
    NB_BACKEND_PROBE_DRM_CARD_CAPACITY = 8
};

struct nb_backend_probe_paths {
    const char *wsdisplay;
    const char *keyboard;
    const char *mouse;
    const char *drm_directory;
};

struct nb_backend_probe_device {
    char path[NB_BACKEND_PROBE_PATH_CAPACITY];
    bool exists;
    bool character_device;
    bool readable;
    bool writable;
    int stat_error;
    int read_error;
    int write_error;
};

struct nb_backend_probe_wsdisplay {
    struct nb_backend_probe_device device;
    bool opened;
    int open_error;
    bool type_available;
    int type;
    int type_error;
    bool mode_available;
    unsigned int mode;
    int mode_error;
    bool framebuffer_info_available;
    int framebuffer_info_error;
    uint64_t framebuffer_size;
    uint64_t framebuffer_offset;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bits_per_pixel;
    uint32_t pixel_type;
    uint32_t red_offset;
    uint32_t red_size;
    uint32_t green_offset;
    uint32_t green_size;
    uint32_t blue_offset;
    uint32_t blue_size;
    uint32_t alpha_offset;
    uint32_t alpha_size;
    bool rgb;
    bool software_layout_supported;
};

struct nb_backend_probe_snapshot {
    bool netbsd;
    size_t sdl_driver_count;
    bool sdl_drivers_truncated;
    char sdl_drivers[NB_BACKEND_PROBE_DRIVER_CAPACITY]
                    [NB_BACKEND_PROBE_DRIVER_NAME_CAPACITY];
    struct nb_backend_probe_wsdisplay wsdisplay;
    struct nb_backend_probe_device keyboard;
    struct nb_backend_probe_device mouse;
    char drm_directory[NB_BACKEND_PROBE_PATH_CAPACITY];
    bool drm_directory_opened;
    int drm_directory_error;
    size_t drm_card_count;
    bool drm_cards_truncated;
    struct nb_backend_probe_device
        drm_cards[NB_BACKEND_PROBE_DRM_CARD_CAPACITY];
};

void nb_backend_probe_default_paths(struct nb_backend_probe_paths *paths);

/*
 * Collecting a snapshot is non-destructive: it never changes a display mode,
 * maps device memory, reads input events, or claims DRM master status.
 */
void nb_backend_probe_collect(struct nb_backend_probe_snapshot *snapshot,
                              const struct nb_backend_probe_paths *paths);

bool nb_backend_probe_has_sdl_driver(
    const struct nb_backend_probe_snapshot *snapshot,
    const char *name);
bool nb_backend_probe_has_hosted_sdl_driver(
    const struct nb_backend_probe_snapshot *snapshot);
bool nb_backend_probe_wsdisplay_software_ready(
    const struct nb_backend_probe_snapshot *snapshot);
bool nb_backend_probe_has_accessible_drm_card(
    const struct nb_backend_probe_snapshot *snapshot);

const char *nb_backend_probe_wsdisplay_mode_name(unsigned int mode);

#endif
