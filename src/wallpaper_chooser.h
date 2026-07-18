#ifndef NIXBENCH_WALLPAPER_CHOOSER_H
#define NIXBENCH_WALLPAPER_CHOOSER_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL.h>

#include "file_browser.h"
#include "window.h"

enum nb_wallpaper_chooser_action_type {
    NB_WALLPAPER_CHOOSER_ACTION_NONE = 0,
    NB_WALLPAPER_CHOOSER_ACTION_PARENT,
    NB_WALLPAPER_CHOOSER_ACTION_TOGGLE_HIDDEN,
    NB_WALLPAPER_CHOOSER_ACTION_SCROLL_UP,
    NB_WALLPAPER_CHOOSER_ACTION_SCROLL_DOWN,
    NB_WALLPAPER_CHOOSER_ACTION_ROW,
    NB_WALLPAPER_CHOOSER_ACTION_OPEN,
    NB_WALLPAPER_CHOOSER_ACTION_USE,
    NB_WALLPAPER_CHOOSER_ACTION_CLEAR,
    NB_WALLPAPER_CHOOSER_ACTION_CANCEL
};

struct nb_wallpaper_chooser_action {
    enum nb_wallpaper_chooser_action_type type;
    size_t row;
};

enum nb_wallpaper_chooser_result {
    NB_WALLPAPER_CHOOSER_RESULT_NONE = 0,
    NB_WALLPAPER_CHOOSER_RESULT_SELECTED,
    NB_WALLPAPER_CHOOSER_RESULT_CLEARED,
    NB_WALLPAPER_CHOOSER_RESULT_CANCELLED
};

struct nb_wallpaper_chooser {
    struct nb_file_browser browser;
    SDL_Texture *preview;
    SDL_Renderer *preview_renderer;
    char preview_path[NB_FILESYSTEM_PATH_CAPACITY];
    int preview_width;
    int preview_height;
    char status[192];
};

void nb_wallpaper_chooser_init(struct nb_wallpaper_chooser *chooser);
void nb_wallpaper_chooser_destroy(struct nb_wallpaper_chooser *chooser);
void nb_wallpaper_chooser_invalidate_preview(
    struct nb_wallpaper_chooser *chooser);
bool nb_wallpaper_chooser_open(struct nb_wallpaper_chooser *chooser,
                              const char *wallpaper,
                              const char *home);
struct nb_wallpaper_chooser_action nb_wallpaper_chooser_hit_test(
    const struct nb_wallpaper_chooser *chooser,
    struct nb_rect content,
    int x,
    int y);
bool nb_wallpaper_chooser_action_equal(
    struct nb_wallpaper_chooser_action first,
    struct nb_wallpaper_chooser_action second);
enum nb_wallpaper_chooser_result nb_wallpaper_chooser_activate(
    struct nb_wallpaper_chooser *chooser,
    struct nb_wallpaper_chooser_action action,
    char *selected_path,
    size_t selected_path_capacity);
bool nb_wallpaper_chooser_render(struct nb_wallpaper_chooser *chooser,
                                SDL_Renderer *renderer,
                                struct nb_rect content);

#endif
