#define _XOPEN_SOURCE 700

#include "theme_bundle.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    NB_THEME_MANIFEST_LIMIT = 16 * 1024,
    NB_THEME_DOCUMENT_LIMIT = 1024 * 1024,
    NB_THEME_STYLESHEET_LIMIT = 2 * 1024 * 1024,
    NB_THEME_LINE_CAPACITY = 2048
};

struct manifest_fields {
    bool format_seen;
    bool id_seen;
    bool name_seen;
    bool desktop_seen;
    bool window_seen;
    bool menubar_seen;
    bool stylesheet_seen;
    bool scripts_seen;
    bool network_seen;
    unsigned int format;
    char id[NB_THEME_ID_CAPACITY];
    char name[NB_THEME_NAME_CAPACITY];
    char desktop[NB_THEME_PATH_CAPACITY];
    char window[NB_THEME_PATH_CAPACITY];
    char menubar[NB_THEME_PATH_CAPACITY];
    char stylesheet[NB_THEME_PATH_CAPACITY];
};

static void set_error(char *error,
                      size_t capacity,
                      const char *format,
                      ...)
{
    va_list arguments;

    if (error == NULL || capacity == 0) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error, capacity, format, arguments);
    va_end(arguments);
}

static bool copy_text(char *destination,
                      size_t capacity,
                      const char *source)
{
    const int length = snprintf(destination, capacity, "%s", source);

    return length >= 0 && (size_t)length < capacity;
}

