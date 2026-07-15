#include "desktop_runtime.h"

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "application.h"
#include "menu.h"
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

enum {
    NIXBENCH_ABOUT_WINDOW_X = 360,
    NIXBENCH_ABOUT_WINDOW_Y = 190,
    NIXBENCH_ABOUT_WINDOW_WIDTH = 430,
    NIXBENCH_ABOUT_WINDOW_HEIGHT = 230,
    NIXBENCH_DESKTOP_RED = 24,
    NIXBENCH_DESKTOP_GREEN = 54,
    NIXBENCH_DESKTOP_BLUE = 76,
    NIXBENCH_SHELL_KEY_CAPTURE_CAPACITY = 32
};

#define NIXBENCH_MENU_SOURCE_DESKTOP UINT64_C(1)
#define NIXBENCH_MENU_SOURCE_ABOUT UINT64_MAX
#define NIXBENCH_MENU_SOURCE_WAYLAND UINT64_C(0x4000000000000000)

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
     NB_MENU_ITEM_COMMAND, true, false},
    {"Open NixInfo", NIXBENCH_DESKTOP_COMMAND_OPEN_NIXINFO,
     NB_MENU_ITEM_COMMAND, true, false},
    {NULL, NB_MENU_COMMAND_NONE, NB_MENU_ITEM_SEPARATOR, false, false},
    {"Quit NixBench", NIXBENCH_DESKTOP_COMMAND_QUIT,
     NB_MENU_ITEM_COMMAND, true, false}
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
     NB_MENU_ITEM_COMMAND, true, false}
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
     NB_MENU_ITEM_COMMAND, true, false}
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

struct nb_desktop_runtime {
    struct nb_desktop_runtime_options options;
    struct nb_shell shell;
    struct nb_application_host applications;
    struct nb_nixinfo nixinfo;
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
    int pointer_x;
    int pointer_y;
    bool pointer_visible;
    bool quit_requested;
};

static void clear_update(struct nb_desktop_runtime_update *update)
{
    (void)memset(update, 0, sizeof(*update));
}

static int clamp_coordinate(int value, int extent)
{
    if (value < 0) {
        return 0;
    }
    return value >= extent ? extent - 1 : value;
}

static void remember_pointer(struct nb_desktop_runtime *runtime,
                             int x,
                             int y)
{
    runtime->pointer_x = clamp_coordinate(x, runtime->viewport.width);
    runtime->pointer_y = clamp_coordinate(y, runtime->viewport.height);
    if (runtime->options.software_pointer) {
        runtime->pointer_visible = true;
    }
}

