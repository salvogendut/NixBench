#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "application.h"
#include "host.h"
#include "host_sdl.h"
#include "nixinfo.h"
#include "nixinfo_renderer.h"
#include "shell.h"
#include "shell_renderer.h"
#include "software_canvas.h"
#include "window_renderer.h"

#ifndef NIXBENCH_HAS_WAYLAND
#define NIXBENCH_HAS_WAYLAND 0
#endif

#if NIXBENCH_HAS_WAYLAND
#include "wayland_renderer.h"
#include "wayland_server.h"
#endif

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
    NIXBENCH_CLOCK_FALLBACK_WAIT_MS = 1000,
    NIXBENCH_WAYLAND_WAIT_MS = 16,
    NIXBENCH_SHELL_KEY_CAPTURE_CAPACITY = 32,
    NIXBENCH_HOST_ERROR_CAPACITY = 256
};

#define NIXBENCH_MENU_SOURCE_DESKTOP UINT64_C(1)
#define NIXBENCH_MENU_SOURCE_ABOUT UINT64_MAX
#define NIXBENCH_MENU_SOURCE_WAYLAND (UINT64_MAX - UINT64_C(1))

enum {
    NIXBENCH_DESKTOP_COMMAND_ABOUT = 1,
    NIXBENCH_DESKTOP_COMMAND_OPEN_NIXINFO,
    NIXBENCH_DESKTOP_COMMAND_QUIT
};

enum {
    NIXBENCH_ABOUT_COMMAND_CLOSE = 1
};

enum {
    NIXBENCH_WAYLAND_COMMAND_CLOSE = 1
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

#if NIXBENCH_HAS_WAYLAND
static const struct nb_menu_item_spec wayland_items[] = {
    {"Close Application", NIXBENCH_WAYLAND_COMMAND_CLOSE,
     NB_MENU_ITEM_COMMAND, true}
};

static const struct nb_menu_spec wayland_menus[] = {
    {"Application", wayland_items,
     sizeof(wayland_items) / sizeof(wayland_items[0])}
};

static const struct nb_menu_model wayland_menu_model = {
    wayland_menus,
    sizeof(wayland_menus) / sizeof(wayland_menus[0])
};
#endif

struct runtime {
    struct nb_shell shell;
    struct nb_application_host applications;
    struct nb_nixinfo nixinfo;
    struct nb_host *host;
    struct nb_software_canvas *canvas;
    struct nb_host_output output;
    struct nb_rect viewport;
    nb_application_id nixinfo_application;
    nb_window_id about_window;
#if NIXBENCH_HAS_WAYLAND
    struct nb_wayland_server *wayland;
    bool host_keyboard_focused;
    char shell_key_capture[NIXBENCH_SHELL_KEY_CAPTURE_CAPACITY]
                          [NB_HOST_XKB_KEY_NAME_CAPACITY];
#endif
    uint64_t next_frame_serial;
    uint64_t pending_frame_serial;
    bool capture_active;
    bool redraw_needed;
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

static uint32_t clock_refresh_timeout(void)
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
    return (uint32_t)(seconds_until_next_minute * 1000);
}

static uint32_t event_wait_timeout(const struct runtime *runtime)
{
    uint32_t timeout = clock_refresh_timeout();

#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL &&
        nb_wayland_server_display_name(runtime->wayland) != NULL &&
        timeout > NIXBENCH_WAYLAND_WAIT_MS) {
        timeout = NIXBENCH_WAYLAND_WAIT_MS;
    }
#else
    (void)runtime;
#endif
    return timeout;
}

static bool render_window_content(SDL_Renderer *renderer,
                                  nb_window_id id,
                                  const struct nb_window *window,
                                  struct nb_rect content_rect,
                                  void *context)
{
    struct runtime *runtime = context;

#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL &&
        nb_wayland_server_owns_window(runtime->wayland, id)) {
        return nb_wayland_render_content(renderer,
                                         id,
                                         window,
                                         content_rect,
                                         runtime->wayland);
    }
