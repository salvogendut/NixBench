#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "framebuffer_format.h"

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

static struct nb_framebuffer_format argb8888(void)
{
    const struct nb_framebuffer_format format = {
        32,
        {16, 8},
        {8, 8},
        {0, 8},
        {24, 8}
    };

    return format;
}

static bool host_is_little_endian(void)
{
    const uint16_t marker = UINT16_C(1);
    uint8_t byte;

    memcpy(&byte, &marker, sizeof(byte));
    return byte == UINT8_C(1);
}

static uint32_t load_pixel(const uint8_t *source, uint32_t bits_per_pixel)
{
    uint32_t value = 0;

    if (bits_per_pixel == 16) {
        uint16_t value16;

        memcpy(&value16, source, sizeof(value16));
        value = value16;
    } else if (bits_per_pixel == 24) {
        if (host_is_little_endian()) {
            value = (uint32_t)source[0] |
                    (uint32_t)source[1] << 8 |
                    (uint32_t)source[2] << 16;
        } else {
            value = (uint32_t)source[0] << 16 |
                    (uint32_t)source[1] << 8 |
                    (uint32_t)source[2];
        }
    } else {
        memcpy(&value, source, sizeof(value));
    }
    return value;
}

static enum nb_framebuffer_status convert_one(
    uint32_t source,
    enum nb_framebuffer_source_format source_format,
    const struct nb_framebuffer_format *format,
    uint8_t destination[4])
{
    const size_t destination_size = format->bits_per_pixel / 8;

    memset(destination, 0xa5, 4);
    return nb_framebuffer_convert(&source,
                                  sizeof(source),
                                  sizeof(source),
                                  source_format,
                                  destination,
                                  destination_size,
                                  destination_size,
                                  1,
                                  1,
                                  format);
}

static uint32_t scale_reference(uint8_t value, uint32_t bits)
{
    const uint64_t maximum =
        bits == 32 ? UINT32_MAX
                   : (UINT64_C(1) << bits) - UINT64_C(1);

    return (uint32_t)(((uint64_t)value * maximum + UINT64_C(127)) /
                      UINT64_C(255));
}

static void test_layout_validation(void)
{
    struct nb_framebuffer_format format = {
        16,
        {0, 4},
        {4, 4},
        {8, 4},
        {12, 4}
    };
    static const uint32_t unsupported[] = {0, 1, 8, 15, 17, 31, 64};
    size_t index;

    CHECK(nb_framebuffer_format_validate(NULL) ==
          NB_FRAMEBUFFER_INVALID_ARGUMENT);
    CHECK(nb_framebuffer_format_validate(&format) == NB_FRAMEBUFFER_OK);

    for (index = 0; index < sizeof(unsupported) / sizeof(unsupported[0]);
         ++index) {
        struct nb_framebuffer_format invalid = format;

        invalid.bits_per_pixel = unsupported[index];
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_UNSUPPORTED_BITS_PER_PIXEL);
    }

    {
        struct nb_framebuffer_format invalid = format;
        invalid.red.size = 0;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_INVALID_CHANNEL);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.green.size = 0;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_INVALID_CHANNEL);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.blue.size = 0;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_INVALID_CHANNEL);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.red.offset = 16;
        invalid.red.size = 1;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_INVALID_CHANNEL);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.green.offset = UINT32_MAX;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_INVALID_CHANNEL);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.alpha.offset = 15;
        invalid.alpha.size = 2;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_INVALID_CHANNEL);
    }

    {
        struct nb_framebuffer_format invalid = format;
        invalid.green.offset = 3;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_OVERLAPPING_CHANNELS);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.blue.offset = 3;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_OVERLAPPING_CHANNELS);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.alpha.offset = 3;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_OVERLAPPING_CHANNELS);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.blue.offset = 7;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_OVERLAPPING_CHANNELS);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.alpha.offset = 7;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_OVERLAPPING_CHANNELS);
    }
    {
        struct nb_framebuffer_format invalid = format;
        invalid.alpha.offset = 11;
        CHECK(nb_framebuffer_format_validate(&invalid) ==
              NB_FRAMEBUFFER_OVERLAPPING_CHANNELS);
    }

    format.alpha.offset = UINT32_MAX;
    format.alpha.size = 0;
    CHECK(nb_framebuffer_format_validate(&format) == NB_FRAMEBUFFER_OK);
}

