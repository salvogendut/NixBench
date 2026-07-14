#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "backend_probe_drm_internal.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef NIXBENCH_HAS_LIBDRM
#define NIXBENCH_HAS_LIBDRM 0
#endif

#if defined(__NetBSD__) && NIXBENCH_HAS_LIBDRM
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

static int error_from_negative_result(int result)
{
    if (result >= 0 || result == INT_MIN) {
        return EIO;
    }
    return -result;
}

static void reset_card(struct nb_backend_probe_drm_card *card,
                       bool query_supported)
{
    struct nb_backend_probe_device device;

    device = card->device;
    memset(card, 0, sizeof(*card));
    card->device = device;
    card->query_supported = query_supported;
}

static void mark_missing_version_collector(
    struct nb_backend_probe_drm_card *card)
{
    card->version.error = ENOSYS;
}

static void mark_missing_capability_collector(
    struct nb_backend_probe_drm_card *card)
{
    struct nb_backend_probe_drm_capability *const capabilities[] = {
        &card->dumb_buffer,
        &card->dumb_preferred_depth,
        &card->dumb_prefer_shadow,
        &card->async_page_flip
    };
    size_t index;

    for (index = 0;
         index < sizeof(capabilities) / sizeof(capabilities[0]);
         ++index) {
        capabilities[index]->attempted = true;
        capabilities[index]->error = ENOSYS;
    }
}

static void mark_missing_resource_collector(
    struct nb_backend_probe_drm_card *card)
{
    card->resources_error = ENOSYS;
    card->planes_error = ENOSYS;
}

void nb_backend_probe_drm_collect_card_with_operations(
    struct nb_backend_probe_drm_card *card,
    const struct nb_backend_probe_drm_operations *operations,
    void *opaque)
{
    bool is_master = false;
    int descriptor;
    int result;

    if (card == NULL) {
        return;
    }

    reset_card(card, operations != NULL);
    if (operations == NULL) {
        return;
    }

    card->open_attempted = true;
    if (operations->open_card == NULL) {
        card->read_write_open_error = ENOSYS;
        card->read_only_open_error = ENOSYS;
        return;
    }

    descriptor = operations->open_card(opaque, card->device.path, true);
    if (descriptor >= 0) {
        card->open_mode = NB_BACKEND_PROBE_DRM_OPEN_READ_WRITE;
    } else {
        card->read_write_open_error =
            error_from_negative_result(descriptor);
        descriptor = operations->open_card(opaque,
                                           card->device.path,
                                           false);
        if (descriptor < 0) {
            card->read_only_open_error =
                error_from_negative_result(descriptor);
            return;
        }
        card->open_mode = NB_BACKEND_PROBE_DRM_OPEN_READ_ONLY;
    }

    if (operations->is_master == NULL) {
        card->master_check_error = ENOSYS;
        goto close_descriptor;
    }
    result = operations->is_master(opaque, descriptor, &is_master);
    if (result != 0) {
        card->master_check_error = error_from_negative_result(result);
        goto close_descriptor;
    }
    card->master_checked = true;

    card->implicit_master = is_master;
    if (is_master) {
        card->master_drop_attempted = true;
        if (operations->drop_master == NULL) {
            card->master_drop_error = ENOSYS;
            goto close_descriptor;
        }
        result = operations->drop_master(opaque, descriptor);
        if (result != 0) {
            card->master_drop_error = error_from_negative_result(result);
            goto close_descriptor;
        }
        card->master_dropped = true;
    }

    if (operations->collect_version != NULL) {
        operations->collect_version(opaque, descriptor, card);
    } else {
        mark_missing_version_collector(card);
    }
    if (operations->collect_capabilities != NULL) {
        operations->collect_capabilities(opaque, descriptor, card);
    } else {
        mark_missing_capability_collector(card);
    }
    if (operations->collect_resources != NULL) {
        operations->collect_resources(opaque, descriptor, card);
    } else {
        mark_missing_resource_collector(card);
    }

close_descriptor:
    if (operations->close_card == NULL) {
        card->close_error = ENOSYS;
        return;
    }
    result = operations->close_card(opaque, descriptor);
    if (result != 0) {
        card->close_error = error_from_negative_result(result);
    }
}

#if defined(__NetBSD__) && NIXBENCH_HAS_LIBDRM

