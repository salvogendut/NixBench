#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "application.h"
#include "nixinfo.h"
#include "nixinfo_renderer.h"
#include "shell.h"
#include "shell_renderer.h"
#include "window_renderer.h"

#ifndef NIXBENCH_VERSION
#define NIXBENCH_VERSION "development"
#endif

enum {
    NIXBENCH_WINDOW_WIDTH = 1024,
    NIXBENCH_WINDOW_HEIGHT = 640,
    NIXBENCH_WINDOW_MIN_WIDTH = 640,
    NIXBENCH_WINDOW_MIN_HEIGHT = 400,
    NIXBENCH_ABOUT_WINDOW_X = 360,
    NIXBENCH_ABOUT_WINDOW_Y = 190,
    NIXBENCH_ABOUT_WINDOW_WIDTH = 430,
    NIXBENCH_ABOUT_WINDOW_HEIGHT = 230,
    NIXBENCH_DESKTOP_RED = 24,
    NIXBENCH_DESKTOP_GREEN = 54,
    NIXBENCH_DESKTOP_BLUE = 76,
    NIXBENCH_CLOCK_CAPACITY = 6,
    NIXBENCH_CLOCK_FALLBACK_WAIT_MS = 1000
};

#define NIXBENCH_MENU_SOURCE_DESKTOP UINT64_C(1)
#define NIXBENCH_MENU_SOURCE_ABOUT UINT64_MAX

enum {
    NIXBENCH_DESKTOP_COMMAND_ABOUT = 1,
    NIXBENCH_DESKTOP_COMMAND_OPEN_NIXINFO,
    NIXBENCH_DESKTOP_COMMAND_QUIT
};

enum {
    NIXBENCH_ABOUT_COMMAND_CLOSE = 1
};

static const struct nb_menu_item_spec desktop_items[] = {
    {"About NixBench", NIXBENCH_DESKTOP_COMMAND_ABOUT,
     NB_MENU_ITEM_COMMAND, true},
    {"Open NixInfo", NIXBENCH_DESKTOP_COMMAND_OPEN_NIXINFO,
     NB_MENU_ITEM_COMMAND, true},
    {NULL, NB_MENU_COMMAND_NONE, NB_MENU_ITEM_SEPARATOR, false},
    {"Quit NixBench", NIXBENCH_DESKTOP_COMMAND_QUIT,
     NB_MENU_ITEM_COMMAND, true}
};

static const struct nb_menu_spec desktop_menus[] = {
    {"NixBench", desktop_items,
     sizeof(desktop_items) / sizeof(desktop_items[0])}
};

static const struct nb_menu_model desktop_menu_model = {
    desktop_menus,
    sizeof(desktop_menus) / sizeof(desktop_menus[0])
};

static const struct nb_menu_item_spec about_items[] = {
    {"Close About", NIXBENCH_ABOUT_COMMAND_CLOSE,
     NB_MENU_ITEM_COMMAND, true}
};

static const struct nb_menu_spec about_menus[] = {
    {"NixBench", about_items,
     sizeof(about_items) / sizeof(about_items[0])}
};

static const struct nb_menu_model about_menu_model = {
    about_menus,
    sizeof(about_menus) / sizeof(about_menus[0])
};

struct runtime {
    struct nb_shell shell;
    struct nb_application_host applications;
    struct nb_nixinfo nixinfo;
    nb_application_id nixinfo_application;
    nb_window_id about_window;
    bool capture_active;
    bool running;
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

static void format_clock(char clock_text[NIXBENCH_CLOCK_CAPACITY])
{
    const time_t now = time(NULL);
    const struct tm *local_time = localtime(&now);

    if (local_time == NULL ||
        strftime(clock_text, NIXBENCH_CLOCK_CAPACITY, "%H:%M", local_time) ==
            0) {
        memcpy(clock_text, "--:--", NIXBENCH_CLOCK_CAPACITY);
    }
}

static Sint32 clock_refresh_timeout(void)
{
    const time_t now = time(NULL);
    const struct tm *local_time = localtime(&now);
    int seconds_until_next_minute;

    if (local_time == NULL) {
        return NIXBENCH_CLOCK_FALLBACK_WAIT_MS;
    }

    seconds_until_next_minute = 60 - local_time->tm_sec;
    if (seconds_until_next_minute < 1 || seconds_until_next_minute > 60) {
        return NIXBENCH_CLOCK_FALLBACK_WAIT_MS;
    }
    return (Sint32)(seconds_until_next_minute * 1000);
}

static bool render_window_content(SDL_Renderer *renderer,
                                  nb_window_id id,
                                  const struct nb_window *window,
                                  struct nb_rect content_rect,
                                  void *context)
{
    struct runtime *runtime = context;

    if (nb_application_host_window_owner(&runtime->applications, id) ==
        runtime->nixinfo_application) {
        return nb_nixinfo_render_content(renderer,
                                         id,
                                         window,
                                         content_rect,
                                         &runtime->nixinfo);
    }
    return nb_window_render_default_content(renderer, window);
}

static bool draw_shell(SDL_Renderer *renderer,
                       struct runtime *runtime)
{
    struct nb_rect viewport;
    char clock_text[NIXBENCH_CLOCK_CAPACITY];

