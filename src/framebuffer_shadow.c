#include "framebuffer_shadow.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct nb_framebuffer_shadow {
    unsigned char *pixels;
    struct nb_changed_span *spans;
    size_t width;
    size_t height;
    size_t stride;
    enum nb_framebuffer_source_format source_format;
    void *destination;
    size_t destination_size;
    size_t destination_stride;
    struct nb_framebuffer_format destination_format;
    bool valid;
};

struct nb_changed_span {
    size_t x;
    size_t width;
    bool source_changed;
};

static bool multiply_size(size_t left, size_t right, size_t *product)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return false;
    }
    *product = left * right;
    return true;
}

static bool channels_equal(const struct nb_framebuffer_channel *left,
                           const struct nb_framebuffer_channel *right)
{
    return left->offset == right->offset && left->size == right->size;
}

static bool formats_equal(const struct nb_framebuffer_format *left,
                          const struct nb_framebuffer_format *right)
{
    return left->bits_per_pixel == right->bits_per_pixel &&
           channels_equal(&left->red, &right->red) &&
           channels_equal(&left->green, &right->green) &&
           channels_equal(&left->blue, &right->blue) &&
           channels_equal(&left->alpha, &right->alpha);
}

static uint32_t load_pixel(const unsigned char *pixel)
{
    uint32_t value;

    memcpy(&value, pixel, sizeof(value));
    return value;
}

static uint32_t meaningful_source_mask(
    enum nb_framebuffer_source_format source_format,
    const struct nb_framebuffer_format *destination_format)
{
    return source_format == NB_FRAMEBUFFER_SOURCE_ARGB8888 &&
                   destination_format->alpha.size != 0
               ? UINT32_MAX
               : UINT32_C(0x00ffffff);
}

static bool pixels_meaningfully_differ(
    const unsigned char *current,
    const unsigned char *previous,
    uint32_t meaningful_mask)
{
    return ((load_pixel(current) ^ load_pixel(previous)) & meaningful_mask) !=
           0;
}

static bool find_changed_span(
    const unsigned char *current,
    const unsigned char *previous,
    size_t pixel_count,
    uint32_t meaningful_mask,
    struct nb_changed_span *span)
{
    size_t first;
    size_t last;

    span->x = 0;
    span->width = 0;
    span->source_changed =
        memcmp(current, previous, pixel_count * sizeof(uint32_t)) != 0;
    if (!span->source_changed) {
        return false;
    }
    first = 0;
    while (first < pixel_count &&
           !pixels_meaningfully_differ(
               current + first * sizeof(uint32_t),
               previous + first * sizeof(uint32_t),
               meaningful_mask)) {
        ++first;
    }
    if (first == pixel_count) {
        /* Only source bits ignored by this conversion changed. */
        return false;
    }
    last = pixel_count;
    while (last > first + 1 &&
           !pixels_meaningfully_differ(
               current + (last - 1) * sizeof(uint32_t),
               previous + (last - 1) * sizeof(uint32_t),
               meaningful_mask)) {
        --last;
    }
    span->x = first;
    span->width = last - first;
    return true;
}

static void copy_source(struct nb_framebuffer_shadow *shadow,
                        const unsigned char *source,
                        size_t source_stride)
{
    size_t row;

    for (row = 0; row < shadow->height; ++row) {
        memcpy(shadow->pixels + row * shadow->stride,
               source + row * source_stride,
               shadow->stride);
    }
}

static void record_full_destination(
    struct nb_framebuffer_shadow *shadow,
    enum nb_framebuffer_source_format source_format,
    void *destination,
    size_t destination_size,
    size_t destination_stride,
    const struct nb_framebuffer_format *destination_format)
{
    shadow->source_format = source_format;
    shadow->destination = destination;
    shadow->destination_size = destination_size;
    shadow->destination_stride = destination_stride;
    shadow->destination_format = *destination_format;
    shadow->valid = true;
}

static bool destination_matches(
    const struct nb_framebuffer_shadow *shadow,
    enum nb_framebuffer_source_format source_format,
    void *destination,
    size_t destination_size,
    size_t destination_stride,
    const struct nb_framebuffer_format *destination_format)
{
    return shadow->valid && shadow->source_format == source_format &&
           shadow->destination == destination &&
           shadow->destination_size == destination_size &&
           shadow->destination_stride == destination_stride &&
           formats_equal(&shadow->destination_format, destination_format);
}

static void clear_stats(struct nb_framebuffer_shadow_stats *stats)
{
    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
}

static void set_full_stats(
    struct nb_framebuffer_shadow_stats *stats,
    size_t width,
    size_t height,
    size_t destination_pixel_size)
{
    if (stats != NULL) {
        stats->regions = 1;
        stats->rows = height;
        stats->converted_pixels = width * height;
        stats->converted_bytes =
            stats->converted_pixels * destination_pixel_size;
        stats->full = true;
    }
}

struct nb_framebuffer_shadow *nb_framebuffer_shadow_create(
    size_t width,
    size_t height)
{
    struct nb_framebuffer_shadow *shadow;
    size_t stride;
    size_t size;
    size_t spans_size;

    if (width == 0 || height == 0 ||
        !multiply_size(width, sizeof(uint32_t), &stride) ||
        !multiply_size(stride, height, &size) ||
        !multiply_size(height, sizeof(*shadow->spans), &spans_size)) {
        return NULL;
    }
    shadow = calloc(1, sizeof(*shadow));
    if (shadow == NULL) {
        return NULL;
    }
    shadow->pixels = malloc(size);
    if (shadow->pixels == NULL) {
        free(shadow);
        return NULL;
    }
    shadow->spans = malloc(spans_size);
    if (shadow->spans == NULL) {
        free(shadow->pixels);
        free(shadow);
        return NULL;
    }
    shadow->width = width;
    shadow->height = height;
    shadow->stride = stride;
    return shadow;
}