enum {
    /* Protect diagnostic loops from a corrupt or hostile kernel response. */
    NB_BACKEND_PROBE_DRM_OBJECT_QUERY_LIMIT = 4096
};

static int current_error_or_io(void)
{
    return errno > 0 ? errno : EIO;
}

static int normalize_libdrm_status(int result)
{
    if (result == 0) {
        return 0;
    }
    if (result == -1) {
        return errno > 0 ? -errno : -EIO;
    }
    if (result < 0) {
        return result;
    }
    return -EIO;
}

static size_t positive_int_count(int count)
{
    return count > 0 ? (size_t)(unsigned int)count : 0;
}

static size_t minimum_size(size_t left, size_t right)
{
    return left < right ? left : right;
}

static void copy_bytes(char *destination,
                       size_t destination_capacity,
                       const char *source,
                       size_t source_length)
{
    size_t length;

    if (destination_capacity == 0) {
        return;
    }
    destination[0] = '\0';
    if (source == NULL) {
        return;
    }

    length = minimum_size(source_length, destination_capacity - 1);
    if (length > 0) {
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

static void copy_fixed_string(char *destination,
                              size_t destination_capacity,
                              const char *source,
                              size_t source_capacity)
{
    size_t length = 0;

    if (source != NULL) {
        while (length < source_capacity && source[length] != '\0') {
            ++length;
        }
    }
    copy_bytes(destination, destination_capacity, source, length);
}

static int real_open_card(void *opaque,
                          const char *path,
                          bool read_write)
{
    int flags = read_write ? O_RDWR : O_RDONLY;
    int descriptor;

    (void)opaque;
    flags |= O_NONBLOCK | O_NOCTTY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    errno = 0;
    descriptor = open(path, flags);
    return descriptor >= 0 ? descriptor : -current_error_or_io();
}

static int real_close_card(void *opaque, int descriptor)
{
    (void)opaque;
    errno = 0;
    return close(descriptor) == 0 ? 0 : -current_error_or_io();
}

static bool result_has_error(int result, int expected_error)
{
    return result == -expected_error ||
           (result == -1 && errno == expected_error);
}

static int real_is_master(void *opaque,
                          int descriptor,
                          bool *is_master)
{
    int result;

    (void)opaque;
    if (is_master == NULL) {
        return -EINVAL;
    }

    /*
     * drmIsMaster() treats every error except EACCES as "master".  Querying
     * authentication with the reserved zero magic lets us distinguish a
     * non-master descriptor without silently swallowing ENODEV/ENOTTY.
     */
    errno = 0;
    result = drmAuthMagic(descriptor, (drm_magic_t)0);
    if (result == 0 || result_has_error(result, EINVAL)) {
        *is_master = true;
        return 0;
    }
    if (result_has_error(result, EACCES)) {
        *is_master = false;
        return 0;
    }
    return normalize_libdrm_status(result);
}

static int real_drop_master(void *opaque, int descriptor)
{
    int result;

    (void)opaque;
    errno = 0;
    result = drmDropMaster(descriptor);
    return normalize_libdrm_status(result);
}

static void real_collect_version(
    void *opaque,
    int descriptor,
    struct nb_backend_probe_drm_card *card)
{
    drmVersionPtr version;

    (void)opaque;
    errno = 0;
    version = drmGetVersion(descriptor);
    if (version == NULL) {
        card->version.error = current_error_or_io();
        return;
    }

    card->version.available = true;
    card->version.major = version->version_major;
    card->version.minor = version->version_minor;
    card->version.patch = version->version_patchlevel;
    copy_bytes(card->version.name,
               sizeof(card->version.name),
               version->name,
               positive_int_count(version->name_len));
    copy_bytes(card->version.date,
               sizeof(card->version.date),
               version->date,
               positive_int_count(version->date_len));
    copy_bytes(card->version.description,
               sizeof(card->version.description),
               version->desc,
               positive_int_count(version->desc_len));
    drmFreeVersion(version);
}

static void query_capability(
    int descriptor,
    uint64_t identifier,
    struct nb_backend_probe_drm_capability *capability)
{
    uint64_t value = 0;
    int result;

    capability->attempted = true;
    errno = 0;
    result = drmGetCap(descriptor, identifier, &value);
    if (result != 0) {
        result = normalize_libdrm_status(result);
        capability->error = error_from_negative_result(result);
        return;
    }
    capability->available = true;
    capability->value = value;
}

static void real_collect_capabilities(
    void *opaque,
    int descriptor,
    struct nb_backend_probe_drm_card *card)
{
    (void)opaque;
    query_capability(descriptor, DRM_CAP_DUMB_BUFFER, &card->dumb_buffer);
    query_capability(descriptor,
                     DRM_CAP_DUMB_PREFERRED_DEPTH,
                     &card->dumb_preferred_depth);
    query_capability(descriptor,
                     DRM_CAP_DUMB_PREFER_SHADOW,
                     &card->dumb_prefer_shadow);
    query_capability(descriptor,
                     DRM_CAP_ASYNC_PAGE_FLIP,
                     &card->async_page_flip);
}

static enum nb_backend_probe_drm_connection convert_connection(
    drmModeConnection connection)
{
    switch (connection) {
    case DRM_MODE_CONNECTED:
        return NB_BACKEND_PROBE_DRM_CONNECTION_CONNECTED;
    case DRM_MODE_DISCONNECTED:
        return NB_BACKEND_PROBE_DRM_CONNECTION_DISCONNECTED;
    case DRM_MODE_UNKNOWNCONNECTION:
        break;
    }
    return NB_BACKEND_PROBE_DRM_CONNECTION_UNKNOWN;
}

static void collect_mode(const drmModeModeInfo *source,
                         struct nb_backend_probe_drm_mode *destination)
{
    copy_fixed_string(destination->name,
                      sizeof(destination->name),
                      source->name,
                      sizeof(source->name));
    destination->width = source->hdisplay;
    destination->height = source->vdisplay;
    destination->refresh_hz = source->vrefresh;
    destination->flags = source->flags;
    destination->type = source->type;
    destination->preferred =
        (source->type & DRM_MODE_TYPE_PREFERRED) != 0;
    destination->interlaced =
        (source->flags & DRM_MODE_FLAG_INTERLACE) != 0;
}

static void collect_connector(
    int descriptor,
    uint32_t connector_id,
    struct nb_backend_probe_drm_connector *destination)
{
    const char *type_name;
    drmModeConnectorPtr connector;
    size_t mode_index;

    destination->id = connector_id;
    errno = 0;
    connector = drmModeGetConnectorCurrent(descriptor, connector_id);
    if (connector == NULL) {
        destination->query_error = current_error_or_io();
        return;
    }

    destination->query_available = true;
    destination->id = connector->connector_id;
    destination->type = connector->connector_type;
    destination->type_id = connector->connector_type_id;
    destination->encoder_id = connector->encoder_id;
    destination->connection = convert_connection(connector->connection);
    destination->width_mm = connector->mmWidth;
    destination->height_mm = connector->mmHeight;
    destination->reported_mode_count =
        positive_int_count(connector->count_modes);
    destination->mode_count = minimum_size(
        destination->reported_mode_count,
        NB_BACKEND_PROBE_DRM_MODE_CAPACITY);
    destination->modes_truncated =
        destination->reported_mode_count > destination->mode_count;

    type_name = drmModeGetConnectorTypeName(connector->connector_type);
    if (type_name == NULL) {
        type_name = "Unknown";
    }
    (void)snprintf(destination->name,
                   sizeof(destination->name),
                   "%s-%" PRIu32,
                   type_name,
                   connector->connector_type_id);

    if (connector->modes == NULL) {
        destination->mode_count = 0;
        destination->modes_truncated =
            destination->reported_mode_count > 0;
    }
    for (mode_index = 0;
         mode_index < destination->mode_count;
         ++mode_index) {
        collect_mode(&connector->modes[mode_index],
                     &destination->modes[mode_index]);
    }
    drmModeFreeConnector(connector);
}

static void collect_crtcs(int descriptor,
                          const drmModeRes *resources,
                          struct nb_backend_probe_drm_card *card)
{
    const size_t reported_count =
        positive_int_count(resources->count_crtcs);
    const size_t query_count = minimum_size(
        reported_count,
        NB_BACKEND_PROBE_DRM_OBJECT_QUERY_LIMIT);
    size_t index;

    card->crtc_count = reported_count;
    if (resources->crtcs == NULL) {
        card->crtc_query_error_count = reported_count;
        return;
    }
    for (index = 0; index < query_count; ++index) {
        drmModeCrtcPtr crtc;

        errno = 0;
        crtc = drmModeGetCrtc(descriptor, resources->crtcs[index]);
        if (crtc == NULL) {
            ++card->crtc_query_error_count;
            continue;
        }
        if (crtc->mode_valid != 0) {
            ++card->active_crtc_count;
        }
        drmModeFreeCrtc(crtc);
    }
    card->crtc_query_error_count += reported_count - query_count;
}

static void collect_connectors(
    int descriptor,
    const drmModeRes *resources,
    struct nb_backend_probe_drm_card *card)
{
    size_t index;

    card->reported_connector_count =
        positive_int_count(resources->count_connectors);
    card->connector_count = minimum_size(
        card->reported_connector_count,
        NB_BACKEND_PROBE_DRM_CONNECTOR_CAPACITY);
    card->connectors_truncated =
        card->reported_connector_count > card->connector_count;
    if (resources->connectors == NULL) {
        card->connector_count = 0;
        card->connectors_truncated =
            card->reported_connector_count > 0;
        return;
    }

    for (index = 0; index < card->connector_count; ++index) {
        collect_connector(descriptor,
                          resources->connectors[index],
                          &card->connectors[index]);
    }
}

static void collect_planes(int descriptor,
                           struct nb_backend_probe_drm_card *card)
{
    drmModePlaneResPtr resources;
    size_t query_count;
    size_t index;

    errno = 0;
    resources = drmModeGetPlaneResources(descriptor);
    if (resources == NULL) {
        card->planes_error = current_error_or_io();
        return;
    }

    card->planes_available = true;
    card->plane_count = (size_t)resources->count_planes;
    query_count = minimum_size(card->plane_count,
                               NB_BACKEND_PROBE_DRM_OBJECT_QUERY_LIMIT);
    if (resources->planes == NULL) {
        card->plane_query_error_count = card->plane_count;
        drmModeFreePlaneResources(resources);
        return;
    }
    for (index = 0; index < query_count; ++index) {
        drmModePlanePtr plane;

        errno = 0;
        plane = drmModeGetPlane(descriptor, resources->planes[index]);
        if (plane == NULL) {
            ++card->plane_query_error_count;
            continue;
        }
        drmModeFreePlane(plane);
    }
    card->plane_query_error_count += card->plane_count - query_count;
    drmModeFreePlaneResources(resources);
}

static void real_collect_resources(
    void *opaque,
    int descriptor,
    struct nb_backend_probe_drm_card *card)
{
    drmModeResPtr resources;

    (void)opaque;
    errno = 0;
    resources = drmModeGetResources(descriptor);
    if (resources == NULL) {
        card->resources_error = current_error_or_io();
        /* Plane enumeration is an independent, read-only diagnostic. */
        collect_planes(descriptor, card);
        return;
    }

    card->resources_available = true;
    card->encoder_count = positive_int_count(resources->count_encoders);
    card->min_width = resources->min_width;
    card->max_width = resources->max_width;
    card->min_height = resources->min_height;
    card->max_height = resources->max_height;
    collect_crtcs(descriptor, resources, card);
    collect_connectors(descriptor, resources, card);
    drmModeFreeResources(resources);

    /* This intentionally enumerates only legacy-visible planes. */
    collect_planes(descriptor, card);
}

static const struct nb_backend_probe_drm_operations real_operations = {
    real_open_card,
    real_close_card,
    real_is_master,
    real_drop_master,
    real_collect_version,
    real_collect_capabilities,
    real_collect_resources
};

#endif

bool nb_backend_probe_drm_query_supported(void)
{
#if defined(__NetBSD__) && NIXBENCH_HAS_LIBDRM
    return true;
#else
    return false;
#endif
}

void nb_backend_probe_drm_collect_card(
    struct nb_backend_probe_drm_card *card)
{
    if (card == NULL) {
        return;
    }

#if defined(__NetBSD__) && NIXBENCH_HAS_LIBDRM
    nb_backend_probe_drm_collect_card_with_operations(card,
                                                      &real_operations,
                                                      NULL);
#else
    reset_card(card, false);
#endif
}
