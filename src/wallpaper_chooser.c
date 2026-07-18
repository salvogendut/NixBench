#include "wallpaper_chooser.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "png_loader.h"

struct color {
    Uint8 red;
    Uint8 green;
    Uint8 blue;
};

enum {
    CHOOSER_PADDING = 12,
    CHOOSER_TOOLBAR_Y = 32,
    CHOOSER_BUTTON_HEIGHT = 22,
    CHOOSER_LIST_Y = 64,
    CHOOSER_ROW_HEIGHT = 22,
    CHOOSER_PREVIEW_WIDTH = 236
};

static const struct color panel = {225, 230, 222};
static const struct color panel_shadow = {74, 89, 91};
static const struct color selection = {43, 113, 137};
static const struct color selection_text = {248, 249, 241};
static const struct color text = {18, 30, 34};
static const struct color muted = {76, 88, 88};
static const struct color preview_background = {16, 29, 33};

static bool set_color(SDL_Renderer *renderer, struct color value)
{
    return SDL_SetRenderDrawColor(renderer,
                                  value.red,
                                  value.green,
                                  value.blue,
                                  SDL_ALPHA_OPAQUE);
}

static bool fill(SDL_Renderer *renderer,
                 struct nb_rect rect,
                 struct color value)
{
    const SDL_FRect destination = {
        (float)rect.x,
        (float)rect.y,
        (float)rect.width,
        (float)rect.height
    };

    return rect.width <= 0 || rect.height <= 0 ||
           (set_color(renderer, value) &&
            SDL_RenderFillRect(renderer, &destination));
}

static bool outline(SDL_Renderer *renderer,
                    struct nb_rect rect,
                    struct color value)
{
    const SDL_FRect destination = {
        (float)rect.x,
        (float)rect.y,
        (float)rect.width,
        (float)rect.height
    };

    return rect.width <= 0 || rect.height <= 0 ||
           (set_color(renderer, value) && SDL_RenderRect(renderer, &destination));
}

static bool contains(struct nb_rect rect, int x, int y)
{
    return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
           x < rect.x + rect.width && y < rect.y + rect.height;
}

static bool render_text(SDL_Renderer *renderer,
                        int x,
                        int y,
                        int right,
                        const char *value,
                        struct color foreground)
{
    char clipped[160];
    size_t capacity;
    size_t length;

    if (value == NULL || value[0] == '\0' || right - x < 8) {
        return true;
    }
    capacity = (size_t)((right - x) / 8);
    if (capacity >= sizeof(clipped)) {
        capacity = sizeof(clipped) - 1;
    }
    length = strlen(value);
    if (length > capacity) {
        length = capacity;
    }
    (void)memcpy(clipped, value, length);
    clipped[length] = '\0';
    return clipped[0] == '\0' ||
           (set_color(renderer, foreground) &&
            SDL_RenderDebugText(renderer, (float)x, (float)y, clipped));
}

static bool button(SDL_Renderer *renderer,
                   struct nb_rect rect,
                   const char *label,
                   bool selected)
{
    return fill(renderer, rect, selected ? selection : panel) &&
           outline(renderer, rect, panel_shadow) &&
           render_text(renderer,
                       rect.x + 6,
                       rect.y + 7,
                       rect.x + rect.width - 4,
                       label,
                       selected ? selection_text : text);
}

static int list_width(struct nb_rect content)
{
    int width = content.width - CHOOSER_PREVIEW_WIDTH -
                3 * CHOOSER_PADDING;

    if (width < 220) {
        width = content.width - 2 * CHOOSER_PADDING;
    }
    return width;
}

static int list_bottom(struct nb_rect content)
{
    return content.y + content.height - 58;
}

static size_t visible_rows(struct nb_rect content)
{
    const int height = list_bottom(content) - (content.y + CHOOSER_LIST_Y);

    return height > 0 ? (size_t)(height / CHOOSER_ROW_HEIGHT) : 0;
}

