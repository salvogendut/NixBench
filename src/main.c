#include <stdbool.h>
#include <stdint.h>
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
    NIXBENCH_WAYLAND_WAIT_MS = 16
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
    nb_application_id nixinfo_application;
    nb_window_id about_window;
#if NIXBENCH_HAS_WAYLAND
    struct nb_wayland_server *wayland;
    bool host_keyboard_focused;
    bool shell_key_capture[SDL_SCANCODE_COUNT];
#endif
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

static Sint32 event_wait_timeout(const struct runtime *runtime)
{
    Sint32 timeout = clock_refresh_timeout();

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

    if (!SDL_RenderPresent(renderer)) {
        return false;
    }
#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL) {
        nb_wayland_server_frame_presented(runtime->wayland,
                                          (uint32_t)SDL_GetTicks());
    }
#endif
    return true;
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

static void reconcile_pointer_capture(struct runtime *runtime)
{
    bool wanted = nb_shell_has_pointer_interaction(&runtime->shell);

#if NIXBENCH_HAS_WAYLAND
    wanted = wanted ||
             (runtime->wayland != NULL &&
              nb_wayland_server_pointer_grab_window(runtime->wayland) !=
                  NB_WINDOW_ID_NONE);
#endif
    if (wanted) {
        begin_pointer_capture(&runtime->capture_active);
    } else {
        release_pointer_capture(&runtime->capture_active);
    }
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
static const char *wayland_key_name_for_sdl(SDL_Scancode scancode)
{
    static const char *const key_names[SDL_SCANCODE_COUNT] = {
        [SDL_SCANCODE_A] = "AC01",
        [SDL_SCANCODE_B] = "AB05",
        [SDL_SCANCODE_C] = "AB03",
        [SDL_SCANCODE_D] = "AC03",
        [SDL_SCANCODE_E] = "AD03",
        [SDL_SCANCODE_F] = "AC04",
        [SDL_SCANCODE_G] = "AC05",
        [SDL_SCANCODE_H] = "AC06",
        [SDL_SCANCODE_I] = "AD08",
        [SDL_SCANCODE_J] = "AC07",
        [SDL_SCANCODE_K] = "AC08",
        [SDL_SCANCODE_L] = "AC09",
        [SDL_SCANCODE_M] = "AB07",
        [SDL_SCANCODE_N] = "AB06",
        [SDL_SCANCODE_O] = "AD09",
        [SDL_SCANCODE_P] = "AD10",
        [SDL_SCANCODE_Q] = "AD01",
        [SDL_SCANCODE_R] = "AD04",
        [SDL_SCANCODE_S] = "AC02",
        [SDL_SCANCODE_T] = "AD05",
        [SDL_SCANCODE_U] = "AD07",
        [SDL_SCANCODE_V] = "AB04",
        [SDL_SCANCODE_W] = "AD02",
        [SDL_SCANCODE_X] = "AB02",
        [SDL_SCANCODE_Y] = "AD06",
        [SDL_SCANCODE_Z] = "AB01",
        [SDL_SCANCODE_1] = "AE01",
        [SDL_SCANCODE_2] = "AE02",
        [SDL_SCANCODE_3] = "AE03",
        [SDL_SCANCODE_4] = "AE04",
        [SDL_SCANCODE_5] = "AE05",
        [SDL_SCANCODE_6] = "AE06",
        [SDL_SCANCODE_7] = "AE07",
        [SDL_SCANCODE_8] = "AE08",
        [SDL_SCANCODE_9] = "AE09",
        [SDL_SCANCODE_0] = "AE10",
        [SDL_SCANCODE_RETURN] = "RTRN",
        [SDL_SCANCODE_ESCAPE] = "ESC",
        [SDL_SCANCODE_BACKSPACE] = "BKSP",
        [SDL_SCANCODE_TAB] = "TAB",
        [SDL_SCANCODE_SPACE] = "SPCE",
        [SDL_SCANCODE_MINUS] = "AE11",
        [SDL_SCANCODE_EQUALS] = "AE12",
        [SDL_SCANCODE_LEFTBRACKET] = "AD11",
        [SDL_SCANCODE_RIGHTBRACKET] = "AD12",
        [SDL_SCANCODE_BACKSLASH] = "BKSL",
        [SDL_SCANCODE_NONUSHASH] = "BKSL",
        [SDL_SCANCODE_SEMICOLON] = "AC10",
        [SDL_SCANCODE_APOSTROPHE] = "AC11",
        [SDL_SCANCODE_GRAVE] = "TLDE",
        [SDL_SCANCODE_COMMA] = "AB08",
        [SDL_SCANCODE_PERIOD] = "AB09",
        [SDL_SCANCODE_SLASH] = "AB10",
        [SDL_SCANCODE_CAPSLOCK] = "CAPS",
        [SDL_SCANCODE_F1] = "FK01",
        [SDL_SCANCODE_F2] = "FK02",
        [SDL_SCANCODE_F3] = "FK03",
        [SDL_SCANCODE_F4] = "FK04",
        [SDL_SCANCODE_F5] = "FK05",
        [SDL_SCANCODE_F6] = "FK06",
        [SDL_SCANCODE_F7] = "FK07",
        [SDL_SCANCODE_F8] = "FK08",
        [SDL_SCANCODE_F9] = "FK09",
        [SDL_SCANCODE_F10] = "FK10",
        [SDL_SCANCODE_F11] = "FK11",
        [SDL_SCANCODE_F12] = "FK12",
        [SDL_SCANCODE_PRINTSCREEN] = "PRSC",
        [SDL_SCANCODE_SCROLLLOCK] = "SCLK",
        [SDL_SCANCODE_PAUSE] = "PAUS",
        [SDL_SCANCODE_INSERT] = "INS",
        [SDL_SCANCODE_HOME] = "HOME",
        [SDL_SCANCODE_PAGEUP] = "PGUP",
        [SDL_SCANCODE_DELETE] = "DELE",
        [SDL_SCANCODE_END] = "END",
        [SDL_SCANCODE_PAGEDOWN] = "PGDN",
        [SDL_SCANCODE_RIGHT] = "RGHT",
        [SDL_SCANCODE_LEFT] = "LEFT",
        [SDL_SCANCODE_DOWN] = "DOWN",
        [SDL_SCANCODE_UP] = "UP",
        [SDL_SCANCODE_NUMLOCKCLEAR] = "NMLK",
        [SDL_SCANCODE_KP_DIVIDE] = "KPDV",
        [SDL_SCANCODE_KP_MULTIPLY] = "KPMU",
        [SDL_SCANCODE_KP_MINUS] = "KPSU",
        [SDL_SCANCODE_KP_PLUS] = "KPAD",
        [SDL_SCANCODE_KP_ENTER] = "KPEN",
        [SDL_SCANCODE_KP_1] = "KP1",
        [SDL_SCANCODE_KP_2] = "KP2",
        [SDL_SCANCODE_KP_3] = "KP3",
        [SDL_SCANCODE_KP_4] = "KP4",
        [SDL_SCANCODE_KP_5] = "KP5",
        [SDL_SCANCODE_KP_6] = "KP6",
        [SDL_SCANCODE_KP_7] = "KP7",
        [SDL_SCANCODE_KP_8] = "KP8",
        [SDL_SCANCODE_KP_9] = "KP9",
        [SDL_SCANCODE_KP_0] = "KP0",
        [SDL_SCANCODE_KP_PERIOD] = "KPDL",
        [SDL_SCANCODE_NONUSBACKSLASH] = "LSGT",
        [SDL_SCANCODE_APPLICATION] = "MENU",
        [SDL_SCANCODE_POWER] = "POWR",
        [SDL_SCANCODE_KP_EQUALS] = "KPEQ",
        [SDL_SCANCODE_F13] = "FK13",
        [SDL_SCANCODE_F14] = "FK14",
        [SDL_SCANCODE_F15] = "FK15",
        [SDL_SCANCODE_F16] = "FK16",
        [SDL_SCANCODE_F17] = "FK17",
        [SDL_SCANCODE_F18] = "FK18",
        [SDL_SCANCODE_F19] = "FK19",
        [SDL_SCANCODE_F20] = "FK20",
        [SDL_SCANCODE_F21] = "FK21",
        [SDL_SCANCODE_F22] = "FK22",
        [SDL_SCANCODE_F23] = "FK23",
        [SDL_SCANCODE_F24] = "FK24",
        [SDL_SCANCODE_LCTRL] = "LCTL",
        [SDL_SCANCODE_LSHIFT] = "LFSH",
        [SDL_SCANCODE_LALT] = "LALT",
        [SDL_SCANCODE_LGUI] = "LWIN",
        [SDL_SCANCODE_RCTRL] = "RCTL",
        [SDL_SCANCODE_RSHIFT] = "RTSH",
        [SDL_SCANCODE_RALT] = "RALT",
        [SDL_SCANCODE_RGUI] = "RWIN",
        [SDL_SCANCODE_MODE] = "RALT"
    };

    if (scancode <= SDL_SCANCODE_UNKNOWN ||
        scancode >= SDL_SCANCODE_COUNT) {
        return NULL;
    }
    return key_names[scancode];
}

static bool wayland_has_keyboard_target(const struct runtime *runtime)
{
    const nb_window_id active =
        nb_desktop_active_window_id(&runtime->shell.desktop);

    return runtime->wayland != NULL && runtime->host_keyboard_focused &&
           nb_wayland_server_owns_window(runtime->wayland, active);
}

static bool forward_wayland_key(struct runtime *runtime,
                                const SDL_KeyboardEvent *event)
{
    const char *key_name;

    if (runtime->wayland == NULL) {
        return true;
    }
    key_name = wayland_key_name_for_sdl(event->scancode);
    if (key_name == NULL) {
        return true;
    }
    return nb_wayland_server_keyboard_key(
        runtime->wayland,
        key_name,
        (uint32_t)SDL_NS_TO_MS(event->timestamp),
        event->down);
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

static bool wayland_button_for_sdl(
    Uint8 sdl_button,
    enum nb_wayland_pointer_button *wayland_button)
{
    if (sdl_button == SDL_BUTTON_LEFT) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_LEFT;
    } else if (sdl_button == SDL_BUTTON_MIDDLE) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_MIDDLE;
    } else if (sdl_button == SDL_BUTTON_RIGHT) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_RIGHT;
    } else if (sdl_button == SDL_BUTTON_X1) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_SIDE;
    } else if (sdl_button == SDL_BUTTON_X2) {
        *wayland_button = NB_WAYLAND_POINTER_BUTTON_EXTRA;
    } else {
        return false;
    }
    return true;
}