void nb_framebuffer_shadow_invalidate(struct nb_framebuffer_shadow *shadow)
{
    if (shadow != NULL) {
        shadow->valid = false;
    }
}

enum nb_framebuffer_status nb_framebuffer_shadow_present(
    struct nb_framebuffer_shadow *shadow,
    const void *source,
    size_t source_size,
    size_t source_stride,
    enum nb_framebuffer_source_format source_format,
    void *destination,
    size_t destination_size,
    size_t destination_stride,
    const struct nb_framebuffer_format *destination_format,
    struct nb_framebuffer_shadow_stats *stats)
{
    enum nb_framebuffer_status status;
    const unsigned char *source_bytes = source;
    unsigned char *destination_bytes = destination;
    size_t destination_pixel_size;
    uint32_t meaningful_mask;
    size_t planned_pixels = 0;
    size_t planned_regions = 0;
    size_t total_pixels;
    bool use_full_conversion = false;
    size_t row;

    clear_stats(stats);
    if (shadow == NULL) {
        return NB_FRAMEBUFFER_INVALID_ARGUMENT;
    }
    status = nb_framebuffer_conversion_validate(
        source,
        source_size,
        source_stride,
        source_format,
        destination,
        destination_size,
        destination_stride,
        shadow->width,
        shadow->height,
        destination_format);
    if (status != NB_FRAMEBUFFER_OK) {
        return status;
    }
    destination_pixel_size = destination_format->bits_per_pixel / 8;
    if (!destination_matches(shadow,
                             source_format,
                             destination,
                             destination_size,
                             destination_stride,
                             destination_format)) {
        status = nb_framebuffer_convert(
            source,
            source_size,
            source_stride,
            source_format,
            destination,
            destination_size,
            destination_stride,
            shadow->width,
            shadow->height,
            destination_format);
        if (status != NB_FRAMEBUFFER_OK) {
            shadow->valid = false;
            return status;
        }
        copy_source(shadow, source_bytes, source_stride);
        record_full_destination(shadow,
                                source_format,
                                destination,
                                destination_size,
                                destination_stride,
                                destination_format);
        set_full_stats(stats,
                       shadow->width,
                       shadow->height,
                       destination_pixel_size);
        return NB_FRAMEBUFFER_OK;
    }

    meaningful_mask =
        meaningful_source_mask(source_format, destination_format);
    total_pixels = shadow->width * shadow->height;
    for (row = 0; row < shadow->height; ++row) {
        const unsigned char *source_row =
            source_bytes + row * source_stride;
        const unsigned char *shadow_row =
            shadow->pixels + row * shadow->stride;
        struct nb_changed_span *span = &shadow->spans[row];

        if (find_changed_span(source_row,
                              shadow_row,
                              shadow->width,
                              meaningful_mask,
                              span)) {
            planned_pixels += span->width;
            ++planned_regions;
            if (planned_pixels > total_pixels / 2) {
                use_full_conversion = true;
                break;
            }
        }
    }
    if (use_full_conversion) {
        status = nb_framebuffer_convert(
            source,
            source_size,
            source_stride,
            source_format,
            destination,
            destination_size,
            destination_stride,
            shadow->width,
            shadow->height,
            destination_format);
        if (status != NB_FRAMEBUFFER_OK) {
            shadow->valid = false;
            return status;
        }
        copy_source(shadow, source_bytes, source_stride);
        set_full_stats(stats,
                       shadow->width,
                       shadow->height,
                       destination_pixel_size);
        return NB_FRAMEBUFFER_OK;
    }

    for (row = 0; row < shadow->height; ++row) {
        const unsigned char *source_row =
            source_bytes + row * source_stride;
        unsigned char *shadow_row = shadow->pixels + row * shadow->stride;
        const struct nb_changed_span *span = &shadow->spans[row];
        size_t source_offset;
        size_t destination_offset;

        if (!span->source_changed) {
            continue;
        }
        if (span->width == 0) {
            memcpy(shadow_row, source_row, shadow->stride);
            continue;
        }
        source_offset =
            row * source_stride + span->x * sizeof(uint32_t);
        destination_offset = row * destination_stride +
                             span->x * destination_pixel_size;
        status = nb_framebuffer_convert(
            source_bytes + source_offset,
            source_size - source_offset,
            source_stride,
            source_format,
            destination_bytes + destination_offset,
            destination_size - destination_offset,
            destination_stride,
            span->width,
            1,
            destination_format);
        if (status != NB_FRAMEBUFFER_OK) {
            shadow->valid = false;
            return status;
        }
        memcpy(shadow_row, source_row, shadow->stride);
    }
    if (stats != NULL) {
        stats->regions = planned_regions;
        stats->rows = planned_regions;
        stats->converted_pixels = planned_pixels;
        stats->converted_bytes =
            planned_pixels * destination_pixel_size;
    }
    return NB_FRAMEBUFFER_OK;
}

void nb_framebuffer_shadow_destroy(struct nb_framebuffer_shadow *shadow)
{
    if (shadow != NULL) {
        free(shadow->spans);
        free(shadow->pixels);
        free(shadow);
    }
}
