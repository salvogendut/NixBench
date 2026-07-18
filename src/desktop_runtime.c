#include "desktop_runtime.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "application.h"
#include "backdrop_renderer.h"
#include "menu.h"
#include "nixinfo.h"
#include "nixinfo_renderer.h"
#include "settings_ui.h"
#include "shell.h"
#include "shell_renderer.h"
#include "screenshot.h"
#include "software_canvas.h"
#include "wallpaper_chooser.h"
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
    NIXBENCH_SETTINGS_WINDOW_WIDTH = 640,
    NIXBENCH_SETTINGS_WINDOW_HEIGHT = 550,
    NIXBENCH_WALLPAPER_WINDOW_WIDTH = 800,
    NIXBENCH_WALLPAPER_WINDOW_HEIGHT = 580,
    NIXBENCH_SHELL_KEY_CAPTURE_CAPACITY = 32
};

#define NIXBENCH_MENU_SOURCE_DESKTOP UINT64_C(1)
#define NIXBENCH_MENU_SOURCE_ABOUT UINT64_MAX
#define NIXBENCH_MENU_SOURCE_SETTINGS (UINT64_MAX - UINT64_C(1))
#define NIXBENCH_MENU_SOURCE_WALLPAPER (UINT64_MAX - UINT64_C(2))
#define NIXBENCH_MENU_SOURCE_WAYLAND UINT64_C(0x4000000000000000)

enum {
    NIXBENCH_DESKTOP_COMMAND_ABOUT = 1,
    NIXBENCH_DESKTOP_COMMAND_OPEN_NIXINFO,
    NIXBENCH_DESKTOP_COMMAND_SETTINGS,
    NIXBENCH_DESKTOP_COMMAND_SCREENSHOT,
    NIXBENCH_DESKTOP_COMMAND_QUIT
};

#define NIXBENCH_DESKTOP_COMMAND_LAUNCH_NIXCLOCK UINT32_C(0xfffffff0)
#define NIXBENCH_DESKTOP_COMMAND_LAUNCH_SAKURA UINT32_C(0xfffffff1)
#define NIXBENCH_DESKTOP_COMMAND_LAUNCH_MIDORI UINT32_C(0xfffffff2)
#define NIXBENCH_DESKTOP_COMMAND_APPLICATION_PINS UINT32_C(0xfffffff3)

enum {
    NIXBENCH_ABOUT_COMMAND_CLOSE = 1
};

enum {
    NIXBENCH_SETTINGS_COMMAND_CLOSE = 1
};

enum {
    NIXBENCH_WALLPAPER_COMMAND_CLOSE = 1
};

enum {
    NIXBENCH_WAYLAND_COMMAND_CLOSE = 1
};

