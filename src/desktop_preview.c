#include "desktop_preview.h"

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "menu.h"
#include "shell.h"
#include "shell_renderer.h"
#include "software_canvas.h"

enum {
    NB_PREVIEW_DESKTOP_RED = 24,
    NB_PREVIEW_DESKTOP_GREEN = 54,
    NB_PREVIEW_DESKTOP_BLUE = 76,
    NB_PREVIEW_MAX_WINDOW_WIDTH = 720,
    NB_PREVIEW_MAX_WINDOW_HEIGHT = 400,
    NB_PREVIEW_WINDOW_MARGIN = 24
};

#define NB_PREVIEW_DESKTOP_MENU_SOURCE UINT64_C(1)
#define NB_PREVIEW_APPLICATION_MENU_SOURCE UINT64_C(2)

enum nb_preview_command {
    NB_PREVIEW_COMMAND_ABOUT = 1,
    NB_PREVIEW_COMMAND_EXIT,
    NB_PREVIEW_COMMAND_REFRESH,
    NB_PREVIEW_COMMAND_CLOSE
};

static const struct nb_menu_item_spec desktop_items[] = {
    {"About NixBench", NB_PREVIEW_COMMAND_ABOUT,
     NB_MENU_ITEM_COMMAND, true, false},
    {NULL, NB_MENU_COMMAND_NONE, NB_MENU_ITEM_SEPARATOR, false, false},
    {"Exit Preview", NB_PREVIEW_COMMAND_EXIT,
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

static const struct nb_menu_item_spec project_items[] = {
    {"About Preview", NB_PREVIEW_COMMAND_ABOUT,
     NB_MENU_ITEM_COMMAND, true, false},
    {NULL, NB_MENU_COMMAND_NONE, NB_MENU_ITEM_SEPARATOR, false, false},
    {"Exit Preview", NB_PREVIEW_COMMAND_EXIT,
     NB_MENU_ITEM_COMMAND, true, false}
};

static const struct nb_menu_item_spec view_items[] = {
    {"Refresh", NB_PREVIEW_COMMAND_REFRESH, NB_MENU_ITEM_COMMAND, true, false}
};

static const struct nb_menu_item_spec window_items[] = {
    {"Close Window", NB_PREVIEW_COMMAND_CLOSE,
     NB_MENU_ITEM_COMMAND, true, false}
};

static const struct nb_menu_spec application_menus[] = {
    {"Project", project_items,
     sizeof(project_items) / sizeof(project_items[0])},
    {"View", view_items, sizeof(view_items) / sizeof(view_items[0])},
    {"Window", window_items,
     sizeof(window_items) / sizeof(window_items[0])}
};

static const struct nb_menu_model application_menu_model = {
    application_menus,
    sizeof(application_menus) / sizeof(application_menus[0])
};

struct nb_desktop_preview {
    struct nb_shell shell;
    struct nb_software_canvas *canvas;
    struct nb_rect viewport;
    nb_window_id window;
    int pointer_x;
    int pointer_y;
    bool initialized;
    bool pointer_visible;
};

static int minimum_int(int left, int right)
{
    return left < right ? left : right;
}

static int clamp_coordinate(int value, int extent)
{
    if (value < 0) {
        return 0;
    }
    return value >= extent ? extent - 1 : value;
}

static void clamp_pointer(struct nb_desktop_preview *preview)
{
    preview->pointer_x = clamp_coordinate(preview->pointer_x,
                                          preview->viewport.width);
    preview->pointer_y = clamp_coordinate(preview->pointer_y,
                                          preview->viewport.height);
}

static struct nb_rect preview_window_frame(struct nb_rect viewport)
{
    const struct nb_rect work = nb_menu_work_area(viewport);
    const int available_width =
        work.width > NB_PREVIEW_WINDOW_MARGIN * 2
            ? work.width - NB_PREVIEW_WINDOW_MARGIN * 2
            : work.width;
    const int available_height =
        work.height > NB_PREVIEW_WINDOW_MARGIN * 2
            ? work.height - NB_PREVIEW_WINDOW_MARGIN * 2
            : work.height;
    const int width = minimum_int(NB_PREVIEW_MAX_WINDOW_WIDTH,
                                  available_width);
    const int height = minimum_int(NB_PREVIEW_MAX_WINDOW_HEIGHT,
                                   available_height);
    const struct nb_rect frame = {
        work.x + (work.width - width) / 2,
        work.y + (work.height - height) / 2,
        width,
        height
    };

    return frame;
}

struct nb_desktop_preview *nb_desktop_preview_create(void)
{
    return calloc(1, sizeof(struct nb_desktop_preview));
}

bool nb_desktop_preview_set_output(
    struct nb_desktop_preview *preview,
    const struct nb_host_output *output)
{
    struct nb_software_canvas *canvas;
    SDL_Renderer *renderer;
    const struct nb_rect viewport = {
        0,
        0,
        output != NULL ? output->logical_width : 0,
        output != NULL ? output->logical_height : 0
    };

    if (preview == NULL || !nb_host_output_is_valid(output) ||
        output->logical_height <= NB_MENU_BAR_HEIGHT) {
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
    if (!preview->initialized) {
        struct nb_shell shell;
        nb_window_id window;

        nb_shell_init(&shell,
                      NB_PREVIEW_DESKTOP_MENU_SOURCE,
                      &desktop_menu_model);
        window = nb_shell_open_window(
            &shell,
            "NixBench Standalone Preview",
            preview_window_frame(viewport),
            NB_PREVIEW_APPLICATION_MENU_SOURCE,
            &application_menu_model);
        if (window == NB_WINDOW_ID_NONE) {
            nb_software_canvas_destroy(canvas);
            return false;
        }
        preview->shell = shell;
        preview->window = window;
        preview->pointer_x = viewport.width / 2;
        preview->pointer_y = viewport.height / 2;
        preview->initialized = true;
    }

    nb_software_canvas_destroy(preview->canvas);
    preview->canvas = canvas;
    preview->viewport = viewport;
    nb_shell_pointer_cancel(&preview->shell);
    (void)nb_shell_clamp_windows(&preview->shell, viewport);
    clamp_pointer(preview);
    return true;
}

static void apply_action(struct nb_desktop_preview *preview,
                         struct nb_shell_action action,
                         struct nb_desktop_preview_update *update)
{
    if (action.type == NB_SHELL_ACTION_WINDOW_CLOSE_REQUESTED) {
        if (action.window == preview->window &&
            nb_shell_destroy_window(&preview->shell, preview->window)) {
            preview->window = NB_WINDOW_ID_NONE;
        }
        return;
    }
    if (action.type != NB_SHELL_ACTION_MENU_COMMAND) {
        return;
    }
    if (action.menu_command == NB_PREVIEW_COMMAND_EXIT) {
        update->exit_requested = true;
    } else if (action.menu_command == NB_PREVIEW_COMMAND_CLOSE &&
               action.window == preview->window &&
               nb_shell_destroy_window(&preview->shell, preview->window)) {
        preview->window = NB_WINDOW_ID_NONE;
    }
}

bool nb_desktop_preview_handle_input(
    struct nb_desktop_preview *preview,
    const struct nb_host_event *event,
    struct nb_desktop_preview_update *update)
{
    int x;
    int y;

    if (preview == NULL || event == NULL || update == NULL ||
        !preview->initialized || !nb_host_event_is_valid(event)) {
        return false;
    }
    memset(update, 0, sizeof(*update));
    if (event->type == NB_HOST_EVENT_KEY) {
        if (event->data.key.pressed &&
            strcmp(event->data.key.xkb_key_name, "ESC") == 0) {
            update->exit_requested = true;
        }
        return true;
    }
    if (event->type != NB_HOST_EVENT_POINTER_MOTION &&
        event->type != NB_HOST_EVENT_POINTER_BUTTON) {
        return false;
    }
    if (event->type == NB_HOST_EVENT_POINTER_MOTION) {
        x = event->data.pointer_motion.x;
        y = event->data.pointer_motion.y;
    } else {
        x = event->data.pointer_button.x;
        y = event->data.pointer_button.y;
    }
    preview->pointer_x = clamp_coordinate(x, preview->viewport.width);
    preview->pointer_y = clamp_coordinate(y, preview->viewport.height);
    preview->pointer_visible = true;
    update->redraw = true;

    if (event->type == NB_HOST_EVENT_POINTER_MOTION) {
        if (nb_shell_wants_pointer_motion(&preview->shell)) {
            (void)nb_shell_pointer_move(&preview->shell,
                                        preview->pointer_x,
                                        preview->pointer_y,
                                        preview->viewport);
        }
    } else if (event->data.pointer_button.button ==
               NB_HOST_POINTER_BUTTON_LEFT) {
        if (event->data.pointer_button.pressed) {
            (void)nb_shell_pointer_down(&preview->shell,
                                        preview->pointer_x,
                                        preview->pointer_y,
                                        preview->viewport);
        } else {
            apply_action(preview,
                         nb_shell_pointer_up(&preview->shell,
                                             preview->pointer_x,
                                             preview->pointer_y,
                                             preview->viewport),
                         update);
        }
    }
    return true;
}

bool nb_desktop_preview_cancel_input(struct nb_desktop_preview *preview)
{
    if (preview == NULL || !preview->initialized) {
        return false;
    }
    nb_shell_pointer_cancel(&preview->shell);
    return true;
}

bool nb_desktop_preview_set_pointer(struct nb_desktop_preview *preview,
                                    int x,
                                    int y,
                                    bool visible)
{
    if (preview == NULL || !preview->initialized) {
        return false;
    }
    preview->pointer_x = clamp_coordinate(x, preview->viewport.width);
    preview->pointer_y = clamp_coordinate(y, preview->viewport.height);
    preview->pointer_visible = visible;
    return true;
}

bool nb_desktop_preview_window_frame(
    const struct nb_desktop_preview *preview,
    struct nb_rect *frame)
{
    const struct nb_window *window;

    if (preview == NULL || frame == NULL || !preview->initialized ||
        preview->window == NB_WINDOW_ID_NONE) {
        return false;
    }
    window = nb_desktop_find_window(&preview->shell.desktop,
                                    preview->window);
    if (window == NULL) {
        return false;
    }
    *frame = window->frame;
    return true;
}

void nb_desktop_preview_destroy(struct nb_desktop_preview *preview)
{
    if (preview == NULL) {
        return;
    }
    nb_software_canvas_destroy(preview->canvas);
    free(preview);
}

bool nb_desktop_preview_render(
    struct nb_desktop_preview *preview,
    const char *clock_text,
    uint64_t serial,
    struct nb_host_frame *frame)
{
    static const char *const pointer_bitmap[] = {
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
    SDL_Renderer *renderer;
    bool rendered;
    size_t row;

    if (preview == NULL || clock_text == NULL || clock_text[0] == '\0' ||
        serial == 0 || frame == NULL) {
        return false;
    }
    renderer = nb_software_canvas_renderer(preview->canvas);
    rendered = renderer != NULL &&
               SDL_SetRenderDrawColor(renderer,
                                      NB_PREVIEW_DESKTOP_RED,
                                      NB_PREVIEW_DESKTOP_GREEN,
                                      NB_PREVIEW_DESKTOP_BLUE,
                                      SDL_ALPHA_OPAQUE) &&
               SDL_RenderClear(renderer) &&
               nb_shell_render(renderer,
                               &preview->shell,
                               preview->viewport,
                               clock_text);
    if (rendered && preview->pointer_visible) {
        for (row = 0;
             row < sizeof(pointer_bitmap) / sizeof(pointer_bitmap[0]) &&
             rendered;
             ++row) {
            size_t column;

            for (column = 0;
                 pointer_bitmap[row][column] != '\0' && rendered;
                 ++column) {
                const char pixel = pointer_bitmap[row][column];

                if (pixel == '.') {
                    continue;
                }
                rendered = SDL_SetRenderDrawColor(
                               renderer,
                               pixel == 'W' ? 255 : 0,
                               pixel == 'W' ? 255 : 0,
                               pixel == 'W' ? 255 : 0,
                               SDL_ALPHA_OPAQUE) &&
                           SDL_RenderPoint(
                               renderer,
                               (float)(preview->pointer_x + (int)column),
                               (float)(preview->pointer_y + (int)row));
            }
        }
    }
    return rendered &&
           nb_software_canvas_finish(preview->canvas, serial, frame);
}