    if (!renderer_bounds(renderer, &viewport)) {
        return false;
    }
    format_clock(clock_text);

    if (!SDL_SetRenderDrawColor(renderer,
                                NIXBENCH_DESKTOP_RED,
                                NIXBENCH_DESKTOP_GREEN,
                                NIXBENCH_DESKTOP_BLUE,
                                SDL_ALPHA_OPAQUE) ||
        !SDL_RenderClear(renderer) ||
        !nb_shell_render_with_content(renderer,
                                      &runtime->shell,
                                      viewport,
                                      clock_text,
                                      render_window_content,
                                      runtime)) {
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

static void begin_pointer_capture(bool *capture_active)
{
    if (*capture_active) {
        return;
    }

    if (SDL_CaptureMouse(true)) {
        *capture_active = true;
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Explicit pointer capture is unavailable; "
                    "the interaction will cancel if the pointer leaves "
                    "the host: %s",
                    SDL_GetError());
    }
}

static bool sync_application_focus(struct runtime *runtime)
{
    if (nb_application_host_sync_focus(&runtime->applications)) {
        return true;
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Could not synchronize application focus");
    return false;
}

static bool show_about_window(struct runtime *runtime)
{
    const struct nb_rect frame = {
        NIXBENCH_ABOUT_WINDOW_X,
        NIXBENCH_ABOUT_WINDOW_Y,
        NIXBENCH_ABOUT_WINDOW_WIDTH,
        NIXBENCH_ABOUT_WINDOW_HEIGHT
    };

    if (runtime->about_window != NB_WINDOW_ID_NONE &&
        nb_desktop_find_window(&runtime->shell.desktop,
                               runtime->about_window) != NULL) {
        return nb_shell_activate_window(&runtime->shell,
                                        runtime->about_window) &&
               sync_application_focus(runtime);
    }

    runtime->about_window = nb_shell_open_window(
        &runtime->shell,
        "About NixBench",
        frame,
        NIXBENCH_MENU_SOURCE_ABOUT,
        &about_menu_model);
    if (runtime->about_window == NB_WINDOW_ID_NONE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not open the About NixBench window");
        return false;
    }
    return sync_application_focus(runtime);
}

static bool close_about_window(struct runtime *runtime,
                               nb_window_id window)
{
    if (window == NB_WINDOW_ID_NONE ||
        window != runtime->about_window) {
        return false;
    }
    if (!nb_shell_destroy_window(&runtime->shell, window)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not close the About NixBench window");
        return false;
    }
    runtime->about_window = NB_WINDOW_ID_NONE;
    return sync_application_focus(runtime);
}

static bool open_nixinfo(struct runtime *runtime)
{
    if (!nb_application_host_is_running(&runtime->applications,
                                        runtime->nixinfo_application)) {
        return nb_application_host_start(&runtime->applications,
                                         runtime->nixinfo_application);
    }

    if (nb_application_host_window_count(&runtime->applications,
                                         runtime->nixinfo_application) == 0) {
        return nb_application_host_restart(&runtime->applications,
                                           runtime->nixinfo_application);
    }

    {
        const size_t last =
            nb_application_host_window_count(&runtime->applications,
                                              runtime->nixinfo_application) -
            1;
        const nb_window_id window =
            nb_application_host_window_at(&runtime->applications,
                                          runtime->nixinfo_application,
                                          last);

        return nb_shell_activate_window(&runtime->shell, window) &&
               sync_application_focus(runtime);
    }
}

static bool apply_shell_action(struct runtime *runtime,
                               struct nb_shell_action action)
{
    const enum nb_application_dispatch_result dispatch =
        nb_application_host_dispatch_shell_action(&runtime->applications,
                                                   action);

    if (dispatch == NB_APPLICATION_DISPATCH_HANDLED) {
        return true;
    }
    if (dispatch == NB_APPLICATION_DISPATCH_ERROR) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Application rejected shell action %d",
                     (int)action.type);
        return false;
    }

