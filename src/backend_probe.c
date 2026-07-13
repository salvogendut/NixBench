#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "backend_probe.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__NetBSD__)
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dev/wscons/wsconsio.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static void collect_sdl_drivers(struct nb_backend_probe_snapshot *snapshot)
{
    const int reported_count = SDL_GetNumVideoDrivers();
    int index;

    if (reported_count <= 0) {
        return;
    }

    for (index = 0; index < reported_count; ++index) {
        const char *name = SDL_GetVideoDriver(index);

        if (snapshot->sdl_driver_count >=
            NB_BACKEND_PROBE_DRIVER_CAPACITY) {
            snapshot->sdl_drivers_truncated = true;
            break;
        }
        if (name == NULL) {
            continue;
        }
        (void)snprintf(
            snapshot->sdl_drivers[snapshot->sdl_driver_count],
            NB_BACKEND_PROBE_DRIVER_NAME_CAPACITY,
            "%s",
            name);
        ++snapshot->sdl_driver_count;
    }
}

void nb_backend_probe_default_paths(struct nb_backend_probe_paths *paths)
{
    if (paths == NULL) {
        return;
    }

    paths->wsdisplay = "/dev/ttyE0";
    paths->keyboard = "/dev/wskbd";
    paths->mouse = "/dev/wsmouse";
    paths->drm_directory = "/dev/dri";
}

#if defined(__NetBSD__)
static void copy_path(char destination[NB_BACKEND_PROBE_PATH_CAPACITY],
                      const char *source)
{
    (void)snprintf(destination,
                   NB_BACKEND_PROBE_PATH_CAPACITY,
                   "%s",
                   source);
}

static void probe_device(const char *path,
                         struct nb_backend_probe_device *device)
{
    struct stat status;

    copy_path(device->path, path);
    if (stat(path, &status) != 0) {
        device->stat_error = errno;
        return;
    }

    device->exists = true;
    device->character_device = S_ISCHR(status.st_mode);
    if (access(path, R_OK) == 0) {
        device->readable = true;
    } else {
        device->read_error = errno;
    }
    if (access(path, W_OK) == 0) {
        device->writable = true;
    } else {
        device->write_error = errno;
    }
}

static bool mask_fits(uint32_t offset,
                      uint32_t size,
                      uint32_t bits_per_pixel)
{
    return size > 0 && (uint64_t)offset + (uint64_t)size <=
                           (uint64_t)bits_per_pixel;
}

static bool framebuffer_layout_supported(
    const struct nb_backend_probe_wsdisplay *display)
{
    uint64_t bytes_per_pixel;
    uint64_t minimum_stride;
    uint64_t final_row_end;

    if (!display->rgb || display->width == 0 || display->height == 0 ||
        display->stride == 0 ||
        (display->bits_per_pixel != 16 &&
         display->bits_per_pixel != 24 &&
         display->bits_per_pixel != 32) ||
        !mask_fits(display->red_offset,
                   display->red_size,
                   display->bits_per_pixel) ||
        !mask_fits(display->green_offset,
                   display->green_size,
                   display->bits_per_pixel) ||
        !mask_fits(display->blue_offset,
                   display->blue_size,
                   display->bits_per_pixel)) {
        return false;
    }

    bytes_per_pixel = ((uint64_t)display->bits_per_pixel + UINT64_C(7)) /
                      UINT64_C(8);
    minimum_stride = (uint64_t)display->width * bytes_per_pixel;
    if (minimum_stride > display->stride) {
        return false;
    }

    if ((uint64_t)display->height - UINT64_C(1) >
        (UINT64_MAX - minimum_stride) / (uint64_t)display->stride) {
        return false;
    }
    final_row_end = ((uint64_t)display->height - UINT64_C(1)) *
                        (uint64_t)display->stride +
                    minimum_stride;
    return final_row_end <= display->framebuffer_size;
}

