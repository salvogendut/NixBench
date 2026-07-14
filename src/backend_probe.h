#ifndef NIXBENCH_BACKEND_PROBE_H
#define NIXBENCH_BACKEND_PROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_BACKEND_PROBE_PATH_CAPACITY = 256,
    NB_BACKEND_PROBE_DRIVER_CAPACITY = 16,
    NB_BACKEND_PROBE_DRIVER_NAME_CAPACITY = 32,
    NB_BACKEND_PROBE_DRM_CARD_CAPACITY = 8,
    NB_BACKEND_PROBE_DRM_VERSION_NAME_CAPACITY = 64,
    NB_BACKEND_PROBE_DRM_VERSION_DATE_CAPACITY = 32,
    NB_BACKEND_PROBE_DRM_VERSION_DESCRIPTION_CAPACITY = 128,
    NB_BACKEND_PROBE_DRM_CONNECTOR_NAME_CAPACITY = 32,
    NB_BACKEND_PROBE_DRM_MODE_NAME_CAPACITY = 32,
    NB_BACKEND_PROBE_DRM_CONNECTOR_CAPACITY = 16,
    NB_BACKEND_PROBE_DRM_MODE_CAPACITY = 32
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

enum nb_backend_probe_drm_open_mode {
    NB_BACKEND_PROBE_DRM_OPEN_NONE,
    NB_BACKEND_PROBE_DRM_OPEN_READ_ONLY,
    NB_BACKEND_PROBE_DRM_OPEN_READ_WRITE
};

enum nb_backend_probe_drm_connection {
    NB_BACKEND_PROBE_DRM_CONNECTION_UNKNOWN,
    NB_BACKEND_PROBE_DRM_CONNECTION_CONNECTED,
    NB_BACKEND_PROBE_DRM_CONNECTION_DISCONNECTED
};

struct nb_backend_probe_drm_capability {
    bool attempted;
    bool available;
    uint64_t value;
    int error;
};

struct nb_backend_probe_drm_version {
    bool available;
    int error;
    int major;
    int minor;
    int patch;
    char name[NB_BACKEND_PROBE_DRM_VERSION_NAME_CAPACITY];
    char date[NB_BACKEND_PROBE_DRM_VERSION_DATE_CAPACITY];
    char description[NB_BACKEND_PROBE_DRM_VERSION_DESCRIPTION_CAPACITY];
};

struct nb_backend_probe_drm_mode {
    char name[NB_BACKEND_PROBE_DRM_MODE_NAME_CAPACITY];
    uint32_t width;
    uint32_t height;
    uint32_t refresh_hz;
    uint32_t flags;
    uint32_t type;
    bool preferred;
    bool interlaced;
};

struct nb_backend_probe_drm_connector {
    uint32_t id;
    uint32_t type;
    uint32_t type_id;
    uint32_t encoder_id;
    char name[NB_BACKEND_PROBE_DRM_CONNECTOR_NAME_CAPACITY];
    bool query_available;
    int query_error;
    enum nb_backend_probe_drm_connection connection;
    uint32_t width_mm;
    uint32_t height_mm;
    size_t reported_mode_count;
    size_t mode_count;
    bool modes_truncated;
    struct nb_backend_probe_drm_mode modes[NB_BACKEND_PROBE_DRM_MODE_CAPACITY];
};

struct nb_backend_probe_drm_card {
    struct nb_backend_probe_device device;
    bool query_supported;
    bool open_attempted;
    enum nb_backend_probe_drm_open_mode open_mode;
    int read_write_open_error;
    int read_only_open_error;
    bool master_checked;
    bool implicit_master;
    int master_check_error;
    bool master_drop_attempted;
    bool master_dropped;
    int master_drop_error;
    int close_error;
    struct nb_backend_probe_drm_version version;
    struct nb_backend_probe_drm_capability dumb_buffer;
    struct nb_backend_probe_drm_capability dumb_preferred_depth;
    struct nb_backend_probe_drm_capability dumb_prefer_shadow;
    struct nb_backend_probe_drm_capability async_page_flip;
    bool resources_available;
    int resources_error;
    size_t crtc_count;
    size_t active_crtc_count;
    size_t crtc_query_error_count;
    size_t encoder_count;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
    size_t reported_connector_count;
    size_t connector_count;
    bool connectors_truncated;
    struct nb_backend_probe_drm_connector
        connectors[NB_BACKEND_PROBE_DRM_CONNECTOR_CAPACITY];
    bool planes_available;
    int planes_error;
    size_t plane_count;
    size_t plane_query_error_count;
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
    bool drm_query_compiled;
    size_t drm_card_count;
    bool drm_cards_truncated;
    struct nb_backend_probe_drm_card
        drm_cards[NB_BACKEND_PROBE_DRM_CARD_CAPACITY];
};

void nb_backend_probe_default_paths(struct nb_backend_probe_paths *paths);

/*
 * Collecting a snapshot is non-destructive: it never changes a display mode,
 * maps device memory, reads input events, explicitly requests DRM master, or
 * changes display state. Opening an idle primary DRM node can grant master
 * implicitly; the DRM collector drops that grant before issuing queries and
 * aborts the card if it cannot do so.
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
bool nb_backend_probe_drm_card_has_connected_output(
    const struct nb_backend_probe_drm_card *card);
bool nb_backend_probe_drm_card_is_kms_candidate(
    const struct nb_backend_probe_drm_card *card);
bool nb_backend_probe_has_kms_candidate(
    const struct nb_backend_probe_snapshot *snapshot);

const char *nb_backend_probe_wsdisplay_mode_name(unsigned int mode);

#endif