static bool update_wayland_pointer(struct runtime *runtime,
                                   struct nb_shell_pointer_target target,
                                   int x,
                                   int y)
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
                                            (uint32_t)SDL_GetTicks());
}
#endif

static bool process_pointer_event(SDL_Renderer *renderer,
                                  SDL_Event *event,
                                  struct runtime *runtime)
{
    struct nb_shell_pointer_target target;
    struct nb_rect viewport;
    int x;
    int y;

    if (!SDL_ConvertEventToRenderCoordinates(renderer, event) ||
        !renderer_bounds(renderer, &viewport)) {
        return false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        x = renderer_coordinate(event->motion.x);
        y = renderer_coordinate(event->motion.y);
        if (nb_shell_wants_pointer_motion(&runtime->shell)) {
            (void)nb_shell_pointer_move(&runtime->shell, x, y, viewport);
        }
        target = nb_shell_pointer_target_at(&runtime->shell,
                                            x,
                                            y,
                                            viewport);
#if NIXBENCH_HAS_WAYLAND
        if (!update_wayland_pointer(runtime, target, x, y)) {
            return false;
        }
#endif
        reconcile_pointer_capture(runtime);
        return true;
    }

    x = renderer_coordinate(event->button.x);
    y = renderer_coordinate(event->button.y);
    target = nb_shell_pointer_target_at(&runtime->shell, x, y, viewport);

