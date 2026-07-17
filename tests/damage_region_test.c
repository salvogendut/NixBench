#include <stdbool.h>
#include <stdio.h>

#include "damage_region.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

int main(void)
{
    struct nb_damage_region region;
    struct nb_damage_region other;
    struct nb_damage_rect bounds;
    size_t index;

    nb_damage_region_clear(&region);
    CHECK(nb_damage_region_add(
        &region, (struct nb_damage_rect){1, 2, 3, 4}, 100, 80));
    CHECK(nb_damage_region_add(
        &region, (struct nb_damage_rect){20, 10, 5, 6}, 100, 80));
    CHECK(region.count == 2);
    CHECK(nb_damage_region_is_valid(&region, 100, 80));
    CHECK(nb_damage_region_bounds(&region, 100, 80, &bounds));
    CHECK(bounds.x == 1 && bounds.y == 2 && bounds.width == 24 &&
          bounds.height == 14);

    CHECK(nb_damage_region_add(
        &region, (struct nb_damage_rect){3, 3, 19, 10}, 100, 80));
    CHECK(region.count == 1);

    nb_damage_region_clear(&region);
    for (index = 0; index < NB_DAMAGE_REGION_CAPACITY + 1U; ++index) {
        CHECK(nb_damage_region_add(
            &region,
            (struct nb_damage_rect){(int)(index * 2U), 0, 1, 1},
            200,
            80));
    }
    CHECK(!region.full);
    CHECK(region.count <= NB_DAMAGE_REGION_CAPACITY);
    CHECK(nb_damage_region_is_valid(&region, 200, 80));

    nb_damage_region_clear(&region);
    CHECK(nb_damage_region_add(
        &region, (struct nb_damage_rect){4, 8, 3, 1}, 100, 80));
    CHECK(nb_damage_region_add(
        &region, (struct nb_damage_rect){7, 8, 5, 1}, 100, 80));
    CHECK(region.count == 1);
    CHECK(region.rects[0].x == 4 && region.rects[0].y == 8 &&
          region.rects[0].width == 8 && region.rects[0].height == 1);

    nb_damage_region_clear(&other);
    CHECK(nb_damage_region_add(
        &other, (struct nb_damage_rect){-2, -3, 5, 7}, 100, 80));
    CHECK(other.count == 1);
    CHECK(other.rects[0].x == 0 && other.rects[0].y == 0 &&
          other.rects[0].width == 3 && other.rects[0].height == 4);
    CHECK(nb_damage_region_merge(&region, &other, 100, 80));
    CHECK(!region.full);

    if (failures != 0) {
        fprintf(stderr, "damage region tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("damage region tests: ok");
    return 0;
}
