#include "png_writer.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static void set_errno_error(char *error,
                            size_t capacity,
                            const char *operation)
{
    if (error != NULL && capacity > 0) {
        (void)snprintf(error,
                       capacity,
                       "%s: %s",
                       operation,
                       strerror(errno));
    }
}

static void store_be32(unsigned char destination[4], uint32_t value)
{
    destination[0] = (unsigned char)(value >> 24);
    destination[1] = (unsigned char)(value >> 16);
    destination[2] = (unsigned char)(value >> 8);
    destination[3] = (unsigned char)value;
}

static bool write_bytes(FILE *file, const void *bytes, size_t size)
{
    return size == 0 || fwrite(bytes, 1, size, file) == size;
}

static bool write_chunk(FILE *file,
                        const char type[4],
                        const unsigned char *data,
                        uint32_t size)
{
    unsigned char encoded[4];
    uLong checksum = crc32(0L, Z_NULL, 0);

    store_be32(encoded, size);
    checksum = crc32(checksum, (const Bytef *)type, 4);
    if (size > 0) {
        checksum = crc32(checksum, data, size);
    }
    if (!write_bytes(file, encoded, sizeof(encoded)) ||
        !write_bytes(file, type, 4) ||
        !write_bytes(file, data, size)) {
        return false;
    }
    store_be32(encoded, (uint32_t)checksum);
    return write_bytes(file, encoded, sizeof(encoded));
}

bool nb_png_write_xrgb8888(const char *path,
                           const void *pixels,
                           int width,
                           int height,
                           size_t stride,
                           char *error,
                           size_t error_capacity)
{
    static const unsigned char signature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
    };
    unsigned char header[13];
    unsigned char *raw = NULL;
    unsigned char *compressed = NULL;
    size_t row_size;
    size_t raw_size;
    uLongf compressed_size;
    FILE *file = NULL;
    bool success = false;
    int y;

    if (error != NULL && error_capacity > 0) {
        error[0] = '\0';
    }
    if (path == NULL || path[0] == '\0' || pixels == NULL || width <= 0 ||
        height <= 0 || (size_t)width > (SIZE_MAX - 1) / 3) {
        set_error(error, error_capacity, "invalid PNG image");
        return false;
    }
    row_size = 1 + (size_t)width * 3;
    if ((size_t)height > SIZE_MAX / row_size ||
        stride < (size_t)width * sizeof(uint32_t)) {
        set_error(error, error_capacity, "PNG image dimensions overflow");
        return false;
    }
    raw_size = row_size * (size_t)height;
    if (raw_size > ULONG_MAX) {
        set_error(error, error_capacity, "PNG image is too large");
        return false;
    }
    raw = malloc(raw_size);
    if (raw == NULL) {
        set_error(error, error_capacity, "out of memory encoding PNG");
        return false;
    }
    for (y = 0; y < height; ++y) {
        const unsigned char *source =
            (const unsigned char *)pixels + (size_t)y * stride;
        unsigned char *destination = raw + (size_t)y * row_size;
        int x;

        *destination++ = 0;
        for (x = 0; x < width; ++x) {
            uint32_t pixel;

            (void)memcpy(&pixel,
                         source + (size_t)x * sizeof(pixel),
                         sizeof(pixel));
            *destination++ = (unsigned char)(pixel >> 16);
            *destination++ = (unsigned char)(pixel >> 8);
            *destination++ = (unsigned char)pixel;
        }
    }

    compressed_size = compressBound((uLong)raw_size);
    if (compressed_size > UINT32_MAX) {
        set_error(error, error_capacity, "compressed PNG is too large");
        goto cleanup;
    }
    compressed = malloc((size_t)compressed_size);
    if (compressed == NULL) {
        set_error(error, error_capacity, "out of memory compressing PNG");
        goto cleanup;
    }
    if (compress2(compressed,
                  &compressed_size,
                  raw,
                  (uLong)raw_size,
                  Z_BEST_SPEED) != Z_OK) {
        set_error(error, error_capacity, "could not compress PNG pixels");
        goto cleanup;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        set_errno_error(error, error_capacity, "could not open screenshot");
        goto cleanup;
    }
    store_be32(header, (uint32_t)width);
    store_be32(header + 4, (uint32_t)height);
    header[8] = 8;
    header[9] = 2;
    header[10] = 0;
    header[11] = 0;
    header[12] = 0;
    if (!write_bytes(file, signature, sizeof(signature)) ||
        !write_chunk(file, "IHDR", header, sizeof(header)) ||
        !write_chunk(file,
                     "IDAT",
                     compressed,
                     (uint32_t)compressed_size) ||
        !write_chunk(file, "IEND", NULL, 0)) {
        set_errno_error(error, error_capacity, "could not write screenshot");
        goto cleanup;
    }
    if (fclose(file) != 0) {
        file = NULL;
        set_errno_error(error, error_capacity, "could not finish screenshot");
        goto cleanup;
    }
    file = NULL;
    success = true;

cleanup:
    if (file != NULL) {
        (void)fclose(file);
    }
    free(compressed);
    free(raw);
    return success;
}
