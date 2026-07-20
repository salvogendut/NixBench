#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "theme_bundle.h"

#ifndef NIXBENCH_SOURCE_THEME_DIR
#error NIXBENCH_SOURCE_THEME_DIR must name the repository theme directory
#endif

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static bool make_path(char *path,
                      size_t capacity,
                      const char *directory,
                      const char *name)
{
    const int length = snprintf(path, capacity, "%s/%s", directory, name);

    return length >= 0 && (size_t)length < capacity;
}

static bool write_file(const char *path, const char *text)
{
    const size_t length = strlen(text);
    size_t used = 0;
    int descriptor = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);

    if (descriptor < 0) {
        return false;
    }
    while (used < length) {
        const ssize_t count = write(descriptor, text + used, length - used);

        if (count > 0) {
            used += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            (void)close(descriptor);
            return false;
        }
    }
    return close(descriptor) == 0;
}

static bool file_contains(const char *path, const char *needle)
{
    FILE *file;
    char buffer[8192];
    size_t count;

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    count = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[count] = '\0';
    (void)fclose(file);
    return strstr(buffer, needle) != NULL;
}

static bool write_manifest(const char *directory, const char *text)
{
    char path[1024];

    return make_path(path, sizeof(path), directory, "theme.conf") &&
           write_file(path, text);
}

static bool create_entries(const char *directory)
{
    static const char *const names[] = {
        "desktop.html", "window.html", "menubar.html", "theme.css"
    };
    char path[1024];
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (!make_path(path, sizeof(path), directory, names[index]) ||
            !write_file(path, "<!-- test -->\n")) {
            return false;
        }
    }
    return true;
}

static void remove_fixture(const char *directory)
{
    static const char *const names[] = {
        "theme.conf", "desktop.html", "window.html", "menubar.html",
        "theme.css"
    };
    char path[1024];
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (make_path(path, sizeof(path), directory, names[index])) {
            (void)unlink(path);
        }
    }
    (void)rmdir(directory);
}

static void test_repository_bundles(void)
{
    static const struct {
        const char *directory;
        const char *id;
        const char *name;
    } expected[] = {
        {"Fantasy", "fantasy", "Fantasy"},
        {"CDE", "cde", "CDE"},
        {"BeOS", "beos", "BeOS-inspired"}
    };
    struct nb_theme_catalog catalog;
    char path[1024];
    char error[256];
    size_t index;

    nb_theme_catalog_init(&catalog);
    CHECK(catalog.count == 1);
    CHECK(catalog.bundles[0].renderer == NB_THEME_RENDERER_NATIVE);
    CHECK(strcmp(catalog.bundles[0].id, "classic") == 0);

    for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        CHECK(make_path(path, sizeof(path), NIXBENCH_SOURCE_THEME_DIR,
                        expected[index].directory));
        CHECK(nb_theme_catalog_add(&catalog, path, error, sizeof(error)));
        CHECK(strcmp(catalog.bundles[index + 1].id, expected[index].id) == 0);
        CHECK(strcmp(catalog.bundles[index + 1].name,
                     expected[index].name) == 0);
        CHECK(catalog.bundles[index + 1].renderer == NB_THEME_RENDERER_HTML);
    }
    CHECK(catalog.count == 4);
    CHECK(nb_theme_catalog_find(&catalog, "cde") == &catalog.bundles[2]);
    CHECK(nb_theme_catalog_resolve(&catalog, "missing") ==
          &catalog.bundles[0]);
    CHECK(nb_theme_catalog_resolve(&catalog, NULL) == &catalog.bundles[0]);
    CHECK(!nb_theme_catalog_add(&catalog, path, error, sizeof(error)));
    CHECK(strstr(error, "duplicate") != NULL);

    CHECK(snprintf(path,
                   sizeof(path),
                   "%s/CDE/window.html",
                   NIXBENCH_SOURCE_THEME_DIR) > 0);
    CHECK(file_contains(path, "data-nixbench-action=\"resize_south_east\""));
    CHECK(snprintf(path,
                   sizeof(path),
                   "%s/CDE/theme.css",
                   NIXBENCH_SOURCE_THEME_DIR) > 0);
    CHECK(file_contains(path, "width: var(--nixbench-resize-size)"));
}

static void test_manifest_rejection(void)
{
    static const char valid_manifest[] =
        "format=1\n"
        "id=test-theme\n"
        "name=Test Theme\n"
        "desktop=desktop.html\n"
        "window=window.html\n"
        "menubar=menubar.html\n"
        "stylesheet=theme.css\n"
        "scripts=false\n"
        "network=false\n";
    char directory[] = "/tmp/nixbench-theme-bundle-XXXXXX";
    char error[256];
    struct nb_theme_bundle bundle;

    CHECK(mkdtemp(directory) != NULL);
    CHECK(create_entries(directory));
    CHECK(write_manifest(directory, valid_manifest));
    CHECK(nb_theme_bundle_load(directory, &bundle, error, sizeof(error)));
    CHECK(strcmp(bundle.id, "test-theme") == 0);

    CHECK(write_manifest(directory,
                         "format=1\n"
                         "id=test-theme\n"
                         "name=Unsafe\n"
                         "desktop=../desktop.html\n"
                         "window=window.html\n"
                         "menubar=menubar.html\n"
                         "stylesheet=theme.css\n"
                         "scripts=false\n"
                         "network=false\n"));
    CHECK(!nb_theme_bundle_load(directory, &bundle, error, sizeof(error)));
    CHECK(strstr(error, "unsafe") != NULL);

    CHECK(write_manifest(directory,
                         "format=1\n"
                         "id=test-theme\n"
                         "name=Scripted\n"
                         "desktop=desktop.html\n"
                         "window=window.html\n"
                         "menubar=menubar.html\n"
                         "stylesheet=theme.css\n"
                         "scripts=true\n"
                         "network=false\n"));
    CHECK(!nb_theme_bundle_load(directory, &bundle, error, sizeof(error)));
    CHECK(strstr(error, "scripts") != NULL);

    CHECK(write_manifest(directory,
                         "format=1\n"
                         "id=Bad_ID\n"
                         "name=Bad identifier\n"
                         "desktop=desktop.html\n"
                         "window=window.html\n"
                         "menubar=menubar.html\n"
                         "stylesheet=theme.css\n"
                         "scripts=false\n"
                         "network=false\n"));
    CHECK(!nb_theme_bundle_load(directory, &bundle, error, sizeof(error)));
    CHECK(strstr(error, "id or name") != NULL);

    remove_fixture(directory);
}

int main(void)
{
    test_repository_bundles();
    test_manifest_rejection();
    if (failures != 0) {
        fprintf(stderr, "theme bundle tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("theme bundle tests: ok");
    return 0;
}
