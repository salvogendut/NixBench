#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "shell.h"
#include "shell_renderer.h"

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
    NIXBENCH_DESKTOP_BLUE = 76,
    NIXBENCH_CLOCK_CAPACITY = 6,
    NIXBENCH_CLOCK_FALLBACK_WAIT_MS = 1000
};

enum {
    NIXBENCH_MENU_SOURCE_DESKTOP = 1,
    NIXBENCH_MENU_SOURCE_DEMO_APPLICATION = 2
};

enum {
    NIXBENCH_DESKTOP_COMMAND_ABOUT = 1,
    NIXBENCH_DESKTOP_COMMAND_QUIT = 2
};

enum {
    NIXBENCH_DEMO_COMMAND_INFO = 1,
    NIXBENCH_DEMO_COMMAND_CLOSE_ACTIVE = 2
};

static const struct nb_menu_item_spec desktop_items[] = {
    {"About NixBench", NIXBENCH_DESKTOP_COMMAND_ABOUT,
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

static const struct nb_menu_item_spec project_items[] = {
    {"Application Info", NIXBENCH_DEMO_COMMAND_INFO,
     NB_MENU_ITEM_COMMAND, true}
};

static const struct nb_menu_item_spec window_items[] = {
    {"Close Active Window", NIXBENCH_DEMO_COMMAND_CLOSE_ACTIVE,
     NB_MENU_ITEM_COMMAND, true}
};

static const struct nb_menu_spec application_menus[] = {
    {"Project", project_items,
     sizeof(project_items) / sizeof(project_items[0])},
    {"Window", window_items,
     sizeof(window_items) / sizeof(window_items[0])}
};

static const struct nb_menu_model application_menu_model = {
    application_menus,
    sizeof(application_menus) / sizeof(application_menus[0])
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

static bool draw_shell(SDL_Renderer *renderer, const struct nb_shell *shell)
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
        !nb_shell_render(renderer, shell, viewport, clock_text)) {
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

static void show_about_window(struct nb_shell *shell,
                              nb_window_id *about_window)
{
    const struct nb_rect frame = {
        NIXBENCH_SECOND_WINDOW_X,
        NIXBENCH_SECOND_WINDOW_Y,
        NIXBENCH_SECOND_WINDOW_WIDTH,
        NIXBENCH_SECOND_WINDOW_HEIGHT
    };

    if (*about_window != NB_WINDOW_ID_NONE &&
        nb_desktop_find_window(&shell->desktop, *about_window) != NULL) {
        nb_shell_activate_window(shell, *about_window);
        return;
    }

    *about_window = nb_shell_open_window(
        shell,
        "About NixBench",
        frame,
        NIXBENCH_MENU_SOURCE_DEMO_APPLICATION,
        &application_menu_model);
    if (*about_window == NB_WINDOW_ID_NONE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not open the About NixBench window");
    }
}

static void close_window(struct nb_shell *shell,
                         nb_window_id window,
                         nb_window_id *about_window)
{
    if (window == NB_WINDOW_ID_NONE) {
        return;
    }
    if (!nb_shell_destroy_window(shell, window)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not close internal window");
        return;
    }
    if (window == *about_window) {
        *about_window = NB_WINDOW_ID_NONE;
    }
}

static void apply_shell_action(struct nb_shell *shell,
                               struct nb_shell_action action,
                               nb_window_id *about_window,
                               bool *running)
{
    if (action.type == NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED) {
        close_window(shell, action.window, about_window);
        return;
    }
    if (action.type != NB_SHELL_ACTION_MENU_COMMAND) {
        return;
    }

    if (action.menu_source == NIXBENCH_MENU_SOURCE_DESKTOP) {
        if (action.menu_command == NIXBENCH_DESKTOP_COMMAND_ABOUT) {
            show_about_window(shell, about_window);
        } else if (action.menu_command == NIXBENCH_DESKTOP_COMMAND_QUIT) {
            *running = false;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Ignored unknown desktop menu command %u",
                        (unsigned int)action.menu_command);
        }
        return;
    }

    if (action.menu_source == NIXBENCH_MENU_SOURCE_DEMO_APPLICATION) {
        if (action.menu_command == NIXBENCH_DEMO_COMMAND_INFO) {
            show_about_window(shell, about_window);
        } else if (action.menu_command ==
                   NIXBENCH_DEMO_COMMAND_CLOSE_ACTIVE) {
            close_window(shell, action.window, about_window);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Ignored unknown demo menu command %u",
                        (unsigned int)action.menu_command);
        }
        return;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Ignored menu command %u from unknown source %llu",
                (unsigned int)action.menu_command,
                (unsigned long long)action.menu_source);
}

static bool process_pointer_event(SDL_Renderer *renderer,
                                  SDL_Event *event,
                                  struct nb_shell *shell,
                                  bool *capture_active,
                                  nb_window_id *about_window,
                                  bool *running)
{
    struct nb_rect viewport;
    int x;
    int y;

    if (!SDL_ConvertEventToRenderCoordinates(renderer, event) ||
        !renderer_bounds(renderer, &viewport)) {
        return false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (!nb_shell_wants_pointer_motion(shell)) {
            return true;
        }
        x = renderer_coordinate(event->motion.x);
        y = renderer_coordinate(event->motion.y);
        nb_shell_pointer_move(shell, x, y, viewport);
        return true;
    }

    x = renderer_coordinate(event->button.x);
    y = renderer_coordinate(event->button.y);
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_LEFT) {
        const bool was_interacting =
            nb_shell_has_pointer_interaction(shell);

        nb_shell_pointer_down(shell, x, y, viewport);
        if (!was_interacting && nb_shell_has_pointer_interaction(shell)) {
            begin_pointer_capture(capture_active);
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
               event->button.button == SDL_BUTTON_LEFT) {
        const struct nb_shell_action action =
            nb_shell_pointer_up(shell, x, y, viewport);

        release_pointer_capture(capture_active);
        apply_shell_action(shell, action, about_window, running);
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
                              struct nb_shell *shell,
                              bool *capture_active,
                              nb_window_id *about_window,
                              bool *running)
{
    enum nb_menu_key menu_key;
    const bool menu_context = nb_menu_is_open(&shell->menu) ||
                              event->key == SDLK_F10;

    if (event->repeat &&
        (event->key == SDLK_ESCAPE || event->key == SDLK_F10 ||
         event->key == SDLK_RETURN || event->key == SDLK_KP_ENTER)) {
        return true;
    }

    if (menu_context && menu_key_for(event->key, &menu_key)) {
        const struct nb_shell_action action =
            nb_shell_menu_key_press(shell, menu_key);

        if (!nb_shell_has_pointer_interaction(shell)) {
            release_pointer_capture(capture_active);
        }
        apply_shell_action(shell, action, about_window, running);
        return true;
    }

    if (event->key == SDLK_ESCAPE) {
        *running = false;
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    struct options options = {false, false};
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
                                   SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Event event;
    struct nb_shell shell;
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
    nb_window_id about_window = NB_WINDOW_ID_NONE;
    bool capture_active = false;
    bool running = true;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }

    nb_shell_init(&shell,
                  NIXBENCH_MENU_SOURCE_DESKTOP,
                  &desktop_menu_model);
    if (nb_shell_open_window(&shell,
                             "NixBench",
                             first_window_frame,
                             NIXBENCH_MENU_SOURCE_DEMO_APPLICATION,
                             &application_menu_model) == NB_WINDOW_ID_NONE) {
        fputs("Could not create the initial desktop window.\n", stderr);
        return 1;
    }
    about_window = nb_shell_open_window(
        &shell,
        "About NixBench",
        second_window_frame,
        NIXBENCH_MENU_SOURCE_DEMO_APPLICATION,
        &application_menu_model);
    if (about_window == NB_WINDOW_ID_NONE) {
        fputs("Could not create the initial About window.\n", stderr);
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
        nb_shell_clamp_windows(&shell, viewport);
    }

    if (!draw_shell(renderer, &shell)) {
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
        const bool received_event =
            SDL_WaitEventTimeout(&event, clock_refresh_timeout());

        if (received_event) {
            do {
                if (event.type == SDL_EVENT_QUIT ||
                    event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    running = false;
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    process_key_event(&event.key,
                                      &shell,
                                      &capture_active,
                                      &about_window,
                                      &running);
                } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    release_pointer_capture(&capture_active);
                    nb_shell_pointer_cancel(&shell);
                } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE &&
                           !capture_active &&
                           nb_shell_has_pointer_interaction(&shell)) {
                    nb_shell_pointer_cancel(&shell);
                } else if (event.type == SDL_EVENT_MOUSE_MOTION ||
                           event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                           event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (!process_pointer_event(renderer,
                                               &event,
                                               &shell,
                                               &capture_active,
                                               &about_window,
                                               &running)) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "Could not process pointer input: %s",
                                     SDL_GetError());
                        exit_status = 1;
                        running = false;
                    }
                }
            } while (running && SDL_PollEvent(&event));
        }

        if (running) {
            struct nb_rect viewport;

            if (!renderer_bounds(renderer, &viewport)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not query the desktop size: %s",
                             SDL_GetError());
                exit_status = 1;
                break;
            }
            nb_shell_clamp_windows(&shell, viewport);
        }

        if (running && !draw_shell(renderer, &shell)) {
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
