#include "theme_atlas.h"

#include <string.h>

static bool kind_is_valid(enum nb_theme_tile_kind kind)
{
    return kind >= NB_THEME_TILE_DESKTOP && kind <= NB_THEME_TILE_WINDOW;
}

static bool key_is_valid(enum nb_theme_tile_kind kind, uint64_t object_id)
{
    return kind_is_valid(kind) &&
           ((kind == NB_THEME_TILE_WINDOW) == (object_id != 0));
}

static bool rect_is_valid(struct nb_theme_rect rect)
{
    return rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0;
}

static bool rect_fits(struct nb_theme_rect outer,
                      struct nb_theme_rect inner)
{
    const int64_t outer_right = (int64_t)outer.x + outer.width;
    const int64_t outer_bottom = (int64_t)outer.y + outer.height;
    const int64_t inner_right = (int64_t)inner.x + inner.width;
    const int64_t inner_bottom = (int64_t)inner.y + inner.height;

    return rect_is_valid(outer) && rect_is_valid(inner) &&
           inner.x >= outer.x && inner.y >= outer.y &&
           inner_right <= outer_right && inner_bottom <= outer_bottom;
}

static bool rects_overlap(struct nb_theme_rect first,
                          struct nb_theme_rect second)
{
    return first.x < second.x + second.width &&
           second.x < first.x + first.width &&
           first.y < second.y + second.height &&
           second.y < first.y + first.height;
}

static bool action_is_valid(enum nb_theme_action action)
{
    return action >= NB_THEME_ACTION_MOVE &&
           action <= NB_THEME_ACTION_RESIZE_NORTH_WEST;
}

void nb_theme_atlas_init(struct nb_theme_atlas *atlas)
{
    if (atlas != NULL) {
        memset(atlas, 0, sizeof(*atlas));
    }
}

bool nb_theme_atlas_begin(struct nb_theme_atlas *atlas,
                          uint64_t generation,
                          int width,
                          int height)
{
    if (atlas == NULL || atlas->pending_active || generation == 0 ||
        generation <= atlas->published.generation || width <= 0 ||
        height <= 0 || width > NB_THEME_ATLAS_MAX_DIMENSION ||
        height > NB_THEME_ATLAS_MAX_DIMENSION) {
        return false;
    }
    memset(&atlas->pending, 0, sizeof(atlas->pending));
    atlas->pending.generation = generation;
    atlas->pending.width = width;
    atlas->pending.height = height;
    atlas->pending_active = true;
    return true;
}

const struct nb_theme_tile *nb_theme_atlas_find_tile(
    const struct nb_theme_atlas_layout *layout,
    enum nb_theme_tile_kind kind,
    uint64_t object_id)
{
    size_t index;

    if (layout == NULL || !key_is_valid(kind, object_id)) {
        return NULL;
    }
    for (index = 0; index < layout->tile_count; ++index) {
        if (layout->tiles[index].kind == kind &&
            layout->tiles[index].object_id == object_id) {
            return &layout->tiles[index];
        }
    }
    return NULL;
}

bool nb_theme_atlas_add_tile(struct nb_theme_atlas *atlas,
                             enum nb_theme_tile_kind kind,
                             uint64_t object_id,
                             struct nb_theme_rect atlas_rect)
{
    struct nb_theme_atlas_layout *layout;
    const struct nb_theme_rect bounds = {
        0,
        0,
        atlas != NULL ? atlas->pending.width : 0,
        atlas != NULL ? atlas->pending.height : 0
    };
    size_t index;

    if (atlas == NULL || !atlas->pending_active ||
        !key_is_valid(kind, object_id) ||
        !rect_fits(bounds, atlas_rect)) {
        return false;
    }
    layout = &atlas->pending;
    if (layout->tile_count == NB_THEME_ATLAS_MAX_TILES ||
        nb_theme_atlas_find_tile(layout, kind, object_id) != NULL) {
        return false;
    }
    for (index = 0; index < layout->tile_count; ++index) {
        if (rects_overlap(layout->tiles[index].atlas_rect, atlas_rect)) {
            return false;
        }
    }
    layout->tiles[layout->tile_count++] = (struct nb_theme_tile){
        kind,
        object_id,
        atlas_rect
    };
    return true;
}

bool nb_theme_atlas_add_action_region(struct nb_theme_atlas *atlas,
                                      enum nb_theme_tile_kind kind,
                                      uint64_t object_id,
                                      enum nb_theme_action action,
                                      struct nb_theme_rect tile_rect)
{
    struct nb_theme_atlas_layout *layout;
    const struct nb_theme_tile *tile;
    struct nb_theme_rect tile_bounds = {0, 0, 0, 0};

    if (atlas == NULL || !atlas->pending_active ||
        kind != NB_THEME_TILE_WINDOW || object_id == 0 ||
        !action_is_valid(action)) {
        return false;
    }
    layout = &atlas->pending;
    tile = nb_theme_atlas_find_tile(layout, kind, object_id);
    if (tile == NULL || layout->action_region_count ==
                            NB_THEME_ATLAS_MAX_ACTION_REGIONS) {
        return false;
    }
    tile_bounds.width = tile->atlas_rect.width;
    tile_bounds.height = tile->atlas_rect.height;
    if (!rect_fits(tile_bounds, tile_rect)) {
        return false;
    }
    layout->action_regions[layout->action_region_count++] =
        (struct nb_theme_action_region){kind, object_id, action, tile_rect};
    return true;
}

bool nb_theme_atlas_commit(struct nb_theme_atlas *atlas,
                           uint64_t generation)
{
    if (atlas == NULL || !atlas->pending_active ||
        atlas->pending.generation != generation ||
        atlas->pending.tile_count == 0) {
        return false;
    }
    atlas->published = atlas->pending;
    memset(&atlas->pending, 0, sizeof(atlas->pending));
    atlas->pending_active = false;
    return true;
}

void nb_theme_atlas_abort(struct nb_theme_atlas *atlas)
{
    if (atlas == NULL) {
        return;
    }
    memset(&atlas->pending, 0, sizeof(atlas->pending));
    atlas->pending_active = false;
}

enum nb_theme_action nb_theme_atlas_hit_test(
    const struct nb_theme_atlas_layout *layout,
    enum nb_theme_tile_kind kind,
    uint64_t object_id,
    int tile_x,
    int tile_y)
{
    size_t index;

    if (layout == NULL || !key_is_valid(kind, object_id)) {
        return NB_THEME_ACTION_NONE;
    }
    index = layout->action_region_count;
    while (index > 0) {
        const struct nb_theme_action_region *region =
            &layout->action_regions[--index];
        const struct nb_theme_rect rect = region->tile_rect;

        if (region->kind == kind && region->object_id == object_id &&
            tile_x >= rect.x && tile_y >= rect.y &&
            tile_x < rect.x + rect.width &&
            tile_y < rect.y + rect.height) {
            return region->action;
        }
    }
    return NB_THEME_ACTION_NONE;
}