static struct nb_rect toolbar_button(struct nb_rect content,
                                     int offset,
                                     int width)
{
    return (struct nb_rect){content.x + CHOOSER_PADDING + offset,
                            content.y + CHOOSER_TOOLBAR_Y,
                            width,
                            CHOOSER_BUTTON_HEIGHT};
}

static struct nb_rect bottom_button(struct nb_rect content,
                                    int offset,
                                    int width)
{
    return (struct nb_rect){content.x + CHOOSER_PADDING + offset,
                            content.y + content.height - 34,
                            width,
                            CHOOSER_BUTTON_HEIGHT};
}

void nb_wallpaper_chooser_invalidate_preview(
    struct nb_wallpaper_chooser *chooser)
{
    if (chooser == NULL) {
        return;
    }
    SDL_DestroyTexture(chooser->preview);
    chooser->preview = NULL;
    chooser->preview_renderer = NULL;
    chooser->preview_path[0] = '\0';
    chooser->preview_width = 0;
    chooser->preview_height = 0;
}

void nb_wallpaper_chooser_init(struct nb_wallpaper_chooser *chooser)
{
    if (chooser == NULL) {
        return;
    }
    (void)memset(chooser, 0, sizeof(*chooser));
    nb_file_browser_init(&chooser->browser, ".png");
}

void nb_wallpaper_chooser_destroy(struct nb_wallpaper_chooser *chooser)
{
    if (chooser == NULL) {
        return;
    }
    nb_wallpaper_chooser_invalidate_preview(chooser);
    nb_file_browser_destroy(&chooser->browser);
    (void)memset(chooser, 0, sizeof(*chooser));
}

static void select_filename(struct nb_wallpaper_chooser *chooser,
                            const char *filename)
{
    size_t index;

    for (index = 0; index < nb_file_browser_visible_count(&chooser->browser);
         ++index) {
        const struct nb_filesystem_entry *entry =
            nb_file_browser_visible_entry(&chooser->browser, index, NULL);

        if (entry != NULL && strcmp(entry->name, filename) == 0) {
            (void)nb_file_browser_select(&chooser->browser, index);
            return;
        }
    }
}

bool nb_wallpaper_chooser_open(struct nb_wallpaper_chooser *chooser,
                              const char *wallpaper,
                              const char *home)
{
    char directory[NB_FILESYSTEM_PATH_CAPACITY] = {0};
    char filename[NB_FILESYSTEM_NAME_CAPACITY] = {0};
    const char *initial = home != NULL && home[0] == '/' ? home : "/";
    const char *slash;

    if (chooser == NULL) {
        return false;
    }
    chooser->status[0] = '\0';
    nb_wallpaper_chooser_invalidate_preview(chooser);
    if (wallpaper != NULL && wallpaper[0] == '/') {
        slash = strrchr(wallpaper, '/');
        if (slash != NULL && slash[1] != '\0' &&
            strlen(slash + 1) < sizeof(filename)) {
            (void)memcpy(filename, slash + 1, strlen(slash + 1) + 1);
            if (slash == wallpaper) {
                (void)snprintf(directory, sizeof(directory), "%s", "/");
            } else if ((size_t)(slash - wallpaper) < sizeof(directory)) {
                const size_t length = (size_t)(slash - wallpaper);

                (void)memcpy(directory, wallpaper, length);
                directory[length] = '\0';
            }
            if (directory[0] == '/' &&
                nb_file_browser_open(&chooser->browser,
                                     directory,
                                     chooser->status,
                                     sizeof(chooser->status))) {
                select_filename(chooser, filename);
                return true;
            }
        }
    }
    if (!nb_file_browser_open(&chooser->browser,
                              initial,
                              chooser->status,
                              sizeof(chooser->status)) &&
        strcmp(initial, "/") != 0 &&
        !nb_file_browser_open(&chooser->browser,
                              "/",
                              chooser->status,
                              sizeof(chooser->status))) {
        return false;
    }
    chooser->status[0] = '\0';
    return true;
}