    if (action.type == NB_SHELL_ACTION_NONE) {
        return true;
    }
    if (action.type == NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED) {
        if (action.window == runtime->about_window) {
            return close_about_window(runtime, action.window);
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Ignored close request for unowned window %llu",
                    (unsigned long long)action.window);
        return true;
    }
    if (action.type != NB_SHELL_ACTION_MENU_COMMAND) {
        return false;
    }

    if (action.menu_source == NIXBENCH_MENU_SOURCE_DESKTOP) {
        if (action.menu_command == NIXBENCH_DESKTOP_COMMAND_ABOUT) {
            return show_about_window(runtime);
        }
        if (action.menu_command ==
            NIXBENCH_DESKTOP_COMMAND_OPEN_NIXINFO) {
            return open_nixinfo(runtime);
        } else if (action.menu_command == NIXBENCH_DESKTOP_COMMAND_QUIT) {
            runtime->running = false;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Ignored unknown desktop menu command %u",
                        (unsigned int)action.menu_command);
        }
        return true;
    }

    if (action.menu_source == NIXBENCH_MENU_SOURCE_ABOUT) {
        if (action.menu_command == NIXBENCH_ABOUT_COMMAND_CLOSE &&
            action.window == runtime->about_window) {
            return close_about_window(runtime, action.window);
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Ignored About menu command %u",
                    (unsigned int)action.menu_command);
        return true;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Ignored menu command %u from unknown source %llu",
                (unsigned int)action.menu_command,
                (unsigned long long)action.menu_source);
    return true;
}

static bool process_pointer_event(SDL_Renderer *renderer,
                                  SDL_Event *event,
                                  struct runtime *runtime)
{
    struct nb_rect viewport;
    int x;
    int y;

    if (!SDL_ConvertEventToRenderCoordinates(renderer, event) ||
        !renderer_bounds(renderer, &viewport)) {
        return false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (!nb_shell_wants_pointer_motion(&runtime->shell)) {
            return true;
        }
        x = renderer_coordinate(event->motion.x);
        y = renderer_coordinate(event->motion.y);
        nb_shell_pointer_move(&runtime->shell, x, y, viewport);
        return true;
    }

    x = renderer_coordinate(event->button.x);
    y = renderer_coordinate(event->button.y);
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_LEFT) {
        const bool was_interacting =
            nb_shell_has_pointer_interaction(&runtime->shell);

        nb_shell_pointer_down(&runtime->shell, x, y, viewport);
        if (!sync_application_focus(runtime)) {
            return false;
        }
        if (!was_interacting &&
            nb_shell_has_pointer_interaction(&runtime->shell)) {
            begin_pointer_capture(&runtime->capture_active);
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
               event->button.button == SDL_BUTTON_LEFT) {
        const struct nb_shell_action action =
            nb_shell_pointer_up(&runtime->shell, x, y, viewport);

        release_pointer_capture(&runtime->capture_active);
        return apply_shell_action(runtime, action);
    }

    return true;
}

static bool menu_key_for(SDL_Keycode keycode, enum nb_menu_key *menu_key)
{
    if (keycode == SDLK_F10) {
        *menu_key = NB_MENU_KEY_TOGGLE;
    } else if (keycode == SDLK_DOWN) {
        *menu_key = NB_MENU_KEY_NEXT_ITEM;
    } else if (keycode == SDLK_UP) {
        *menu_key = NB_MENU_KEY_PREVIOUS_ITEM;
    } else if (keycode == SDLK_RIGHT) {
        *menu_key = NB_MENU_KEY_NEXT_MENU;
    } else if (keycode == SDLK_LEFT) {
        *menu_key = NB_MENU_KEY_PREVIOUS_MENU;
    } else if (keycode == SDLK_RETURN || keycode == SDLK_KP_ENTER) {
        *menu_key = NB_MENU_KEY_ACTIVATE;
    } else if (keycode == SDLK_ESCAPE) {
        *menu_key = NB_MENU_KEY_DISMISS;
    } else {
        return false;
    }
    return true;
}

