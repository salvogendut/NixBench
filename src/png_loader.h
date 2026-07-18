#ifndef NIXBENCH_PNG_LOADER_H
#define NIXBENCH_PNG_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_PNG_DIMENSION_LIMIT = 8192,
    NB_PNG_DECODE_BYTE_LIMIT = 64 * 1024 * 1024
};

struct nb_png_image {
    uint8_t *pixels;
    int width;
    int height;
    size_t pitch;
};

void nb_png_image_destroy(struct nb_png_image *image);
bool nb_png_load(const char *path,
                 struct nb_png_image *image,
                 char *error,
                 size_t error_capacity);

#endif
