#include "desktop_preview.h"

#include <stdlib.h>

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
     NB_MENU_ITEM_COMMAND, true},
    {NULL, NB_MENU_COMMAND_NONE, NB_MENU_ITEM_SEPARATOR, false},
    {"Exit Preview", NB_PREVIEW_COMMAND_EXIT,
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
    {"About Preview", NB_PREVIEW_COMMAND_ABOUT,
     NB_MENU_ITEM_COMMAND, true},
    {NULL, NB_MENU_COMMAND_NONE, NB_MENU_ITEM_SEPARATOR, false},
    {"Exit Preview", NB_PREVIEW_COMMAND_EXIT,
     NB_MENU_ITEM_COMMAND, true}
};

static const struct nb_menu_item_spec view_items[] = {
    {"Refresh", NB_PREVIEW_COMMAND_REFRESH, NB_MENU_ITEM_COMMAND, true}
};

static const struct nb_menu_item_spec window_items[] = {
    {"Close Window", NB_PREVIEW_COMMAND_CLOSE,
     NB_MENU_ITEM_COMMAND, true}
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
};

static int minimum_int(int left, int right)
{
    return left < right ? left : right;
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
    struct nb_shell shell;
    SDL_Renderer *renderer;
    nb_window_id window;
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

    nb_software_canvas_destroy(preview->canvas);
    preview->shell = shell;
    preview->canvas = canvas;
    preview->viewport = viewport;
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
    SDL_Renderer *renderer;

    if (preview == NULL || clock_text == NULL || clock_text[0] == '\0' ||
        serial == 0 || frame == NULL) {
        return false;
    }
    renderer = nb_software_canvas_renderer(preview->canvas);
    return renderer != NULL &&
           SDL_SetRenderDrawColor(renderer,
                                  NB_PREVIEW_DESKTOP_RED,
                                  NB_PREVIEW_DESKTOP_GREEN,
                                  NB_PREVIEW_DESKTOP_BLUE,
                                  SDL_ALPHA_OPAQUE) &&
           SDL_RenderClear(renderer) &&
           nb_shell_render(renderer,
                           &preview->shell,
                           preview->viewport,
                           clock_text) &&
           nb_software_canvas_finish(preview->canvas, serial, frame);
}