static void probe_wsdisplay(const char *path,
                            struct nb_backend_probe_wsdisplay *display)
{
    struct wsdisplayio_fbinfo information;
    int descriptor;
    int type = 0;
    unsigned int mode = 0;
    int open_flags = O_RDONLY | O_NONBLOCK | O_NOCTTY;

#if defined(O_CLOEXEC)
    open_flags |= O_CLOEXEC;
#endif

    probe_device(path, &display->device);
    descriptor = open(path, open_flags);
    if (descriptor == -1) {
        display->open_error = errno;
        return;
    }
    display->opened = true;

    if (ioctl(descriptor, WSDISPLAYIO_GTYPE, &type) == 0) {
        display->type_available = true;
        display->type = type;
    } else {
        display->type_error = errno;
    }

    if (ioctl(descriptor, WSDISPLAYIO_GMODE, &mode) == 0) {
        display->mode_available = true;
        display->mode = mode;
    } else {
        display->mode_error = errno;
    }

    memset(&information, 0, sizeof(information));
    if (ioctl(descriptor, WSDISPLAYIO_GET_FBINFO, &information) == 0) {
        display->framebuffer_info_available = true;
        display->framebuffer_size = information.fbi_fbsize;
        display->framebuffer_offset = information.fbi_fboffset;
        display->width = information.fbi_width;
        display->height = information.fbi_height;
        display->stride = information.fbi_stride;
        display->bits_per_pixel = information.fbi_bitsperpixel;
        display->pixel_type = information.fbi_pixeltype;
        if (information.fbi_pixeltype == WSFB_RGB) {
            display->rgb = true;
            display->red_offset =
                information.fbi_subtype.fbi_rgbmasks.red_offset;
            display->red_size =
                information.fbi_subtype.fbi_rgbmasks.red_size;
            display->green_offset =
                information.fbi_subtype.fbi_rgbmasks.green_offset;
            display->green_size =
                information.fbi_subtype.fbi_rgbmasks.green_size;
            display->blue_offset =
                information.fbi_subtype.fbi_rgbmasks.blue_offset;
            display->blue_size =
                information.fbi_subtype.fbi_rgbmasks.blue_size;
            display->alpha_offset =
                information.fbi_subtype.fbi_rgbmasks.alpha_offset;
            display->alpha_size =
                information.fbi_subtype.fbi_rgbmasks.alpha_size;
        }
        display->software_layout_supported =
            framebuffer_layout_supported(display);
    } else {
        display->framebuffer_info_error = errno;
    }

    (void)close(descriptor);
}

static bool drm_card_name(const char *name)
{
    size_t index;

    if (strncmp(name, "card", 4) != 0 || name[4] == '\0') {
        return false;
    }
    for (index = 4; name[index] != '\0'; ++index) {
        if (name[index] < '0' || name[index] > '9') {
            return false;
        }
    }
    return true;
}

static int compare_devices(const void *left, const void *right)
{
    const struct nb_backend_probe_device *left_device = left;
    const struct nb_backend_probe_device *right_device = right;

    return strcmp(left_device->path, right_device->path);
}

static void probe_drm_directory(const char *path,
                                struct nb_backend_probe_snapshot *snapshot)
{
    DIR *directory;
    struct dirent *entry;

    copy_path(snapshot->drm_directory, path);
    directory = opendir(path);
    if (directory == NULL) {
        snapshot->drm_directory_error = errno;
        return;
    }
    snapshot->drm_directory_opened = true;

    while ((entry = readdir(directory)) != NULL) {
        struct nb_backend_probe_device *card;
        char card_path[NB_BACKEND_PROBE_PATH_CAPACITY];
        const int length = snprintf(card_path,
                                    sizeof(card_path),
                                    "%s/%s",
                                    path,
                                    entry->d_name);

        if (!drm_card_name(entry->d_name)) {
            continue;
        }
        if (length < 0 || (size_t)length >= sizeof(card_path)) {
            snapshot->drm_cards_truncated = true;
            continue;
        }
        if (snapshot->drm_card_count >= NB_BACKEND_PROBE_DRM_CARD_CAPACITY) {
            snapshot->drm_cards_truncated = true;
            continue;
        }

        card = &snapshot->drm_cards[snapshot->drm_card_count++];
        probe_device(card_path, card);
    }

