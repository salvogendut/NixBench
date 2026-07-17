#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "user_config.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr,                                                   \
                    "%s:%d: check failed: %s\n",                           \
                    __FILE__,                                                 \
                    __LINE__,                                                 \
                    #expression);                                             \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static bool write_text(const char *path, const char *text)
{
    const size_t length = strlen(text);
    size_t used = 0;
    int descriptor = open(path, O_WRONLY | O_TRUNC);

    if (descriptor < 0) {
        return false;
    }
    while (used < length) {
        const ssize_t count = write(descriptor,
                                    text + used,
                                    length - used);

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

int main(void)
{
    char directory[] = "/tmp/nixbench-user-config-XXXXXX";
    char path[512];
    char resolved[512];
    char error[256] = {0};
    struct nb_user_preferences preferences;
    struct nb_user_preferences loaded;
    struct stat status;
    enum nb_user_config_load_result result;

    CHECK(mkdtemp(directory) != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/.nixbenchrc", directory) > 0);
    CHECK(nb_user_config_path(path,
                              resolved,
                              sizeof(resolved),
                              error,
                              sizeof(error)));
    CHECK(strcmp(path, resolved) == 0);
    CHECK(!nb_user_config_path("relative",
                               resolved,
                               sizeof(resolved),
                               error,
                               sizeof(error)));

    result = nb_user_config_load_or_create(path,
                                           &preferences,
                                           error,
                                           sizeof(error));
    CHECK(result == NB_USER_CONFIG_CREATED);
    CHECK(stat(path, &status) == 0);
    CHECK((status.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) ==
          (S_IRUSR | S_IWUSR));
    CHECK(preferences.pinned_applications[NB_PINNED_APPLICATION_NIXCLOCK]);
    CHECK(preferences.backdrop_primary.red == 24);
    CHECK(!preferences.backdrop_gradient_enabled);
    CHECK(preferences.minimize_gadget_visible);
    CHECK(preferences.maximize_gadget_visible);

    preferences.pinned_applications[NB_PINNED_APPLICATION_SAKURA] = false;
    preferences.backdrop_primary = (struct nb_color){1, 2, 3};
    preferences.backdrop_secondary = (struct nb_color){250, 128, 64};
    preferences.backdrop_gradient_enabled = true;
    preferences.backdrop_gradient_direction =
        NB_BACKDROP_GRADIENT_DIAGONAL;
    preferences.minimize_gadget_visible = false;
    preferences.maximize_gadget_visible = false;
    preferences.window_control_layout = NB_WINDOW_CONTROLS_RIGHT;
    CHECK(nb_user_config_save(path,
                              &preferences,
                              error,
                              sizeof(error)));
    result = nb_user_config_load_or_create(path,
                                           &loaded,
                                           error,
                                           sizeof(error));
    CHECK(result == NB_USER_CONFIG_LOADED);
    CHECK(!loaded.pinned_applications[NB_PINNED_APPLICATION_SAKURA]);
    CHECK(nb_color_equal(loaded.backdrop_primary,
                         (struct nb_color){1, 2, 3}));
    CHECK(nb_color_equal(loaded.backdrop_secondary,
                         (struct nb_color){250, 128, 64}));
    CHECK(loaded.backdrop_gradient_enabled);
    CHECK(loaded.backdrop_gradient_direction ==
          NB_BACKDROP_GRADIENT_DIAGONAL);
    CHECK(!loaded.minimize_gadget_visible);
    CHECK(!loaded.maximize_gadget_visible);
    CHECK(loaded.window_control_layout == NB_WINDOW_CONTROLS_RIGHT);

    CHECK(write_text(path, "desktop.backdrop.primary=not-a-color\n"));
    nb_user_preferences_init(&loaded);
    CHECK(nb_user_config_load_or_create(path,
                                        &loaded,
                                        error,
                                        sizeof(error)) ==
          NB_USER_CONFIG_LOAD_ERROR);
    CHECK(strstr(error, "desktop.backdrop.primary") != NULL);

    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    if (failures != 0) {
        fprintf(stderr, "user config tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("user config tests: ok");
    return 0;
}