#endif
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
    char clock_text[NIXBENCH_CLOCK_CAPACITY];

    format_clock(clock_text);

    if (!SDL_SetRenderDrawColor(renderer,
                                NIXBENCH_DESKTOP_RED,
                                NIXBENCH_DESKTOP_GREEN,
                                NIXBENCH_DESKTOP_BLUE,
                                SDL_ALPHA_OPAQUE) ||
        !SDL_RenderClear(renderer) ||
        !nb_shell_render_with_content(renderer,
                                      &runtime->shell,
                                      runtime->viewport,
                                      clock_text,
                                      render_window_content,
                                      runtime)) {
        return false;
    }
    return true;
}

static void log_host_error(const struct runtime *runtime,
                           const char *operation)
{
    char message[NIXBENCH_HOST_ERROR_CAPACITY];
    int system_error;

    if (runtime->host != NULL &&
        nb_host_get_last_error(runtime->host,
                               &system_error,
                               message,
                               sizeof(message))) {
        if (system_error != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "%s: %s (system error %d)",
                         operation,
                         message,
                         system_error);
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "%s: %s",
                         operation,
                         message);
        }
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", operation);
    }
}

static void set_pointer_capture(struct runtime *runtime, bool captured)
{
    if (runtime->host == NULL || runtime->capture_active == captured) {
        return;
    }

    if (nb_host_set_pointer_capture(runtime->host, captured)) {
        runtime->capture_active = captured;
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "The host could not %s pointer capture",
                    captured ? "begin" : "release");
        if (!captured) {
            runtime->capture_active = false;
        }
    }
}

static void reconcile_pointer_capture(struct runtime *runtime)
{
    bool wanted = nb_shell_has_pointer_interaction(&runtime->shell);

#if NIXBENCH_HAS_WAYLAND
    wanted = wanted ||
             (runtime->wayland != NULL &&
              nb_wayland_server_pointer_grab_window(runtime->wayland) !=
                  NB_WINDOW_ID_NONE);
#endif
    set_pointer_capture(runtime, wanted);
}

static bool sync_application_focus(struct runtime *runtime)
{
    if (!nb_application_host_sync_focus(&runtime->applications)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not synchronize application focus");
        return false;
    }
#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL) {
        nb_window_id focus = runtime->host_keyboard_focused
                                 ? nb_desktop_active_window_id(
                                       &runtime->shell.desktop)
                                 : NB_WINDOW_ID_NONE;

        if (!nb_wayland_server_owns_window(runtime->wayland, focus)) {
            focus = NB_WINDOW_ID_NONE;
        }
        if (!nb_wayland_server_keyboard_focus(runtime->wayland, focus)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not synchronize Wayland keyboard focus");
            return false;
        }
    }
#endif
    return true;
}

#if NIXBENCH_HAS_WAYLAND
static bool wayland_has_keyboard_target(const struct runtime *runtime)
{
    const nb_window_id active =
        nb_desktop_active_window_id(&runtime->shell.desktop);

    return runtime->wayland != NULL && runtime->host_keyboard_focused &&
           nb_wayland_server_owns_window(runtime->wayland, active);
}

static bool forward_wayland_key(struct runtime *runtime,
                                const struct nb_host_event *event)
{
    if (runtime->wayland == NULL) {
        return true;
    }
    return nb_wayland_server_keyboard_key(
        runtime->wayland,
        event->data.key.xkb_key_name,
        (uint32_t)event->milliseconds,
        event->data.key.pressed);
}

static bool dispatch_wayland(struct runtime *runtime)
{
    if (runtime->wayland == NULL) {
        return true;
    }
    if (!nb_wayland_server_dispatch(runtime->wayland)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not dispatch the nested Wayland display");
        return false;
    }
    reconcile_pointer_capture(runtime);
    return sync_application_focus(runtime);
}

