#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "desktop_preview.h"
#include "menu.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static uint64_t frame_hash(const struct nb_host_frame *frame)
{
    const unsigned char *bytes = frame->pixels;
    const size_t size = frame->stride * (size_t)frame->height;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static size_t sampled_color_count(const struct nb_host_frame *frame)
{
    uint32_t colors[32];
    size_t count = 0;
    int y;

    for (y = 0; y < frame->height; y += 4) {
        const uint32_t *row = (const uint32_t *)
            ((const unsigned char *)frame->pixels +
             (size_t)y * frame->stride);
        int x;

        for (x = 0; x < frame->width; x += 4) {
            size_t index;

            for (index = 0; index < count; ++index) {
                if (colors[index] == row[x]) {
                    break;
                }
            }
            if (index == count && count < sizeof(colors) / sizeof(colors[0])) {
                colors[count++] = row[x];
            }
        }
    }
    return count;
}

static void test_preview_frame(void)
{
    struct nb_desktop_preview *preview;
    struct nb_host_frame first;
    struct nb_host_frame second;
    struct nb_host_output output = {1024, 640, 1024, 640, 60000};
    uint64_t first_hash;

    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
    preview = nb_desktop_preview_create();
    CHECK(preview != NULL);
    if (preview == NULL) {
        return;
    }
    memset(&first, 0, sizeof(first));
    CHECK(!nb_desktop_preview_render(preview, "12:34", 1, &first));
    CHECK(!nb_desktop_preview_set_output(NULL, &output));
    output.logical_width = 0;
    CHECK(!nb_desktop_preview_set_output(preview, &output));
    output.logical_width = 1024;
    output.logical_height = NB_MENU_BAR_HEIGHT;
    CHECK(!nb_desktop_preview_set_output(preview, &output));
    output.logical_height = 640;
    CHECK(nb_desktop_preview_set_output(preview, &output));
    CHECK(!nb_desktop_preview_render(preview, "12:34", 0, &first));
    CHECK(!nb_desktop_preview_render(preview, "", 1, &first));
    CHECK(nb_desktop_preview_render(preview, "12:34", 1, &first));
    CHECK(first.width == 1024);
    CHECK(first.height == 640);
    CHECK(first.format == NB_HOST_PIXEL_FORMAT_XRGB8888);
    CHECK(first.serial == 1);
    CHECK(nb_host_frame_is_valid(&first));
    CHECK(sampled_color_count(&first) >= 8);
    first_hash = frame_hash(&first);

    output.logical_width = 0;
    CHECK(!nb_desktop_preview_set_output(preview, &output));
    memset(&second, 0, sizeof(second));
    CHECK(nb_desktop_preview_render(preview, "12:34", 2, &second));
    CHECK(second.serial == 2);
    CHECK(frame_hash(&second) == first_hash);

    output = (struct nb_host_output){320, 200, 640, 400, 0};
    CHECK(nb_desktop_preview_set_output(preview, &output));
    CHECK(nb_desktop_preview_render(preview, "12:34", 3, &first));
    CHECK(nb_host_frame_is_valid(&first));
    CHECK(first.width == 640);
    CHECK(first.height == 400);
    nb_desktop_preview_destroy(preview);
    nb_desktop_preview_destroy(NULL);
    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
}

int main(void)
{
    test_preview_frame();
    if (failures != 0) {
        fprintf(stderr, "desktop preview tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("desktop preview tests: ok");
    return 0;
}
