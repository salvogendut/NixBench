#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "software_canvas.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void test_canvas_frame(void)
{
    struct nb_software_canvas *canvas = nb_software_canvas_create(3, 2);
    SDL_Renderer *renderer;
    struct nb_host_frame frame;
    int width = 0;
    int height = 0;
    int y;

    CHECK(canvas != NULL);
    renderer = nb_software_canvas_renderer(canvas);
    CHECK(renderer != NULL);
    CHECK(nb_software_canvas_get_size(canvas, &width, &height));
    CHECK(width == 3);
    CHECK(height == 2);
    CHECK(SDL_SetRenderDrawColor(renderer, 0x12, 0x34, 0x56, 0xff));
    CHECK(SDL_RenderClear(renderer));
    CHECK(nb_software_canvas_finish(canvas, 17, &frame));
    CHECK(frame.width == 3);
    CHECK(frame.height == 2);
    CHECK(frame.serial == 17);
    CHECK(frame.format == NB_HOST_PIXEL_FORMAT_XRGB8888);
    CHECK(frame.stride >= 3 * sizeof(uint32_t));

    for (y = 0; y < frame.height; ++y) {
        const uint32_t *row = (const uint32_t *)(
            (const unsigned char *)frame.pixels +
            ((size_t)y * frame.stride));
        int x;

        for (x = 0; x < frame.width; ++x) {
            CHECK((row[x] & UINT32_C(0x00ffffff)) ==
                  UINT32_C(0x00123456));
        }
    }
    nb_software_canvas_destroy(canvas);
}

static void test_invalid_arguments(void)
{
    struct nb_host_frame frame;
    int width;
    int height;

    CHECK(nb_software_canvas_create(0, 1) == NULL);
    CHECK(nb_software_canvas_create(1, 0) == NULL);
    CHECK(nb_software_canvas_renderer(NULL) == NULL);
    CHECK(!nb_software_canvas_get_size(NULL, &width, &height));
    CHECK(!nb_software_canvas_finish(NULL, 1, &frame));
    nb_software_canvas_destroy(NULL);
}

int main(void)
{
    test_canvas_frame();
    test_invalid_arguments();

    if (failures != 0) {
        fprintf(stderr, "software canvas tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("software canvas tests: ok");
    return 0;
}
