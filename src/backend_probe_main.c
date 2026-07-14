#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL_main.h>

#include "backend_probe.h"

static void print_usage(const char *program_name)
{
    printf("Usage: %s [OPTION]\n", program_name);
    puts("Inspect display and input backend capabilities without taking "
         "control of them.\n");
    puts("  --wsdisplay PATH     wsdisplay device (default /dev/ttyE0)");
    puts("  --keyboard PATH      wscons keyboard device (default /dev/wskbd)");
    puts("  --mouse PATH         wscons pointer device (default /dev/wsmouse)");
    puts("  --drm-directory PATH DRM device directory (default /dev/dri)");
    puts("  --help               show this help");
}

static bool read_option_value(int argc,
                              char *argv[],
                              int *index,
                              const char **destination)
{
    if (*index + 1 >= argc) {
        fprintf(stderr, "%s: option requires a path: %s\n",
                argv[0], argv[*index]);
        return false;
    }

    ++*index;
    *destination = argv[*index];
    return true;
}

static bool parse_options(int argc,
                          char *argv[],
                          struct nb_backend_probe_paths *paths,
                          bool *show_help)
{
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--wsdisplay") == 0) {
            if (!read_option_value(argc,
                                   argv,
                                   &index,
                                   &paths->wsdisplay)) {
                return false;
            }
        } else if (strcmp(argv[index], "--keyboard") == 0) {
            if (!read_option_value(argc,
                                   argv,
                                   &index,
                                   &paths->keyboard)) {
                return false;
            }
        } else if (strcmp(argv[index], "--mouse") == 0) {
            if (!read_option_value(argc,
                                   argv,
                                   &index,
                                   &paths->mouse)) {
                return false;
            }
        } else if (strcmp(argv[index], "--drm-directory") == 0) {
            if (!read_option_value(argc,
                                   argv,
                                   &index,
                                   &paths->drm_directory)) {
                return false;
            }
        } else if (strcmp(argv[index], "--help") == 0) {
            *show_help = true;
        } else {
            fprintf(stderr, "%s: unknown option: %s\n",
                    argv[0], argv[index]);
            fprintf(stderr, "Try '%s --help' for more information.\n",
                    argv[0]);
            return false;
        }
    }
    return true;
}

static const char *error_text(int error)
{
    return error != 0 ? strerror(error) : "not available";
}

static void print_sdl_drivers(
    const struct nb_backend_probe_snapshot *snapshot)
{
    size_t index;

    printf("SDL video drivers (%zu%s):\n",
           snapshot->sdl_driver_count,
           snapshot->sdl_drivers_truncated ? "+" : "");
    if (snapshot->sdl_driver_count == 0) {
        puts("  none reported");
    }
    for (index = 0; index < snapshot->sdl_driver_count; ++index) {
        printf("  %s\n", snapshot->sdl_drivers[index]);
    }

    printf("Hosted SDL output: %s\n",
           nb_backend_probe_has_hosted_sdl_driver(snapshot)
               ? "available"
               : "unavailable (no hosted video driver was reported)");
    printf("SDL KMSDRM backend: %s\n",
           nb_backend_probe_has_sdl_driver(snapshot, "kmsdrm")
               ? "compiled in"
               : "unavailable (kmsdrm was not reported)");
}

static void print_access(const char *label,
                         const struct nb_backend_probe_device *device)
{
    printf("%s: %s\n", label, device->path);
    if (!device->exists) {
        printf("  unavailable: %s\n", error_text(device->stat_error));
        return;
    }

    printf("  type: %s\n",
           device->character_device ? "character device" : "not a character device");
    if (device->readable) {
        puts("  readable: yes");
    } else {
        printf("  readable: no (%s)\n", error_text(device->read_error));
    }
    if (device->writable) {
        puts("  writable: yes");
    } else {
        printf("  writable: no (%s)\n", error_text(device->write_error));
    }
}

