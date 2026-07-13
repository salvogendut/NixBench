#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "desktop.h"
#include "desktop_renderer.h"

#ifndef NIXBENCH_VERSION
#define NIXBENCH_VERSION "development"
#endif

enum {
    NIXBENCH_WINDOW_WIDTH = 1024,
    NIXBENCH_WINDOW_HEIGHT = 640,
    NIXBENCH_WINDOW_MIN_WIDTH = 640,
    NIXBENCH_WINDOW_MIN_HEIGHT = 400,
    NIXBENCH_FIRST_WINDOW_X = 120,
    NIXBENCH_FIRST_WINDOW_Y = 80,
    NIXBENCH_FIRST_WINDOW_WIDTH = 500,
    NIXBENCH_FIRST_WINDOW_HEIGHT = 310,
    NIXBENCH_SECOND_WINDOW_X = 430,
    NIXBENCH_SECOND_WINDOW_Y = 230,
    NIXBENCH_SECOND_WINDOW_WIDTH = 420,
    NIXBENCH_SECOND_WINDOW_HEIGHT = 250,
    NIXBENCH_DESKTOP_RED = 24,
    NIXBENCH_DESKTOP_GREEN = 54,
    NIXBENCH_DESKTOP_BLUE = 76
};

struct options {
    bool fullscreen;
    bool exit_after_first_frame;
};

static void print_usage(const char *program_name)
{
    printf("Usage: %s [OPTION]\n", program_name);
    puts("Open the initial NixBench desktop screen.\n");
    puts("  --fullscreen              occupy the current display");
    puts("  --exit-after-first-frame  render once and exit (smoke test)");
    puts("  --help                    show this help");
    puts("  --version                 show the NixBench version");
}

static bool parse_options(int argc,
                          char *argv[],
                          struct options *options,
                          int *exit_status)
{
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--fullscreen") == 0) {
            options->fullscreen = true;
        } else if (strcmp(argv[index], "--exit-after-first-frame") == 0) {
            options->exit_after_first_frame = true;
        } else if (strcmp(argv[index], "--help") == 0) {
            print_usage(argv[0]);
            return false;
        } else if (strcmp(argv[index], "--version") == 0) {
            printf("NixBench %s\n", NIXBENCH_VERSION);
            return false;
        } else {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[index]);
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            *exit_status = 2;
            return false;
        }
    }

    return true;
}

static int renderer_coordinate(float coordinate)
{
    const int truncated = (int)coordinate;

    return coordinate < (float)truncated ? truncated - 1 : truncated;
}

static bool renderer_bounds(SDL_Renderer *renderer, struct nb_rect *bounds)
{
    bounds->x = 0;
    bounds->y = 0;
    return SDL_GetCurrentRenderOutputSize(renderer,
                                          &bounds->width,
                                          &bounds->height);
}

static bool draw_desktop(SDL_Renderer *renderer,
                         const struct nb_desktop *desktop)
{
    if (!SDL_SetRenderDrawColor(renderer,
                                NIXBENCH_DESKTOP_RED,
                                NIXBENCH_DESKTOP_GREEN,
                                NIXBENCH_DESKTOP_BLUE,
                                SDL_ALPHA_OPAQUE)) {
        return false;
    }

    if (!SDL_RenderClear(renderer)) {
        return false;
    }

    if (!nb_desktop_render(renderer, desktop)) {
        return false;
    }

    return SDL_RenderPresent(renderer);
}

static void release_pointer_capture(bool *capture_active)
{
    if (!*capture_active) {
        return;
    }

    if (!SDL_CaptureMouse(false)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not release pointer capture: %s",
                    SDL_GetError());
    }
    *capture_active = false;
}

static bool process_pointer_event(SDL_Renderer *renderer,
                                  SDL_Event *event,
                                  struct nb_desktop *desktop,
                                  bool *capture_active)
{
    int x;
    int y;

    if (!SDL_ConvertEventToRenderCoordinates(renderer, event)) {
        return false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        struct nb_rect bounds;

        if (!nb_desktop_has_pointer_interaction(desktop)) {
            return true;
        }
        if (!renderer_bounds(renderer, &bounds)) {
            return false;
        }

        x = renderer_coordinate(event->motion.x);
        y = renderer_coordinate(event->motion.y);
        nb_desktop_pointer_move(desktop, x, y, bounds);
    } else {
        x = renderer_coordinate(event->button.x);
        y = renderer_coordinate(event->button.y);

        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event->button.button == SDL_BUTTON_LEFT) {
            const bool was_interacting =
                nb_desktop_has_pointer_interaction(desktop);

            nb_desktop_pointer_down(desktop, x, y);
            if (!was_interacting &&
                nb_desktop_has_pointer_interaction(desktop)) {
                if (SDL_CaptureMouse(true)) {
                    *capture_active = true;
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Explicit pointer capture is unavailable; "
                                "the interaction will cancel if the pointer "
                                "leaves the host: %s",
                                SDL_GetError());
                }
            }
        } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
                   event->button.button == SDL_BUTTON_LEFT) {
            const struct nb_desktop_action action =
                nb_desktop_pointer_up(desktop, x, y);

            release_pointer_capture(capture_active);

            if (action.type == NB_WINDOW_ACTION_CLOSE_REQUESTED) {
                nb_desktop_destroy_window(desktop, action.window);
            }
        }
    }

    return true;
}