static void start_wayland(struct runtime *runtime,
                           int output_width,
                           int output_height)
{
    const char *display_name;

    runtime->wayland = nb_wayland_server_create(
        &runtime->shell,
        NIXBENCH_MENU_SOURCE_WAYLAND,
        &wayland_menu_model,
        output_width,
        output_height);
    if (runtime->wayland == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not initialize the nested Wayland display; "
                    "continuing without Wayland clients");
        return;
    }

    display_name = nb_wayland_server_add_socket_auto(runtime->wayland);
    if (display_name == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not publish a Wayland socket. Set "
                    "XDG_RUNTIME_DIR to a private directory with mode 0700 "
                    "to accept Wayland clients");
        return;
    }
    SDL_Log("Nested Wayland display is ready: WAYLAND_DISPLAY=%s",
            display_name);
}
#endif

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
#if NIXBENCH_HAS_WAYLAND
        if (runtime->wayland != NULL &&
            nb_wayland_server_owns_window(runtime->wayland,
                                          action.window)) {
            if (nb_wayland_server_request_close(runtime->wayland,
                                                action.window)) {
                return true;
            }
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Could not ask Wayland client window %llu to close",
                        (unsigned long long)action.window);
            return false;
        }
#endif
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

#if NIXBENCH_HAS_WAYLAND
    if (action.menu_source == NIXBENCH_MENU_SOURCE_WAYLAND) {
        if (action.menu_command == NIXBENCH_WAYLAND_COMMAND_CLOSE &&
            runtime->wayland != NULL &&
            nb_wayland_server_owns_window(runtime->wayland,
                                          action.window)) {
            if (nb_wayland_server_request_close(runtime->wayland,
                                                action.window)) {
                return true;
            }
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Could not ask Wayland client window %llu to close",
                        (unsigned long long)action.window);
            return false;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Ignored Wayland application menu command %u",
                    (unsigned int)action.menu_command);
        return true;
    }
#endif

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Ignored menu command %u from unknown source %llu",
                (unsigned int)action.menu_command,
                (unsigned long long)action.menu_source);
    return true;
}

#if NIXBENCH_HAS_WAYLAND
static nb_window_id wayland_hover_window(
    const struct runtime *runtime,
    struct nb_shell_pointer_target target)
{
    if (runtime->wayland == NULL || target.hit != NB_WINDOW_HIT_CONTENT ||
        !nb_wayland_server_owns_window(runtime->wayland, target.window)) {
        return NB_WINDOW_ID_NONE;
    }
    return target.window;
}

static bool wayland_button_for_host(
    enum nb_host_pointer_button host_button,
    enum nb_wayland_pointer_button *wayland_button)
{
    if (host_button == NB_HOST_POINTER_BUTTON_LEFT) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_LEFT;
    } else if (host_button == NB_HOST_POINTER_BUTTON_MIDDLE) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_MIDDLE;
    } else if (host_button == NB_HOST_POINTER_BUTTON_RIGHT) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_RIGHT;
    } else if (host_button == NB_HOST_POINTER_BUTTON_SIDE) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_SIDE;
    } else if (host_button == NB_HOST_POINTER_BUTTON_EXTRA) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_EXTRA;
    } else {
        return false;
    }
    return true;
}

static bool update_wayland_pointer(struct runtime *runtime,
                                   struct nb_shell_pointer_target target,
                                   int x,
                                   int y,
                                   uint64_t milliseconds)
{
    nb_window_id hover_window;

    if (runtime->wayland == NULL) {
        return true;
    }
    hover_window = nb_shell_has_pointer_interaction(&runtime->shell)
                       ? NB_WINDOW_ID_NONE
                       : wayland_hover_window(runtime, target);
    return nb_wayland_server_pointer_motion(runtime->wayland,
                                            hover_window,
                                            x,
                                            y,
                                            (uint32_t)milliseconds);
}
#endif

static bool process_pointer_event(const struct nb_host_event *event,
                                  struct runtime *runtime)
{
    struct nb_shell_pointer_target target;
    int x;
    int y;