static void test_common_formats(void)
{
    uint8_t destination[4];
    struct nb_framebuffer_format format = rgb565();

    CHECK(convert_one(UINT32_C(0xffff8000),
                      NB_FRAMEBUFFER_SOURCE_ARGB8888,
                      &format,
                      destination) == NB_FRAMEBUFFER_OK);
    CHECK(load_pixel(destination, 16) == UINT32_C(0xfc00));
    CHECK(destination[2] == UINT8_C(0xa5));
    CHECK(destination[3] == UINT8_C(0xa5));

    format.bits_per_pixel = 24;
    format.red.offset = 0;
    format.red.size = 8;
    format.green.offset = 8;
    format.green.size = 8;
    format.blue.offset = 16;
    format.blue.size = 8;
    CHECK(convert_one(UINT32_C(0x00112233),
                      NB_FRAMEBUFFER_SOURCE_XRGB8888,
                      &format,
                      destination) == NB_FRAMEBUFFER_OK);
    CHECK(load_pixel(destination, 24) == UINT32_C(0x00332211));
    if (host_is_little_endian()) {
        CHECK(destination[0] == UINT8_C(0x11));
        CHECK(destination[1] == UINT8_C(0x22));
        CHECK(destination[2] == UINT8_C(0x33));
    } else {
        CHECK(destination[0] == UINT8_C(0x33));
        CHECK(destination[1] == UINT8_C(0x22));
        CHECK(destination[2] == UINT8_C(0x11));
    }
    CHECK(destination[3] == UINT8_C(0xa5));

    format = argb8888();
    CHECK(convert_one(UINT32_C(0x80112233),
                      NB_FRAMEBUFFER_SOURCE_ARGB8888,
                      &format,
                      destination) == NB_FRAMEBUFFER_OK);
    CHECK(load_pixel(destination, 32) == UINT32_C(0x80112233));

    CHECK(convert_one(UINT32_C(0x00112233),
                      NB_FRAMEBUFFER_SOURCE_XRGB8888,
                      &format,
                      destination) == NB_FRAMEBUFFER_OK);
    CHECK(load_pixel(destination, 32) == UINT32_C(0xff112233));

    format.alpha.offset = UINT32_MAX;
    format.alpha.size = 0;
    CHECK(convert_one(UINT32_C(0x80112233),
                      NB_FRAMEBUFFER_SOURCE_ARGB8888,
                      &format,
                      destination) == NB_FRAMEBUFFER_OK);
    CHECK(load_pixel(destination, 32) == UINT32_C(0x00112233));
}

static void test_arbitrary_masks(void)
{
    const struct nb_framebuffer_format format16 = {
        16,
        {0, 4},
        {6, 5},
        {12, 4},
        {0, 0}
    };
    const struct nb_framebuffer_format format32 = {
        32,
        {20, 10},
        {10, 10},
        {0, 10},
        {30, 2}
    };
    uint8_t destination[4];
    uint32_t expected;

    CHECK(convert_one(UINT32_C(0xffff7f40),
                      NB_FRAMEBUFFER_SOURCE_ARGB8888,
                      &format16,
                      destination) == NB_FRAMEBUFFER_OK);
    expected = UINT32_C(0x000f) |
               scale_reference(UINT8_C(0x7f), 5) << 6 |
               scale_reference(UINT8_C(0x40), 4) << 12;
    CHECK(load_pixel(destination, 16) == expected);

    CHECK(convert_one(UINT32_C(0x80ff8000),
                      NB_FRAMEBUFFER_SOURCE_ARGB8888,
                      &format32,
                      destination) == NB_FRAMEBUFFER_OK);
    expected = scale_reference(UINT8_C(0xff), 10) << 20 |
               scale_reference(UINT8_C(0x80), 10) << 10 |
               scale_reference(UINT8_C(0x80), 2) << 30;
    CHECK(load_pixel(destination, 32) == expected);

    CHECK(convert_one(UINT32_C(0x00ff8000),
                      NB_FRAMEBUFFER_SOURCE_XRGB8888,
                      &format32,
                      destination) == NB_FRAMEBUFFER_OK);
    expected = scale_reference(UINT8_C(0xff), 10) << 20 |
               scale_reference(UINT8_C(0x80), 10) << 10 |
               UINT32_C(3) << 30;
    CHECK(load_pixel(destination, 32) == expected);
}