static void print_wsdisplay(
    const struct nb_backend_probe_wsdisplay *display)
{
    print_access("wsdisplay", &display->device);
    if (!display->opened) {
        printf("  query open: failed (%s)\n",
               error_text(display->open_error));
        return;
    }
    puts("  query open: yes (read-only)");

    if (display->type_available) {
        printf("  display type: %d\n", display->type);
    } else {
        printf("  display type: unavailable (%s)\n",
               error_text(display->type_error));
    }
    if (display->mode_available) {
        printf("  current mode: %u (%s)\n",
               display->mode,
               nb_backend_probe_wsdisplay_mode_name(display->mode));
    } else {
        printf("  current mode: unavailable (%s)\n",
               error_text(display->mode_error));
    }
    if (!display->framebuffer_info_available) {
        printf("  framebuffer information: unavailable (%s)\n",
               error_text(display->framebuffer_info_error));
        return;
    }

    printf("  framebuffer: %ux%u, stride %u, %u bpp\n",
           display->width,
           display->height,
           display->stride,
           display->bits_per_pixel);
    printf("  mapping range: offset %llu, size %llu bytes\n",
           (unsigned long long)display->framebuffer_offset,
           (unsigned long long)display->framebuffer_size);
    printf("  pixel type: %u%s\n",
           display->pixel_type,
           display->rgb ? " (RGB)" : "");
    if (display->rgb) {
        printf("  channel masks: R %u:%u, G %u:%u, B %u:%u, A %u:%u\n",
               display->red_offset,
               display->red_size,
               display->green_offset,
               display->green_size,
               display->blue_offset,
               display->blue_size,
               display->alpha_offset,
               display->alpha_size);
    }
    printf("  supported software layout: %s\n",
           display->software_layout_supported ? "yes" : "no");
}

static void print_drm_close_error(
    const struct nb_backend_probe_drm_card *card)
{
    if (card->close_error != 0) {
        printf("  close: failed (%s)\n", error_text(card->close_error));
    }
}

