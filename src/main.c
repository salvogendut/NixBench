#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "window.h"
#include "window_renderer.h"

#ifndef NIXBENCH_VERSION
#define NIXBENCH_VERSION "development"
#endif

enum {
    NIXBENCH_WINDOW_WIDTH = 1024,
    NIXBENCH_WINDOW_HEIGHT = 640,
    NIXBENCH_WINDOW_MIN_WIDTH = 640,
    NIXBENCH_WINDOW_MIN_HEIGHT = 400,
    NIXBENCH_FIRST_WINDOW_X = 160,
    NIXBENCH_FIRST_WINDOW_Y = 110,
    NIXBENCH_FIRST_WINDOW_WIDTH = 480,
    NIXBENCH_FIRST_WINDOW_HEIGHT = 300,
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
                         const struct nb_window *first_window)
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

    if (!nb_window_render(renderer, first_window)) {
        return false;
    }

    return SDL_RenderPresent(renderer);
}

static bool process_pointer_event(SDL_Renderer *renderer,
                                  SDL_Event *event,
                                  struct nb_window *first_window)
{
    struct nb_rect bounds;
    int x;
    int y;

    if (!SDL_ConvertEventToRenderCoordinates(renderer, event) ||
        !renderer_bounds(renderer, &bounds)) {
        return false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        x = renderer_coordinate(event->motion.x);
        y = renderer_coordinate(event->motion.y);
        nb_window_pointer_move(first_window, x, y, bounds);
    } else {
        x = renderer_coordinate(event->button.x);
        y = renderer_coordinate(event->button.y);

        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event->button.button == SDL_BUTTON_LEFT) {
            const enum nb_window_hit hit =
                nb_window_pointer_down(first_window, x, y);

            if (hit != NB_WINDOW_HIT_NONE) {
                first_window->active = true;
            } else if (first_window->pointer_mode == NB_WINDOW_POINTER_IDLE) {
                first_window->active = false;
            }

            if (first_window->pointer_mode != NB_WINDOW_POINTER_IDLE &&
                !SDL_CaptureMouse(true)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Could not capture the pointer: %s",
                            SDL_GetError());
                nb_window_pointer_cancel(first_window);
            }
        } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
                   event->button.button == SDL_BUTTON_LEFT) {
            const enum nb_window_action action =
                nb_window_pointer_up(first_window, x, y);

            if (!SDL_CaptureMouse(false)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Could not release pointer capture: %s",
                            SDL_GetError());
            }

            if (action == NB_WINDOW_ACTION_CLOSE_REQUESTED) {
                first_window->visible = false;
                first_window->active = false;
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
    struct nb_window first_window;
    const struct nb_rect first_window_frame = {
        NIXBENCH_FIRST_WINDOW_X,
        NIXBENCH_FIRST_WINDOW_Y,
        NIXBENCH_FIRST_WINDOW_WIDTH,
        NIXBENCH_FIRST_WINDOW_HEIGHT
    };
    bool running = true;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }

    nb_window_init(&first_window, "NixBench window", first_window_frame);

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
        nb_window_clamp_to(&first_window, bounds);
    }

    if (!draw_desktop(renderer, &first_window)) {
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
                nb_window_pointer_cancel(&first_window);
                SDL_CaptureMouse(false);
            } else if (event.type == SDL_EVENT_MOUSE_MOTION ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (!process_pointer_event(renderer, &event, &first_window)) {
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
            nb_window_clamp_to(&first_window, bounds);
        }

        if (running && !draw_desktop(renderer, &first_window)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not redraw the desktop screen: %s",
                         SDL_GetError());
            exit_status = 1;
            break;
        }
    }

cleanup:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return exit_status;
}
