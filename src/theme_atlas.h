#ifndef NIXBENCH_THEME_ATLAS_H
#define NIXBENCH_THEME_ATLAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_THEME_ATLAS_MAX_DIMENSION = 8192,
    NB_THEME_ATLAS_MAX_TILES = 258,
    NB_THEME_ATLAS_MAX_ACTION_REGIONS = 2048
};

enum nb_theme_tile_kind {
    NB_THEME_TILE_DESKTOP = 1,
    NB_THEME_TILE_MENUBAR,
    NB_THEME_TILE_WINDOW
};

enum nb_theme_action {
    NB_THEME_ACTION_NONE = 0,
    NB_THEME_ACTION_MOVE,
    NB_THEME_ACTION_CLOSE,
    NB_THEME_ACTION_MINIMIZE,
    NB_THEME_ACTION_MAXIMIZE,
    NB_THEME_ACTION_RESIZE_NORTH,
    NB_THEME_ACTION_RESIZE_NORTH_EAST,
    NB_THEME_ACTION_RESIZE_EAST,
    NB_THEME_ACTION_RESIZE_SOUTH_EAST,
    NB_THEME_ACTION_RESIZE_SOUTH,
    NB_THEME_ACTION_RESIZE_SOUTH_WEST,
    NB_THEME_ACTION_RESIZE_WEST,
    NB_THEME_ACTION_RESIZE_NORTH_WEST
};

struct nb_theme_rect {
    int x;
    int y;
    int width;
    int height;
};

struct nb_theme_tile {
    enum nb_theme_tile_kind kind;
    uint64_t object_id;
    struct nb_theme_rect atlas_rect;
};

struct nb_theme_action_region {
    enum nb_theme_tile_kind kind;
    uint64_t object_id;
    enum nb_theme_action action;
    struct nb_theme_rect tile_rect;
};

struct nb_theme_atlas_layout {
    uint64_t generation;
    int width;
    int height;
    struct nb_theme_tile tiles[NB_THEME_ATLAS_MAX_TILES];
    size_t tile_count;
    struct nb_theme_action_region
        action_regions[NB_THEME_ATLAS_MAX_ACTION_REGIONS];
    size_t action_region_count;
};

struct nb_theme_atlas {
    struct nb_theme_atlas_layout published;
    struct nb_theme_atlas_layout pending;
    bool pending_active;
};

void nb_theme_atlas_init(struct nb_theme_atlas *atlas);

bool nb_theme_atlas_begin(struct nb_theme_atlas *atlas,
                          uint64_t generation,
                          int width,
                          int height);

bool nb_theme_atlas_add_tile(struct nb_theme_atlas *atlas,
                             enum nb_theme_tile_kind kind,
                             uint64_t object_id,
                             struct nb_theme_rect atlas_rect);

bool nb_theme_atlas_add_action_region(struct nb_theme_atlas *atlas,
                                      enum nb_theme_tile_kind kind,
                                      uint64_t object_id,
                                      enum nb_theme_action action,
                                      struct nb_theme_rect tile_rect);

bool nb_theme_atlas_commit(struct nb_theme_atlas *atlas,
                           uint64_t generation);

void nb_theme_atlas_abort(struct nb_theme_atlas *atlas);

const struct nb_theme_tile *nb_theme_atlas_find_tile(
    const struct nb_theme_atlas_layout *layout,
    enum nb_theme_tile_kind kind,
    uint64_t object_id);

/* Later regions have DOM-like priority over earlier containing regions. */
enum nb_theme_action nb_theme_atlas_hit_test(
    const struct nb_theme_atlas_layout *layout,
    enum nb_theme_tile_kind kind,
    uint64_t object_id,
    int tile_x,
    int tile_y);

#endif