    if (event->button.button == SDL_BUTTON_LEFT) {
#if NIXBENCH_HAS_WAYLAND
        const bool wayland_grabbed =
            runtime->wayland != NULL &&
            nb_wayland_server_pointer_grab_window(runtime->wayland) !=
                NB_WINDOW_ID_NONE;
#endif

        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
#if NIXBENCH_HAS_WAYLAND
            if (wayland_grabbed) {
                const bool forwarded = nb_wayland_server_pointer_button(
                    runtime->wayland,
                    wayland_hover_window(runtime, target),
                    x,
                    y,
                    (uint32_t)SDL_GetTicks(),
                    NB_WAYLAND_POINTER_BUTTON_LEFT,
                    true);

                reconcile_pointer_capture(runtime);
                return forwarded;
            }
#endif
            (void)nb_shell_pointer_down(&runtime->shell, x, y, viewport);
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
                        (uint32_t)SDL_GetTicks(),
                        NB_WAYLAND_POINTER_BUTTON_LEFT,
                        true)) {
                    return false;
                }
                if (!forwards_to_wayland &&
                    !update_wayland_pointer(runtime, target, x, y)) {
                    return false;
                }
            }
#endif
        } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
#if NIXBENCH_HAS_WAYLAND
            if (wayland_grabbed) {
                const bool forwarded = nb_wayland_server_pointer_button(
                    runtime->wayland,
                    wayland_hover_window(runtime, target),
                    x,
                    y,
                    (uint32_t)SDL_GetTicks(),
                    NB_WAYLAND_POINTER_BUTTON_LEFT,
                    false);

                reconcile_pointer_capture(runtime);
                return forwarded;
            }