int main(int argc, char *argv[])
{
    struct options options = {false, false};
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
                                   SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Event event;
    struct nb_desktop desktop;
    const struct nb_rect first_window_frame = {
        NIXBENCH_FIRST_WINDOW_X,
        NIXBENCH_FIRST_WINDOW_Y,
        NIXBENCH_FIRST_WINDOW_WIDTH,
        NIXBENCH_FIRST_WINDOW_HEIGHT
    };
    const struct nb_rect second_window_frame = {
        NIXBENCH_SECOND_WINDOW_X,
        NIXBENCH_SECOND_WINDOW_Y,
        NIXBENCH_SECOND_WINDOW_WIDTH,
        NIXBENCH_SECOND_WINDOW_HEIGHT
    };
    bool capture_active = false;
    bool running = true;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }

    nb_desktop_init(&desktop);
    if (nb_desktop_open_window(&desktop, "NixBench", first_window_frame) ==
            NB_WINDOW_ID_NONE ||
        nb_desktop_open_window(&desktop,
                               "About NixBench",
                               second_window_frame) == NB_WINDOW_ID_NONE) {
        fputs("Could not create the initial desktop windows.\n", stderr);
        return 1;
    }

    if (options.fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not initialize SDL video: %s",
                     SDL_GetError());
        return 1;
    }

    if (!SDL_CreateWindowAndRenderer("NixBench",
                                     NIXBENCH_WINDOW_WIDTH,
                                     NIXBENCH_WINDOW_HEIGHT,
                                     window_flags,
                                     &window,
                                     &renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not create the desktop screen: %s",
                     SDL_GetError());
        exit_status = 1;
        goto cleanup;
    }

    if (!SDL_SetWindowMinimumSize(window,
                                  NIXBENCH_WINDOW_MIN_WIDTH,
                                  NIXBENCH_WINDOW_MIN_HEIGHT)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not set the minimum window size: %s",
                    SDL_GetError());
    }

    {
        struct nb_rect bounds;

        if (!renderer_bounds(renderer, &bounds)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not query the initial desktop size: %s",
                         SDL_GetError());
            exit_status = 1;
            goto cleanup;
        }
        nb_desktop_clamp_windows(&desktop, bounds);
    }

    if (!draw_desktop(renderer, &desktop)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not draw the desktop screen: %s",
                     SDL_GetError());
        exit_status = 1;
        goto cleanup;
    }

    if (options.exit_after_first_frame) {
        goto cleanup;
    }

    while (running) {
        if (!SDL_WaitEvent(&event)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not wait for an event: %s",
                         SDL_GetError());
            exit_status = 1;
            break;
        }

        do {
            if (event.type == SDL_EVENT_QUIT ||
                event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       event.key.key == SDLK_ESCAPE) {
                running = false;
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                release_pointer_capture(&capture_active);
                nb_desktop_pointer_cancel(&desktop);
            } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE &&
                       !capture_active &&
                       nb_desktop_has_pointer_interaction(&desktop)) {
                nb_desktop_pointer_cancel(&desktop);
            } else if (event.type == SDL_EVENT_MOUSE_MOTION ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (!process_pointer_event(renderer,
                                           &event,
                                           &desktop,
                                           &capture_active)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "Could not process pointer input: %s",
                                 SDL_GetError());
                    exit_status = 1;
                    running = false;
                }
            }
        } while (SDL_PollEvent(&event));

        if (running) {
            struct nb_rect bounds;

            if (!renderer_bounds(renderer, &bounds)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not query the desktop size: %s",
                             SDL_GetError());
                exit_status = 1;
                break;
            }
            nb_desktop_clamp_windows(&desktop, bounds);
        }

        if (running && !draw_desktop(renderer, &desktop)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not redraw the desktop screen: %s",
                         SDL_GetError());
            exit_status = 1;
            break;
        }
    }

cleanup:
    release_pointer_capture(&capture_active);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return exit_status;
}
