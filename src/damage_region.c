#include "damage_region.h"

#include <string.h>

void nb_damage_region_clear(struct nb_damage_region *region)
{
    if (region != NULL) {
        (void)memset(region, 0, sizeof(*region));
    }
}

void nb_damage_region_set_full(struct nb_damage_region *region)
{
    if (region != NULL) {
        nb_damage_region_clear(region);
        region->full = true;
    }
}

bool nb_damage_rect_intersects(struct nb_damage_rect left,
                               struct nb_damage_rect right)
{
    return left.width > 0 && left.height > 0 && right.width > 0 &&
           right.height > 0 && left.x < right.x + right.width &&
           right.x < left.x + left.width &&
           left.y < right.y + right.height &&
           right.y < left.y + left.height;
}

static bool clip_rect(struct nb_damage_rect *rect,
                      int bounds_width,
                      int bounds_height)
{
    if (rect == NULL || bounds_width <= 0 || bounds_height <= 0 ||
        rect->width <= 0 || rect->height <= 0) {
        return false;
    }
    if (rect->x < 0) {
        rect->width += rect->x;
        rect->x = 0;
    }
    if (rect->y < 0) {
        rect->height += rect->y;
        rect->y = 0;
    }
    if (rect->x >= bounds_width || rect->y >= bounds_height ||
        rect->width <= 0 || rect->height <= 0) {
        return false;
    }
    if (rect->width > bounds_width - rect->x) {
        rect->width = bounds_width - rect->x;
    }
    if (rect->height > bounds_height - rect->y) {
        rect->height = bounds_height - rect->y;
    }
    return true;
}

static struct nb_damage_rect rect_union(struct nb_damage_rect left,
                                        struct nb_damage_rect right)
{
    const int x = left.x < right.x ? left.x : right.x;
    const int y = left.y < right.y ? left.y : right.y;
    const int left_right = left.x + left.width;
    const int right_right = right.x + right.width;
    const int left_bottom = left.y + left.height;
    const int right_bottom = right.y + right.height;
    const int maximum_x = left_right > right_right ? left_right : right_right;
    const int maximum_y =
        left_bottom > right_bottom ? left_bottom : right_bottom;

    return (struct nb_damage_rect){x,
                                   y,
                                   maximum_x - x,
                                   maximum_y - y};
}

bool nb_damage_region_add(struct nb_damage_region *region,
                          struct nb_damage_rect rect,
                          int bounds_width,
                          int bounds_height)
{
    size_t index = 0;

    if (region == NULL) {
        return false;
    }
    if (region->full || !clip_rect(&rect, bounds_width, bounds_height)) {
        return true;
    }

    /* Collapse actual overlaps, but preserve disjoint animation spans. */
    while (index < region->count) {
        if (!nb_damage_rect_intersects(region->rects[index], rect)) {
            ++index;
            continue;
        }
        rect = rect_union(region->rects[index], rect);
        --region->count;
        region->rects[index] = region->rects[region->count];
        index = 0;
    }
    if (region->count == NB_DAMAGE_REGION_CAPACITY) {
        nb_damage_region_set_full(region);
        return true;
    }
    region->rects[region->count++] = rect;
    return true;
}

bool nb_damage_region_merge(struct nb_damage_region *destination,
                            const struct nb_damage_region *source,
                            int bounds_width,
                            int bounds_height)
{
    size_t index;

    if (destination == NULL || source == NULL) {
        return false;
    }
    if (source->full) {
        nb_damage_region_set_full(destination);
        return true;
    }
    if (destination->full) {
        return true;
    }
    for (index = 0; index < source->count; ++index) {
        if (!nb_damage_region_add(destination,
                                  source->rects[index],
                                  bounds_width,
                                  bounds_height)) {
            return false;
        }
    }
    return true;
}

bool nb_damage_region_is_valid(const struct nb_damage_region *region,
                               int bounds_width,
                               int bounds_height)
{
    size_t index;
    size_t other;

    if (region == NULL || bounds_width <= 0 || bounds_height <= 0 ||
        region->count > NB_DAMAGE_REGION_CAPACITY ||
        (region->full && region->count != 0)) {
        return false;
    }
    for (index = 0; index < region->count; ++index) {
        const struct nb_damage_rect rect = region->rects[index];

        if (rect.x < 0 || rect.y < 0 || rect.width <= 0 ||
            rect.height <= 0 || rect.x >= bounds_width ||
            rect.y >= bounds_height ||
            rect.width > bounds_width - rect.x ||
            rect.height > bounds_height - rect.y) {
            return false;
        }
        for (other = 0; other < index; ++other) {
            if (nb_damage_rect_intersects(rect, region->rects[other])) {
                return false;
            }
        }
    }
    return true;
}

bool nb_damage_region_bounds(const struct nb_damage_region *region,
                             int bounds_width,
                             int bounds_height,
                             struct nb_damage_rect *bounds)
{
    size_t index;

    if (bounds == NULL ||
        !nb_damage_region_is_valid(region, bounds_width, bounds_height) ||
        (!region->full && region->count == 0)) {
        return false;
    }
    if (region->full) {
        *bounds = (struct nb_damage_rect){0,
                                          0,
                                          bounds_width,
                                          bounds_height};
        return true;
    }
    *bounds = region->rects[0];
    for (index = 1; index < region->count; ++index) {
        *bounds = rect_union(*bounds, region->rects[index]);
    }
    return true;
}