#endif
            {
                const struct nb_shell_action action =
                    nb_shell_pointer_up(&runtime->shell, x, y, viewport);

                if (!apply_shell_action(runtime, action)) {
                    return false;
                }
            }
#if NIXBENCH_HAS_WAYLAND
            target = nb_shell_pointer_target_at(&runtime->shell,
                                                x,
                                                y,
                                                viewport);
            if (!update_wayland_pointer(runtime, target, x, y)) {
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

        if (!wayland_button_for_sdl(event->button.button, &button)) {
            return true;
        }
        hover_window = wayland_hover_window(runtime, target);
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
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
                (uint32_t)SDL_GetTicks(),
                button,
                event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
            return false;
        }
        reconcile_pointer_capture(runtime);
    }
#else
    (void)target;
#endif
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

static bool menu_key_repeats(SDL_Keycode keycode)
{
    return keycode == SDLK_DOWN || keycode == SDLK_UP ||
           keycode == SDLK_RIGHT || keycode == SDLK_LEFT;
}

static bool process_key_event(const SDL_KeyboardEvent *event,
                              struct runtime *runtime)
{
    enum nb_menu_key menu_key;
    bool menu_open;
    bool menu_context;

#if NIXBENCH_HAS_WAYLAND
    if (event->scancode > SDL_SCANCODE_UNKNOWN &&
        event->scancode < SDL_SCANCODE_COUNT &&
        !event->down && runtime->shell_key_capture[event->scancode]) {
        runtime->shell_key_capture[event->scancode] = false;
        return true;
    }
#endif
    if (!event->down) {
#if NIXBENCH_HAS_WAYLAND
        return forward_wayland_key(runtime, event);
#else
        return true;
#endif
    }

