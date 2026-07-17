#ifndef NIXBENCH_DAMAGE_REGION_H
#define NIXBENCH_DAMAGE_REGION_H

#include <stdbool.h>
#include <stddef.h>

enum { NB_DAMAGE_REGION_CAPACITY = 64 };

struct nb_damage_rect {
    int x;
    int y;
    int width;
    int height;
};

/*
 * A bounded set of non-overlapping damaged rectangles. full is an explicit
 * fallback used when damage is unknown. When the bounded list fills, the
 * least-expensive pair is coalesced instead of degrading to full damage. An
 * empty, non-full region means that no pixels changed.
 */
struct nb_damage_region {
    bool full;
    size_t count;
    struct nb_damage_rect rects[NB_DAMAGE_REGION_CAPACITY];
};

void nb_damage_region_clear(struct nb_damage_region *region);
void nb_damage_region_set_full(struct nb_damage_region *region);
bool nb_damage_region_add(struct nb_damage_region *region,
                          struct nb_damage_rect rect,
                          int bounds_width,
                          int bounds_height);
bool nb_damage_region_merge(struct nb_damage_region *destination,
                            const struct nb_damage_region *source,
                            int bounds_width,
                            int bounds_height);
bool nb_damage_region_is_valid(const struct nb_damage_region *region,
                               int bounds_width,
                               int bounds_height);
bool nb_damage_region_bounds(const struct nb_damage_region *region,
                             int bounds_width,
                             int bounds_height,
                             struct nb_damage_rect *bounds);
bool nb_damage_rect_intersects(struct nb_damage_rect left,
                               struct nb_damage_rect right);

#endif