static void print_drm(const struct nb_backend_probe_snapshot *snapshot)
{
    size_t index;

    printf("DRM directory: %s\n", snapshot->drm_directory);
    if (!snapshot->drm_directory_opened) {
        printf("  unavailable: %s\n",
               error_text(snapshot->drm_directory_error));
        return;
    }
    printf("Detailed DRM queries: %s\n",
           snapshot->drm_query_compiled
               ? "enabled (libdrm)"
               : "unavailable (built without NetBSD libdrm support)");
    if (snapshot->drm_card_count == 0) {
        puts("  no card nodes found");
    }
    for (index = 0; index < snapshot->drm_card_count; ++index) {
        const struct nb_backend_probe_drm_card *card =
            &snapshot->drm_cards[index];
        size_t connector_index;

        print_access("DRM card", &card->device);
        if (!card->device.exists || !card->device.character_device) {
            puts("  detailed query: skipped (not an existing character "
                 "device)");
            continue;
        }
        if (!card->query_supported) {
            puts("  detailed query: unavailable (built without libdrm)");
            continue;
        }
        if (card->open_mode == NB_BACKEND_PROBE_DRM_OPEN_NONE) {
            printf("  query open: failed (read-write: %s; read-only: %s)\n",
                   error_text(card->read_write_open_error),
                   error_text(card->read_only_open_error));
            continue;
        }
        printf("  query open: yes (%s)\n",
               card->open_mode == NB_BACKEND_PROBE_DRM_OPEN_READ_WRITE
                   ? "read-write"
                   : "read-only");
        if (!card->master_checked) {
            printf("  master safety check: failed (%s); queries aborted\n",
                   error_text(card->master_check_error));
            print_drm_close_error(card);
            continue;
        }
        if (card->implicit_master) {
            if (!card->master_dropped) {
                printf("  implicit DRM master: could not drop (%s); "
                       "queries aborted\n",
                       error_text(card->master_drop_error));
                print_drm_close_error(card);
                continue;
            }
            puts("  implicit DRM master: granted by open and dropped before "
                 "queries");
        } else {
            puts("  DRM master: held by another client or not granted");
        }
        if (card->version.available) {
            printf("  driver: %s %d.%d.%d",
                   card->version.name,
                   card->version.major,
                   card->version.minor,
                   card->version.patch);
            if (card->version.date[0] != '\0') {
                printf(" (%s)", card->version.date);
            }
            putchar('\n');
            if (card->version.description[0] != '\0') {
                printf("  description: %s\n", card->version.description);
            }
        } else {
            printf("  driver version: unavailable (%s)\n",
                   error_text(card->version.error));
        }
        if (card->dumb_buffer.attempted) {
            if (card->dumb_buffer.available) {
                printf("  dumb buffers: %s\n",
                       card->dumb_buffer.value != 0 ? "yes" : "no");
            } else {
                printf("  dumb buffers: unavailable (%s)\n",
                       error_text(card->dumb_buffer.error));
            }
        }
        if (card->dumb_preferred_depth.available) {
            printf("  preferred dumb-buffer depth: %llu\n",
                   (unsigned long long)card->dumb_preferred_depth.value);
        }
        if (card->dumb_prefer_shadow.available) {
            printf("  dumb-buffer shadow preferred: %s\n",
                   card->dumb_prefer_shadow.value != 0 ? "yes" : "no");
        }
        if (card->async_page_flip.attempted) {
            if (card->async_page_flip.available) {
                printf("  asynchronous page flips: %s\n",
                       card->async_page_flip.value != 0 ? "yes" : "no");
            } else {
                printf("  asynchronous page flips: unavailable (%s)\n",
                       error_text(card->async_page_flip.error));
            }
        }
        if (!card->resources_available) {
            printf("  KMS resources: unavailable (%s)\n",
                   error_text(card->resources_error));
        } else {
            printf("  KMS resources: %zu CRTC%s (%zu active, %zu query "
                   "error%s), %zu encoder%s, %zu connector%s\n",
                   card->crtc_count,
                   card->crtc_count == 1 ? "" : "s",
                   card->active_crtc_count,
                   card->crtc_query_error_count,
                   card->crtc_query_error_count == 1 ? "" : "s",
                   card->encoder_count,
                   card->encoder_count == 1 ? "" : "s",
                   card->reported_connector_count,
                   card->reported_connector_count == 1 ? "" : "s");
            printf("  KMS size range: %ux%u to %ux%u\n",
                   card->min_width,
                   card->min_height,
                   card->max_width,
                   card->max_height);
        }
        for (connector_index = 0;
             connector_index < card->connector_count;
             ++connector_index) {
            const struct nb_backend_probe_drm_connector *connector =
                &card->connectors[connector_index];
            const char *connection = "unknown";
            size_t mode_index;

            if (connector->connection ==
                NB_BACKEND_PROBE_DRM_CONNECTION_CONNECTED) {
                connection = "connected";
            } else if (connector->connection ==
                       NB_BACKEND_PROBE_DRM_CONNECTION_DISCONNECTED) {
                connection = "disconnected";
            }
            printf("  connector %s (id %u): ",
                   connector->name,
                   connector->id);
            if (!connector->query_available) {
                printf("query failed (%s)\n",
                       error_text(connector->query_error));
                continue;
            }
            printf("%s, %ux%u mm, %zu cached mode%s%s\n",
                   connection,
                   connector->width_mm,
                   connector->height_mm,
                   connector->reported_mode_count,
                   connector->reported_mode_count == 1 ? "" : "s",
                   connector->modes_truncated ? " (truncated)" : "");
            for (mode_index = 0;
                 mode_index < connector->mode_count;
                 ++mode_index) {
                const struct nb_backend_probe_drm_mode *mode =
                    &connector->modes[mode_index];

                printf("    %s: %ux%u@%u Hz%s%s\n",
                       mode->name,
                       mode->width,
                       mode->height,
                       mode->refresh_hz,
                       mode->preferred ? " preferred" : "",
                       mode->interlaced ? " interlaced" : "");
            }
        }
        if (card->connectors_truncated) {
            puts("  additional connectors were omitted");
        }
        if (card->planes_available) {
            printf("  legacy-visible planes: %zu (%zu query error%s)\n",
                   card->plane_count,
                   card->plane_query_error_count,
                   card->plane_query_error_count == 1 ? "" : "s");
        } else {
            printf("  legacy-visible planes: unavailable (%s)\n",
                   error_text(card->planes_error));
        }
        puts("  ordinary page flips: not exercised by this probe");
        printf("  direct KMS candidate: %s\n",
               nb_backend_probe_drm_card_is_kms_candidate(card)
                   ? "yes"
                   : "no");
        print_drm_close_error(card);
    }
    if (snapshot->drm_cards_truncated) {
        puts("  additional card nodes were omitted");
    }
}