    menu_open = nb_menu_is_open(&runtime->shell.menu);
    if (event->repeat) {
        if (!menu_open || !menu_key_repeats(event->key)) {
            return true;
        }
#if NIXBENCH_HAS_WAYLAND
        if (event->scancode <= SDL_SCANCODE_UNKNOWN ||
            event->scancode >= SDL_SCANCODE_COUNT ||
            !runtime->shell_key_capture[event->scancode]) {
            return true;
        }
#endif
    }

    menu_context = menu_open || event->key == SDLK_F10;
    if (menu_context && menu_key_for(event->key, &menu_key)) {
        const struct nb_shell_action action =
            nb_shell_menu_key_press(&runtime->shell, menu_key);

#if NIXBENCH_HAS_WAYLAND
        if (!event->repeat &&
            event->scancode > SDL_SCANCODE_UNKNOWN &&
            event->scancode < SDL_SCANCODE_COUNT) {
            runtime->shell_key_capture[event->scancode] = true;
        }
#endif
        reconcile_pointer_capture(runtime);
        return apply_shell_action(runtime, action);
    }

    if (menu_open) {
#if NIXBENCH_HAS_WAYLAND
        if (!event->repeat &&
            event->scancode > SDL_SCANCODE_UNKNOWN &&
            event->scancode < SDL_SCANCODE_COUNT) {
            runtime->shell_key_capture[event->scancode] = true;
        }
#endif
        return true;
    }

    if (event->key == SDLK_ESCAPE) {
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

static void cancel_pointer_input(struct runtime *runtime)
{
    nb_shell_pointer_cancel(&runtime->shell);
#if NIXBENCH_HAS_WAYLAND
    nb_wayland_server_pointer_cancel(runtime->wayland,
                                     (uint32_t)SDL_GetTicks());
#endif
    reconcile_pointer_capture(runtime);
}

static void cancel_keyboard_input(struct runtime *runtime)
{
#if NIXBENCH_HAS_WAYLAND
    runtime->host_keyboard_focused = false;
    (void)memset(runtime->shell_key_capture,
                 0,
                 sizeof(runtime->shell_key_capture));
    nb_wayland_server_keyboard_cancel(runtime->wayland,
                                      (uint32_t)SDL_GetTicks());
#else
    (void)runtime;
#endif
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
#if NIXBENCH_HAS_WAYLAND
    runtime.host_keyboard_focused =
        (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
#endif

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
#if NIXBENCH_HAS_WAYLAND
        start_wayland(&runtime, viewport.width, viewport.height);
#endif
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
            SDL_WaitEventTimeout(&event, event_wait_timeout(&runtime));

#if NIXBENCH_HAS_WAYLAND
        if (!dispatch_wayland(&runtime)) {
            exit_status = 1;
            break;
        }
#endif

        if (received_event) {
            do {
                if (event.type == SDL_EVENT_QUIT ||
                    event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    runtime.running = false;
                } else if (event.type == SDL_EVENT_KEY_DOWN ||
                           event.type == SDL_EVENT_KEY_UP) {
                    if (!process_key_event(&event.key, &runtime)) {
                        exit_status = 1;
                        runtime.running = false;
                    }
                } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    cancel_pointer_input(&runtime);
                    cancel_keyboard_input(&runtime);
#if NIXBENCH_HAS_WAYLAND
                } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                    runtime.host_keyboard_focused = true;
                    if (!sync_application_focus(&runtime)) {
                        exit_status = 1;
                        runtime.running = false;
                    }
#endif
                } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE &&
                           !runtime.capture_active) {
                    cancel_pointer_input(&runtime);
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
#if NIXBENCH_HAS_WAYLAND
            if (runtime.wayland != NULL &&
                !nb_wayland_server_set_output_size(runtime.wayland,
                                                    viewport.width,
                                                    viewport.height)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Could not update the Wayland output size");
                exit_status = 1;
                break;
            }
#endif
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
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return exit_status;
}
