#ifndef NIXBENCH_FRAMEBUFFER_SHADOW_H
#define NIXBENCH_FRAMEBUFFER_SHADOW_H

#include <stdbool.h>
#include <stddef.h>

#include "framebuffer_format.h"

struct nb_framebuffer_shadow;

struct nb_framebuffer_shadow_stats {
    size_t regions;
    size_t rows;
    size_t converted_pixels;
    size_t converted_bytes;
    bool full;
};

/*
 * A shadow owns a tightly packed copy of the last successfully presented
 * visible 32-bit source pixels. It never reads destination storage.
 */
struct nb_framebuffer_shadow *nb_framebuffer_shadow_create(
    size_t width,
    size_t height);

/* Force the next successful presentation to convert the complete frame. */
void nb_framebuffer_shadow_invalidate(struct nb_framebuffer_shadow *shadow);

/*
 * Validate the complete conversion before writing anything. The first frame,
 * a changed source/destination format, a changed destination allocation, or an
 * explicit invalidation converts the full frame. Otherwise each changed row
 * converts only the span from its first through its last meaningful changed
 * pixel. Source and destination padding are ignored and preserved.
 *
 * Call invalidate whenever destination contents may have changed externally,
 * even if its address and layout did not change.
 */
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
    struct nb_framebuffer_shadow_stats *stats);

void nb_framebuffer_shadow_destroy(struct nb_framebuffer_shadow *shadow);

#endif
