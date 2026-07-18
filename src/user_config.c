#define _POSIX_C_SOURCE 200809L

#include "user_config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    NB_USER_CONFIG_LINE_CAPACITY = 1024,
    NB_USER_CONFIG_TEMP_SUFFIX_CAPACITY = 16
};

static void set_error(char *error,
                      size_t error_capacity,
                      const char *format,
                      ...)
{
    va_list arguments;

    if (error == NULL || error_capacity == 0) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error, error_capacity, format, arguments);
    va_end(arguments);
}

static bool copy_value(char *destination,
                       size_t capacity,
                       const char *value)
{
    const size_t length = strlen(value);

    if (length >= capacity) {
        return false;
    }
    (void)memcpy(destination, value, length + 1);
    return true;
}

static bool parse_boolean(const char *value, bool *parsed)
{
    if (strcmp(value, "true") == 0 || strcmp(value, "on") == 0 ||
        strcmp(value, "pinned") == 0) {
        *parsed = true;
        return true;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "off") == 0 ||
        strcmp(value, "unpinned") == 0) {
        *parsed = false;
        return true;
    }
    return false;
}

static int hex_digit(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    character = (char)tolower((unsigned char)character);
    if (character >= 'a' && character <= 'f') {
        return 10 + character - 'a';
    }
    return -1;
}

static bool parse_color(const char *value, struct nb_color *color)
{
    int digits[6];
    size_t index;

    if (strlen(value) != 7 || value[0] != '#') {
        return false;
    }
    for (index = 0; index < 6; ++index) {
        digits[index] = hex_digit(value[index + 1]);
        if (digits[index] < 0) {
            return false;
        }
    }
    color->red = (uint8_t)((digits[0] << 4) | digits[1]);
    color->green = (uint8_t)((digits[2] << 4) | digits[3]);
    color->blue = (uint8_t)((digits[4] << 4) | digits[5]);
    return true;
}

static bool parse_known_key(struct nb_user_preferences *preferences,
                            const char *key,
                            const char *value,
                            unsigned int *version,
                            bool *minimize_value,
                            bool *minimize_present)
{
    if (strcmp(key, "version") == 0) {
        if (strcmp(value, "1") == 0) {
            *version = 1;
            return true;
        }
        if (strcmp(value, "2") == 0) {
            *version = 2;
            return true;
        }
        if (strcmp(value, "3") == 0) {
            *version = 3;
            return true;
        }
        return false;
    }
    if (strcmp(key, "applications.nixclock") == 0) {
        return parse_boolean(value,
                             &preferences->pinned_applications[
                                 NB_PINNED_APPLICATION_NIXCLOCK]);
    }
    if (strcmp(key, "applications.sakura") == 0) {
        return parse_boolean(value,
                             &preferences->pinned_applications[
                                 NB_PINNED_APPLICATION_SAKURA]);
    }
    if (strcmp(key, "applications.midori") == 0) {
        return parse_boolean(value,
                             &preferences->pinned_applications[
                                 NB_PINNED_APPLICATION_MIDORI]);
    }
    if (strcmp(key, "desktop.backdrop.primary") == 0) {
        return parse_color(value, &preferences->backdrop_primary);
    }
    if (strcmp(key, "desktop.backdrop.secondary") == 0) {
        return parse_color(value, &preferences->backdrop_secondary);
    }
    if (strcmp(key, "desktop.backdrop.gradient") == 0) {
        return parse_boolean(value,
                             &preferences->backdrop_gradient_enabled);
    }
    if (strcmp(key, "desktop.backdrop.direction") == 0) {
        if (strcmp(value, "vertical") == 0) {
            preferences->backdrop_gradient_direction =
                NB_BACKDROP_GRADIENT_VERTICAL;
        } else if (strcmp(value, "horizontal") == 0) {
            preferences->backdrop_gradient_direction =
                NB_BACKDROP_GRADIENT_HORIZONTAL;
        } else if (strcmp(value, "diagonal") == 0) {
            preferences->backdrop_gradient_direction =
                NB_BACKDROP_GRADIENT_DIAGONAL;
        } else {
            return false;
        }
        return true;
    }
    if (strcmp(key, "desktop.wallpaper") == 0) {
        return copy_value(preferences->wallpaper,
                          sizeof(preferences->wallpaper),
                          value);
    }
    if (strcmp(key, "desktop.wallpaper.mode") == 0) {
        if (strcmp(value, "center") == 0) {
            preferences->wallpaper_mode = NB_WALLPAPER_CENTER;
        } else if (strcmp(value, "tile") == 0) {
            preferences->wallpaper_mode = NB_WALLPAPER_TILE;
        } else if (strcmp(value, "fit") == 0) {
            preferences->wallpaper_mode = NB_WALLPAPER_FIT;
        } else if (strcmp(value, "fill") == 0) {
            preferences->wallpaper_mode = NB_WALLPAPER_FILL;
        } else {
            return false;
        }
        return true;
    }
    if (strcmp(key, "desktop.theme") == 0) {
        return value[0] != '\0' &&
               copy_value(preferences->desktop_theme,
                          sizeof(preferences->desktop_theme),
                          value);
    }
    if (strcmp(key, "windows.theme") == 0) {
        return value[0] != '\0' &&
               copy_value(preferences->window_theme,
                          sizeof(preferences->window_theme),
                          value);
    }
    if (strcmp(key, "windows.maximize") == 0) {
        return parse_boolean(value, &preferences->maximize_gadget_visible);
    }
    if (strcmp(key, "windows.minimize") == 0) {
        *minimize_present = parse_boolean(value, minimize_value);
        return *minimize_present;
    }
    if (strcmp(key, "windows.controls") == 0) {
        if (strcmp(value, "split") == 0) {
            /* Version-2 split placed close on the left. Keep old files
             * readable while adopting the right-hand control cluster. */
            preferences->window_control_layout = NB_WINDOW_CONTROLS_RIGHT;
        } else if (strcmp(value, "left") == 0) {
            preferences->window_control_layout = NB_WINDOW_CONTROLS_LEFT;
        } else if (strcmp(value, "right") == 0) {
            preferences->window_control_layout = NB_WINDOW_CONTROLS_RIGHT;
        } else {
            return false;
        }
        return true;
    }
    /* Unknown keys are retained conceptually for forward compatibility. */
    return true;
}

