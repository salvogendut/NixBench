#ifndef NIXBENCH_FRAMEBUFFER_FORMAT_H
#define NIXBENCH_FRAMEBUFFER_FORMAT_H

#include <stddef.h>
#include <stdint.h>

enum nb_framebuffer_source_format {
    NB_FRAMEBUFFER_SOURCE_XRGB8888 = 0,
    NB_FRAMEBUFFER_SOURCE_ARGB8888
};

struct nb_framebuffer_channel {
    uint32_t offset;
    uint32_t size;
};

struct nb_framebuffer_format {
    uint32_t bits_per_pixel;
    struct nb_framebuffer_channel red;
    struct nb_framebuffer_channel green;
    struct nb_framebuffer_channel blue;
    struct nb_framebuffer_channel alpha;
};

enum nb_framebuffer_status {
    NB_FRAMEBUFFER_OK = 0,
    NB_FRAMEBUFFER_INVALID_ARGUMENT,
    NB_FRAMEBUFFER_UNSUPPORTED_BITS_PER_PIXEL,
    NB_FRAMEBUFFER_INVALID_CHANNEL,
    NB_FRAMEBUFFER_OVERLAPPING_CHANNELS,
    NB_FRAMEBUFFER_SOURCE_STRIDE_TOO_SMALL,
    NB_FRAMEBUFFER_DESTINATION_STRIDE_TOO_SMALL,
    NB_FRAMEBUFFER_SOURCE_BUFFER_TOO_SMALL,
    NB_FRAMEBUFFER_DESTINATION_BUFFER_TOO_SMALL,
    NB_FRAMEBUFFER_SIZE_OVERFLOW
};

/*
 * Source pixels are native-endian uint32_t values with the numeric form
 * 0xAARRGGBB. XRGB ignores the high byte and supplies opaque alpha if the
 * destination has an alpha channel. ARGB uses the high byte as alpha.
 *
 * Destination mask offsets count from the least-significant bit of a native
 * 16-, 24-, or 32-bit packed pixel value. Therefore 16- and 32-bit values are
 * stored in host byte order. A 24-bit value is stored least-significant byte
 * first on a little-endian host and most-significant byte first on a
 * big-endian host.
 *
 * Red, green, and blue channels must be non-empty, in range, and mutually
 * non-overlapping. Alpha follows the same rules when present; alpha size zero
 * is supported and its offset is ignored. Unassigned destination bits are
 * zero. Source channels are rounded to the closest representable destination
 * value.
 *
 * Strides and buffer sizes are explicit. Only the pixel bytes of each row are
 * accessed, so trailing row padding is preserved. Source and destination
 * storage must not overlap. Empty frames are valid no-ops and may use null
 * buffers. All validation happens before destination bytes are modified.
 */
enum nb_framebuffer_status nb_framebuffer_format_validate(
    const struct nb_framebuffer_format *format);

/* Perform the exact no-write argument and buffer validation used by convert. */
enum nb_framebuffer_status nb_framebuffer_conversion_validate(
    const void *source,
    size_t source_size,
    size_t source_stride,
    enum nb_framebuffer_source_format source_format,
    void *destination,
    size_t destination_size,
    size_t destination_stride,
    size_t width,
    size_t height,
    const struct nb_framebuffer_format *destination_format);

enum nb_framebuffer_status nb_framebuffer_convert(
    const void *source,
    size_t source_size,
    size_t source_stride,
    enum nb_framebuffer_source_format source_format,
    void *destination,
    size_t destination_size,
    size_t destination_stride,
    size_t width,
    size_t height,
    const struct nb_framebuffer_format *destination_format);

const char *nb_framebuffer_status_string(enum nb_framebuffer_status status);

#endif
