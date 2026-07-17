#include "damage_region.h"

#include <stdint.h>
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

static bool rects_merge_without_extra(struct nb_damage_rect left,
                                      struct nb_damage_rect right)
{
    if (nb_damage_rect_intersects(left, right)) {
        return true;
    }
    return (left.y == right.y && left.height == right.height &&
            (left.x + left.width == right.x ||
             right.x + right.width == left.x)) ||
           (left.x == right.x && left.width == right.width &&
            (left.y + left.height == right.y ||
             right.y + right.height == left.y));
}

static uint64_t rect_area(struct nb_damage_rect rect)
{
    return (uint64_t)(unsigned int)rect.width *
           (uint64_t)(unsigned int)rect.height;
}

static void remove_rect(struct nb_damage_region *region, size_t index)
{
    --region->count;
    region->rects[index] = region->rects[region->count];
}

/*
 * Preserve a bounded approximation when a client supplies more rectangles
 * than the wire protocol can retain.  Collapsing the cheapest pair is much
 * less destructive than turning a sparse animation into full-screen damage.
 */
static void compact_for_rect(struct nb_damage_region *region,
                             struct nb_damage_rect *rect,
                             int bounds_width,
                             int bounds_height)
{
    const size_t incoming = region->count;
    size_t best_left = 0;
    size_t best_right = 1;
    uint64_t best_extra = UINT64_MAX;
    uint64_t best_union_area = UINT64_MAX;
    size_t left;
    size_t right;
    struct nb_damage_rect merged;

    for (left = 0; left <= incoming; ++left) {
        const struct nb_damage_rect left_rect =
            left == incoming ? *rect : region->rects[left];

        for (right = left + 1; right <= incoming; ++right) {
            const struct nb_damage_rect right_rect =
                right == incoming ? *rect : region->rects[right];
            const struct nb_damage_rect united =
                rect_union(left_rect, right_rect);
            const uint64_t union_area = rect_area(united);
            const uint64_t extra =
                union_area - rect_area(left_rect) - rect_area(right_rect);

            if (extra < best_extra ||
                (extra == best_extra && union_area < best_union_area)) {
                best_left = left;
                best_right = right;
                best_extra = extra;
                best_union_area = union_area;
            }
        }
    }

    if (best_right == incoming) {
        *rect = rect_union(region->rects[best_left], *rect);
        remove_rect(region, best_left);
        return;
    }

    merged = rect_union(region->rects[best_left],
                        region->rects[best_right]);
    remove_rect(region, best_right);
    remove_rect(region, best_left);
    (void)nb_damage_region_add(region,
                               merged,
                               bounds_width,
                               bounds_height);
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

merge_again:
    index = 0;
    /* Collapse overlaps and perfectly adjacent spans without adding area. */
    while (index < region->count) {
        if (!rects_merge_without_extra(region->rects[index], rect)) {
            ++index;
            continue;
        }
        rect = rect_union(region->rects[index], rect);
        remove_rect(region, index);
        index = 0;
    }
    if (region->count == NB_DAMAGE_REGION_CAPACITY) {
        compact_for_rect(region, &rect, bounds_width, bounds_height);
        goto merge_again;
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
