#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "backend_probe.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void add_driver(struct nb_backend_probe_snapshot *snapshot,
                       const char *name)
{
    if (snapshot->sdl_driver_count >= NB_BACKEND_PROBE_DRIVER_CAPACITY) {
        return;
    }
    (void)snprintf(snapshot->sdl_drivers[snapshot->sdl_driver_count],
                   NB_BACKEND_PROBE_DRIVER_NAME_CAPACITY,
                   "%s",
                   name);
    ++snapshot->sdl_driver_count;
}

static void test_default_paths(void)
{
    struct nb_backend_probe_paths paths;

    memset(&paths, 0, sizeof(paths));
    nb_backend_probe_default_paths(&paths);
    CHECK(strcmp(paths.wsdisplay, "/dev/ttyE0") == 0);
    CHECK(strcmp(paths.keyboard, "/dev/wskbd") == 0);
    CHECK(strcmp(paths.mouse, "/dev/wsmouse") == 0);
    CHECK(strcmp(paths.drm_directory, "/dev/dri") == 0);
    nb_backend_probe_default_paths(NULL);
}

static void test_driver_classification(void)
{
    struct nb_backend_probe_snapshot snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    add_driver(&snapshot, "dummy");
    add_driver(&snapshot, "x11");
    add_driver(&snapshot, "kmsdrm");

    CHECK(nb_backend_probe_has_sdl_driver(&snapshot, "dummy"));
    CHECK(nb_backend_probe_has_sdl_driver(&snapshot, "x11"));
    CHECK(nb_backend_probe_has_sdl_driver(&snapshot, "kmsdrm"));
    CHECK(!nb_backend_probe_has_sdl_driver(&snapshot, "wayland"));
    CHECK(!nb_backend_probe_has_sdl_driver(NULL, "x11"));
    CHECK(!nb_backend_probe_has_sdl_driver(&snapshot, NULL));
    CHECK(nb_backend_probe_has_hosted_sdl_driver(&snapshot));

    memset(&snapshot, 0, sizeof(snapshot));
    add_driver(&snapshot, "dummy");
    add_driver(&snapshot, "offscreen");
    CHECK(!nb_backend_probe_has_hosted_sdl_driver(&snapshot));
}

