#include "framebuffer_format.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool channel_in_range(const struct nb_framebuffer_channel *channel,
                             uint32_t bits_per_pixel,
                             bool allow_empty)
{
    if (channel->size == 0) {
        return allow_empty;
    }
    return channel->size <= bits_per_pixel &&
           channel->offset <= bits_per_pixel - channel->size;
}

static uint32_t channel_mask(const struct nb_framebuffer_channel *channel)
{
    uint32_t mask;

    if (channel->size == 0) {
        return 0;
    }
    if (channel->size == 32) {
        mask = UINT32_MAX;
    } else {
        mask = (UINT32_C(1) << channel->size) - UINT32_C(1);
    }
    return mask << channel->offset;
}

enum nb_framebuffer_status nb_framebuffer_format_validate(
    const struct nb_framebuffer_format *format)
{
    uint32_t used = 0;
    uint32_t mask;
    const struct nb_framebuffer_channel *channels[4];
    size_t index;

    if (format == NULL) {
        return NB_FRAMEBUFFER_INVALID_ARGUMENT;
    }
    if (format->bits_per_pixel != 16 &&
        format->bits_per_pixel != 24 &&
        format->bits_per_pixel != 32) {
        return NB_FRAMEBUFFER_UNSUPPORTED_BITS_PER_PIXEL;
    }
    if (!channel_in_range(&format->red,
                          format->bits_per_pixel,
                          false) ||
        !channel_in_range(&format->green,
                          format->bits_per_pixel,
                          false) ||
        !channel_in_range(&format->blue,
                          format->bits_per_pixel,
                          false) ||
        !channel_in_range(&format->alpha,
                          format->bits_per_pixel,
                          true)) {
        return NB_FRAMEBUFFER_INVALID_CHANNEL;
    }

    channels[0] = &format->red;
    channels[1] = &format->green;
    channels[2] = &format->blue;
    channels[3] = &format->alpha;
    for (index = 0; index < sizeof(channels) / sizeof(channels[0]);
         ++index) {
        mask = channel_mask(channels[index]);
        if ((used & mask) != 0) {
            return NB_FRAMEBUFFER_OVERLAPPING_CHANNELS;
        }
        used |= mask;
    }
    return NB_FRAMEBUFFER_OK;
}

static bool multiply_size(size_t left, size_t right, size_t *product)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return false;
    }
    *product = left * right;
    return true;
}

static bool add_size(size_t left, size_t right, size_t *sum)
{
    if (right > SIZE_MAX - left) {
        return false;
    }
    *sum = left + right;
    return true;
}

static bool required_buffer_size(size_t row_size,
                                 size_t stride,
                                 size_t height,
                                 size_t *required)
{
    size_t preceding_rows;

    if (!multiply_size(height - 1, stride, &preceding_rows)) {
        return false;
    }
    return add_size(preceding_rows, row_size, required);
}

static uint32_t channel_maximum(uint32_t size)
{
    if (size == 32) {
        return UINT32_MAX;
    }
    return (UINT32_C(1) << size) - UINT32_C(1);
}

static uint32_t scale_channel(uint8_t value, uint32_t size)
{
    const uint64_t maximum = channel_maximum(size);

    return (uint32_t)(((uint64_t)value * maximum + UINT64_C(127)) /
                      UINT64_C(255));
}

static uint32_t insert_channel(
    uint32_t packed,
    uint8_t value,
    const struct nb_framebuffer_channel *channel)
{
    if (channel->size == 0) {
        return packed;
    }
    return packed | scale_channel(value, channel->size) << channel->offset;
}

static uint32_t pack_pixel(uint32_t source,
                           enum nb_framebuffer_source_format source_format,
                           const struct nb_framebuffer_format *format)
{
    const uint8_t red = (uint8_t)((source >> 16) & UINT32_C(0xff));
    const uint8_t green = (uint8_t)((source >> 8) & UINT32_C(0xff));
    const uint8_t blue = (uint8_t)(source & UINT32_C(0xff));
    const uint8_t alpha =
        source_format == NB_FRAMEBUFFER_SOURCE_ARGB8888
            ? (uint8_t)((source >> 24) & UINT32_C(0xff))
            : UINT8_MAX;
    uint32_t packed = 0;

    packed = insert_channel(packed, red, &format->red);
    packed = insert_channel(packed, green, &format->green);
    packed = insert_channel(packed, blue, &format->blue);
    return insert_channel(packed, alpha, &format->alpha);
}

static bool host_is_little_endian(void)
{
    const uint16_t marker = UINT16_C(1);
    uint8_t first_byte;

    memcpy(&first_byte, &marker, sizeof(first_byte));
    return first_byte == UINT8_C(1);
}

