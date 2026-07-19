#include <stdbool.h>
#include <stdio.h>

#include "theme_atlas.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void test_atomic_layout(void)
{
    struct nb_theme_atlas atlas;
    const struct nb_theme_tile *tile;

    nb_theme_atlas_init(&atlas);
    CHECK(atlas.published.generation == 0);
    CHECK(nb_theme_atlas_begin(&atlas, 1, 1024, 512));
    CHECK(!nb_theme_atlas_begin(&atlas, 2, 1024, 512));
    CHECK(nb_theme_atlas_add_tile(
        &atlas, NB_THEME_TILE_DESKTOP, 0,
        (struct nb_theme_rect){0, 0, 640, 480}));
    CHECK(nb_theme_atlas_add_tile(
        &atlas, NB_THEME_TILE_WINDOW, 42,
        (struct nb_theme_rect){640, 0, 300, 200}));
    CHECK(!nb_theme_atlas_add_tile(
        &atlas, NB_THEME_TILE_WINDOW, 43,
        (struct nb_theme_rect){900, 20, 100, 100}));
    CHECK(!nb_theme_atlas_add_tile(
        &atlas, NB_THEME_TILE_WINDOW, 42,
        (struct nb_theme_rect){0, 480, 100, 30}));
    CHECK(!nb_theme_atlas_commit(&atlas, 2));
    CHECK(atlas.published.generation == 0);
    CHECK(nb_theme_atlas_commit(&atlas, 1));
    CHECK(atlas.published.generation == 1);
    CHECK(atlas.published.tile_count == 2);
    tile = nb_theme_atlas_find_tile(&atlas.published,
                                     NB_THEME_TILE_WINDOW,
                                     42);
    CHECK(tile != NULL);
    CHECK(tile != NULL && tile->atlas_rect.width == 300);

    CHECK(nb_theme_atlas_begin(&atlas, 2, 100, 100));
    CHECK(nb_theme_atlas_add_tile(
        &atlas, NB_THEME_TILE_MENUBAR, 0,
        (struct nb_theme_rect){0, 0, 100, 24}));
    nb_theme_atlas_abort(&atlas);
    CHECK(atlas.published.generation == 1);
    CHECK(!atlas.pending_active);
    CHECK(!nb_theme_atlas_begin(&atlas, 1, 100, 100));
}

static void test_action_priority(void)
{
    struct nb_theme_atlas atlas;

    nb_theme_atlas_init(&atlas);
    CHECK(nb_theme_atlas_begin(&atlas, 10, 400, 200));
    CHECK(nb_theme_atlas_add_tile(
        &atlas, NB_THEME_TILE_WINDOW, 9,
        (struct nb_theme_rect){0, 0, 320, 180}));
    CHECK(nb_theme_atlas_add_action_region(
        &atlas, NB_THEME_TILE_WINDOW, 9, NB_THEME_ACTION_MOVE,
        (struct nb_theme_rect){0, 0, 320, 30}));
    CHECK(nb_theme_atlas_add_action_region(
        &atlas, NB_THEME_TILE_WINDOW, 9, NB_THEME_ACTION_CLOSE,
        (struct nb_theme_rect){290, 4, 22, 22}));
    CHECK(!nb_theme_atlas_add_action_region(
        &atlas, NB_THEME_TILE_WINDOW, 9, NB_THEME_ACTION_MINIMIZE,
        (struct nb_theme_rect){310, 4, 22, 22}));
    CHECK(!nb_theme_atlas_add_action_region(
        &atlas, NB_THEME_TILE_DESKTOP, 0, NB_THEME_ACTION_MOVE,
        (struct nb_theme_rect){0, 0, 20, 20}));
    CHECK(nb_theme_atlas_commit(&atlas, 10));
    CHECK(nb_theme_atlas_hit_test(&atlas.published,
                                  NB_THEME_TILE_WINDOW,
                                  9,
                                  20,
                                  10) == NB_THEME_ACTION_MOVE);
    CHECK(nb_theme_atlas_hit_test(&atlas.published,
                                  NB_THEME_TILE_WINDOW,
                                  9,
                                  300,
                                  10) == NB_THEME_ACTION_CLOSE);
    CHECK(nb_theme_atlas_hit_test(&atlas.published,
                                  NB_THEME_TILE_WINDOW,
                                  9,
                                  20,
                                  80) == NB_THEME_ACTION_NONE);
}

static void test_invalid_dimensions(void)
{
    struct nb_theme_atlas atlas;

    nb_theme_atlas_init(&atlas);
    CHECK(!nb_theme_atlas_begin(&atlas, 0, 10, 10));
    CHECK(!nb_theme_atlas_begin(&atlas, 1, 0, 10));
    CHECK(!nb_theme_atlas_begin(&atlas, 1,
                                NB_THEME_ATLAS_MAX_DIMENSION + 1,
                                10));
    CHECK(nb_theme_atlas_begin(&atlas, 1, 10, 10));
    CHECK(!nb_theme_atlas_add_tile(
        &atlas, NB_THEME_TILE_WINDOW, 0,
        (struct nb_theme_rect){0, 0, 5, 5}));
    CHECK(!nb_theme_atlas_add_tile(
        &atlas, NB_THEME_TILE_DESKTOP, 2,
        (struct nb_theme_rect){0, 0, 5, 5}));
    CHECK(!nb_theme_atlas_commit(&atlas, 1));
}

int main(void)
{
    test_atomic_layout();
    test_action_priority();
    test_invalid_dimensions();
    if (failures != 0) {
        fprintf(stderr, "theme atlas tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("theme atlas tests: ok");
    return 0;
}
