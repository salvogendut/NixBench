#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "host_sdl.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void drain_initial_events(struct nb_host *host)
{
    struct nb_host_event event;

    while (nb_host_poll_event(host, &event) ==
           NB_HOST_EVENT_STATUS_AVAILABLE) {
        CHECK(event.type == NB_HOST_EVENT_FOCUS_CHANGED ||
              event.type == NB_HOST_EVENT_OUTPUT_CHANGED);
    }
}

static void test_sdl_host(void)
{
    struct nb_host_sdl_options options;
    struct nb_host_output output;
    struct nb_host_frame frame;
    struct nb_host_event event;
    SDL_Event quit_event;
    uint32_t *pixels;
    size_t pixel_count;
    size_t index;
    bool completed = false;

    nb_host_sdl_options_init(&options);
    options.title = "NixBench SDL host test";
    options.window_width = 64;
    options.window_height = 48;
    options.minimum_width = 0;
    options.minimum_height = 0;
    options.resizable = false;
    options.high_pixel_density = true;

    {
        struct nb_host *host = nb_host_sdl_create(&options);

        if (host == NULL) {
            fprintf(stderr,
                    "SDL host creation failed: %s\n",
                    nb_host_sdl_creation_error());
            ++failures;
            return;
        }
        CHECK(nb_host_get_state(host) == NB_HOST_STATE_ACTIVE);
        CHECK(nb_host_get_output(host, &output));
        CHECK(output.pixel_width > 0);
        CHECK(output.pixel_height > 0);
        drain_initial_events(host);

        pixel_count = (size_t)output.pixel_width *
                      (size_t)output.pixel_height;
        pixels = malloc(pixel_count * sizeof(*pixels));
        CHECK(pixels != NULL);
        if (pixels == NULL) {
            nb_host_destroy(host);
            return;
        }
        for (index = 0; index < pixel_count; ++index) {
            pixels[index] = UINT32_C(0xff18364c);
        }
        memset(&frame, 0, sizeof(frame));
        frame.pixels = pixels;
        frame.width = output.pixel_width;
        frame.height = output.pixel_height;
        frame.stride = (size_t)output.pixel_width * sizeof(*pixels);
        frame.format = NB_HOST_PIXEL_FORMAT_XRGB8888;
        frame.serial = 1;
        CHECK(nb_host_present(host, &frame) == NB_HOST_RESULT_OK);
        memset(pixels, 0, pixel_count * sizeof(*pixels));

        while (nb_host_poll_event(host, &event) ==
               NB_HOST_EVENT_STATUS_AVAILABLE) {
            if (event.type == NB_HOST_EVENT_FRAME_COMPLETE) {
                CHECK(event.data.frame_complete.frame_serial == 1);
                completed = true;
            }
        }
        CHECK(completed);
        CHECK(nb_host_present(host, &frame) ==
              NB_HOST_RESULT_INVALID_STATE);
        CHECK(nb_host_complete_console_release(host) ==
              NB_HOST_RESULT_UNSUPPORTED);
        CHECK(nb_host_complete_console_acquire(host) ==
              NB_HOST_RESULT_UNSUPPORTED);

        memset(&quit_event, 0, sizeof(quit_event));
        quit_event.type = SDL_EVENT_QUIT;
        quit_event.common.timestamp = SDL_GetTicksNS();
        CHECK(SDL_PushEvent(&quit_event));
        CHECK(nb_host_wait_event(host, 100, &event) ==
              NB_HOST_EVENT_STATUS_AVAILABLE);
        CHECK(event.type == NB_HOST_EVENT_QUIT);

        free(pixels);
        nb_host_destroy(host);
    }
}

int main(void)
{
    struct nb_host_sdl_options options;

    nb_host_sdl_options_init(NULL);
    CHECK(nb_host_sdl_create(NULL) == NULL);
    CHECK(nb_host_sdl_creation_error()[0] != '\0');
    nb_host_sdl_options_init(&options);
    CHECK(options.window_width > 0);
    CHECK(options.window_height > 0);
    test_sdl_host();

    if (failures != 0) {
        fprintf(stderr, "SDL host tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("SDL host tests: ok");
    return 0;
}