static bool parse_stream(FILE *stream,
                         struct nb_user_preferences *preferences,
                         char *error,
                         size_t error_capacity)
{
    char line[NB_USER_CONFIG_LINE_CAPACITY];
    unsigned long line_number = 0;
    unsigned int version = 1;
    bool minimize_value = preferences->minimize_gadget_visible;
    bool minimize_present = false;

    while (fgets(line, sizeof(line), stream) != NULL) {
        char *key;
        char *value;
        char *equals;
        char *end;

        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(stream)) {
            set_error(error,
                      error_capacity,
                      "configuration line %lu is too long",
                      line_number);
            return false;
        }
        key = line;
        while (isspace((unsigned char)*key)) {
            ++key;
        }
        if (*key == '\0' || *key == '\n' || *key == '#') {
            continue;
        }
        end = key + strlen(key);
        while (end > key && isspace((unsigned char)end[-1])) {
            *--end = '\0';
        }
        equals = strchr(key, '=');
        if (equals == NULL) {
            set_error(error,
                      error_capacity,
                      "configuration line %lu has no '='",
                      line_number);
            return false;
        }
        *equals = '\0';
        value = equals + 1;
        end = equals;
        while (end > key && isspace((unsigned char)end[-1])) {
            *--end = '\0';
        }
        while (isspace((unsigned char)*value)) {
            ++value;
        }
        end = value + strlen(value);
        while (end > value && isspace((unsigned char)end[-1])) {
            *--end = '\0';
        }
        if (key[0] == '\0' ||
            !parse_known_key(preferences,
                             key,
                             value,
                             &version,
                             &minimize_value,
                             &minimize_present)) {
            set_error(error,
                      error_capacity,
                      "invalid value for '%s' on line %lu",
                      key,
                      line_number);
            return false;
        }
    }
    if (ferror(stream)) {
        set_error(error,
                  error_capacity,
                  "could not read configuration: %s",
                  strerror(errno));
        return false;
    }
    if (version >= 2 && minimize_present) {
        preferences->minimize_gadget_visible = minimize_value;
    }
    return nb_user_preferences_is_valid(preferences);
}