struct nb_wallpaper_chooser_action nb_wallpaper_chooser_hit_test(
    const struct nb_wallpaper_chooser *chooser,
    struct nb_rect content,
    int x,
    int y)
{
    struct nb_wallpaper_chooser_action action = {
        NB_WALLPAPER_CHOOSER_ACTION_NONE,
        0
    };
    const int width = list_width(content);
    const size_t rows = visible_rows(content);
    size_t row;

    if (chooser == NULL) {
        return action;
    }
    if (contains(toolbar_button(content, 0, 82), x, y)) {
        action.type = NB_WALLPAPER_CHOOSER_ACTION_PARENT;
    } else if (contains(toolbar_button(content, 90, 112), x, y)) {
        action.type = NB_WALLPAPER_CHOOSER_ACTION_TOGGLE_HIDDEN;
    } else if (contains(toolbar_button(content, width - 54, 24), x, y)) {
        action.type = NB_WALLPAPER_CHOOSER_ACTION_SCROLL_UP;
        action.row = rows;
    } else if (contains(toolbar_button(content, width - 26, 24), x, y)) {
        action.type = NB_WALLPAPER_CHOOSER_ACTION_SCROLL_DOWN;
        action.row = rows;
    } else if (contains(bottom_button(content, 0, 96), x, y)) {
        action.type = NB_WALLPAPER_CHOOSER_ACTION_USE;
    } else if (contains(bottom_button(content, 104, 96), x, y)) {
        action.type = NB_WALLPAPER_CHOOSER_ACTION_OPEN;
    } else if (contains(bottom_button(content, 208, 96), x, y)) {
        action.type = NB_WALLPAPER_CHOOSER_ACTION_CLEAR;
    } else if (contains(bottom_button(content, 312, 96), x, y)) {
        action.type = NB_WALLPAPER_CHOOSER_ACTION_CANCEL;
    } else {
        for (row = 0; row < rows; ++row) {
            const struct nb_rect row_rect = {
                content.x + CHOOSER_PADDING,
                content.y + CHOOSER_LIST_Y + (int)row * CHOOSER_ROW_HEIGHT,
                width,
                CHOOSER_ROW_HEIGHT
            };

            if (contains(row_rect, x, y)) {
                action.type = NB_WALLPAPER_CHOOSER_ACTION_ROW;
                action.row = chooser->browser.first_visible + row;
                break;
            }
        }
    }
    return action;
}

bool nb_wallpaper_chooser_action_equal(
    struct nb_wallpaper_chooser_action first,
    struct nb_wallpaper_chooser_action second)
{
    return first.type == second.type &&
           (first.type != NB_WALLPAPER_CHOOSER_ACTION_ROW ||
            first.row == second.row);
}

static enum nb_wallpaper_chooser_result use_selection(
    struct nb_wallpaper_chooser *chooser,
    char *selected_path,
    size_t selected_path_capacity)
{
    const struct nb_filesystem_entry *entry =
        nb_file_browser_selected(&chooser->browser, NULL);
    struct nb_png_image image = {0};
    char path[NB_FILESYSTEM_PATH_CAPACITY];

    if (entry == NULL || entry->kind != NB_FILESYSTEM_ENTRY_REGULAR) {
        (void)snprintf(chooser->status,
                       sizeof(chooser->status),
                       "%s",
                       "Select a PNG image first.");
        return NB_WALLPAPER_CHOOSER_RESULT_NONE;
    }
    if (!nb_file_browser_selected_path(&chooser->browser,
                                       path,
                                       sizeof(path)) ||
        strlen(path) >= selected_path_capacity) {
        (void)snprintf(chooser->status,
                       sizeof(chooser->status),
                       "%s",
                       "The selected path is too long for .nixbenchrc.");
        return NB_WALLPAPER_CHOOSER_RESULT_NONE;
    }
    {
        size_t index;
        const size_t length = strlen(path);

        for (index = 0; index < length; ++index) {
            if (path[index] == '\n' || path[index] == '\r') {
                (void)snprintf(chooser->status,
                               sizeof(chooser->status),
                               "%s",
                               "That filename cannot be stored safely.");
                return NB_WALLPAPER_CHOOSER_RESULT_NONE;
            }
        }
        if (length > 0 && isspace((unsigned char)path[length - 1])) {
            (void)snprintf(chooser->status,
                           sizeof(chooser->status),
                           "%s",
                           "A filename ending in whitespace is unsupported.");
            return NB_WALLPAPER_CHOOSER_RESULT_NONE;
        }
    }
    if (!nb_png_load(path,
                     &image,
                     chooser->status,
                     sizeof(chooser->status))) {
        return NB_WALLPAPER_CHOOSER_RESULT_NONE;
    }
    nb_png_image_destroy(&image);
    (void)memcpy(selected_path, path, strlen(path) + 1);
    chooser->status[0] = '\0';
    return NB_WALLPAPER_CHOOSER_RESULT_SELECTED;
}