static void print_netbsd_probe(
    const struct nb_backend_probe_snapshot *snapshot)
{
    bool keyboard_ready;
    bool mouse_ready;
    bool kms_candidate;

    if (!snapshot->netbsd) {
        puts("NetBSD console probe: not applicable on this platform");
        return;
    }

    puts("NetBSD console devices:");
    print_wsdisplay(&snapshot->wsdisplay);
    print_access("keyboard", &snapshot->keyboard);
    print_access("mouse", &snapshot->mouse);
    print_drm(snapshot);

    keyboard_ready = snapshot->keyboard.exists &&
                     snapshot->keyboard.character_device &&
                     snapshot->keyboard.readable &&
                     snapshot->keyboard.writable;
    mouse_ready = snapshot->mouse.exists &&
                  snapshot->mouse.character_device &&
                  snapshot->mouse.readable &&
                  snapshot->mouse.writable;
    kms_candidate = nb_backend_probe_has_kms_candidate(snapshot);

    printf("wsdisplay software output: %s\n",
           nb_backend_probe_wsdisplay_software_ready(snapshot)
               ? "probe-ready"
               : "unavailable or unsupported");
    printf("wscons keyboard input: %s\n",
           keyboard_ready ? "accessible" : "unavailable or inaccessible");
    printf("wscons pointer input: %s\n",
           mouse_ready ? "accessible" : "unavailable or inaccessible");
    printf("SDL KMSDRM path: %s\n",
           nb_backend_probe_has_sdl_driver(snapshot, "kmsdrm") &&
                   kms_candidate
               ? "probe-ready (functional KMS still requires a takeover test)"
               : "unavailable or incomplete");
    printf("Direct DRM/KMS output: %s\n",
           kms_candidate
               ? "candidate found (modesetting and page flips not exercised)"
               : "no usable candidate found");
}

int main(int argc, char *argv[])
{
    struct nb_backend_probe_paths paths;
    struct nb_backend_probe_snapshot snapshot;
    bool show_help = false;

    nb_backend_probe_default_paths(&paths);
    if (!parse_options(argc, argv, &paths, &show_help)) {
        return 2;
    }
    if (show_help) {
        print_usage(argv[0]);
        return 0;
    }

    puts("NixBench backend capability probe");
    puts("This command does not change display modes, map framebuffers, read "
         "input events, allocate DRM buffers, or explicitly request DRM "
         "master. An idle primary DRM node may grant master implicitly when "
         "opened; the probe drops it before querying and aborts that card if "
         "the drop fails.\n");
    nb_backend_probe_collect(&snapshot, &paths);
    print_sdl_drivers(&snapshot);
    putchar('\n');
    print_netbsd_probe(&snapshot);
    return 0;
}