static bool process_key_event(const SDL_KeyboardEvent *event,
                              struct runtime *runtime)
{
    enum nb_menu_key menu_key;
    const bool menu_context = nb_menu_is_open(&runtime->shell.menu) ||
                              event->key == SDLK_F10;

    if (event->repeat &&
        (event->key == SDLK_ESCAPE || event->key == SDLK_F10 ||
         event->key == SDLK_RETURN || event->key == SDLK_KP_ENTER)) {
        return true;
    }

    if (menu_context && menu_key_for(event->key, &menu_key)) {
        const struct nb_shell_action action =
            nb_shell_menu_key_press(&runtime->shell, menu_key);

        if (!nb_shell_has_pointer_interaction(&runtime->shell)) {
            release_pointer_capture(&runtime->capture_active);
        }
        return apply_shell_action(runtime, action);
    }

    if (event->key == SDLK_ESCAPE) {
        runtime->running = false;
        return true;
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
    struct runtime runtime;
    struct nb_application_spec nixinfo_spec;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }

    (void)memset(&runtime, 0, sizeof(runtime));
    runtime.about_window = NB_WINDOW_ID_NONE;
    runtime.running = true;
    nb_shell_init(&runtime.shell,
                  NIXBENCH_MENU_SOURCE_DESKTOP,
                  &desktop_menu_model);
    if (!nb_application_host_init(&runtime.applications,
                                  &runtime.shell)) {
        fputs("Could not initialize the application host.\n", stderr);
        return 1;
    }
    nb_nixinfo_init(&runtime.nixinfo, NULL, NULL);
    nixinfo_spec = nb_nixinfo_application_spec(&runtime.nixinfo);
    runtime.nixinfo_application =
        nb_application_host_register(&runtime.applications,
                                     &nixinfo_spec);
    if (runtime.nixinfo_application == NB_APPLICATION_ID_NONE ||
        !nb_application_host_start(&runtime.applications,
                                   runtime.nixinfo_application)) {
        fputs("Could not start the NixInfo application.\n", stderr);
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

    if (!SDL_CreateWindowAndRenderer("NixBench Desktop",
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
        struct nb_rect viewport;

        if (!renderer_bounds(renderer, &viewport)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not query the initial desktop size: %s",
                         SDL_GetError());
            exit_status = 1;
            goto cleanup;
        }
        nb_shell_clamp_windows(&runtime.shell, viewport);
    }

    if (!draw_shell(renderer, &runtime)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not draw the desktop screen: %s",
                     SDL_GetError());
        exit_status = 1;
        goto cleanup;
    }
    if (options.exit_after_first_frame) {
        goto cleanup;
    }

    while (runtime.running) {
        const bool received_event =
            SDL_WaitEventTimeout(&event, clock_refresh_timeout());

        if (received_event) {
            do {
                if (event.type == SDL_EVENT_QUIT ||
                    event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    runtime.running = false;
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (!process_key_event(&event.key, &runtime)) {
                        exit_status = 1;
                        runtime.running = false;
                    }
                } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    release_pointer_capture(&runtime.capture_active);
                    nb_shell_pointer_cancel(&runtime.shell);
                } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE &&
                           !runtime.capture_active &&
                           nb_shell_has_pointer_interaction(
                               &runtime.shell)) {
                    nb_shell_pointer_cancel(&runtime.shell);
                } else if (event.type == SDL_EVENT_MOUSE_MOTION ||
                           event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                           event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (!process_pointer_event(renderer,
                                               &event,
                                               &runtime)) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "Could not process pointer input or "
                                     "application action: %s",
                                     SDL_GetError());
                        exit_status = 1;
                        runtime.running = false;
                    }
                }
            } while (runtime.running && SDL_PollEvent(&event));
        }

        if (runtime.running) {
            struct nb_rect viewport;

            if (!renderer_bounds(renderer, &viewport)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not query the desktop size: %s",
                             SDL_GetError());
                exit_status = 1;
                break;
            }
            nb_shell_clamp_windows(&runtime.shell, viewport);
        }

        if (runtime.running && !draw_shell(renderer, &runtime)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not redraw the desktop screen: %s",
                         SDL_GetError());
            exit_status = 1;
            break;
        }
    }

cleanup:
    release_pointer_capture(&runtime.capture_active);
    if (nb_application_host_is_running(&runtime.applications,
                                       runtime.nixinfo_application) &&
        !nb_application_host_stop(&runtime.applications,
                                  runtime.nixinfo_application)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not stop NixInfo cleanly");
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return exit_status;
}
