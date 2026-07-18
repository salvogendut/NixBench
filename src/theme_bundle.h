#ifndef NIXBENCH_THEME_BUNDLE_H
#define NIXBENCH_THEME_BUNDLE_H

#include <stdbool.h>
#include <stddef.h>

enum {
    NB_THEME_BUNDLE_FORMAT = 1,
    NB_THEME_ID_CAPACITY = 32,
    NB_THEME_NAME_CAPACITY = 96,
    NB_THEME_PATH_CAPACITY = 1024,
    NB_THEME_CATALOG_CAPACITY = 32
};

enum nb_theme_renderer_kind {
    NB_THEME_RENDERER_NATIVE = 0,
    NB_THEME_RENDERER_HTML
};

struct nb_theme_bundle {
    enum nb_theme_renderer_kind renderer;
    unsigned int format;
    char id[NB_THEME_ID_CAPACITY];
    char name[NB_THEME_NAME_CAPACITY];
    char directory[NB_THEME_PATH_CAPACITY];
    char desktop_path[NB_THEME_PATH_CAPACITY];
    char window_path[NB_THEME_PATH_CAPACITY];
    char menubar_path[NB_THEME_PATH_CAPACITY];
    char stylesheet_path[NB_THEME_PATH_CAPACITY];
};

struct nb_theme_catalog {
    struct nb_theme_bundle bundles[NB_THEME_CATALOG_CAPACITY];
    size_t count;
};

void nb_theme_bundle_init_classic(struct nb_theme_bundle *bundle);

bool nb_theme_bundle_load(const char *directory,
                          struct nb_theme_bundle *bundle,
                          char *error,
                          size_t error_capacity);

void nb_theme_catalog_init(struct nb_theme_catalog *catalog);

bool nb_theme_catalog_add(struct nb_theme_catalog *catalog,
                          const char *directory,
                          char *error,
                          size_t error_capacity);

const struct nb_theme_bundle *nb_theme_catalog_find(
    const struct nb_theme_catalog *catalog,
    const char *id);

/* Unknown, empty, or NULL identifiers resolve to the native Classic bundle. */
const struct nb_theme_bundle *nb_theme_catalog_resolve(
    const struct nb_theme_catalog *catalog,
    const char *id);

#endif