    if (event->type == NB_HOST_EVENT_POINTER_MOTION) {
        x = event->data.pointer_motion.x;
        y = event->data.pointer_motion.y;
        if (nb_shell_wants_pointer_motion(&runtime->shell)) {
            (void)nb_shell_pointer_move(&runtime->shell,
                                        x,
                                        y,
                                        runtime->viewport);
        }
        target = nb_shell_pointer_target_at(&runtime->shell,
                                            x,
                                            y,
                                            runtime->viewport);
#if NIXBENCH_HAS_WAYLAND
        if (!update_wayland_pointer(runtime,
                                    target,
                                    x,
                                    y,
                                    event->milliseconds)) {
            return false;
        }
#endif
        reconcile_pointer_capture(runtime);
        return true;
    }

    x = event->data.pointer_button.x;
    y = event->data.pointer_button.y;
    target = nb_shell_pointer_target_at(&runtime->shell,
                                        x,
                                        y,
                                        runtime->viewport);

    if (event->data.pointer_button.button == NB_HOST_POINTER_BUTTON_LEFT) {
#if NIXBENCH_HAS_WAYLAND
        const bool wayland_grabbed =
            runtime->wayland != NULL &&
            nb_wayland_server_pointer_grab_window(runtime->wayland) !=
                NB_WINDOW_ID_NONE;
#endif

        if (event->data.pointer_button.pressed) {
#if NIXBENCH_HAS_WAYLAND
            if (wayland_grabbed) {
                const bool forwarded = nb_wayland_server_pointer_button(
                    runtime->wayland,
                    wayland_hover_window(runtime, target),
                    x,
                    y,
                    (uint32_t)event->milliseconds,
                    NB_WAYLAND_POINTER_BUTTON_LEFT,
                    true);

                reconcile_pointer_capture(runtime);
                return forwarded;
            }
#endif
            (void)nb_shell_pointer_down(&runtime->shell,
                                        x,
                                        y,
                                        runtime->viewport);
            if (!sync_application_focus(runtime)) {
                return false;
            }
#if NIXBENCH_HAS_WAYLAND
            {
                const nb_window_id hover_window =
                    wayland_hover_window(runtime, target);
                const bool forwards_to_wayland =
                    runtime->wayland != NULL &&
                    !nb_shell_has_pointer_interaction(&runtime->shell) &&
                    hover_window != NB_WINDOW_ID_NONE;

                if (forwards_to_wayland &&
                    !nb_wayland_server_pointer_button(
                        runtime->wayland,
                        hover_window,
                        x,
                        y,
                        (uint32_t)event->milliseconds,
                        NB_WAYLAND_POINTER_BUTTON_LEFT,
                        true)) {
                    return false;
                }
                if (!forwards_to_wayland &&
                    !update_wayland_pointer(runtime,
                                            target,
                                            x,
                                            y,
                                            event->milliseconds)) {
                    return false;
                }
            }
#endif
        } else {
#if NIXBENCH_HAS_WAYLAND
            if (wayland_grabbed) {
                const bool forwarded = nb_wayland_server_pointer_button(
                    runtime->wayland,
                    wayland_hover_window(runtime, target),
                    x,
                    y,
                    (uint32_t)event->milliseconds,
                    NB_WAYLAND_POINTER_BUTTON_LEFT,
                    false);

                reconcile_pointer_capture(runtime);
                return forwarded;
            }
#endif
            {
                const struct nb_shell_action action =
                    nb_shell_pointer_up(&runtime->shell,
                                        x,
                                        y,
                                        runtime->viewport);

                if (!apply_shell_action(runtime, action)) {
                    return false;
                }
            }
#if NIXBENCH_HAS_WAYLAND
            target = nb_shell_pointer_target_at(&runtime->shell,
                                                x,
                                                y,
                                                runtime->viewport);
            if (!update_wayland_pointer(runtime,
                                        target,
                                        x,
                                        y,
                                        event->milliseconds)) {
                return false;
            }
#endif
        }
        reconcile_pointer_capture(runtime);
        return true;
    }

#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL &&
        !nb_shell_has_pointer_interaction(&runtime->shell)) {
        enum nb_wayland_pointer_button button;
        nb_window_id hover_window;

        if (!wayland_button_for_host(event->data.pointer_button.button,
                                     &button)) {
            return true;
        }
        hover_window = wayland_hover_window(runtime, target);
        if (event->data.pointer_button.pressed &&
            nb_wayland_server_pointer_grab_window(runtime->wayland) ==
                NB_WINDOW_ID_NONE &&
            hover_window != NB_WINDOW_ID_NONE) {
            if (!nb_shell_activate_window(&runtime->shell, hover_window) ||
                !sync_application_focus(runtime)) {
                return false;
            }
        }
        if (!nb_wayland_server_pointer_button(
                runtime->wayland,
                hover_window,
                x,
                y,
                (uint32_t)event->milliseconds,
                button,
                event->data.pointer_button.pressed)) {
            return false;
        }
        reconcile_pointer_capture(runtime);
    }