bool nb_user_config_path(const char *override_path,
                         char *path,
                         size_t path_capacity,
                         char *error,
                         size_t error_capacity)
{
    const char *home;
    int length;

    if (path == NULL || path_capacity == 0) {
        set_error(error, error_capacity, "configuration path is unavailable");
        return false;
    }
    if (override_path != NULL) {
        if (override_path[0] != '/') {
            set_error(error,
                      error_capacity,
                      "configuration override must be absolute");
            return false;
        }
        length = snprintf(path, path_capacity, "%s", override_path);
    } else {
        home = getenv("HOME");
        if (home == NULL || home[0] != '/') {
            set_error(error, error_capacity, "HOME is not an absolute path");
            return false;
        }
        length = snprintf(path, path_capacity, "%s/.nixbenchrc", home);
    }
    if (length < 0 || (size_t)length >= path_capacity) {
        set_error(error, error_capacity, "configuration path is too long");
        return false;
    }
    return true;
}

static const char *direction_name(enum nb_backdrop_gradient_direction value)
{
    switch (value) {
    case NB_BACKDROP_GRADIENT_VERTICAL:
        return "vertical";
    case NB_BACKDROP_GRADIENT_HORIZONTAL:
        return "horizontal";
    case NB_BACKDROP_GRADIENT_DIAGONAL:
        return "diagonal";
    }
    return "vertical";
}

static const char *layout_name(enum nb_window_control_layout value)
{
    switch (value) {
    case NB_WINDOW_CONTROLS_SPLIT:
        return "right";
    case NB_WINDOW_CONTROLS_LEFT:
        return "left";
    case NB_WINDOW_CONTROLS_RIGHT:
        return "right";
    }
    return "split";
}

static const char *wallpaper_mode_name(enum nb_wallpaper_mode value)
{
    switch (value) {
    case NB_WALLPAPER_CENTER:
        return "center";
    case NB_WALLPAPER_TILE:
        return "tile";
    case NB_WALLPAPER_FIT:
        return "fit";
    case NB_WALLPAPER_FILL:
        return "fill";
    }
    return "fit";
}

static bool write_stream(FILE *stream,
                         const struct nb_user_preferences *preferences)
{
    return fprintf(stream,
                   "# NixBench user configuration\n"
                   "# This file is rewritten atomically by NixBench Settings.\n"
                   "version=3\n"
                   "applications.nixclock=%s\n"
                   "applications.sakura=%s\n"
                   "applications.midori=%s\n"
                   "desktop.backdrop.primary=#%02x%02x%02x\n"
                   "desktop.backdrop.secondary=#%02x%02x%02x\n"
                   "desktop.backdrop.gradient=%s\n"
                   "desktop.backdrop.direction=%s\n"
                   "desktop.wallpaper=%s\n"
                   "desktop.wallpaper.mode=%s\n"
                   "desktop.theme=%s\n"
                   "windows.theme=%s\n"
                   "windows.minimize=%s\n"
                   "windows.maximize=%s\n"
                   "windows.controls=%s\n",
                   preferences->pinned_applications[
                       NB_PINNED_APPLICATION_NIXCLOCK]
                       ? "pinned" : "unpinned",
                   preferences->pinned_applications[
                       NB_PINNED_APPLICATION_SAKURA]
                       ? "pinned" : "unpinned",
                   preferences->pinned_applications[
                       NB_PINNED_APPLICATION_MIDORI]
                       ? "pinned" : "unpinned",
                   (unsigned int)preferences->backdrop_primary.red,
                   (unsigned int)preferences->backdrop_primary.green,
                   (unsigned int)preferences->backdrop_primary.blue,
                   (unsigned int)preferences->backdrop_secondary.red,
                   (unsigned int)preferences->backdrop_secondary.green,
                   (unsigned int)preferences->backdrop_secondary.blue,
                   preferences->backdrop_gradient_enabled ? "on" : "off",
                   direction_name(preferences->backdrop_gradient_direction),
                   preferences->wallpaper,
                   wallpaper_mode_name(preferences->wallpaper_mode),
                   preferences->desktop_theme,
                   preferences->window_theme,
                   preferences->minimize_gadget_visible ? "true" : "false",
                   preferences->maximize_gadget_visible ? "true" : "false",
                   layout_name(preferences->window_control_layout)) > 0;
}

