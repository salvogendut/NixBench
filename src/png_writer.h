#ifndef NIXBENCH_PNG_WRITER_H
#define NIXBENCH_PNG_WRITER_H

#include <stdbool.h>
#include <stddef.h>

bool nb_png_write_xrgb8888(const char *path,
                           const void *pixels,
                           int width,
                           int height,
                           size_t stride,
                           char *error,
                           size_t error_capacity);

#endif