#else
    (void)target;
#endif
    return true;
}

static bool menu_key_for(const char *key_name, enum nb_menu_key *menu_key)
{
    if (strcmp(key_name, "FK10") == 0) {
        *menu_key = NB_MENU_KEY_TOGGLE;
    } else if (strcmp(key_name, "DOWN") == 0) {
        *menu_key = NB_MENU_KEY_NEXT_ITEM;
    } else if (strcmp(key_name, "UP") == 0) {
        *menu_key = NB_MENU_KEY_PREVIOUS_ITEM;
    } else if (strcmp(key_name, "RGHT") == 0) {
        *menu_key = NB_MENU_KEY_NEXT_MENU;
    } else if (strcmp(key_name, "LEFT") == 0) {
        *menu_key = NB_MENU_KEY_PREVIOUS_MENU;
    } else if (strcmp(key_name, "RTRN") == 0 ||
               strcmp(key_name, "KPEN") == 0) {
        *menu_key = NB_MENU_KEY_ACTIVATE;
    } else if (strcmp(key_name, "ESC") == 0) {
        *menu_key = NB_MENU_KEY_DISMISS;
    } else {
        return false;
    }
    return true;
}

static bool menu_key_repeats(const char *key_name)
{
    return strcmp(key_name, "DOWN") == 0 ||
           strcmp(key_name, "UP") == 0 ||
           strcmp(key_name, "RGHT") == 0 ||
           strcmp(key_name, "LEFT") == 0;
}

#if NIXBENCH_HAS_WAYLAND
static bool shell_key_is_captured(const struct runtime *runtime,
                                  const char *key_name)
{
    size_t index;

    for (index = 0; index < NIXBENCH_SHELL_KEY_CAPTURE_CAPACITY; ++index) {
        if (strcmp(runtime->shell_key_capture[index], key_name) == 0) {
            return true;
        }
    }
    return false;
}

static void capture_shell_key(struct runtime *runtime,
                              const char *key_name)
{
    size_t index;

    if (shell_key_is_captured(runtime, key_name)) {
        return;
    }
    for (index = 0; index < NIXBENCH_SHELL_KEY_CAPTURE_CAPACITY; ++index) {
        if (runtime->shell_key_capture[index][0] == '\0') {
            (void)memcpy(runtime->shell_key_capture[index],
                         key_name,
                         NB_HOST_XKB_KEY_NAME_CAPACITY);
            return;
        }
    }
}

static bool release_shell_key(struct runtime *runtime,
                              const char *key_name)
{
    size_t index;

    for (index = 0; index < NIXBENCH_SHELL_KEY_CAPTURE_CAPACITY; ++index) {
        if (strcmp(runtime->shell_key_capture[index], key_name) == 0) {
            runtime->shell_key_capture[index][0] = '\0';
            return true;
        }
    }
    return false;
}
#endif

