#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "framebuffer_shadow.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static struct nb_framebuffer_format rgb565(void)
{
    const struct nb_framebuffer_format format = {
        16,
        {11, 5},
        {5, 6},
        {0, 5},
        {0, 0}
    };

    return format;
}

static struct nb_framebuffer_format rgb888(void)
{
    const struct nb_framebuffer_format format = {
        24,
        {16, 8},
        {8, 8},
        {0, 8},
        {0, 0}
    };

    return format;
}

static struct nb_framebuffer_format xrgb8888(void)
{
    const struct nb_framebuffer_format format = {
        32,
        {16, 8},
        {8, 8},
        {0, 8},
        {0, 0}
    };

    return format;
}

static struct nb_framebuffer_format argb8888(void)
{
    struct nb_framebuffer_format format = xrgb8888();

    format.alpha.offset = 24;
    format.alpha.size = 8;
    return format;
}

static void store_pixel(unsigned char *destination, uint32_t pixel)
{
    memcpy(destination, &pixel, sizeof(pixel));
}

static void fill_source(unsigned char *source,
                        size_t stride,
                        size_t width,
                        size_t height)
{
    size_t row;

    memset(source, 0xa5, stride * height);
    for (row = 0; row < height; ++row) {
        size_t column;

        for (column = 0; column < width; ++column) {
            const uint32_t pixel =
                (uint32_t)((row + 1) * UINT32_C(0x00110000)) |
                (uint32_t)((column + 1) * UINT32_C(0x00001100)) |
                (uint32_t)(row * width + column + 1);

            store_pixel(source + row * stride + column * sizeof(pixel),
                        pixel);
        }
    }
}

static void check_matches_full_conversion(
    const unsigned char *source,
    size_t source_size,
    size_t source_stride,
    enum nb_framebuffer_source_format source_format,
    const unsigned char *actual,
    size_t destination_size,
    size_t destination_stride,
    size_t width,
    size_t height,
    const struct nb_framebuffer_format *format)
{
    unsigned char *expected = malloc(destination_size);

    CHECK(expected != NULL);
    if (expected == NULL) {
        return;
    }
    memcpy(expected, actual, destination_size);
    CHECK(nb_framebuffer_convert(source,
                                 source_size,
                                 source_stride,
                                 source_format,
                                 expected,
                                 destination_size,
                                 destination_stride,
                                 width,
                                 height,
                                 format) == NB_FRAMEBUFFER_OK);
    CHECK(memcmp(actual, expected, destination_size) == 0);
    free(expected);
}