static char *trim(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

static bool valid_id(const char *id)
{
    const unsigned char *cursor = (const unsigned char *)id;

    if (cursor == NULL || *cursor == '\0' || !islower(*cursor)) {
        return false;
    }
    for (; *cursor != '\0'; ++cursor) {
        if (!islower(*cursor) && !isdigit(*cursor) && *cursor != '-') {
            return false;
        }
    }
    return true;
}

static bool valid_name(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (cursor == NULL || *cursor == '\0') {
        return false;
    }
    for (; *cursor != '\0'; ++cursor) {
        if (iscntrl(*cursor)) {
            return false;
        }
    }
    return true;
}

static bool valid_relative_path(const char *path)
{
    const char *component = path;
    const char *cursor;

    if (path == NULL || path[0] == '\0' || path[0] == '/') {
        return false;
    }
    for (cursor = path;; ++cursor) {
        if (*cursor == '/' || *cursor == '\0') {
            const size_t length = (size_t)(cursor - component);

            if (length == 0 ||
                (length == 1 && component[0] == '.') ||
                (length == 2 && component[0] == '.' &&
                 component[1] == '.')) {
                return false;
            }
            if (*cursor == '\0') {
                return true;
            }
            component = cursor + 1;
        }
    }
}

static bool parse_boolean_false(const char *key,
                                const char *value,
                                char *error,
                                size_t error_capacity)
{
    if (strcmp(value, "false") == 0) {
        return true;
    }
    set_error(error,
              error_capacity,
              "theme capability %s must be false in format 1",
              key);
    return false;
}

static bool assign_field(char *destination,
                         size_t capacity,
                         bool *seen,
                         const char *key,
                         const char *value,
                         char *error,
                         size_t error_capacity)
{
    if (*seen) {
        set_error(error, error_capacity, "duplicate theme key: %s", key);
        return false;
    }
    if (value[0] == '\0' || !copy_text(destination, capacity, value)) {
        set_error(error, error_capacity, "invalid theme value for %s", key);
        return false;
    }
    *seen = true;
    return true;
}

static bool parse_manifest_line(struct manifest_fields *fields,
                                char *line,
                                char *error,
                                size_t error_capacity)
{
    char *separator;
    char *key;
    char *value;

    key = trim(line);
    if (key[0] == '\0' || key[0] == '#') {
        return true;
    }
    separator = strchr(key, '=');
    if (separator == NULL) {
        set_error(error, error_capacity, "theme line has no '=': %s", key);
        return false;
    }
    *separator = '\0';
    value = trim(separator + 1);
    key = trim(key);

    if (strcmp(key, "format") == 0) {
        char *end = NULL;
        unsigned long parsed;

        if (fields->format_seen) {
            set_error(error, error_capacity, "duplicate theme key: format");
            return false;
        }
        errno = 0;
        parsed = strtoul(value, &end, 10);
        if (errno != 0 || end == value || *end != '\0' ||
            parsed > UINT_MAX) {
            set_error(error, error_capacity, "invalid theme format: %s", value);
            return false;
        }
        fields->format = (unsigned int)parsed;
        fields->format_seen = true;
        return true;
    }
    if (strcmp(key, "id") == 0) {
        return assign_field(fields->id, sizeof(fields->id), &fields->id_seen,
                            key, value, error, error_capacity);
    }
    if (strcmp(key, "name") == 0) {
        return assign_field(fields->name, sizeof(fields->name),
                            &fields->name_seen, key, value, error,
                            error_capacity);
    }
    if (strcmp(key, "desktop") == 0) {
        return assign_field(fields->desktop, sizeof(fields->desktop),
                            &fields->desktop_seen, key, value, error,
                            error_capacity);
    }
    if (strcmp(key, "window") == 0) {
        return assign_field(fields->window, sizeof(fields->window),
                            &fields->window_seen, key, value, error,
                            error_capacity);
    }
    if (strcmp(key, "menubar") == 0) {
        return assign_field(fields->menubar, sizeof(fields->menubar),
                            &fields->menubar_seen, key, value, error,
                            error_capacity);
    }
    if (strcmp(key, "stylesheet") == 0) {
        return assign_field(fields->stylesheet, sizeof(fields->stylesheet),
                            &fields->stylesheet_seen, key, value, error,
                            error_capacity);
    }
    if (strcmp(key, "scripts") == 0) {
        if (fields->scripts_seen ||
            !parse_boolean_false(key, value, error, error_capacity)) {
            if (fields->scripts_seen) {
                set_error(error, error_capacity, "duplicate theme key: %s", key);
            }
            return false;
        }
        fields->scripts_seen = true;
        return true;
    }
    if (strcmp(key, "network") == 0) {
        if (fields->network_seen ||
            !parse_boolean_false(key, value, error, error_capacity)) {
            if (fields->network_seen) {
                set_error(error, error_capacity, "duplicate theme key: %s", key);
            }
            return false;
        }
        fields->network_seen = true;
        return true;
    }
    set_error(error, error_capacity, "unknown theme key: %s", key);
    return false;
}

static bool path_is_inside(const char *root, const char *path)
{
    const size_t root_length = strlen(root);

    return strncmp(root, path, root_length) == 0 &&
           (root_length == 1 || path[root_length] == '/');
}

static bool resolve_theme_file(const char *root,
                               const char *relative,
                               off_t maximum_size,
                               char *destination,
                               size_t destination_capacity,
                               char *error,
                               size_t error_capacity)
{
    char combined[PATH_MAX];
    char canonical[PATH_MAX];
    struct stat status;
    const int length = snprintf(combined, sizeof(combined), "%s/%s",
                                root, relative);

    if (!valid_relative_path(relative) || length < 0 ||
        (size_t)length >= sizeof(combined)) {
        set_error(error, error_capacity, "unsafe theme path: %s", relative);
        return false;
    }
    if (realpath(combined, canonical) == NULL) {
        set_error(error, error_capacity, "cannot resolve theme file %s: %s",
                  relative, strerror(errno));
        return false;
    }
    if (!path_is_inside(root, canonical)) {
        set_error(error, error_capacity, "theme file escapes bundle: %s",
                  relative);
        return false;
    }
    if (stat(canonical, &status) != 0 || !S_ISREG(status.st_mode)) {
        set_error(error, error_capacity, "theme entry is not a regular file: %s",
                  relative);
        return false;
    }
    if (status.st_size < 0 || status.st_size > maximum_size) {
        set_error(error, error_capacity, "theme entry is too large: %s",
                  relative);
        return false;
    }
    if (!copy_text(destination, destination_capacity, canonical)) {
        set_error(error, error_capacity, "theme path is too long: %s", relative);
        return false;
    }
    return true;
}

void nb_theme_bundle_init_classic(struct nb_theme_bundle *bundle)
{
    if (bundle == NULL) {
        return;
    }
    memset(bundle, 0, sizeof(*bundle));
    bundle->renderer = NB_THEME_RENDERER_NATIVE;
    bundle->format = NB_THEME_BUNDLE_FORMAT;
    (void)copy_text(bundle->id, sizeof(bundle->id), "classic");
    (void)copy_text(bundle->name, sizeof(bundle->name), "Classic");
}

bool nb_theme_bundle_load(const char *directory,
                          struct nb_theme_bundle *bundle,
                          char *error,
                          size_t error_capacity)
{
    char root[PATH_MAX];
    char manifest_path[PATH_MAX];
    char line[NB_THEME_LINE_CAPACITY];
    struct manifest_fields fields = {0};
    struct nb_theme_bundle loaded = {0};
    struct stat status;
    FILE *manifest;
    unsigned long line_number = 0;
    int manifest_path_length;

    if (error != NULL && error_capacity > 0) {
        error[0] = '\0';
    }
    if (directory == NULL || bundle == NULL) {
        set_error(error, error_capacity, "theme directory and output are required");
        return false;
    }
    if (realpath(directory, root) == NULL || stat(root, &status) != 0 ||
        !S_ISDIR(status.st_mode)) {
        set_error(error, error_capacity, "cannot resolve theme directory: %s",
                  strerror(errno));
        return false;
    }
    manifest_path_length = snprintf(manifest_path,
                                    sizeof(manifest_path),
                                    "%s/theme.conf",
                                    root);
    if (manifest_path_length < 0 ||
        (size_t)manifest_path_length >= sizeof(manifest_path)) {
        set_error(error, error_capacity, "theme directory path is too long");
        return false;
    }
    if (stat(manifest_path, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < 0 || status.st_size > NB_THEME_MANIFEST_LIMIT) {
        set_error(error, error_capacity, "invalid or oversized theme.conf");
        return false;
    }
    manifest = fopen(manifest_path, "r");
    if (manifest == NULL) {
        set_error(error, error_capacity, "cannot open theme.conf: %s",
                  strerror(errno));
        return false;
    }
    while (fgets(line, sizeof(line), manifest) != NULL) {
        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(manifest)) {
            set_error(error, error_capacity, "theme.conf line %lu is too long",
                      line_number);
            (void)fclose(manifest);
            return false;
        }
        if (!parse_manifest_line(&fields, line, error, error_capacity)) {
            char detail[256];

            (void)snprintf(detail, sizeof(detail), "%s",
                           error != NULL ? error : "invalid manifest");
            set_error(error, error_capacity, "theme.conf line %lu: %s",
                      line_number, detail);
            (void)fclose(manifest);
            return false;
        }
    }
    if (ferror(manifest) || fclose(manifest) != 0) {
        set_error(error, error_capacity, "cannot read theme.conf");
        return false;
    }
    if (!fields.format_seen || fields.format != NB_THEME_BUNDLE_FORMAT ||
        !fields.id_seen || !fields.name_seen || !fields.desktop_seen ||
        !fields.window_seen || !fields.menubar_seen ||
        !fields.stylesheet_seen || !fields.scripts_seen ||
        !fields.network_seen) {
        set_error(error, error_capacity,
                  "theme.conf is incomplete or uses an unsupported format");
        return false;
    }
    if (!valid_id(fields.id) || !valid_name(fields.name)) {
        set_error(error, error_capacity, "theme id or name is invalid");
        return false;
    }

    loaded.renderer = NB_THEME_RENDERER_HTML;
    loaded.format = fields.format;
    if (!copy_text(loaded.id, sizeof(loaded.id), fields.id) ||
        !copy_text(loaded.name, sizeof(loaded.name), fields.name) ||
        !copy_text(loaded.directory, sizeof(loaded.directory), root) ||
        !resolve_theme_file(root, fields.desktop, NB_THEME_DOCUMENT_LIMIT,
                            loaded.desktop_path, sizeof(loaded.desktop_path),
                            error, error_capacity) ||
        !resolve_theme_file(root, fields.window, NB_THEME_DOCUMENT_LIMIT,
                            loaded.window_path, sizeof(loaded.window_path),
                            error, error_capacity) ||
        !resolve_theme_file(root, fields.menubar, NB_THEME_DOCUMENT_LIMIT,
                            loaded.menubar_path, sizeof(loaded.menubar_path),
                            error, error_capacity) ||
        !resolve_theme_file(root, fields.stylesheet, NB_THEME_STYLESHEET_LIMIT,
                            loaded.stylesheet_path,
                            sizeof(loaded.stylesheet_path), error,
                            error_capacity)) {
        if (error != NULL && error_capacity > 0 && error[0] == '\0') {
            set_error(error, error_capacity, "theme metadata is too long");
        }
        return false;
    }
    *bundle = loaded;
    return true;
}

void nb_theme_catalog_init(struct nb_theme_catalog *catalog)
{
    if (catalog == NULL) {
        return;
    }
    memset(catalog, 0, sizeof(*catalog));
    nb_theme_bundle_init_classic(&catalog->bundles[0]);
    catalog->count = 1;
}

const struct nb_theme_bundle *nb_theme_catalog_find(
    const struct nb_theme_catalog *catalog,
    const char *id)
{
    size_t index;

    if (catalog == NULL || id == NULL || id[0] == '\0') {
        return NULL;
    }
    for (index = 0; index < catalog->count; ++index) {
        if (strcmp(catalog->bundles[index].id, id) == 0) {
            return &catalog->bundles[index];
        }
    }
    return NULL;
}

bool nb_theme_catalog_add(struct nb_theme_catalog *catalog,
                          const char *directory,
                          char *error,
                          size_t error_capacity)
{
    struct nb_theme_bundle loaded;

    if (catalog == NULL || catalog->count == 0 ||
        catalog->count > NB_THEME_CATALOG_CAPACITY) {
        set_error(error, error_capacity, "theme catalog is not initialized");
        return false;
    }
    if (catalog->count == NB_THEME_CATALOG_CAPACITY) {
        set_error(error, error_capacity, "theme catalog is full");
        return false;
    }
    if (!nb_theme_bundle_load(directory, &loaded, error, error_capacity)) {
        return false;
    }
    if (nb_theme_catalog_find(catalog, loaded.id) != NULL) {
        set_error(error, error_capacity, "duplicate theme id: %s", loaded.id);
        return false;
    }
    catalog->bundles[catalog->count++] = loaded;
    return true;
}

const struct nb_theme_bundle *nb_theme_catalog_resolve(
    const struct nb_theme_catalog *catalog,
    const char *id)
{
    const struct nb_theme_bundle *found = nb_theme_catalog_find(catalog, id);

    if (found != NULL) {
        return found;
    }
    if (catalog == NULL || catalog->count == 0) {
        return NULL;
    }
    return &catalog->bundles[0];
}