    (void)closedir(directory);
    qsort(snapshot->drm_cards,
          snapshot->drm_card_count,
          sizeof(snapshot->drm_cards[0]),
          compare_devices);
}
#endif

void nb_backend_probe_collect(struct nb_backend_probe_snapshot *snapshot,
                              const struct nb_backend_probe_paths *paths)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    collect_sdl_drivers(snapshot);

#if defined(__NetBSD__)
    {
        struct nb_backend_probe_paths defaults;
        struct nb_backend_probe_paths selected;

        nb_backend_probe_default_paths(&defaults);
        selected.wsdisplay = paths != NULL && paths->wsdisplay != NULL
                                 ? paths->wsdisplay
                                 : defaults.wsdisplay;
        selected.keyboard = paths != NULL && paths->keyboard != NULL
                                ? paths->keyboard
                                : defaults.keyboard;
        selected.mouse = paths != NULL && paths->mouse != NULL
                             ? paths->mouse
                             : defaults.mouse;
        selected.drm_directory = paths != NULL && paths->drm_directory != NULL
                                     ? paths->drm_directory
                                     : defaults.drm_directory;

        snapshot->netbsd = true;
        probe_wsdisplay(selected.wsdisplay, &snapshot->wsdisplay);
        probe_device(selected.keyboard, &snapshot->keyboard);
        probe_device(selected.mouse, &snapshot->mouse);
        probe_drm_directory(selected.drm_directory, snapshot);
    }
#else
    (void)paths;
#endif
}

bool nb_backend_probe_has_sdl_driver(
    const struct nb_backend_probe_snapshot *snapshot,
    const char *name)
{
    size_t index;

    if (snapshot == NULL || name == NULL) {
        return false;
    }
    for (index = 0; index < snapshot->sdl_driver_count; ++index) {
        if (strcmp(snapshot->sdl_drivers[index], name) == 0) {
            return true;
        }
    }
    return false;
}

bool nb_backend_probe_has_hosted_sdl_driver(
    const struct nb_backend_probe_snapshot *snapshot)
{
    static const char *const hosted_drivers[] = {
        "x11", "wayland", "windows", "cocoa", "uikit",
        "android", "haiku", "emscripten"
    };
    size_t index;

    for (index = 0;
         index < sizeof(hosted_drivers) / sizeof(hosted_drivers[0]);
         ++index) {
        if (nb_backend_probe_has_sdl_driver(snapshot,
                                            hosted_drivers[index])) {
            return true;
        }
    }
    return false;
}

bool nb_backend_probe_wsdisplay_software_ready(
    const struct nb_backend_probe_snapshot *snapshot)
{
    const struct nb_backend_probe_wsdisplay *display;

    if (snapshot == NULL || !snapshot->netbsd) {
        return false;
    }

    display = &snapshot->wsdisplay;
    return display->device.exists && display->device.character_device &&
           display->device.readable && display->device.writable &&
           display->opened && display->mode_available &&
           display->framebuffer_info_available &&
           display->software_layout_supported;
}

bool nb_backend_probe_has_accessible_drm_card(
    const struct nb_backend_probe_snapshot *snapshot)
{
    size_t index;

    if (snapshot == NULL || !snapshot->netbsd) {
        return false;
    }
    for (index = 0; index < snapshot->drm_card_count; ++index) {
        const struct nb_backend_probe_device *card =
            &snapshot->drm_cards[index];

        if (card->exists && card->character_device && card->readable &&
            card->writable) {
            return true;
        }
    }
    return false;
}

const char *nb_backend_probe_wsdisplay_mode_name(unsigned int mode)
{
#if defined(__NetBSD__)
    switch (mode) {
    case WSDISPLAYIO_MODE_EMUL:
        return "emulation";
    case WSDISPLAYIO_MODE_MAPPED:
        return "mapped";
    case WSDISPLAYIO_MODE_DUMBFB:
        return "dumb framebuffer";
    default:
        break;
    }
#else
    (void)mode;
#endif
    return "unknown";
}