static void test_incremental_format(
    const struct nb_framebuffer_format *format)
{
    enum { width = 6, height = 3, source_stride = 28 };
    const size_t destination_pixel_size = format->bits_per_pixel / 8;
    const size_t destination_stride =
        width * destination_pixel_size + 5;
    const size_t destination_size = destination_stride * height;
    unsigned char source[source_stride * height];
    unsigned char destination[4 * width * height + 15];
    unsigned char unchanged[sizeof(destination)];
    struct nb_framebuffer_shadow *shadow =
        nb_framebuffer_shadow_create(width, height);
    struct nb_framebuffer_shadow_stats stats;
    size_t row;

    CHECK(shadow != NULL);
    if (shadow == NULL) {
        return;
    }
    CHECK(destination_size <= sizeof(destination));
    fill_source(source, source_stride, width, height);
    memset(destination, 0x5a, sizeof(destination));

    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              destination_size,
              destination_stride,
              format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);
    CHECK(stats.regions == 1);
    CHECK(stats.rows == height);
    CHECK(stats.converted_pixels == width * height);
    CHECK(stats.converted_bytes ==
          width * height * destination_pixel_size);
    check_matches_full_conversion(source,
                                  sizeof(source),
                                  source_stride,
                                  NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                  destination,
                                  destination_size,
                                  destination_stride,
                                  width,
                                  height,
                                  format);

    memcpy(unchanged, destination, sizeof(destination));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              destination_size,
              destination_stride,
              format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(!stats.full);
    CHECK(stats.regions == 0);
    CHECK(stats.rows == 0);
    CHECK(stats.converted_pixels == 0);
    CHECK(memcmp(destination, unchanged, sizeof(destination)) == 0);

    store_pixel(source + source_stride + sizeof(uint32_t),
                UINT32_C(0x00abcdef));
    store_pixel(source + source_stride + 4 * sizeof(uint32_t),
                UINT32_C(0x00123456));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              destination_size,
              destination_stride,
              format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(!stats.full);
    CHECK(stats.regions == 1);
    CHECK(stats.rows == 1);
    CHECK(stats.converted_pixels == 4);
    CHECK(stats.converted_bytes == 4 * destination_pixel_size);
    check_matches_full_conversion(source,
                                  sizeof(source),
                                  source_stride,
                                  NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                  destination,
                                  destination_size,
                                  destination_stride,
                                  width,
                                  height,
                                  format);

    store_pixel(source, UINT32_C(0x00fefefe));
    store_pixel(source + 2 * source_stride + 5 * sizeof(uint32_t),
                UINT32_C(0x00010203));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              destination_size,
              destination_stride,
              format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(!stats.full);
    CHECK(stats.regions == 2);
    CHECK(stats.rows == 2);
    CHECK(stats.converted_pixels == 2);
    check_matches_full_conversion(source,
                                  sizeof(source),
                                  source_stride,
                                  NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                  destination,
                                  destination_size,
                                  destination_stride,
                                  width,
                                  height,
                                  format);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              destination_size,
              destination_stride,
              format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(!stats.full);
    CHECK(stats.regions == 0);

    for (row = 0; row < height; ++row) {
        memset(source + row * source_stride,
               0x6d,
               width * sizeof(uint32_t));
    }
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              destination_size,
              destination_stride,
              format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);
    CHECK(stats.regions == 1);
    CHECK(stats.rows == height);
    check_matches_full_conversion(source,
                                  sizeof(source),
                                  source_stride,
                                  NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                  destination,
                                  destination_size,
                                  destination_stride,
                                  width,
                                  height,
                                  format);

    for (row = 0; row < height; ++row) {
        memset(destination + row * destination_stride,
               0,
               width * destination_pixel_size);
    }
    nb_framebuffer_shadow_invalidate(shadow);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              destination_size,
              destination_stride,
              format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);
    check_matches_full_conversion(source,
                                  sizeof(source),
                                  source_stride,
                                  NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                  destination,
                                  destination_size,
                                  destination_stride,
                                  width,
                                  height,
                                  format);
    for (row = 0; row < height; ++row) {
        size_t offset;

        for (offset = width * destination_pixel_size;
             offset < destination_stride;
             ++offset) {
            CHECK(destination[row * destination_stride + offset] ==
                  UINT8_C(0x5a));
        }
    }
    for (row = destination_size; row < sizeof(destination); ++row) {
        CHECK(destination[row] == UINT8_C(0x5a));
    }
    nb_framebuffer_shadow_destroy(shadow);
}