static void test_readiness_classification(void)
{
    struct nb_backend_probe_snapshot snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    CHECK(!nb_backend_probe_wsdisplay_software_ready(&snapshot));
    CHECK(!nb_backend_probe_has_kms_candidate(&snapshot));

    snapshot.netbsd = true;
    snapshot.wsdisplay.device.exists = true;
    snapshot.wsdisplay.device.character_device = true;
    snapshot.wsdisplay.device.readable = true;
    snapshot.wsdisplay.device.writable = true;
    snapshot.wsdisplay.opened = true;
    snapshot.wsdisplay.mode_available = true;
    snapshot.wsdisplay.framebuffer_info_available = true;
    snapshot.wsdisplay.software_layout_supported = true;
    CHECK(nb_backend_probe_wsdisplay_software_ready(&snapshot));

    snapshot.wsdisplay.device.writable = false;
    CHECK(!nb_backend_probe_wsdisplay_software_ready(&snapshot));

    snapshot.drm_card_count = 1;
    snapshot.drm_cards[0].device.exists = true;
    snapshot.drm_cards[0].device.character_device = true;
    snapshot.drm_cards[0].device.readable = true;
    snapshot.drm_cards[0].device.writable = true;
    CHECK(!nb_backend_probe_has_kms_candidate(&snapshot));

    snapshot.drm_cards[0].query_supported = true;
    snapshot.drm_cards[0].open_mode =
        NB_BACKEND_PROBE_DRM_OPEN_READ_WRITE;
    snapshot.drm_cards[0].master_checked = true;
    snapshot.drm_cards[0].version.available = true;
    snapshot.drm_cards[0].resources_available = true;
    snapshot.drm_cards[0].crtc_count = 1;
    snapshot.drm_cards[0].encoder_count = 1;
    snapshot.drm_cards[0].dumb_buffer.attempted = true;
    snapshot.drm_cards[0].dumb_buffer.available = true;
    snapshot.drm_cards[0].dumb_buffer.value = 1;
    snapshot.drm_cards[0].connector_count = 1;
    snapshot.drm_cards[0].connectors[0].query_available = true;
    snapshot.drm_cards[0].connectors[0].connection =
        NB_BACKEND_PROBE_DRM_CONNECTION_CONNECTED;
    snapshot.drm_cards[0].connectors[0].mode_count = 1;
    CHECK(nb_backend_probe_drm_card_has_connected_output(
        &snapshot.drm_cards[0]));
    CHECK(nb_backend_probe_drm_card_is_kms_candidate(
        &snapshot.drm_cards[0]));
    CHECK(nb_backend_probe_has_kms_candidate(&snapshot));

    snapshot.drm_cards[0].open_mode =
        NB_BACKEND_PROBE_DRM_OPEN_READ_ONLY;
    CHECK(!nb_backend_probe_drm_card_is_kms_candidate(
        &snapshot.drm_cards[0]));
    snapshot.drm_cards[0].open_mode =
        NB_BACKEND_PROBE_DRM_OPEN_READ_WRITE;
    snapshot.drm_cards[0].dumb_buffer.value = 0;
    CHECK(!nb_backend_probe_has_kms_candidate(&snapshot));
    snapshot.drm_cards[0].dumb_buffer.value = 1;
    snapshot.drm_cards[0].connectors[0].connection =
        NB_BACKEND_PROBE_DRM_CONNECTION_DISCONNECTED;
    CHECK(!nb_backend_probe_drm_card_has_connected_output(
        &snapshot.drm_cards[0]));
    CHECK(!nb_backend_probe_has_kms_candidate(&snapshot));
    snapshot.drm_cards[0].connectors[0].connection =
        NB_BACKEND_PROBE_DRM_CONNECTION_CONNECTED;
    snapshot.drm_cards[0].master_checked = false;
    CHECK(!nb_backend_probe_has_kms_candidate(&snapshot));
    snapshot.drm_cards[0].master_checked = true;
    snapshot.drm_cards[0].implicit_master = true;
    CHECK(!nb_backend_probe_has_kms_candidate(&snapshot));
    snapshot.drm_cards[0].master_dropped = true;
    CHECK(nb_backend_probe_has_kms_candidate(&snapshot));
    snapshot.drm_cards[0].encoder_count = 0;
    CHECK(!nb_backend_probe_has_kms_candidate(&snapshot));
    snapshot.drm_cards[0].encoder_count = 1;
    snapshot.drm_cards[0].close_error = EIO;
    CHECK(!nb_backend_probe_has_kms_candidate(&snapshot));
    CHECK(!nb_backend_probe_drm_card_has_connected_output(NULL));
    CHECK(!nb_backend_probe_drm_card_is_kms_candidate(NULL));
}

static void test_native_collection_smoke(void)
{
    struct nb_backend_probe_snapshot snapshot;

    memset(&snapshot, 0xa5, sizeof(snapshot));
    nb_backend_probe_collect(&snapshot, NULL);
    CHECK(snapshot.sdl_driver_count <= NB_BACKEND_PROBE_DRIVER_CAPACITY);
#if defined(__NetBSD__)
    CHECK(snapshot.netbsd);
    CHECK(snapshot.wsdisplay.device.path[0] != '\0');
    CHECK(snapshot.keyboard.path[0] != '\0');
    CHECK(snapshot.mouse.path[0] != '\0');
    CHECK(snapshot.drm_directory[0] != '\0');
#else
    CHECK(!snapshot.netbsd);
    CHECK(snapshot.wsdisplay.device.path[0] == '\0');
    CHECK(snapshot.keyboard.path[0] == '\0');
    CHECK(snapshot.mouse.path[0] == '\0');
    CHECK(snapshot.drm_directory[0] == '\0');
#endif
    nb_backend_probe_collect(NULL, NULL);
}

int main(void)
{
    test_default_paths();
    test_driver_classification();
    test_readiness_classification();
    test_native_collection_smoke();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    puts("backend probe tests passed");
    return 0;
}