static bool process_key_event(const struct nb_host_event *event,
                              struct runtime *runtime)
{
    const char *key_name = event->data.key.xkb_key_name;
    enum nb_menu_key menu_key;
    bool menu_open;
    bool menu_context;

#if NIXBENCH_HAS_WAYLAND
    if (!event->data.key.pressed &&
        release_shell_key(runtime, key_name)) {
        return true;
    }
#endif
    if (!event->data.key.pressed) {
#if NIXBENCH_HAS_WAYLAND
        return forward_wayland_key(runtime, event);
#else
        return true;
#endif
    }

    menu_open = nb_menu_is_open(&runtime->shell.menu);
    if (event->data.key.repeat) {
        if (!menu_open || !menu_key_repeats(key_name)) {
            return true;
        }
#if NIXBENCH_HAS_WAYLAND
        if (!shell_key_is_captured(runtime, key_name)) {
            return true;
        }
#endif
    }

    menu_context = menu_open || strcmp(key_name, "FK10") == 0;
    if (menu_context && menu_key_for(key_name, &menu_key)) {
        const struct nb_shell_action action =
            nb_shell_menu_key_press(&runtime->shell, menu_key);

#if NIXBENCH_HAS_WAYLAND
        if (!event->data.key.repeat) {
            capture_shell_key(runtime, key_name);
        }
#endif
        reconcile_pointer_capture(runtime);
        return apply_shell_action(runtime, action);
    }

    if (menu_open) {
#if NIXBENCH_HAS_WAYLAND
        if (!event->data.key.repeat) {
            capture_shell_key(runtime, key_name);
        }
#endif
        return true;
    }

    if (strcmp(key_name, "ESC") == 0) {
#if NIXBENCH_HAS_WAYLAND
        if (wayland_has_keyboard_target(runtime)) {
            return forward_wayland_key(runtime, event);
        }
#endif
        runtime->running = false;
        return true;
    }
#if NIXBENCH_HAS_WAYLAND
    return forward_wayland_key(runtime, event);
#else
    return true;
#endif
}

static void cancel_pointer_input(struct runtime *runtime,
                                 uint64_t milliseconds)
{
    nb_shell_pointer_cancel(&runtime->shell);
#if NIXBENCH_HAS_WAYLAND
    nb_wayland_server_pointer_cancel(runtime->wayland,
                                     (uint32_t)milliseconds);
#else
    (void)milliseconds;
#endif
    reconcile_pointer_capture(runtime);
}

static void cancel_keyboard_input(struct runtime *runtime,
                                  uint64_t milliseconds)
{
#if NIXBENCH_HAS_WAYLAND
    runtime->host_keyboard_focused = false;
    (void)memset(runtime->shell_key_capture,
                 0,
                 sizeof(runtime->shell_key_capture));
    nb_wayland_server_keyboard_cancel(runtime->wayland,
                                      (uint32_t)milliseconds);
#else
    (void)runtime;
    (void)milliseconds;
#endif
}

static bool set_output(struct runtime *runtime,
                       const struct nb_host_output *output)
{
    struct nb_software_canvas *canvas;
    SDL_Renderer *renderer;

    canvas = nb_software_canvas_create(output->pixel_width,
                                       output->pixel_height);
    if (canvas == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not create the software canvas: %s",
                     SDL_GetError());
        return false;
    }
    renderer = nb_software_canvas_renderer(canvas);
    if (renderer == NULL ||
        !SDL_SetRenderLogicalPresentation(
            renderer,
            output->logical_width,
            output->logical_height,
            SDL_LOGICAL_PRESENTATION_STRETCH)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not configure the software canvas: %s",
                     SDL_GetError());
        nb_software_canvas_destroy(canvas);
        return false;
    }

    nb_software_canvas_destroy(runtime->canvas);
    runtime->canvas = canvas;
    runtime->output = *output;
    runtime->viewport.x = 0;
    runtime->viewport.y = 0;
    runtime->viewport.width = output->logical_width;
    runtime->viewport.height = output->logical_height;
    nb_shell_clamp_windows(&runtime->shell, runtime->viewport);