enum nb_wallpaper_chooser_result nb_wallpaper_chooser_activate(
    struct nb_wallpaper_chooser *chooser,
    struct nb_wallpaper_chooser_action action,
    char *selected_path,
    size_t selected_path_capacity)
{
    const size_t rows = action.row > 0 ? action.row : 12;

    if (chooser == NULL || selected_path == NULL ||
        selected_path_capacity == 0) {
        return NB_WALLPAPER_CHOOSER_RESULT_NONE;
    }
    if (action.type == NB_WALLPAPER_CHOOSER_ACTION_ROW) {
        if (nb_file_browser_select(&chooser->browser, action.row)) {
            chooser->status[0] = '\0';
            nb_wallpaper_chooser_invalidate_preview(chooser);
        }
    } else if (action.type == NB_WALLPAPER_CHOOSER_ACTION_PARENT) {
        if (nb_file_browser_parent(&chooser->browser,
                                   chooser->status,
                                   sizeof(chooser->status))) {
            chooser->status[0] = '\0';
            nb_wallpaper_chooser_invalidate_preview(chooser);
        }
    } else if (action.type == NB_WALLPAPER_CHOOSER_ACTION_TOGGLE_HIDDEN) {
        chooser->browser.show_hidden = !chooser->browser.show_hidden;
        chooser->browser.selected = SIZE_MAX;
        chooser->browser.first_visible = 0;
        chooser->status[0] = '\0';
        nb_wallpaper_chooser_invalidate_preview(chooser);
    } else if (action.type == NB_WALLPAPER_CHOOSER_ACTION_SCROLL_UP) {
        nb_file_browser_scroll(&chooser->browser, -(int)rows, rows);
    } else if (action.type == NB_WALLPAPER_CHOOSER_ACTION_SCROLL_DOWN) {
        nb_file_browser_scroll(&chooser->browser, (int)rows, rows);
    } else if (action.type == NB_WALLPAPER_CHOOSER_ACTION_OPEN) {
        const struct nb_filesystem_entry *entry =
            nb_file_browser_selected(&chooser->browser, NULL);

        if (entry != NULL &&
            entry->kind == NB_FILESYSTEM_ENTRY_DIRECTORY) {
            if (nb_file_browser_enter_selected(&chooser->browser,
                                               chooser->status,
                                               sizeof(chooser->status))) {
                chooser->status[0] = '\0';
                nb_wallpaper_chooser_invalidate_preview(chooser);
            }
        } else {
            return use_selection(chooser,
                                 selected_path,
                                 selected_path_capacity);
        }
    } else if (action.type == NB_WALLPAPER_CHOOSER_ACTION_USE) {
        return use_selection(chooser,
                             selected_path,
                             selected_path_capacity);
    } else if (action.type == NB_WALLPAPER_CHOOSER_ACTION_CLEAR) {
        selected_path[0] = '\0';
        return NB_WALLPAPER_CHOOSER_RESULT_CLEARED;
    } else if (action.type == NB_WALLPAPER_CHOOSER_ACTION_CANCEL) {
        return NB_WALLPAPER_CHOOSER_RESULT_CANCELLED;
    }
    return NB_WALLPAPER_CHOOSER_RESULT_NONE;
}

