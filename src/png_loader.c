#include "png_loader.h"

#include <png.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error,
                      size_t error_capacity,
                      const char *format,
                      ...)
{
    va_list arguments;

    if (error == NULL || error_capacity == 0) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error, error_capacity, format, arguments);
    va_end(arguments);
}

void nb_png_image_destroy(struct nb_png_image *image)
{
    if (image == NULL) {
        return;
    }
    free(image->pixels);
    (void)memset(image, 0, sizeof(*image));
}

bool nb_png_load(const char *path,
                 struct nb_png_image *image,
                 char *error,
                 size_t error_capacity)
{
    png_image source;
    struct nb_png_image decoded = {0};
    png_alloc_size_t byte_count;

    if (path == NULL || path[0] != '/' || image == NULL) {
        set_error(error, error_capacity, "PNG path must be absolute");
        return false;
    }
    (void)memset(&source, 0, sizeof(source));
    source.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&source, path)) {
        set_error(error,
                  error_capacity,
                  "could not read PNG: %s",
                  source.message[0] != '\0' ? source.message : "invalid image");
        return false;
    }
    if (source.width == 0 || source.height == 0 ||
        source.width > NB_PNG_DIMENSION_LIMIT ||
        source.height > NB_PNG_DIMENSION_LIMIT) {
        set_error(error,
                  error_capacity,
                  "PNG dimensions exceed the %d pixel limit",
                  NB_PNG_DIMENSION_LIMIT);
        png_image_free(&source);
        return false;
    }
    source.format = PNG_FORMAT_RGBA;
    byte_count = PNG_IMAGE_SIZE(source);
    if (byte_count == 0 || byte_count > NB_PNG_DECODE_BYTE_LIMIT) {
        set_error(error,
                  error_capacity,
                  "decoded PNG exceeds the %d MiB limit",
                  NB_PNG_DECODE_BYTE_LIMIT / (1024 * 1024));
        png_image_free(&source);
        return false;
    }
    decoded.pixels = malloc((size_t)byte_count);
    if (decoded.pixels == NULL) {
        set_error(error, error_capacity, "out of memory decoding PNG");
        png_image_free(&source);
        return false;
    }
    if (!png_image_finish_read(&source,
                               NULL,
                               decoded.pixels,
                               0,
                               NULL)) {
        set_error(error,
                  error_capacity,
                  "could not decode PNG: %s",
                  source.message[0] != '\0' ? source.message : "invalid image");
        png_image_free(&source);
        nb_png_image_destroy(&decoded);
        return false;
    }
    decoded.width = (int)source.width;
    decoded.height = (int)source.height;
    decoded.pitch = (size_t)decoded.width * 4;
    png_image_free(&source);
    nb_png_image_destroy(image);
    *image = decoded;
    return true;
}