static void store_pixel(uint8_t *destination,
                        uint32_t packed,
                        uint32_t bits_per_pixel)
{
    if (bits_per_pixel == 16) {
        const uint16_t value = (uint16_t)packed;

        memcpy(destination, &value, sizeof(value));
    } else if (bits_per_pixel == 24) {
        if (host_is_little_endian()) {
            destination[0] = (uint8_t)(packed & UINT32_C(0xff));
            destination[1] =
                (uint8_t)((packed >> 8) & UINT32_C(0xff));
            destination[2] =
                (uint8_t)((packed >> 16) & UINT32_C(0xff));
        } else {
            destination[0] =
                (uint8_t)((packed >> 16) & UINT32_C(0xff));
            destination[1] =
                (uint8_t)((packed >> 8) & UINT32_C(0xff));
            destination[2] = (uint8_t)(packed & UINT32_C(0xff));
        }
    } else {
        memcpy(destination, &packed, sizeof(packed));
    }
}

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
    const struct nb_framebuffer_format *destination_format)
{
    enum nb_framebuffer_status status;
    size_t source_row_size;
    size_t destination_row_size;
    size_t source_required;
    size_t destination_required;
    size_t destination_pixel_size;
    size_t row;
    const uint8_t *source_bytes = source;
    uint8_t *destination_bytes = destination;

    status = nb_framebuffer_format_validate(destination_format);
    if (status != NB_FRAMEBUFFER_OK) {
        return status;
    }
    if (source_format != NB_FRAMEBUFFER_SOURCE_XRGB8888 &&
        source_format != NB_FRAMEBUFFER_SOURCE_ARGB8888) {
        return NB_FRAMEBUFFER_INVALID_ARGUMENT;
    }
    if (width == 0 || height == 0) {
        return NB_FRAMEBUFFER_OK;
    }
    if (source == NULL || destination == NULL) {
        return NB_FRAMEBUFFER_INVALID_ARGUMENT;
    }

    destination_pixel_size = destination_format->bits_per_pixel / 8;
    if (!multiply_size(width, sizeof(uint32_t), &source_row_size) ||
        !multiply_size(width,
                       destination_pixel_size,
                       &destination_row_size)) {
        return NB_FRAMEBUFFER_SIZE_OVERFLOW;
    }
    if (source_stride < source_row_size) {
        return NB_FRAMEBUFFER_SOURCE_STRIDE_TOO_SMALL;
    }
    if (destination_stride < destination_row_size) {
        return NB_FRAMEBUFFER_DESTINATION_STRIDE_TOO_SMALL;
    }
    if (!required_buffer_size(source_row_size,
                              source_stride,
                              height,
                              &source_required) ||
        !required_buffer_size(destination_row_size,
                              destination_stride,
                              height,
                              &destination_required)) {
        return NB_FRAMEBUFFER_SIZE_OVERFLOW;
    }
    if (source_size < source_required) {
        return NB_FRAMEBUFFER_SOURCE_BUFFER_TOO_SMALL;
    }
    if (destination_size < destination_required) {
        return NB_FRAMEBUFFER_DESTINATION_BUFFER_TOO_SMALL;
    }

    for (row = 0; row < height; ++row) {
        const uint8_t *source_pixel = source_bytes + row * source_stride;
        uint8_t *destination_pixel =
            destination_bytes + row * destination_stride;
        size_t column;

        for (column = 0; column < width; ++column) {
            uint32_t source_value;
            uint32_t packed;

            memcpy(&source_value, source_pixel, sizeof(source_value));
            packed = pack_pixel(source_value,
                                source_format,
                                destination_format);
            store_pixel(destination_pixel,
                        packed,
                        destination_format->bits_per_pixel);
            source_pixel += sizeof(source_value);
            destination_pixel += destination_pixel_size;
        }
    }
    return NB_FRAMEBUFFER_OK;
}

const char *nb_framebuffer_status_string(enum nb_framebuffer_status status)
{
    switch (status) {
    case NB_FRAMEBUFFER_OK:
        return "ok";
    case NB_FRAMEBUFFER_INVALID_ARGUMENT:
        return "invalid argument";
    case NB_FRAMEBUFFER_UNSUPPORTED_BITS_PER_PIXEL:
        return "unsupported bits per pixel";
    case NB_FRAMEBUFFER_INVALID_CHANNEL:
        return "invalid channel";
    case NB_FRAMEBUFFER_OVERLAPPING_CHANNELS:
        return "overlapping channels";
    case NB_FRAMEBUFFER_SOURCE_STRIDE_TOO_SMALL:
        return "source stride is too small";
    case NB_FRAMEBUFFER_DESTINATION_STRIDE_TOO_SMALL:
        return "destination stride is too small";
    case NB_FRAMEBUFFER_SOURCE_BUFFER_TOO_SMALL:
        return "source buffer is too small";
    case NB_FRAMEBUFFER_DESTINATION_BUFFER_TOO_SMALL:
        return "destination buffer is too small";
    case NB_FRAMEBUFFER_SIZE_OVERFLOW:
        return "frame size overflow";
    default:
        return "unknown framebuffer status";
    }
}