static void test_padding_alpha_and_destination_change(void)
{
    enum { width = 3, height = 2, source_stride = 16 };
    const struct nb_framebuffer_format format = xrgb8888();
    const struct nb_framebuffer_format alpha_format = argb8888();
    unsigned char source[source_stride * height + 1];
    unsigned char destination[32];
    unsigned char replacement[32];
    struct nb_framebuffer_shadow *shadow =
        nb_framebuffer_shadow_create(width, height);
    struct nb_framebuffer_shadow_stats stats;
    uint32_t pixel;

    CHECK(shadow != NULL);
    if (shadow == NULL) {
        return;
    }
    fill_source(source + 1, source_stride, width, height);
    memset(destination, 0x5a, sizeof(destination));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source + 1,
              source_stride * height,
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              sizeof(destination),
              16,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);

    source[1 + width * sizeof(uint32_t)] ^= UINT8_C(0xff);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source + 1,
              source_stride * height,
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              sizeof(destination),
              16,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.regions == 0);

    memcpy(&pixel, source + 1, sizeof(pixel));
    pixel ^= UINT32_C(0xff000000);
    memcpy(source + 1, &pixel, sizeof(pixel));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source + 1,
              source_stride * height,
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              sizeof(destination),
              16,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.regions == 0);

    memset(replacement, 0x3c, sizeof(replacement));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source + 1,
              source_stride * height,
              source_stride,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              replacement,
              sizeof(replacement),
              16,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);
    check_matches_full_conversion(source + 1,
                                  source_stride * height,
                                  source_stride,
                                  NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                  replacement,
                                  sizeof(replacement),
                                  16,
                                  width,
                                  height,
                                  &format);

    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source + 1,
              source_stride * height,
              source_stride,
              NB_FRAMEBUFFER_SOURCE_ARGB8888,
              replacement,
              sizeof(replacement),
              16,
              &alpha_format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);

    memcpy(&pixel, source + 1 + source_stride, sizeof(pixel));
    pixel ^= UINT32_C(0x80000000);
    memcpy(source + 1 + source_stride, &pixel, sizeof(pixel));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source + 1,
              source_stride * height,
              source_stride,
              NB_FRAMEBUFFER_SOURCE_ARGB8888,
              replacement,
              sizeof(replacement),
              16,
              &alpha_format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(!stats.full);
    CHECK(stats.regions == 1);
    CHECK(stats.rows == 1);
    CHECK(stats.converted_pixels == 1);
    check_matches_full_conversion(source + 1,
                                  source_stride * height,
                                  source_stride,
                                  NB_FRAMEBUFFER_SOURCE_ARGB8888,
                                  replacement,
                                  sizeof(replacement),
                                  16,
                                  width,
                                  height,
                                  &alpha_format);
    nb_framebuffer_shadow_destroy(shadow);
}

static void test_validation_before_write(void)
{
    const struct nb_framebuffer_format format = rgb565();
    const uint32_t source[4] = {
        UINT32_C(0x00112233),
        UINT32_C(0x00445566),
        UINT32_C(0x00778899),
        UINT32_C(0x00abcdef)
    };
    unsigned char destination[8];
    unsigned char unchanged[8];
    struct nb_framebuffer_shadow *shadow =
        nb_framebuffer_shadow_create(2, 2);

    CHECK(nb_framebuffer_shadow_create(0, 2) == NULL);
    CHECK(nb_framebuffer_shadow_create(2, 0) == NULL);
    CHECK(nb_framebuffer_shadow_create(SIZE_MAX, 2) == NULL);
    CHECK(shadow != NULL);
    if (shadow == NULL) {
        return;
    }
    memset(destination, 0xa5, sizeof(destination));
    memcpy(unchanged, destination, sizeof(destination));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source) - 1,
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              sizeof(destination),
              2 * sizeof(uint16_t),
              &format,
              NULL) == NB_FRAMEBUFFER_SOURCE_BUFFER_TOO_SMALL);
    CHECK(memcmp(destination, unchanged, sizeof(destination)) == 0);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              sizeof(destination) - 1,
              2 * sizeof(uint16_t),
              &format,
              NULL) == NB_FRAMEBUFFER_DESTINATION_BUFFER_TOO_SMALL);
    CHECK(memcmp(destination, unchanged, sizeof(destination)) == 0);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t) - 1,
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              sizeof(destination),
              2 * sizeof(uint16_t),
              &format,
              NULL) == NB_FRAMEBUFFER_SOURCE_STRIDE_TOO_SMALL);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              sizeof(destination),
              2 * sizeof(uint16_t) - 1,
              &format,
              NULL) == NB_FRAMEBUFFER_DESTINATION_STRIDE_TOO_SMALL);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              (enum nb_framebuffer_source_format)99,
              destination,
              sizeof(destination),
              2 * sizeof(uint16_t),
              &format,
              NULL) == NB_FRAMEBUFFER_INVALID_ARGUMENT);
    CHECK(memcmp(destination, unchanged, sizeof(destination)) == 0);
    CHECK(nb_framebuffer_shadow_present(NULL,
                                         source,
                                         sizeof(source),
                                         2 * sizeof(uint32_t),
                                         NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                         destination,
                                         sizeof(destination),
                                         2 * sizeof(uint16_t),
                                         &format,
                                         NULL) ==
          NB_FRAMEBUFFER_INVALID_ARGUMENT);
    nb_framebuffer_shadow_invalidate(NULL);
    nb_framebuffer_shadow_destroy(NULL);
    nb_framebuffer_shadow_destroy(shadow);
}