#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL &&
        !nb_wayland_server_set_output_size(runtime->wayland,
                                            output->logical_width,
                                            output->logical_height)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not update the Wayland output size");
        return false;
    }
#endif
    runtime->redraw_needed = true;
    return true;
}

static bool present_desktop(struct runtime *runtime)
{
    struct nb_host_frame frame;
    SDL_Renderer *renderer =
        nb_software_canvas_renderer(runtime->canvas);
    enum nb_host_result result;

    if (runtime->pending_frame_serial != 0) {
        return true;
    }
    if (renderer == NULL || !draw_shell(renderer, runtime) ||
        !nb_software_canvas_finish(runtime->canvas,
                                   runtime->next_frame_serial,
                                   &frame)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not draw the desktop screen: %s",
                     SDL_GetError());
        return false;
    }
    result = nb_host_present(runtime->host, &frame);
    if (result == NB_HOST_RESULT_WOULD_BLOCK ||
        result == NB_HOST_RESULT_SUSPENDED) {
        return true;
    }
    if (result != NB_HOST_RESULT_OK) {
        log_host_error(runtime, "Could not present the desktop screen");
        return false;
    }
    runtime->pending_frame_serial = runtime->next_frame_serial;
    ++runtime->next_frame_serial;
    runtime->redraw_needed = false;
    return true;
}

static bool process_host_event(struct runtime *runtime,
                               const struct nb_host_event *event)
{
    enum nb_host_result result;

    switch (event->type) {
    case NB_HOST_EVENT_QUIT:
        runtime->running = false;
        return true;
    case NB_HOST_EVENT_OUTPUT_CHANGED:
        return set_output(runtime, &event->data.output);
    case NB_HOST_EVENT_FOCUS_CHANGED:
        if (!event->data.focus.focused) {
            cancel_pointer_input(runtime, event->milliseconds);
            cancel_keyboard_input(runtime, event->milliseconds);
        }
#if NIXBENCH_HAS_WAYLAND
        else {
            runtime->host_keyboard_focused = true;
            if (!sync_application_focus(runtime)) {
                return false;
            }
        }
#endif
        runtime->redraw_needed = true;
        return true;
    case NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED:
        cancel_pointer_input(runtime, event->milliseconds);
        cancel_keyboard_input(runtime, event->milliseconds);
        result = nb_host_complete_console_release(runtime->host);
        if (result != NB_HOST_RESULT_OK) {
            log_host_error(runtime, "Could not release the console");
            return false;
        }
        return true;
    case NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED:
    {
        struct nb_host_output output;

        result = nb_host_complete_console_acquire(runtime->host);
        if (result != NB_HOST_RESULT_OK ||
            !nb_host_get_output(runtime->host, &output)) {
            log_host_error(runtime, "Could not reacquire the console");
            return false;
        }
        runtime->pending_frame_serial = 0;
        return set_output(runtime, &output);
    }
    case NB_HOST_EVENT_POINTER_MOTION:
    case NB_HOST_EVENT_POINTER_BUTTON:
        runtime->redraw_needed = true;
        return process_pointer_event(event, runtime);
    case NB_HOST_EVENT_POINTER_LEAVE:
        if (!runtime->capture_active) {
            cancel_pointer_input(runtime, event->milliseconds);
            runtime->redraw_needed = true;
        }
        return true;
    case NB_HOST_EVENT_KEY:
        runtime->redraw_needed = true;
        return process_key_event(event, runtime);
    case NB_HOST_EVENT_FRAME_COMPLETE:
        if (event->data.frame_complete.frame_serial ==
            runtime->pending_frame_serial) {
            runtime->pending_frame_serial = 0;
#if NIXBENCH_HAS_WAYLAND
            if (runtime->wayland != NULL) {
                nb_wayland_server_frame_presented(
                    runtime->wayland,
                    (uint32_t)event->milliseconds);
            }
#endif
        }
        return true;
    case NB_HOST_EVENT_FAILED:
        log_host_error(runtime, "The display host failed");
        return false;
    case NB_HOST_EVENT_NONE:
    default:
        return false;
    }
}