bool nb_user_config_save(const char *path,
                         const struct nb_user_preferences *preferences,
                         char *error,
                         size_t error_capacity)
{
    char *temporary_path;
    size_t capacity;
    int descriptor = -1;
    FILE *stream = NULL;
    bool stream_ok;
    bool ok = false;

    if (path == NULL || path[0] != '/' ||
        !nb_user_preferences_is_valid(preferences)) {
        set_error(error, error_capacity, "invalid configuration save request");
        return false;
    }
    capacity = strlen(path) + NB_USER_CONFIG_TEMP_SUFFIX_CAPACITY;
    temporary_path = malloc(capacity);
    if (temporary_path == NULL) {
        set_error(error, error_capacity, "out of memory forming save path");
        return false;
    }
    (void)snprintf(temporary_path, capacity, "%s.tmp.XXXXXX", path);
    descriptor = mkstemp(temporary_path);
    /* POSIX mkstemp creates the file with S_IRUSR | S_IWUSR. */
    if (descriptor < 0) {
        set_error(error,
                  error_capacity,
                  "could not create configuration temporary file: %s",
                  strerror(errno));
        goto cleanup;
    }
    stream = fdopen(descriptor, "w");
    if (stream == NULL) {
        set_error(error,
                  error_capacity,
                  "could not open configuration temporary file: %s",
                  strerror(errno));
        goto cleanup;
    }
    descriptor = -1;
    stream_ok = write_stream(stream, preferences);
    if (stream_ok && fflush(stream) != 0) {
        stream_ok = false;
    }
    if (stream_ok && fsync(fileno(stream)) != 0) {
        stream_ok = false;
    }
    if (fclose(stream) != 0) {
        stream_ok = false;
    }
    stream = NULL;
    if (!stream_ok) {
        set_error(error,
                  error_capacity,
                  "could not finish configuration temporary file: %s",
                  strerror(errno));
        goto cleanup;
    }
    if (rename(temporary_path, path) != 0) {
        set_error(error,
                  error_capacity,
                  "could not replace configuration: %s",
                  strerror(errno));
        goto cleanup;
    }
    ok = true;

cleanup:
    if (stream != NULL) {
        (void)fclose(stream);
    } else if (descriptor >= 0) {
        (void)close(descriptor);
    }
    if (!ok) {
        (void)unlink(temporary_path);
    }
    free(temporary_path);
    return ok;
}

enum nb_user_config_load_result nb_user_config_load_or_create(
    const char *path,
    struct nb_user_preferences *preferences,
    char *error,
    size_t error_capacity)
{
    FILE *stream;
    struct nb_user_preferences parsed;

    if (path == NULL || preferences == NULL) {
        set_error(error, error_capacity, "invalid configuration load request");
        return NB_USER_CONFIG_LOAD_ERROR;
    }
    nb_user_preferences_init(&parsed);
    stream = fopen(path, "r");
    if (stream == NULL) {
        if (errno != ENOENT) {
            set_error(error,
                      error_capacity,
                      "could not open configuration: %s",
                      strerror(errno));
            return NB_USER_CONFIG_LOAD_ERROR;
        }
        if (!nb_user_config_save(path, &parsed, error, error_capacity)) {
            return NB_USER_CONFIG_LOAD_ERROR;
        }
        *preferences = parsed;
        return NB_USER_CONFIG_CREATED;
    }
    if (!parse_stream(stream, &parsed, error, error_capacity)) {
        (void)fclose(stream);
        return NB_USER_CONFIG_LOAD_ERROR;
    }
    if (fclose(stream) != 0) {
        set_error(error,
                  error_capacity,
                  "could not close configuration: %s",
                  strerror(errno));
        return NB_USER_CONFIG_LOAD_ERROR;
    }
    *preferences = parsed;
    return NB_USER_CONFIG_LOADED;
}