static void test_identity_changes_force_full(void)
{
    const struct nb_framebuffer_format format = xrgb8888();
    const struct nb_framebuffer_format alpha_format = argb8888();
    uint32_t source[4] = {
        UINT32_C(0x10112233),
        UINT32_C(0x20445566),
        UINT32_C(0x30778899),
        UINT32_C(0x40abcdef)
    };
    unsigned char destination[32];
    struct nb_framebuffer_shadow *shadow =
        nb_framebuffer_shadow_create(2, 2);
    struct nb_framebuffer_shadow_stats stats;

    CHECK(shadow != NULL);
    if (shadow == NULL) {
        return;
    }
    memset(destination, 0xa5, sizeof(destination));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination,
              sizeof(destination),
              12,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);

    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_ARGB8888,
              destination,
              sizeof(destination),
              12,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);

    source[0] ^= UINT32_C(0x80000000);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_ARGB8888,
              destination,
              sizeof(destination),
              12,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(!stats.full);
    CHECK(stats.regions == 0);

    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_ARGB8888,
              destination,
              sizeof(destination) - 1,
              12,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);

    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_ARGB8888,
              destination,
              sizeof(destination) - 1,
              13,
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);

    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              2 * sizeof(uint32_t),
              NB_FRAMEBUFFER_SOURCE_ARGB8888,
              destination,
              sizeof(destination) - 1,
              13,
              &alpha_format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);
    nb_framebuffer_shadow_destroy(shadow);
}

static void test_unaligned_destination(void)
{
    const struct nb_framebuffer_format format = xrgb8888();
    uint32_t source[2] = {
        UINT32_C(0x00112233),
        UINT32_C(0x00445566)
    };
    unsigned char destination[10];
    struct nb_framebuffer_shadow *shadow =
        nb_framebuffer_shadow_create(2, 1);
    struct nb_framebuffer_shadow_stats stats;

    CHECK(shadow != NULL);
    if (shadow == NULL) {
        return;
    }
    memset(destination, 0xa5, sizeof(destination));
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              sizeof(source),
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination + 1,
              sizeof(destination) - 2,
              2 * sizeof(uint32_t),
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(stats.full);
    CHECK(destination[0] == UINT8_C(0xa5));
    CHECK(destination[9] == UINT8_C(0xa5));

    source[1] = UINT32_C(0x00778899);
    CHECK(nb_framebuffer_shadow_present(
              shadow,
              source,
              sizeof(source),
              sizeof(source),
              NB_FRAMEBUFFER_SOURCE_XRGB8888,
              destination + 1,
              sizeof(destination) - 2,
              2 * sizeof(uint32_t),
              &format,
              &stats) == NB_FRAMEBUFFER_OK);
    CHECK(!stats.full);
    CHECK(stats.regions == 1);
    CHECK(stats.converted_pixels == 1);
    CHECK(destination[0] == UINT8_C(0xa5));
    CHECK(destination[9] == UINT8_C(0xa5));
    nb_framebuffer_shadow_destroy(shadow);
}

int main(void)
{
    const struct nb_framebuffer_format format16 = rgb565();
    const struct nb_framebuffer_format format24 = rgb888();
    const struct nb_framebuffer_format format32 = xrgb8888();

    test_incremental_format(&format16);
    test_incremental_format(&format24);
    test_incremental_format(&format32);
    test_padding_alpha_and_destination_change();
    test_validation_before_write();
    test_identity_changes_force_full();
    test_unaligned_destination();

    if (failures != 0) {
        fprintf(stderr, "framebuffer shadow tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("framebuffer shadow tests: ok");
    return 0;
}