static void test_all_channel_scalings(void)
{
    uint32_t bits;

    for (bits = 1; bits <= 30; ++bits) {
        struct nb_framebuffer_format format;
        unsigned int value;

        memset(&format, 0, sizeof(format));
        format.bits_per_pixel = bits + 2 <= 16
                                    ? 16
                                    : (bits + 2 <= 24 ? 24 : 32);
        format.red.offset = 0;
        format.red.size = bits;
        format.green.offset = bits;
        format.green.size = 1;
        format.blue.offset = bits + 1;
        format.blue.size = 1;

        CHECK(nb_framebuffer_format_validate(&format) ==
              NB_FRAMEBUFFER_OK);
        for (value = 0; value <= UINT8_MAX; ++value) {
            const uint32_t source = (uint32_t)value << 16;
            uint8_t destination[4];

            CHECK(convert_one(source,
                              NB_FRAMEBUFFER_SOURCE_XRGB8888,
                              &format,
                              destination) == NB_FRAMEBUFFER_OK);
            CHECK(load_pixel(destination, format.bits_per_pixel) ==
                  scale_reference((uint8_t)value, bits));
        }
    }
}

static void test_all_alpha_scalings(void)
{
    uint32_t bits;

    for (bits = 1; bits <= 8; ++bits) {
        struct nb_framebuffer_format format = {
            16,
            {0, 1},
            {1, 1},
            {2, 1},
            {3, bits}
        };
        unsigned int value;

        for (value = 0; value <= UINT8_MAX; ++value) {
            const uint32_t source = (uint32_t)value << 24;
            const uint32_t expected =
                scale_reference((uint8_t)value, bits) << 3;
            uint8_t destination[4];

            CHECK(convert_one(source,
                              NB_FRAMEBUFFER_SOURCE_ARGB8888,
                              &format,
                              destination) == NB_FRAMEBUFFER_OK);
            CHECK(load_pixel(destination, 16) == expected);
        }
    }
}

static void store_source(uint8_t *destination, uint32_t source)
{
    memcpy(destination, &source, sizeof(source));
}

static void test_strides_and_exact_sizes(void)
{
    const struct nb_framebuffer_format format = rgb565();
    uint8_t source[24];
    uint8_t source_copy[24];
    uint8_t destination[12];

    memset(source, 0x3c, sizeof(source));
    store_source(source, UINT32_C(0x00ff0000));
    store_source(source + 4, UINT32_C(0x0000ff00));
    store_source(source + 12, UINT32_C(0x000000ff));
    store_source(source + 16, UINT32_C(0x00ffffff));
    memcpy(source_copy, source, sizeof(source));
    memset(destination, 0xa5, sizeof(destination));

    CHECK(nb_framebuffer_convert(source,
                                 20,
                                 12,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 10,
                                 6,
                                 2,
                                 2,
                                 &format) == NB_FRAMEBUFFER_OK);
    CHECK(load_pixel(destination, 16) == UINT32_C(0xf800));
    CHECK(load_pixel(destination + 2, 16) == UINT32_C(0x07e0));
    CHECK(destination[4] == UINT8_C(0xa5));
    CHECK(destination[5] == UINT8_C(0xa5));
    CHECK(load_pixel(destination + 6, 16) == UINT32_C(0x001f));
    CHECK(load_pixel(destination + 8, 16) == UINT32_C(0xffff));
    CHECK(destination[10] == UINT8_C(0xa5));
    CHECK(destination[11] == UINT8_C(0xa5));
    CHECK(memcmp(source, source_copy, sizeof(source)) == 0);
}