int main(int argc, char *argv[])
{
    struct options options = {false, false};
    struct runtime runtime;
    struct nb_application_spec nixinfo_spec;
    struct nb_host_sdl_options host_options;
    struct nb_host_event event;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }

    (void)memset(&runtime, 0, sizeof(runtime));
    runtime.about_window = NB_WINDOW_ID_NONE;
    runtime.next_frame_serial = 1;
    runtime.redraw_needed = true;
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

    nb_host_sdl_options_init(&host_options);
    host_options.window_width = NIXBENCH_WINDOW_WIDTH;
    host_options.window_height = NIXBENCH_WINDOW_HEIGHT;
    host_options.minimum_width = NIXBENCH_WINDOW_MIN_WIDTH;
    host_options.minimum_height = NIXBENCH_WINDOW_MIN_HEIGHT;
    host_options.fullscreen = options.fullscreen;
    runtime.host = nb_host_sdl_create(&host_options);
    if (runtime.host == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not create the SDL display host: %s",
                     nb_host_sdl_creation_error());
        exit_status = 1;
        goto cleanup;
    }
    if (!nb_host_get_output(runtime.host, &runtime.output) ||
        !set_output(&runtime, &runtime.output)) {
        log_host_error(&runtime, "Could not initialize the display output");
        exit_status = 1;
        goto cleanup;
    }
#if NIXBENCH_HAS_WAYLAND
    start_wayland(&runtime,
                  runtime.output.logical_width,
                  runtime.output.logical_height);
#endif

    if (!present_desktop(&runtime)) {
        exit_status = 1;
        goto cleanup;
    }
    if (options.exit_after_first_frame) {
        goto cleanup;
    }

    while (runtime.running) {
        enum nb_host_event_status event_status =
            nb_host_wait_event(runtime.host,
                               event_wait_timeout(&runtime),
                               &event);

        if (event_status == NB_HOST_EVENT_STATUS_ERROR) {
            log_host_error(&runtime, "Could not wait for host events");
            exit_status = 1;
            break;
        }

#if NIXBENCH_HAS_WAYLAND
        if (!dispatch_wayland(&runtime)) {
            exit_status = 1;
            break;
        }
#endif

        if (event_status == NB_HOST_EVENT_STATUS_AVAILABLE) {
            do {
                if (!process_host_event(&runtime, &event)) {
                    exit_status = 1;
                    runtime.running = false;
                    break;
                }
                event_status = nb_host_poll_event(runtime.host, &event);
                if (event_status == NB_HOST_EVENT_STATUS_ERROR) {
                    log_host_error(&runtime,
                                   "Could not poll host events");
                    exit_status = 1;
                    runtime.running = false;
                    break;
                }
            } while (runtime.running &&
                     event_status == NB_HOST_EVENT_STATUS_AVAILABLE);
        } else {
            runtime.redraw_needed = true;
        }

        if (runtime.running && runtime.redraw_needed &&
            !present_desktop(&runtime)) {
            exit_status = 1;
            break;
        }
    }

cleanup:
    set_pointer_capture(&runtime, false);
#if NIXBENCH_HAS_WAYLAND
    nb_wayland_server_destroy(runtime.wayland);
    runtime.wayland = NULL;
#endif
    if (nb_application_host_is_running(&runtime.applications,
                                       runtime.nixinfo_application) &&
        !nb_application_host_stop(&runtime.applications,
                                  runtime.nixinfo_application)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not stop NixInfo cleanly");
    }
    nb_software_canvas_destroy(runtime.canvas);
    runtime.canvas = NULL;
    nb_host_destroy(runtime.host);
    runtime.host = NULL;

    return exit_status;
}