static const struct nb_menu_item_spec desktop_items[] = {
    {"About NixBench", NIXBENCH_DESKTOP_COMMAND_ABOUT,
     NB_MENU_ITEM_COMMAND, true, false},
    {"Open NixInfo", NIXBENCH_DESKTOP_COMMAND_OPEN_NIXINFO,
     NB_MENU_ITEM_COMMAND, true, false},
    {"Settings...", NIXBENCH_DESKTOP_COMMAND_SETTINGS,
     NB_MENU_ITEM_COMMAND, true, false},
    {"Take Screenshot", NIXBENCH_DESKTOP_COMMAND_SCREENSHOT,
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

static const struct nb_menu_item_spec settings_items[] = {
    {"Close Settings", NIXBENCH_SETTINGS_COMMAND_CLOSE,
     NB_MENU_ITEM_COMMAND, true, false}
};

static const struct nb_menu_spec settings_menus[] = {
    {"NixBench", settings_items,
     sizeof(settings_items) / sizeof(settings_items[0])}
};

static const struct nb_menu_model settings_menu_model = {
    settings_menus,
    sizeof(settings_menus) / sizeof(settings_menus[0])
};

static const struct nb_menu_item_spec wallpaper_items[] = {
    {"Close Wallpaper Chooser", NIXBENCH_WALLPAPER_COMMAND_CLOSE,
     NB_MENU_ITEM_COMMAND, true, false}
};

static const struct nb_menu_spec wallpaper_menus[] = {
    {"NixBench", wallpaper_items,
     sizeof(wallpaper_items) / sizeof(wallpaper_items[0])}
};

static const struct nb_menu_model wallpaper_menu_model = {
    wallpaper_menus,
    sizeof(wallpaper_menus) / sizeof(wallpaper_menus[0])
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
    struct nb_backdrop_cache *backdrop_cache;
    struct nb_host_output output;
    struct nb_rect viewport;
    nb_application_id nixinfo_application;
    nb_window_id about_window;
    nb_window_id settings_window;
    nb_window_id wallpaper_window;
    struct nb_wallpaper_chooser wallpaper_chooser;
    struct nb_user_preferences preferences;
    struct nb_menu_item_spec launcher_items[5];
    struct nb_menu_spec launcher_menus[1];
    struct nb_menu_model launcher_menu_model;
    enum nb_settings_color_target settings_color_target;
    enum nb_settings_action settings_pressed_action;
    struct nb_wallpaper_chooser_action wallpaper_pressed_action;
    bool pending_preferences_changed;
#if NIXBENCH_HAS_WAYLAND
    struct nb_wayland_server *wayland;
    struct nb_wayland_renderer *wayland_renderer;
    bool host_keyboard_focused;
    char shell_key_capture[NIXBENCH_SHELL_KEY_CAPTURE_CAPACITY]
                          [NB_HOST_XKB_KEY_NAME_CAPACITY];
#endif
    int pointer_x;
    int pointer_y;
    bool pointer_visible;
    bool quit_requested;
    uint64_t screenshot_deadline;
    uint64_t screenshot_notice_deadline;
    unsigned int screenshot_countdown;
    bool screenshot_start_pending;
    bool screenshot_countdown_active;
    bool screenshot_capture_pending;
    bool screenshot_notice_pending;
    bool screenshot_notice_active;
    bool screenshot_notice_success;
    enum nb_desktop_launch_request pending_launch_request;
    bool render_damage_valid;
    struct nb_damage_region render_damage;
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

static void set_launcher_item(struct nb_menu_item_spec *item,
                              const char *label,
                              nb_menu_command command,
                              enum nb_menu_item_kind kind)
{
    item->label = label;
    item->command = command;
    item->kind = kind;
    item->enabled = kind == NB_MENU_ITEM_COMMAND;
    item->checked = false;
}

static void rebuild_launcher_menu(struct nb_desktop_runtime *runtime)
{
    size_t count = 0;

    if (runtime->preferences.pinned_applications[
            NB_PINNED_APPLICATION_NIXCLOCK]) {
        set_launcher_item(&runtime->launcher_items[count++],
                          "NixClock",
                          NIXBENCH_DESKTOP_COMMAND_LAUNCH_NIXCLOCK,
                          NB_MENU_ITEM_COMMAND);
    }
    if (runtime->preferences.pinned_applications[
            NB_PINNED_APPLICATION_SAKURA]) {
        set_launcher_item(&runtime->launcher_items[count++],
                          "Sakura Terminal",
                          NIXBENCH_DESKTOP_COMMAND_LAUNCH_SAKURA,
                          NB_MENU_ITEM_COMMAND);
    }
    if (runtime->preferences.pinned_applications[
            NB_PINNED_APPLICATION_MIDORI]) {
        set_launcher_item(&runtime->launcher_items[count++],
                          "Midori Web Browser",
                          NIXBENCH_DESKTOP_COMMAND_LAUNCH_MIDORI,
                          NB_MENU_ITEM_COMMAND);
    }
    if (count != 0) {
        set_launcher_item(&runtime->launcher_items[count++],
                          NULL,
                          NB_MENU_COMMAND_NONE,
                          NB_MENU_ITEM_SEPARATOR);
    }
    set_launcher_item(&runtime->launcher_items[count++],
                      "Edit Application Pins...",
                      NIXBENCH_DESKTOP_COMMAND_APPLICATION_PINS,
                      NB_MENU_ITEM_COMMAND);
    runtime->launcher_menus[0].label = "Applications";
    runtime->launcher_menus[0].items = runtime->launcher_items;
    runtime->launcher_menus[0].item_count = count;
    runtime->launcher_menu_model.menus = runtime->launcher_menus;
    runtime->launcher_menu_model.menu_count = 1;

    nb_shell_set_menu_overlay(&runtime->shell, NULL);
    if (runtime->options.enable_application_launcher) {
        nb_shell_set_menu_overlay(&runtime->shell,
                                  &runtime->launcher_menu_model);
    }
}

static bool render_window_content(SDL_Renderer *renderer,
                                  nb_window_id id,
                                  const struct nb_window *window,
                                  struct nb_rect content_rect,
                                  void *context)
{
    struct nb_desktop_runtime *runtime = context;
    bool outside_damage = runtime->render_damage_valid;
    size_t damage_index;

    for (damage_index = 0;
         outside_damage && damage_index < runtime->render_damage.count;
         ++damage_index) {
        const struct nb_damage_rect damage =
            runtime->render_damage.rects[damage_index];

        outside_damage =
            !nb_damage_rect_intersects(
                damage,
                (struct nb_damage_rect){content_rect.x,
                                         content_rect.y,
                                         content_rect.width,
                                         content_rect.height});
    }

    if (outside_damage) {
        return true;
    }

#if NIXBENCH_HAS_WAYLAND
    if (runtime->wayland != NULL &&
        nb_wayland_server_owns_window(runtime->wayland, id)) {
        return nb_wayland_render_content(renderer,
                                         id,
                                         window,
                                         content_rect,
                                         runtime->wayland_renderer);
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
    if (id == runtime->settings_window) {
        return nb_settings_render(renderer,
                                  content_rect,
                                  &runtime->preferences,
                                  runtime->settings_color_target);
    }
    if (id == runtime->wallpaper_window) {
        return nb_wallpaper_chooser_render(&runtime->wallpaper_chooser,
                                           renderer,
                                           content_rect);
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
    runtime->wayland_renderer =
        nb_wayland_renderer_create(runtime->wayland);
    if (runtime->wayland_renderer == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not initialize the Wayland texture cache; "
                    "continuing without Wayland clients");
        nb_wayland_server_destroy(runtime->wayland);
        runtime->wayland = NULL;
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

static bool show_settings_window(struct nb_desktop_runtime *runtime)
{
    const struct nb_rect work_area = nb_menu_work_area(runtime->viewport);
    int width = NIXBENCH_SETTINGS_WINDOW_WIDTH;
    int height = NIXBENCH_SETTINGS_WINDOW_HEIGHT;
    struct nb_rect frame;

    if (runtime->settings_window != NB_WINDOW_ID_NONE &&
        nb_desktop_find_window(&runtime->shell.desktop,
                               runtime->settings_window) != NULL) {
        return nb_shell_activate_window(&runtime->shell,
                                        runtime->settings_window) &&
               sync_application_focus(runtime);
    }
    if (width > work_area.width - 24) {
        width = work_area.width - 24;
    }
    if (height > work_area.height - 24) {
        height = work_area.height - 24;
    }
    if (width < NB_WINDOW_MIN_WIDTH) {
        width = NB_WINDOW_MIN_WIDTH;
    }
    if (height < NB_WINDOW_MIN_HEIGHT) {
        height = NB_WINDOW_MIN_HEIGHT;
    }
    frame = (struct nb_rect){
        work_area.x + (work_area.width - width) / 2,
        work_area.y + (work_area.height - height) / 2,
        width,
        height
    };
    runtime->settings_window = nb_shell_open_window(
        &runtime->shell,
        "NixBench Settings",
        frame,
        NIXBENCH_MENU_SOURCE_SETTINGS,
        &settings_menu_model);
    if (runtime->settings_window == NB_WINDOW_ID_NONE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not open the NixBench Settings window");
        return false;
    }
    return sync_application_focus(runtime);
}

static bool close_settings_window(struct nb_desktop_runtime *runtime,
                                  nb_window_id window)
{
    if (window == NB_WINDOW_ID_NONE || window != runtime->settings_window) {
        return false;
    }
    if (!nb_shell_destroy_window(&runtime->shell, window)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not close the NixBench Settings window");
        return false;
    }
    runtime->settings_window = NB_WINDOW_ID_NONE;
    runtime->settings_pressed_action = NB_SETTINGS_ACTION_NONE;
    return sync_application_focus(runtime);
}

static bool show_wallpaper_window(struct nb_desktop_runtime *runtime)
{
    const struct nb_rect work_area = nb_menu_work_area(runtime->viewport);
    const char *home = getenv("HOME");
    int width = NIXBENCH_WALLPAPER_WINDOW_WIDTH;
    int height = NIXBENCH_WALLPAPER_WINDOW_HEIGHT;
    struct nb_rect frame;

    if (runtime->wallpaper_window != NB_WINDOW_ID_NONE &&
        nb_desktop_find_window(&runtime->shell.desktop,
                               runtime->wallpaper_window) != NULL) {
        return nb_shell_activate_window(&runtime->shell,
                                        runtime->wallpaper_window) &&
               sync_application_focus(runtime);
    }
    if (!nb_wallpaper_chooser_open(&runtime->wallpaper_chooser,
                                   runtime->preferences.wallpaper,
                                   home)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not open a directory for the wallpaper chooser");
        return false;
    }
    if (width > work_area.width - 24) {
        width = work_area.width - 24;
    }
    if (height > work_area.height - 24) {
        height = work_area.height - 24;
    }
    if (width < NB_WINDOW_MIN_WIDTH) {
        width = NB_WINDOW_MIN_WIDTH;
    }
    if (height < NB_WINDOW_MIN_HEIGHT) {
        height = NB_WINDOW_MIN_HEIGHT;
    }
    frame = (struct nb_rect){
        work_area.x + (work_area.width - width) / 2,
        work_area.y + (work_area.height - height) / 2,
        width,
        height
    };
    runtime->wallpaper_window = nb_shell_open_window(
        &runtime->shell,
        "Choose Wallpaper",
        frame,
        NIXBENCH_MENU_SOURCE_WALLPAPER,
        &wallpaper_menu_model);
    if (runtime->wallpaper_window == NB_WINDOW_ID_NONE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not open the wallpaper chooser window");
        return false;
    }
    return sync_application_focus(runtime);
}

static bool close_wallpaper_window(struct nb_desktop_runtime *runtime,
                                   nb_window_id window)
{
    if (window == NB_WINDOW_ID_NONE || window != runtime->wallpaper_window) {
        return false;
    }
    if (!nb_shell_destroy_window(&runtime->shell, window)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not close the wallpaper chooser window");
        return false;
    }
    runtime->wallpaper_window = NB_WINDOW_ID_NONE;
    runtime->wallpaper_pressed_action =
        (struct nb_wallpaper_chooser_action){
            NB_WALLPAPER_CHOOSER_ACTION_NONE,
            0
        };
    nb_wallpaper_chooser_invalidate_preview(&runtime->wallpaper_chooser);
    return sync_application_focus(runtime);
}

static bool apply_wallpaper_action(
    struct nb_desktop_runtime *runtime,
    struct nb_wallpaper_chooser_action action)
{
    char selected[NB_PREFERENCES_WALLPAPER_CAPACITY] = {0};
    const enum nb_wallpaper_chooser_result result =
        nb_wallpaper_chooser_activate(&runtime->wallpaper_chooser,
                                      action,
                                      selected,
                                      sizeof(selected));

    if (result == NB_WALLPAPER_CHOOSER_RESULT_SELECTED ||
        result == NB_WALLPAPER_CHOOSER_RESULT_CLEARED) {
        if (strcmp(runtime->preferences.wallpaper, selected) != 0) {
            (void)memcpy(runtime->preferences.wallpaper,
                         selected,
                         strlen(selected) + 1);
            runtime->pending_preferences_changed = true;
            nb_backdrop_cache_invalidate(runtime->backdrop_cache);
        }
        return close_wallpaper_window(runtime, runtime->wallpaper_window);
    }
    if (result == NB_WALLPAPER_CHOOSER_RESULT_CANCELLED) {
        return close_wallpaper_window(runtime, runtime->wallpaper_window);
    }
    return true;
}

static bool apply_settings_action(struct nb_desktop_runtime *runtime,
                                  enum nb_settings_action action)
{
    struct nb_color color;
    bool changed = false;

    if (action == NB_SETTINGS_ACTION_SELECT_PRIMARY) {
        runtime->settings_color_target = NB_SETTINGS_COLOR_PRIMARY;
        return true;
    }
    if (action == NB_SETTINGS_ACTION_SELECT_SECONDARY) {
        runtime->settings_color_target = NB_SETTINGS_COLOR_SECONDARY;
        return true;
    }
    if (action >= NB_SETTINGS_ACTION_COLOR_FIRST &&
        action <= NB_SETTINGS_ACTION_COLOR_LAST) {
        const size_t index =
            (size_t)(action - NB_SETTINGS_ACTION_COLOR_FIRST);
        struct nb_color *selected =
            runtime->settings_color_target == NB_SETTINGS_COLOR_PRIMARY
                ? &runtime->preferences.backdrop_primary
                : &runtime->preferences.backdrop_secondary;

        if (!nb_settings_palette_color(index, &color)) {
            return false;
        }
        if (!nb_color_equal(*selected, color)) {
            *selected = color;
            changed = true;
        }
    } else if (action == NB_SETTINGS_ACTION_TOGGLE_GRADIENT) {
        runtime->preferences.backdrop_gradient_enabled =
            !runtime->preferences.backdrop_gradient_enabled;
        changed = true;
    } else if (action == NB_SETTINGS_ACTION_CYCLE_GRADIENT_DIRECTION) {
        runtime->preferences.backdrop_gradient_direction =
            (enum nb_backdrop_gradient_direction)(
                (runtime->preferences.backdrop_gradient_direction + 1) % 3);
        changed = true;
    } else if (action == NB_SETTINGS_ACTION_TOGGLE_NIXCLOCK_PIN) {
        runtime->preferences.pinned_applications[
            NB_PINNED_APPLICATION_NIXCLOCK] =
            !runtime->preferences.pinned_applications[
                NB_PINNED_APPLICATION_NIXCLOCK];
        changed = true;
        rebuild_launcher_menu(runtime);
    } else if (action == NB_SETTINGS_ACTION_TOGGLE_SAKURA_PIN) {
        runtime->preferences.pinned_applications[
            NB_PINNED_APPLICATION_SAKURA] =
            !runtime->preferences.pinned_applications[
                NB_PINNED_APPLICATION_SAKURA];
        changed = true;
        rebuild_launcher_menu(runtime);
    } else if (action == NB_SETTINGS_ACTION_TOGGLE_MIDORI_PIN) {
        runtime->preferences.pinned_applications[
            NB_PINNED_APPLICATION_MIDORI] =
            !runtime->preferences.pinned_applications[
                NB_PINNED_APPLICATION_MIDORI];
        changed = true;
        rebuild_launcher_menu(runtime);
    } else if (action == NB_SETTINGS_ACTION_TOGGLE_MINIMIZE) {
        runtime->preferences.minimize_gadget_visible =
            !runtime->preferences.minimize_gadget_visible;
        changed = true;
        nb_shell_set_window_controls(
            &runtime->shell,
            runtime->preferences.minimize_gadget_visible,
            runtime->preferences.maximize_gadget_visible,
            runtime->preferences.window_control_layout);
    } else if (action == NB_SETTINGS_ACTION_TOGGLE_MAXIMIZE) {
        runtime->preferences.maximize_gadget_visible =
            !runtime->preferences.maximize_gadget_visible;
        changed = true;
        nb_shell_set_window_controls(
            &runtime->shell,
            runtime->preferences.minimize_gadget_visible,
            runtime->preferences.maximize_gadget_visible,
            runtime->preferences.window_control_layout);
    } else if (action == NB_SETTINGS_ACTION_CYCLE_CONTROL_LAYOUT) {
        runtime->preferences.window_control_layout =
            runtime->preferences.window_control_layout ==
                    NB_WINDOW_CONTROLS_LEFT
                ? NB_WINDOW_CONTROLS_RIGHT
                : NB_WINDOW_CONTROLS_LEFT;
        changed = true;
        nb_shell_set_window_controls(
            &runtime->shell,
            runtime->preferences.minimize_gadget_visible,
            runtime->preferences.maximize_gadget_visible,
            runtime->preferences.window_control_layout);
    } else if (action == NB_SETTINGS_ACTION_CHOOSE_WALLPAPER) {
        return show_wallpaper_window(runtime);
    } else if (action == NB_SETTINGS_ACTION_CYCLE_WALLPAPER_MODE) {
        runtime->preferences.wallpaper_mode =
            (enum nb_wallpaper_mode)((runtime->preferences.wallpaper_mode + 1) %
                                     4);
        changed = true;
        nb_backdrop_cache_invalidate(runtime->backdrop_cache);
    } else if (action != NB_SETTINGS_ACTION_NONE) {
        return false;
    }
    runtime->pending_preferences_changed =
        runtime->pending_preferences_changed || changed;
    return true;
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
    if (action.type == NB_SHELL_ACTION_MENU_COMMAND) {
        if (action.menu_command ==
            NIXBENCH_DESKTOP_COMMAND_LAUNCH_NIXCLOCK) {
            runtime->pending_launch_request = NB_DESKTOP_LAUNCH_NIXCLOCK;
            return true;
        }
        if (action.menu_command ==
            NIXBENCH_DESKTOP_COMMAND_LAUNCH_SAKURA) {
            runtime->pending_launch_request = NB_DESKTOP_LAUNCH_SAKURA;
            return true;
        }
        if (action.menu_command ==
            NIXBENCH_DESKTOP_COMMAND_LAUNCH_MIDORI) {
            runtime->pending_launch_request = NB_DESKTOP_LAUNCH_MIDORI;
            return true;
        }
        if (action.menu_command ==
            NIXBENCH_DESKTOP_COMMAND_APPLICATION_PINS) {
            return show_settings_window(runtime);
        }
    }

    const enum nb_application_dispatch_result dispatch =
        nb_application_host_dispatch_shell_action(&runtime->applications,
                                                   action);

    if (dispatch == NB_APPLICATION_DISPATCH_HANDLED) {
        return sync_application_focus(runtime);
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
        if (action.window == runtime->settings_window) {
            return close_settings_window(runtime, action.window);
        }
        if (action.window == runtime->wallpaper_window) {
            return close_wallpaper_window(runtime, action.window);
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
    if (action.type == NB_SHELL_ACTION_WINDOW_MINIMIZE_TOGGLED) {
        if (!nb_shell_toggle_window_minimized(&runtime->shell,
                                              action.window)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Could not toggle minimize for window %llu",
                        (unsigned long long)action.window);
            return false;
        }
        return sync_application_focus(runtime);
    }
    if (action.type == NB_SHELL_ACTION_WINDOW_MAXIMIZE_TOGGLED) {
        const struct nb_rect bounds = nb_menu_work_area(runtime->viewport);

        if (!nb_desktop_toggle_window_maximized(&runtime->shell.desktop,
                                                action.window,
                                                bounds)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Could not toggle maximize for window %llu",
                        (unsigned long long)action.window);
            return false;
        }
#if NIXBENCH_HAS_WAYLAND
        if (runtime->wayland != NULL &&
            nb_wayland_server_owns_window(runtime->wayland,
                                          action.window)) {
            if (!nb_wayland_server_window_resized(runtime->wayland,
                                                  action.window)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Could not update Wayland client window %llu",
                            (unsigned long long)action.window);
                return false;
            }
        }
#endif
        return true;
    }
    if (action.type == NB_SHELL_ACTION_WINDOW_RESIZED) {
#if NIXBENCH_HAS_WAYLAND
        if (runtime->wayland != NULL &&
            nb_wayland_server_owns_window(runtime->wayland,
                                          action.window)) {
            if (!nb_wayland_server_window_resized(runtime->wayland,
                                                  action.window)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Could not update Wayland client window %llu",
                            (unsigned long long)action.window);
                return false;
            }
        }
#endif
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
        if (action.menu_command == NIXBENCH_DESKTOP_COMMAND_SETTINGS) {
            return show_settings_window(runtime);
        }
        if (action.menu_command == NIXBENCH_DESKTOP_COMMAND_SCREENSHOT) {
            runtime->screenshot_start_pending = true;
            runtime->screenshot_countdown_active = false;
            runtime->screenshot_capture_pending = false;
            runtime->screenshot_notice_pending = false;
            runtime->screenshot_notice_active = false;
            return true;
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

    if (action.menu_source == NIXBENCH_MENU_SOURCE_SETTINGS) {
        if (action.menu_command == NIXBENCH_SETTINGS_COMMAND_CLOSE &&
            action.window == runtime->settings_window) {
            return close_settings_window(runtime, action.window);
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Ignored Settings menu command %u",
                    (unsigned int)action.menu_command);
        return true;
    }

    if (action.menu_source == NIXBENCH_MENU_SOURCE_WALLPAPER) {
        if (action.menu_command == NIXBENCH_WALLPAPER_COMMAND_CLOSE &&
            action.window == runtime->wallpaper_window) {
            return close_wallpaper_window(runtime, action.window);
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Ignored wallpaper chooser menu command %u",
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

static bool maybe_resize_wayland_window(struct nb_desktop_runtime *runtime)
{
    nb_window_id window;

    if (runtime->wayland == NULL ||
        runtime->shell.pointer_owner != NB_SHELL_POINTER_WINDOW) {
        return true;
    }
    window = runtime->shell.desktop.pointer_window;
    if (window == NB_WINDOW_ID_NONE ||
        !nb_wayland_server_owns_window(runtime->wayland, window)) {
        return true;
    }
    return nb_wayland_server_window_resized(runtime->wayland, window);
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
            runtime->settings_pressed_action = NB_SETTINGS_ACTION_NONE;
            runtime->wallpaper_pressed_action =
                (struct nb_wallpaper_chooser_action){
                    NB_WALLPAPER_CHOOSER_ACTION_NONE,
                    0
                };
            if (target.window == runtime->settings_window &&
                target.hit == NB_WINDOW_HIT_CONTENT) {
                const struct nb_window *settings =
                    nb_desktop_find_window(&runtime->shell.desktop,
                                           runtime->settings_window);

                if (settings != NULL) {
                    runtime->settings_pressed_action =
                        nb_settings_hit_test(nb_window_content_rect(settings),
                                             x,
                                             y);
                }
            }
            if (target.window == runtime->wallpaper_window &&
                target.hit == NB_WINDOW_HIT_CONTENT) {
                const struct nb_window *wallpaper =
                    nb_desktop_find_window(&runtime->shell.desktop,
                                           runtime->wallpaper_window);

                if (wallpaper != NULL) {
                    runtime->wallpaper_pressed_action =
                        nb_wallpaper_chooser_hit_test(
                            &runtime->wallpaper_chooser,
                            nb_window_content_rect(wallpaper),
                            x,
                            y);
                }
            }
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
            const enum nb_settings_action settings_action =
                runtime->settings_pressed_action;
            const struct nb_wallpaper_chooser_action wallpaper_action =
                runtime->wallpaper_pressed_action;

            runtime->settings_pressed_action = NB_SETTINGS_ACTION_NONE;
            runtime->wallpaper_pressed_action =
                (struct nb_wallpaper_chooser_action){
                    NB_WALLPAPER_CHOOSER_ACTION_NONE,
                    0
                };
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
            if (settings_action != NB_SETTINGS_ACTION_NONE &&
                target.window == runtime->settings_window &&
                target.hit == NB_WINDOW_HIT_CONTENT) {
                const struct nb_window *settings =
                    nb_desktop_find_window(&runtime->shell.desktop,
                                           runtime->settings_window);

                if (settings != NULL &&
                    nb_settings_hit_test(nb_window_content_rect(settings),
                                         x,
                                         y) == settings_action &&
                    !apply_settings_action(runtime, settings_action)) {
                    return false;
                }
            }
            if (wallpaper_action.type !=
                    NB_WALLPAPER_CHOOSER_ACTION_NONE &&
                target.window == runtime->wallpaper_window &&
                target.hit == NB_WINDOW_HIT_CONTENT) {
                const struct nb_window *wallpaper =
                    nb_desktop_find_window(&runtime->shell.desktop,
                                           runtime->wallpaper_window);

                if (wallpaper != NULL &&
                    nb_wallpaper_chooser_action_equal(
                        nb_wallpaper_chooser_hit_test(
                            &runtime->wallpaper_chooser,
                            nb_window_content_rect(wallpaper),
                            x,
                            y),
                        wallpaper_action) &&
                    !apply_wallpaper_action(runtime, wallpaper_action)) {
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

static bool shell_handles_menu_toggle(
    const struct nb_desktop_runtime *runtime,
    const char *key_name)
{
    if (strcmp(key_name, "FK10") != 0) {
        return false;
    }
#if NIXBENCH_HAS_WAYLAND
    /* Ordinary keys belong to the focused client, including function keys. */
    return !wayland_has_keyboard_target(runtime);
#else
    (void)runtime;
    return true;
#endif
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

    menu_context = menu_open || shell_handles_menu_toggle(runtime, key_name);
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
    runtime->settings_pressed_action = NB_SETTINGS_ACTION_NONE;
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

static bool render_screenshot_status(
    SDL_Renderer *renderer,
    const struct nb_desktop_runtime *runtime)
{
    char countdown[32];
    const char *text = NULL;
    Uint8 red = 34;
    Uint8 green = 76;
    Uint8 blue = 94;
    size_t text_length;
    int text_width;
    int width;
    int height = 58;
    int x;
    int y;
    SDL_FRect panel;

    if (runtime->screenshot_countdown_active) {
        (void)snprintf(countdown,
                       sizeof(countdown),
                       "SCREENSHOT IN %u",
                       runtime->screenshot_countdown);
        text = countdown;
    } else if (runtime->screenshot_notice_active) {
        text = runtime->screenshot_notice_success
                   ? "SCREENSHOT SAVED"
                   : "SCREENSHOT FAILED";
        if (runtime->screenshot_notice_success) {
            red = 37;
            green = 112;
            blue = 78;
        } else {
            red = 142;
            green = 47;
            blue = 39;
        }
    }
    if (text == NULL) {
        return true;
    }
    text_length = strlen(text);
    text_width = text_length > (size_t)(INT_MAX / 8)
                     ? INT_MAX
                     : (int)text_length * 8;
    width = text_width + 36;
    if (width > runtime->viewport.width - 24) {
        width = runtime->viewport.width - 24;
    }
    if (height > runtime->viewport.height - 24) {
        height = runtime->viewport.height - 24;
    }
    if (width <= 4 || height <= 4) {
        return true;
    }
    x = runtime->viewport.x + (runtime->viewport.width - width) / 2;
    y = runtime->viewport.y + (runtime->viewport.height - height) / 2;
    panel = (SDL_FRect){(float)x, (float)y, (float)width, (float)height};

    return SDL_SetRenderDrawColor(renderer,
                                  red,
                                  green,
                                  blue,
                                  SDL_ALPHA_OPAQUE) &&
           SDL_RenderFillRect(renderer, &panel) &&
           SDL_SetRenderDrawColor(renderer, 242, 246, 238, SDL_ALPHA_OPAQUE) &&
           SDL_RenderLine(renderer,
                          (float)x,
                          (float)(y + height - 1),
                          (float)x,
                          (float)y) &&
           SDL_RenderLine(renderer,
                          (float)x,
                          (float)y,
                          (float)(x + width - 1),
                          (float)y) &&
           SDL_SetRenderDrawColor(renderer, 20, 30, 34, SDL_ALPHA_OPAQUE) &&
           SDL_RenderLine(renderer,
                          (float)(x + width - 1),
                          (float)y,
                          (float)(x + width - 1),
                          (float)(y + height - 1)) &&
           SDL_RenderLine(renderer,
                          (float)(x + width - 1),
                          (float)(y + height - 1),
                          (float)x,
                          (float)(y + height - 1)) &&
           SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE) &&
           SDL_RenderDebugText(renderer,
                               (float)(x + (width - text_width) / 2),
                               (float)(y + (height - 8) / 2),
                               text);
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
    if (options->preferences != NULL &&
        !nb_user_preferences_is_valid(options->preferences)) {
        return NULL;
    }

    runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return NULL;
    }
    runtime->options = *options;
    runtime->about_window = NB_WINDOW_ID_NONE;
    runtime->settings_window = NB_WINDOW_ID_NONE;
    runtime->wallpaper_window = NB_WINDOW_ID_NONE;
    runtime->settings_color_target = NB_SETTINGS_COLOR_PRIMARY;
    runtime->nixinfo_application = NB_APPLICATION_ID_NONE;
    runtime->backdrop_cache = nb_backdrop_cache_create();
    if (runtime->backdrop_cache == NULL) {
        free(runtime);
        return NULL;
    }
    nb_wallpaper_chooser_init(&runtime->wallpaper_chooser);
    nb_user_preferences_init(&runtime->preferences);
    if (options->preferences != NULL) {
        runtime->preferences = *options->preferences;
    }
    nb_shell_init(&runtime->shell,
                  NIXBENCH_MENU_SOURCE_DESKTOP,
                  &desktop_menu_model);
    nb_shell_set_window_controls(
        &runtime->shell,
        runtime->preferences.minimize_gadget_visible,
        runtime->preferences.maximize_gadget_visible,
        runtime->preferences.window_control_layout);
    rebuild_launcher_menu(runtime);
    if (!nb_application_host_init(&runtime->applications,
                                  &runtime->shell)) {
        nb_backdrop_cache_destroy(runtime->backdrop_cache);
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
        !sync_application_focus(runtime) ||
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
    nb_wayland_renderer_destroy(runtime->wayland_renderer);
    runtime->wayland_renderer = NULL;
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
    nb_backdrop_cache_destroy(runtime->backdrop_cache);
    runtime->backdrop_cache = NULL;
    nb_wallpaper_chooser_destroy(&runtime->wallpaper_chooser);
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
    nb_backdrop_cache_invalidate(runtime->backdrop_cache);
    nb_wallpaper_chooser_invalidate_preview(&runtime->wallpaper_chooser);
#if NIXBENCH_HAS_WAYLAND
    nb_wayland_renderer_reset(runtime->wayland_renderer);
#endif
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
    update->launch_request = runtime->pending_launch_request;
    runtime->pending_launch_request = NB_DESKTOP_LAUNCH_NONE;
    update->preferences_changed = runtime->pending_preferences_changed;
    if (update->preferences_changed) {
        update->preferences = runtime->preferences;
        runtime->pending_preferences_changed = false;
    }
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
        update->redraw = nb_wayland_server_take_redraw_region(
            runtime->wayland,
            &update->damage_region);
        if (update->redraw && !update->damage_region.full) {
            struct nb_damage_rect bounds;

            update->damage_valid = nb_damage_region_bounds(
                &update->damage_region,
                runtime->viewport.width,
                runtime->viewport.height,
                &bounds);
            if (update->damage_valid) {
                update->damage = (struct nb_rect){bounds.x,
                                                  bounds.y,
                                                  bounds.width,
                                                  bounds.height};
            }
        }
    }
#endif
    update->quit_requested = runtime->quit_requested;
    return true;
}

int nb_desktop_runtime_event_descriptor(
    const struct nb_desktop_runtime *runtime)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL
               ? nb_wayland_server_event_descriptor(runtime->wayland)
               : -1;
#else
    (void)runtime;
    return -1;
#endif
}

static unsigned int screenshot_seconds_remaining(
    const struct nb_desktop_runtime *runtime,
    uint64_t milliseconds)
{
    uint64_t remaining;
    uint64_t seconds;

    if (!runtime->screenshot_countdown_active ||
        milliseconds >= runtime->screenshot_deadline) {
        return 0;
    }
    remaining = runtime->screenshot_deadline - milliseconds;
    seconds = remaining / UINT64_C(1000);
    if (remaining % UINT64_C(1000) != 0) {
        ++seconds;
    }
    return seconds > 5 ? 5 : (unsigned int)seconds;
}

bool nb_desktop_runtime_tick(
    struct nb_desktop_runtime *runtime,
    uint64_t milliseconds,
    struct nb_desktop_runtime_update *update)
{
    unsigned int seconds;

    if (runtime == NULL || update == NULL) {
        return false;
    }
    clear_update(update);
    if (runtime->screenshot_start_pending) {
        runtime->screenshot_start_pending = false;
        runtime->screenshot_deadline =
            milliseconds > UINT64_MAX - UINT64_C(5000)
                ? UINT64_MAX
                : milliseconds + UINT64_C(5000);
        runtime->screenshot_countdown = 5;
        runtime->screenshot_countdown_active = true;
        update->redraw = true;
    }
    seconds = screenshot_seconds_remaining(runtime, milliseconds);
    if (runtime->screenshot_countdown_active && seconds == 0) {
        runtime->screenshot_countdown_active = false;
        runtime->screenshot_countdown = 0;
        runtime->screenshot_capture_pending = true;
        update->redraw = true;
    } else if (seconds != 0 && seconds != runtime->screenshot_countdown) {
        runtime->screenshot_countdown = seconds;
        update->redraw = true;
    }
    if (runtime->screenshot_notice_pending) {
        runtime->screenshot_notice_pending = false;
        runtime->screenshot_notice_active = true;
        runtime->screenshot_notice_deadline =
            milliseconds > UINT64_MAX - UINT64_C(2500)
                ? UINT64_MAX
                : milliseconds + UINT64_C(2500);
        update->redraw = true;
    } else if (runtime->screenshot_notice_active &&
               milliseconds >= runtime->screenshot_notice_deadline) {
        runtime->screenshot_notice_active = false;
        update->redraw = true;
    }
    if (nb_nixinfo_tick(&runtime->nixinfo, milliseconds)) {
        update->redraw = true;
    }
    update->quit_requested = runtime->quit_requested;
    return true;
}

uint32_t nb_desktop_runtime_timer_timeout(
    const struct nb_desktop_runtime *runtime,
    uint64_t milliseconds)
{
    uint64_t remaining;
    unsigned int seconds;
    uint64_t until_boundary;

    uint32_t timeout;
    uint32_t nixinfo_timeout;

    if (runtime == NULL) {
        return UINT32_MAX;
    }
    nixinfo_timeout = nb_nixinfo_timer_timeout(&runtime->nixinfo,
                                               milliseconds);
    if (runtime->screenshot_start_pending ||
        runtime->screenshot_notice_pending) {
        return 0;
    }
    if (runtime->screenshot_notice_active) {
        uint64_t notice_remaining;
        uint32_t notice_timeout;

        if (milliseconds >= runtime->screenshot_notice_deadline) {
            return 0;
        }
        notice_remaining = runtime->screenshot_notice_deadline - milliseconds;
        notice_timeout = notice_remaining > UINT32_MAX
                             ? UINT32_MAX
                             : (uint32_t)notice_remaining;
        if (notice_timeout < nixinfo_timeout) {
            nixinfo_timeout = notice_timeout;
        }
    }
    if (!runtime->screenshot_countdown_active) {
        return nixinfo_timeout;
    }
    if (milliseconds >= runtime->screenshot_deadline) {
        return 0;
    }
    remaining = runtime->screenshot_deadline - milliseconds;
    seconds = screenshot_seconds_remaining(runtime, milliseconds);
    until_boundary = remaining - (uint64_t)(seconds - 1) * UINT64_C(1000);
    timeout = until_boundary > UINT32_MAX
                  ? UINT32_MAX
                  : (uint32_t)until_boundary;
    return nixinfo_timeout < timeout ? nixinfo_timeout : timeout;
}

bool nb_desktop_runtime_render(
    struct nb_desktop_runtime *runtime,
    const char *clock_text,
    uint64_t serial,
    struct nb_host_frame *frame)
{
    return nb_desktop_runtime_render_damage(runtime,
                                            clock_text,
                                            serial,
                                            NULL,
                                            frame);
}

bool nb_desktop_runtime_render_damage(
    struct nb_desktop_runtime *runtime,
    const char *clock_text,
    uint64_t serial,
    const struct nb_rect *damage,
    struct nb_host_frame *frame)
{
    struct nb_damage_region region;

    if (damage == NULL) {
        return nb_desktop_runtime_render_region(runtime,
                                                clock_text,
                                                serial,
                                                NULL,
                                                frame);
    }
    nb_damage_region_clear(&region);
    if (!nb_damage_region_add(
            &region,
            (struct nb_damage_rect){damage->x,
                                     damage->y,
                                     damage->width,
                                     damage->height},
            runtime != NULL ? runtime->viewport.width : 0,
            runtime != NULL ? runtime->viewport.height : 0)) {
        return false;
    }
    return nb_desktop_runtime_render_region(runtime,
                                            clock_text,
                                            serial,
                                            &region,
                                            frame);
}

bool nb_desktop_runtime_render_region(
    struct nb_desktop_runtime *runtime,
    const char *clock_text,
    uint64_t serial,
    const struct nb_damage_region *damage,
    struct nb_host_frame *frame)
{
    SDL_Renderer *renderer;
    SDL_Rect clip;
    struct nb_damage_rect bounds;
    char countdown_text[16];
    const char *bar_text = clock_text;
    bool rendered;

    if (runtime == NULL || clock_text == NULL || clock_text[0] == '\0' ||
        serial == 0 || frame == NULL) {
        return false;
    }
    if (damage != NULL &&
        (!nb_damage_region_is_valid(damage,
                                    runtime->viewport.width,
                                    runtime->viewport.height) ||
         damage->full || damage->count == 0 ||
         !nb_damage_region_bounds(damage,
                                  runtime->viewport.width,
                                  runtime->viewport.height,
                                  &bounds))) {
        return false;
    }
    renderer = nb_software_canvas_renderer(runtime->canvas);
    if (renderer == NULL) {
        return false;
    }
    runtime->render_damage_valid = damage != NULL;
    if (runtime->screenshot_countdown_active) {
        (void)snprintf(countdown_text,
                       sizeof(countdown_text),
                       "SHOT %u",
                       runtime->screenshot_countdown);
        bar_text = countdown_text;
    }
#if NIXBENCH_HAS_WAYLAND
    nb_wayland_renderer_set_damage(runtime->wayland_renderer, damage);
#endif
    if (damage != NULL) {
        runtime->render_damage = *damage;
        clip = (SDL_Rect){bounds.x,
                          bounds.y,
                          bounds.width,
                          bounds.height};
        if (!SDL_SetRenderClipRect(renderer, &clip)) {
            runtime->render_damage_valid = false;
            return false;
        }
    }
    rendered = nb_backdrop_cache_render(runtime->backdrop_cache,
                                        renderer,
                                        runtime->viewport,
                                        &runtime->preferences) &&
               nb_shell_render_with_content(renderer,
                                            &runtime->shell,
                                            runtime->viewport,
                                            bar_text,
                                            render_window_content,
                                            runtime) &&
               render_screenshot_status(renderer, runtime) &&
               (!runtime->pointer_visible ||
                render_software_pointer(renderer,
                                        runtime->pointer_x,
                                        runtime->pointer_y));
    runtime->render_damage_valid = false;
    if (damage != NULL && !SDL_SetRenderClipRect(renderer, NULL)) {
        return false;
    }
    if (!rendered ||
        !nb_software_canvas_finish(runtime->canvas, serial, frame)) {
        return false;
    }
    if (damage != NULL) {
        frame->damage_x = bounds.x;
        frame->damage_y = bounds.y;
        frame->damage_width = bounds.width;
        frame->damage_height = bounds.height;
        frame->damage_rects = damage->rects;
        frame->damage_count = damage->count;
    }
    if (runtime->screenshot_capture_pending && damage == NULL) {
        char saved_path[1024];
        char error[256];

        runtime->screenshot_capture_pending = false;
        if (nb_screenshot_save_home(frame,
                                    saved_path,
                                    sizeof(saved_path),
                                    error,
                                    sizeof(error))) {
            runtime->screenshot_notice_success = true;
            runtime->screenshot_notice_pending = true;
            SDL_Log("NixBench screenshot saved: %s", saved_path);
        } else {
            runtime->screenshot_notice_success = false;
            runtime->screenshot_notice_pending = true;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Could not save NixBench screenshot: %s",
                         error);
        }
    }
    return true;
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

bool nb_desktop_runtime_set_xwayland_interface(
    struct nb_desktop_runtime *runtime,
    const struct nb_desktop_xwayland_interface *interface,
    void *context)
{
#if NIXBENCH_HAS_WAYLAND
    struct nb_wayland_xwayland_interface wayland_interface;

    if (runtime == NULL || runtime->wayland == NULL) {
        return false;
    }
    if (interface == NULL) {
        nb_wayland_server_set_xwayland_interface(runtime->wayland,
                                                 NULL,
                                                 NULL);
        return true;
    }
    wayland_interface.configure_window = interface->configure_window;
    wayland_interface.close_window = interface->close_window;
    wayland_interface.focus_window = interface->focus_window;
    wayland_interface.set_clipboard_owner =
        interface->set_clipboard_owner;
    nb_wayland_server_set_xwayland_interface(runtime->wayland,
                                             &wayland_interface,
                                             context);
    return true;
#else
    (void)runtime;
    (void)interface;
    (void)context;
    return false;
#endif
}

bool nb_desktop_runtime_associate_xwayland_surface(
    struct nb_desktop_runtime *runtime,
    uint32_t surface_resource_id,
    uint32_t xwindow,
    const char *title,
    const char *application_name)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL &&
           nb_wayland_server_associate_xwayland_surface(
               runtime->wayland,
               surface_resource_id,
               xwindow,
               title,
               application_name);
#else
    (void)runtime;
    (void)surface_resource_id;
    (void)xwindow;
    (void)title;
    (void)application_name;
    return false;
#endif
}

bool nb_desktop_runtime_authorize_xwayland_client(
    struct nb_desktop_runtime *runtime,
    pid_t process)
{
#if NIXBENCH_HAS_WAYLAND
    if (runtime == NULL || runtime->wayland == NULL || process < 0) {
        return false;
    }
    nb_wayland_server_authorize_xwayland_client(runtime->wayland, process);
    return true;
#else
    (void)runtime;
    (void)process;
    return false;
#endif
}

bool nb_desktop_runtime_associate_xwayland_serial(
    struct nb_desktop_runtime *runtime,
    uint64_t surface_serial,
    uint32_t xwindow,
    const char *title,
    const char *application_name)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL &&
           nb_wayland_server_associate_xwayland_serial(
               runtime->wayland,
               surface_serial,
               xwindow,
               title,
               application_name);
#else
    (void)runtime;
    (void)surface_serial;
    (void)xwindow;
    (void)title;
    (void)application_name;
    return false;
#endif
}

bool nb_desktop_runtime_update_xwayland_identity(
    struct nb_desktop_runtime *runtime,
    uint32_t xwindow,
    const char *title,
    const char *application_name)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL &&
           nb_wayland_server_update_xwayland_identity(runtime->wayland,
                                                      xwindow,
                                                      title,
                                                      application_name);
#else
    (void)runtime;
    (void)xwindow;
    (void)title;
    (void)application_name;
    return false;
#endif
}

bool nb_desktop_runtime_set_xwayland_fullscreen(
    struct nb_desktop_runtime *runtime,
    uint32_t xwindow,
    bool fullscreen)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL &&
           nb_wayland_server_set_xwayland_fullscreen(runtime->wayland,
                                                     xwindow,
                                                     fullscreen);
#else
    (void)runtime;
    (void)xwindow;
    (void)fullscreen;
    return false;
#endif
}

bool nb_desktop_runtime_unmap_xwayland_window(
    struct nb_desktop_runtime *runtime,
    uint32_t xwindow)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL &&
           nb_wayland_server_unmap_xwayland_window(runtime->wayland,
                                                   xwindow);
#else
    (void)runtime;
    (void)xwindow;
    return false;
#endif
}

bool nb_desktop_runtime_set_external_clipboard_text(
    struct nb_desktop_runtime *runtime,
    const char *text,
    size_t size)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL &&
           nb_wayland_server_set_external_clipboard_text(
               runtime->wayland,
               text,
               size);
#else
    (void)runtime;
    (void)text;
    (void)size;
    return false;
#endif
}

void nb_desktop_runtime_clear_external_clipboard(
    struct nb_desktop_runtime *runtime)
{
#if NIXBENCH_HAS_WAYLAND
    if (runtime != NULL && runtime->wayland != NULL) {
        nb_wayland_server_clear_external_clipboard(runtime->wayland);
    }
#else
    (void)runtime;
#endif
}

bool nb_desktop_runtime_clipboard_text(
    const struct nb_desktop_runtime *runtime,
    const char **text,
    size_t *size)
{
#if NIXBENCH_HAS_WAYLAND
    return runtime != NULL && runtime->wayland != NULL &&
           nb_wayland_server_clipboard_text(runtime->wayland,
                                            text,
                                            size);
#else
    (void)runtime;
    (void)text;
    (void)size;
    return false;
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

bool nb_desktop_runtime_get_preferences(
    const struct nb_desktop_runtime *runtime,
    struct nb_user_preferences *preferences)
{
    if (runtime == NULL || preferences == NULL) {
        return false;
    }
    *preferences = runtime->preferences;
    return true;
}