static void test_conversion_errors(void)
{
    const struct nb_framebuffer_format format = rgb565();
    const uint32_t source = UINT32_C(0x00112233);
    uint8_t destination[8];
    uint8_t unchanged[8];

    memset(destination, 0x5a, sizeof(destination));
    memcpy(unchanged, destination, sizeof(destination));

    CHECK(nb_framebuffer_convert(&source,
                                 sizeof(source),
                                 sizeof(source),
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 2,
                                 2,
                                 1,
                                 1,
                                 NULL) == NB_FRAMEBUFFER_INVALID_ARGUMENT);
    CHECK(nb_framebuffer_convert(&source,
                                 sizeof(source),
                                 sizeof(source),
                                 (enum nb_framebuffer_source_format)99,
                                 destination,
                                 2,
                                 2,
                                 1,
                                 1,
                                 &format) == NB_FRAMEBUFFER_INVALID_ARGUMENT);
    CHECK(nb_framebuffer_convert(NULL,
                                 0,
                                 0,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 NULL,
                                 0,
                                 0,
                                 0,
                                 SIZE_MAX,
                                 &format) == NB_FRAMEBUFFER_OK);
    CHECK(nb_framebuffer_convert(NULL,
                                 0,
                                 0,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 NULL,
                                 0,
                                 0,
                                 SIZE_MAX,
                                 0,
                                 &format) == NB_FRAMEBUFFER_OK);
    CHECK(nb_framebuffer_convert(NULL,
                                 4,
                                 4,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 2,
                                 2,
                                 1,
                                 1,
                                 &format) == NB_FRAMEBUFFER_INVALID_ARGUMENT);
    CHECK(nb_framebuffer_convert(&source,
                                 4,
                                 4,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 NULL,
                                 2,
                                 2,
                                 1,
                                 1,
                                 &format) == NB_FRAMEBUFFER_INVALID_ARGUMENT);
    CHECK(nb_framebuffer_convert(&source,
                                 4,
                                 3,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 2,
                                 2,
                                 1,
                                 1,
                                 &format) ==
          NB_FRAMEBUFFER_SOURCE_STRIDE_TOO_SMALL);
    CHECK(nb_framebuffer_convert(&source,
                                 4,
                                 4,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 2,
                                 1,
                                 1,
                                 1,
                                 &format) ==
          NB_FRAMEBUFFER_DESTINATION_STRIDE_TOO_SMALL);
    CHECK(nb_framebuffer_convert(&source,
                                 3,
                                 4,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 2,
                                 2,
                                 1,
                                 1,
                                 &format) ==
          NB_FRAMEBUFFER_SOURCE_BUFFER_TOO_SMALL);
    CHECK(nb_framebuffer_convert(&source,
                                 4,
                                 4,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 1,
                                 2,
                                 1,
                                 1,
                                 &format) ==
          NB_FRAMEBUFFER_DESTINATION_BUFFER_TOO_SMALL);
    CHECK(nb_framebuffer_convert(&source,
                                 SIZE_MAX,
                                 SIZE_MAX,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 SIZE_MAX,
                                 2,
                                 1,
                                 3,
                                 &format) == NB_FRAMEBUFFER_SIZE_OVERFLOW);
    CHECK(nb_framebuffer_convert(&source,
                                 SIZE_MAX,
                                 SIZE_MAX,
                                 NB_FRAMEBUFFER_SOURCE_XRGB8888,
                                 destination,
                                 SIZE_MAX,
                                 SIZE_MAX,
                                 SIZE_MAX / 4 + 1,
                                 1,
                                 &format) == NB_FRAMEBUFFER_SIZE_OVERFLOW);
    CHECK(memcmp(destination, unchanged, sizeof(destination)) == 0);
}

static void test_status_strings(void)
{
    enum nb_framebuffer_status status;

    for (status = NB_FRAMEBUFFER_OK;
         status <= NB_FRAMEBUFFER_SIZE_OVERFLOW;
         status = (enum nb_framebuffer_status)(status + 1)) {
        const char *text = nb_framebuffer_status_string(status);

        CHECK(text != NULL);
        CHECK(text[0] != '\0');
    }
    CHECK(strcmp(nb_framebuffer_status_string(
                     (enum nb_framebuffer_status)999),
                 "unknown framebuffer status") == 0);
}

int main(void)
{
    test_layout_validation();
    test_common_formats();
    test_arbitrary_masks();
    test_all_channel_scalings();
    test_all_alpha_scalings();
    test_strides_and_exact_sizes();
    test_conversion_errors();
    test_status_strings();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    puts("framebuffer format tests passed");
    return 0;
}
