#define _POSIX_C_SOURCE 200809L

#include <png.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "png_loader.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

int main(void)
{
    char directory[] = "/tmp/nixbench-png-loader-XXXXXX";
    char path[512];
    char error[256] = {0};
    png_image output;
    const unsigned char pixels[] = {
        255, 0, 0, 255, 0, 255, 0, 128,
        0, 0, 255, 255, 255, 255, 255, 255
    };
    struct nb_png_image image = {0};

    CHECK(mkdtemp(directory) != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/test.png", directory) > 0);
    (void)memset(&output, 0, sizeof(output));
    output.version = PNG_IMAGE_VERSION;
    output.width = 2;
    output.height = 2;
    output.format = PNG_FORMAT_RGBA;
    CHECK(png_image_write_to_file(&output, path, false, pixels, 0, NULL));
    CHECK(nb_png_load(path, &image, error, sizeof(error)));
    CHECK(image.width == 2);
    CHECK(image.height == 2);
    CHECK(image.pitch == 8);
    CHECK(image.pixels != NULL && image.pixels[0] == 255);
    CHECK(image.pixels != NULL && image.pixels[5] == 255);
    nb_png_image_destroy(&image);
    CHECK(!nb_png_load("relative.png", &image, error, sizeof(error)));
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);

    if (failures != 0) {
        fprintf(stderr, "PNG loader tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("PNG loader tests: ok");
    return 0;
}