static bool prepare_preview(struct nb_wallpaper_chooser *chooser,
                            SDL_Renderer *renderer)
{
    const struct nb_filesystem_entry *entry =
        nb_file_browser_selected(&chooser->browser, NULL);
    struct nb_png_image image = {0};
    SDL_Surface *surface = NULL;
    SDL_Texture *texture = NULL;
    char path[NB_FILESYSTEM_PATH_CAPACITY];

    if (entry == NULL || entry->kind != NB_FILESYSTEM_ENTRY_REGULAR ||
        !nb_file_browser_selected_path(&chooser->browser,
                                       path,
                                       sizeof(path))) {
        nb_wallpaper_chooser_invalidate_preview(chooser);
        return true;
    }
    if (chooser->preview != NULL && chooser->preview_renderer == renderer &&
        strcmp(chooser->preview_path, path) == 0) {
        return true;
    }
    nb_wallpaper_chooser_invalidate_preview(chooser);
    if (!nb_png_load(path,
                     &image,
                     chooser->status,
                     sizeof(chooser->status))) {
        return true;
    }
    surface = SDL_CreateSurfaceFrom(image.width,
                                    image.height,
                                    SDL_PIXELFORMAT_RGBA32,
                                    image.pixels,
                                    (int)image.pitch);
    if (surface != NULL) {
        texture = SDL_CreateTextureFromSurface(renderer, surface);
    }
    if (texture == NULL ||
        !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) ||
        !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR)) {
        (void)snprintf(chooser->status,
                       sizeof(chooser->status),
                       "Could not prepare preview: %s",
                       SDL_GetError());
        SDL_DestroyTexture(texture);
    } else {
        chooser->preview = texture;
        chooser->preview_renderer = renderer;
        chooser->preview_width = image.width;
        chooser->preview_height = image.height;
        (void)snprintf(chooser->preview_path,
                       sizeof(chooser->preview_path),
                       "%s",
                       path);
        chooser->status[0] = '\0';
    }
    SDL_DestroySurface(surface);
    nb_png_image_destroy(&image);
    return true;
}

bool nb_wallpaper_chooser_render(struct nb_wallpaper_chooser *chooser,
                                SDL_Renderer *renderer,
                                struct nb_rect content)
{
    int width;
    int bottom;
    size_t rows;
    size_t count;
    char label[256];
    size_t row;
    struct nb_rect list;