static bool render_window_content(SDL_Renderer *renderer,
                                  nb_window_id id,
                                  const struct nb_window *window,
                                  struct nb_rect content_rect,
                                  void *context)
{
    struct nb_desktop_runtime *runtime = context;

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

static bool sync_application_focus(struct nb_desktop_runtime *runtime)
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
static bool wayland_has_keyboard_target(
    const struct nb_desktop_runtime *runtime)
{
    const nb_window_id active =
        nb_desktop_active_window_id(&runtime->shell.desktop);

    return runtime->wayland != NULL && runtime->host_keyboard_focused &&
           nb_wayland_server_owns_window(runtime->wayland, active);
}

static bool forward_wayland_key(struct nb_desktop_runtime *runtime,
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

static void start_wayland(struct nb_desktop_runtime *runtime)
{
    const char *display_name;

    runtime->wayland = nb_wayland_server_create(
        &runtime->shell,
        NIXBENCH_MENU_SOURCE_WAYLAND,
        &wayland_menu_model,
        runtime->output.logical_width,
        runtime->output.logical_height);
    if (runtime->wayland == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not initialize the nested Wayland display; "
                    "continuing without Wayland clients");
        return;
    }
    if (!runtime->options.publish_wayland_socket) {
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

static bool show_about_window(struct nb_desktop_runtime *runtime)
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

static bool close_about_window(struct nb_desktop_runtime *runtime,
                               nb_window_id window)
{
    if (window == NB_WINDOW_ID_NONE || window != runtime->about_window) {
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

static bool open_nixinfo(struct nb_desktop_runtime *runtime)
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

static bool apply_shell_action(struct nb_desktop_runtime *runtime,
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
        if (action.menu_command == NIXBENCH_DESKTOP_COMMAND_OPEN_NIXINFO) {
            return open_nixinfo(runtime);
        }
        if (action.menu_command == NIXBENCH_DESKTOP_COMMAND_QUIT) {
            runtime->quit_requested = true;
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
    if (runtime->wayland != NULL &&
        nb_wayland_server_owns_window(runtime->wayland, action.window)) {
        if (nb_wayland_server_dispatch_menu_command(runtime->wayland,
                                                    action.window,
                                                    action.menu_source,
                                                    action.menu_command)) {
            return true;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Ignored invalid Wayland application menu command %u",
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
    const struct nb_desktop_runtime *runtime,
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

static bool update_wayland_pointer(
    struct nb_desktop_runtime *runtime,
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
                                  struct nb_desktop_runtime *runtime)
{
    struct nb_shell_pointer_target target;
    int x;
    int y;

    if (event->type == NB_HOST_EVENT_POINTER_MOTION) {
        x = event->data.pointer_motion.x;
        y = event->data.pointer_motion.y;
        remember_pointer(runtime, x, y);
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
        return true;
    }

    x = event->data.pointer_button.x;
    y = event->data.pointer_button.y;
    remember_pointer(runtime, x, y);
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
                return nb_wayland_server_pointer_button(
                    runtime->wayland,
                    wayland_hover_window(runtime, target),
                    x,
                    y,
                    (uint32_t)event->milliseconds,
                    NB_WAYLAND_POINTER_BUTTON_LEFT,
                    true);
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
                return nb_wayland_server_pointer_button(
                    runtime->wayland,
                    wayland_hover_window(runtime, target),
                    x,
                    y,
                    (uint32_t)event->milliseconds,
                    NB_WAYLAND_POINTER_BUTTON_LEFT,
                    false);
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
static bool shell_key_is_captured(
    const struct nb_desktop_runtime *runtime,
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

static void capture_shell_key(struct nb_desktop_runtime *runtime,
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

static bool release_shell_key(struct nb_desktop_runtime *runtime,
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
                              struct nb_desktop_runtime *runtime)
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
        runtime->quit_requested = true;
        return true;
    }
#if NIXBENCH_HAS_WAYLAND
    return forward_wayland_key(runtime, event);
#else
    return true;
#endif
}

static void cancel_pointer_input(struct nb_desktop_runtime *runtime,
                                 uint64_t milliseconds)
{
    nb_shell_pointer_cancel(&runtime->shell);
#if NIXBENCH_HAS_WAYLAND
    nb_wayland_server_pointer_cancel(runtime->wayland,
                                     (uint32_t)milliseconds);
#else
    (void)milliseconds;
#endif
}

static void cancel_keyboard_input(struct nb_desktop_runtime *runtime,
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

static bool render_software_pointer(SDL_Renderer *renderer,
                                    int pointer_x,
                                    int pointer_y)
{
    static const char *const bitmap[] = {
        "B...........",
        "BW..........",
        "BWW.........",
        "BWWW........",
        "BWWWW.......",
        "BWWWWW......",
        "BWWWWWW.....",
        "BWWWWWWW....",
        "BWWWWWWWW...",
        "BWWWWB......",
        "BWWBWB......",
        "BWB.BW......",
        "BB..BWW.....",
        "B...BWW.....",
        "....BWW.....",
        "....BBB....."
    };
    size_t row;

    for (row = 0; row < sizeof(bitmap) / sizeof(bitmap[0]); ++row) {
        size_t column;

        for (column = 0; bitmap[row][column] != '\0'; ++column) {
            const char pixel = bitmap[row][column];

            if (pixel == '.') {
                continue;
            }
            if (!SDL_SetRenderDrawColor(renderer,
                                        pixel == 'W' ? 255 : 0,
                                        pixel == 'W' ? 255 : 0,
                                        pixel == 'W' ? 255 : 0,
                                        SDL_ALPHA_OPAQUE) ||
                !SDL_RenderPoint(renderer,
                                 (float)(pointer_x + (int)column),
                                 (float)(pointer_y + (int)row))) {
                return false;
            }
        }
    }
    return true;
}

void nb_desktop_runtime_options_init(
    struct nb_desktop_runtime_options *options)
{
    if (options != NULL) {
        (void)memset(options, 0, sizeof(*options));
    }
}

struct nb_desktop_runtime *nb_desktop_runtime_create(
    const struct nb_desktop_runtime_options *options,
    const struct nb_host_output *initial_output)
{
    struct nb_desktop_runtime_options defaults;
    struct nb_desktop_runtime *runtime;
    struct nb_application_spec nixinfo_spec;

    if (!nb_host_output_is_valid(initial_output)) {
        return NULL;
    }
    nb_desktop_runtime_options_init(&defaults);
    if (options == NULL) {
        options = &defaults;
    }
    if (options->publish_wayland_socket && !options->enable_wayland) {
        return NULL;
    }

    runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return NULL;
    }
    runtime->options = *options;
    runtime->about_window = NB_WINDOW_ID_NONE;
    runtime->nixinfo_application = NB_APPLICATION_ID_NONE;
    nb_shell_init(&runtime->shell,
                  NIXBENCH_MENU_SOURCE_DESKTOP,
                  &desktop_menu_model);
    if (!nb_application_host_init(&runtime->applications,
                                  &runtime->shell)) {
        free(runtime);
        return NULL;
    }
    nb_nixinfo_init(&runtime->nixinfo, NULL, NULL);
    nixinfo_spec = nb_nixinfo_application_spec(&runtime->nixinfo);
    runtime->nixinfo_application =
        nb_application_host_register(&runtime->applications, &nixinfo_spec);
    if (runtime->nixinfo_application == NB_APPLICATION_ID_NONE ||
        !nb_application_host_start(&runtime->applications,
                                   runtime->nixinfo_application) ||
        !nb_desktop_runtime_set_output(runtime, initial_output)) {
        nb_desktop_runtime_destroy(runtime);
        return NULL;
    }

    runtime->pointer_x = (initial_output->logical_width - 1) / 2;
    runtime->pointer_y = (initial_output->logical_height - 1) / 2;
#if NIXBENCH_HAS_WAYLAND
    if (runtime->options.enable_wayland) {
        start_wayland(runtime);
    }
#endif
    return runtime;
}

void nb_desktop_runtime_destroy(struct nb_desktop_runtime *runtime)
{
    if (runtime == NULL) {
        return;
    }
#if NIXBENCH_HAS_WAYLAND
    nb_wayland_server_destroy(runtime->wayland);
    runtime->wayland = NULL;
#endif
    if (runtime->nixinfo_application != NB_APPLICATION_ID_NONE &&
        nb_application_host_is_running(&runtime->applications,
                                       runtime->nixinfo_application) &&
        !nb_application_host_stop(&runtime->applications,
                                  runtime->nixinfo_application)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not stop NixInfo cleanly");
    }
    nb_software_canvas_destroy(runtime->canvas);
    runtime->canvas = NULL;
    free(runtime);
}

bool nb_desktop_runtime_set_output(
    struct nb_desktop_runtime *runtime,
    const struct nb_host_output *output)
{
    struct nb_software_canvas *canvas;
    SDL_Renderer *renderer;
    struct nb_rect viewport;

    if (runtime == NULL || !nb_host_output_is_valid(output)) {
        return false;
    }
    canvas = nb_software_canvas_create(output->pixel_width,
                                       output->pixel_height);
    renderer = nb_software_canvas_renderer(canvas);
    if (renderer == NULL ||
        !SDL_SetRenderLogicalPresentation(
            renderer,
            output->logical_width,
            output->logical_height,
            SDL_LOGICAL_PRESENTATION_STRETCH)) {
        nb_software_canvas_destroy(canvas);
        return false;
    }

    viewport.x = 0;
    viewport.y = 0;
    viewport.width = output->logical_width;
    viewport.height = output->logical_height;
#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL &&
        !nb_wayland_server_set_output_size(runtime->wayland,
                                           output->logical_width,
                                           output->logical_height)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not update the Wayland output size");
        nb_software_canvas_destroy(canvas);
        return false;
    }
#endif
    cancel_pointer_input(runtime, 0);
    nb_software_canvas_destroy(runtime->canvas);
    runtime->canvas = canvas;
    runtime->output = *output;
    runtime->viewport = viewport;
    runtime->pointer_x = clamp_coordinate(runtime->pointer_x,
                                          viewport.width);
    runtime->pointer_y = clamp_coordinate(runtime->pointer_y,
                                          viewport.height);
    (void)nb_shell_clamp_windows(&runtime->shell, viewport);
    return true;
}

bool nb_desktop_runtime_get_output(
    const struct nb_desktop_runtime *runtime,
    struct nb_host_output *output)
{
    if (runtime == NULL || output == NULL ||
        !nb_host_output_is_valid(&runtime->output)) {
        return false;
    }
    *output = runtime->output;
    return true;
}

bool nb_desktop_runtime_set_pointer(struct nb_desktop_runtime *runtime,
                                    int x,
                                    int y,
                                    bool visible)
{
    if (runtime == NULL || runtime->viewport.width <= 0 ||
        runtime->viewport.height <= 0) {
        return false;
    }
    runtime->pointer_x = clamp_coordinate(x, runtime->viewport.width);
    runtime->pointer_y = clamp_coordinate(y, runtime->viewport.height);
    runtime->pointer_visible = visible && runtime->options.software_pointer;
    return true;
}

bool nb_desktop_runtime_handle_input(
    struct nb_desktop_runtime *runtime,
    const struct nb_host_event *event,
    struct nb_desktop_runtime_update *update)
{
    bool handled;

    if (runtime == NULL || event == NULL || update == NULL ||
        !nb_host_event_is_valid(event)) {
        return false;
    }
    clear_update(update);
    if (event->type == NB_HOST_EVENT_POINTER_MOTION ||
        event->type == NB_HOST_EVENT_POINTER_BUTTON) {
        handled = process_pointer_event(event, runtime);
    } else if (event->type == NB_HOST_EVENT_KEY) {
        handled = process_key_event(event, runtime);
    } else {
        return false;
    }
    if (!handled) {
        return false;
    }
    update->redraw = true;
    update->quit_requested = runtime->quit_requested;
    return true;
}

bool nb_desktop_runtime_set_focus(
    struct nb_desktop_runtime *runtime,
    bool focused,
    uint64_t milliseconds,
    struct nb_desktop_runtime_update *update)
{
    if (runtime == NULL || update == NULL) {
        return false;
    }
    clear_update(update);
    if (!focused) {
        cancel_pointer_input(runtime, milliseconds);
        cancel_keyboard_input(runtime, milliseconds);
    }
#if NIXBENCH_HAS_WAYLAND
    else {
        runtime->host_keyboard_focused = true;
        if (!sync_application_focus(runtime)) {
            return false;
        }
    }
#else
    (void)milliseconds;
#endif
    update->redraw = true;
    update->quit_requested = runtime->quit_requested;
    return true;
}

bool nb_desktop_runtime_pointer_leave(
    struct nb_desktop_runtime *runtime,
    bool actual_capture,
    uint64_t milliseconds,
    struct nb_desktop_runtime_update *update)
{
    if (runtime == NULL || update == NULL) {
        return false;
    }
    clear_update(update);
    if (!actual_capture) {
        cancel_pointer_input(runtime, milliseconds);
        update->redraw = true;
    }
    update->quit_requested = runtime->quit_requested;
    return true;
}

void nb_desktop_runtime_cancel_input(
    struct nb_desktop_runtime *runtime,
    uint64_t milliseconds)
{
    if (runtime == NULL) {
        return;
    }
    cancel_pointer_input(runtime, milliseconds);
    cancel_keyboard_input(runtime, milliseconds);
}

bool nb_desktop_runtime_dispatch(
    struct nb_desktop_runtime *runtime,
    struct nb_desktop_runtime_update *update)
{
    if (runtime == NULL || update == NULL) {
        return false;
    }
    clear_update(update);
#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL) {
        if (!nb_wayland_server_dispatch(runtime->wayland) ||
            !sync_application_focus(runtime)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not dispatch the nested Wayland display");
            return false;
        }
        update->redraw =
            nb_wayland_server_take_redraw(runtime->wayland);
    }
#endif
    update->quit_requested = runtime->quit_requested;
    return true;
}

bool nb_desktop_runtime_render(
    struct nb_desktop_runtime *runtime,
    const char *clock_text,
    uint64_t serial,
    struct nb_host_frame *frame)
{
    SDL_Renderer *renderer;

    if (runtime == NULL || clock_text == NULL || clock_text[0] == '\0' ||
        serial == 0 || frame == NULL) {
        return false;
    }
    renderer = nb_software_canvas_renderer(runtime->canvas);
    if (renderer == NULL ||
        !SDL_SetRenderDrawColor(renderer,
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
                                      runtime) ||
        (runtime->pointer_visible &&
         !render_software_pointer(renderer,
                                  runtime->pointer_x,
                                  runtime->pointer_y))) {
        return false;
    }
    return nb_software_canvas_finish(runtime->canvas, serial, frame);
}

void nb_desktop_runtime_frame_presented(
    struct nb_desktop_runtime *runtime,
    uint64_t milliseconds)
{
#if NIXBENCH_HAS_WAYLAND
    if (runtime != NULL && runtime->wayland != NULL) {
        nb_wayland_server_frame_presented(runtime->wayland,
                                          (uint32_t)milliseconds);
    }
#else
    (void)runtime;
    (void)milliseconds;
#endif
}

bool nb_desktop_runtime_wants_pointer_capture(
    const struct nb_desktop_runtime *runtime)
{
    bool wanted;

    if (runtime == NULL) {
        return false;
    }
    wanted = nb_shell_has_pointer_interaction(&runtime->shell);
#if NIXBENCH_HAS_WAYLAND
    wanted = wanted ||
             (runtime->wayland != NULL &&
              nb_wayland_server_pointer_grab_window(runtime->wayland) !=
                  NB_WINDOW_ID_NONE);
#endif
    return wanted;
}

bool nb_desktop_runtime_quit_requested(
    const struct nb_desktop_runtime *runtime)
{
    return runtime != NULL && runtime->quit_requested;
}

const char *nb_desktop_runtime_wayland_display_name(
    const struct nb_desktop_runtime *runtime)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL
               ? nb_wayland_server_display_name(runtime->wayland)
               : NULL;
#else
    (void)runtime;
    return NULL;
#endif
}

size_t nb_desktop_runtime_window_count(
    const struct nb_desktop_runtime *runtime)
{
    return runtime == NULL
               ? 0
               : nb_desktop_window_count(&runtime->shell.desktop);
}

bool nb_desktop_runtime_active_window_frame(
    const struct nb_desktop_runtime *runtime,
    nb_window_id *window,
    struct nb_rect *frame)
{
    const struct nb_window *active;
    nb_window_id id;

    if (runtime == NULL || window == NULL || frame == NULL) {
        return false;
    }
    id = nb_desktop_active_window_id(&runtime->shell.desktop);
    active = nb_desktop_find_window(&runtime->shell.desktop, id);
    if (id == NB_WINDOW_ID_NONE || active == NULL) {
        return false;
    }
    *window = id;
    *frame = active->frame;
    return true;
}