    if (chooser == NULL || renderer == NULL || content.width <= 0 ||
        content.height <= 0) {
        return false;
    }
    width = list_width(content);
    bottom = list_bottom(content);
    rows = visible_rows(content);
    count = nb_file_browser_visible_count(&chooser->browser);
    list = (struct nb_rect){content.x + CHOOSER_PADDING,
                            content.y + CHOOSER_LIST_Y,
                            width,
                            bottom - (content.y + CHOOSER_LIST_Y)};
    (void)snprintf(label,
                   sizeof(label),
                   "Directory: %s%s",
                   chooser->browser.directory.path,
                   chooser->browser.directory.truncated ? " (truncated)" : "");
    if (!render_text(renderer,
                     content.x + CHOOSER_PADDING,
                     content.y + 12,
                     content.x + content.width - CHOOSER_PADDING,
                     label,
                     text) ||
        !button(renderer, toolbar_button(content, 0, 82), "Parent", false) ||
        !button(renderer,
                toolbar_button(content, 90, 112),
                chooser->browser.show_hidden ? "Hide dotfiles" : "Show dotfiles",
                chooser->browser.show_hidden) ||
        !button(renderer, toolbar_button(content, width - 54, 24), "^", false) ||
        !button(renderer, toolbar_button(content, width - 26, 24), "v", false) ||
        !fill(renderer, list, panel) || !outline(renderer, list, panel_shadow)) {
        return false;
    }
    for (row = 0; row < rows; ++row) {
        const size_t visible_index = chooser->browser.first_visible + row;
        const struct nb_filesystem_entry *entry =
            nb_file_browser_visible_entry(&chooser->browser,
                                          visible_index,
                                          NULL);
        const struct nb_rect row_rect = {
            list.x + 1,
            list.y + (int)row * CHOOSER_ROW_HEIGHT + 1,
            list.width - 2,
            CHOOSER_ROW_HEIGHT - 1
        };
        const bool selected_row = chooser->browser.selected == visible_index;

        if (entry == NULL) {
            break;
        }
        (void)snprintf(label,
                       sizeof(label),
                       entry->kind == NB_FILESYSTEM_ENTRY_DIRECTORY
                           ? "[DIR] %s"
                           : "%s",
                       entry->name);
        if (!fill(renderer,
                  row_rect,
                  selected_row ? selection : panel) ||
            !render_text(renderer,
                         row_rect.x + 5,
                         row_rect.y + 7,
                         row_rect.x + row_rect.width - 4,
                         label,
                         selected_row ? selection_text : text)) {
            return false;
        }
    }
    (void)snprintf(label,
                   sizeof(label),
                   "%zu visible item%s%s",
                   count,
                   count == 1 ? "" : "s",
                   chooser->status[0] != '\0' ? " - " : "");
    if (!render_text(renderer,
                     list.x,
                     bottom + 5,
                     list.x + list.width,
                     label,
                     muted) ||
        !render_text(renderer,
                     list.x + (int)strlen(label) * 8,
                     bottom + 5,
                     list.x + list.width,
                     chooser->status,
                     muted) ||
        !button(renderer, bottom_button(content, 0, 96), "Use PNG", false) ||
        !button(renderer, bottom_button(content, 104, 96), "Open", false) ||
        !button(renderer, bottom_button(content, 208, 96), "No wallpaper", false) ||
        !button(renderer, bottom_button(content, 312, 96), "Cancel", false)) {
        return false;
    }
    if (content.width - width - 3 * CHOOSER_PADDING > 0) {
        struct nb_rect preview = {
            list.x + list.width + CHOOSER_PADDING,
            list.y,
            content.x + content.width - CHOOSER_PADDING -
                (list.x + list.width + CHOOSER_PADDING),
            list.height
        };

        if (!fill(renderer, preview, preview_background) ||
            !outline(renderer, preview, panel_shadow) ||
            !prepare_preview(chooser, renderer)) {
            return false;
        }
        if (chooser->preview != NULL) {
            int draw_width = preview.width - 16;
            int draw_height = preview.height - 36;

            if ((int64_t)draw_width * chooser->preview_height <=
                (int64_t)draw_height * chooser->preview_width) {
                draw_height = (int)((int64_t)chooser->preview_height *
                                    draw_width / chooser->preview_width);
            } else {
                draw_width = (int)((int64_t)chooser->preview_width *
                                   draw_height / chooser->preview_height);
            }
            {
                const SDL_FRect destination = {
                    (float)(preview.x + (preview.width - draw_width) / 2),
                    (float)(preview.y + 8 +
                            (preview.height - 28 - draw_height) / 2),
                    (float)draw_width,
                    (float)draw_height
                };

                if (!SDL_RenderTexture(renderer,
                                       chooser->preview,
                                       NULL,
                                       &destination)) {
                    return false;
                }
            }
            (void)snprintf(label,
                           sizeof(label),
                           "%d x %d",
                           chooser->preview_width,
                           chooser->preview_height);
            if (!render_text(renderer,
                             preview.x + 8,
                             preview.y + preview.height - 18,
                             preview.x + preview.width - 8,
                             label,
                             selection_text)) {
                return false;
            }
        } else if (!render_text(renderer,
                                preview.x + 10,
                                preview.y + 12,
                                preview.x + preview.width - 8,
                                "Select a PNG to preview",
                                selection_text)) {
            return false;
        }
    }
    return true;
}
