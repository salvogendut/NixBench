#ifndef NIXBENCH_USER_CONFIG_H
#define NIXBENCH_USER_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "preferences.h"

enum nb_user_config_load_result {
    NB_USER_CONFIG_LOAD_ERROR = -1,
    NB_USER_CONFIG_LOADED = 0,
    NB_USER_CONFIG_CREATED = 1
};

/* Resolve an absolute override, or HOME/.nixbenchrc when override is NULL. */
bool nb_user_config_path(const char *override_path,
                         char *path,
                         size_t path_capacity,
                         char *error,
                         size_t error_capacity);

/* Missing files are created with defaults and mode 0600. */
enum nb_user_config_load_result nb_user_config_load_or_create(
    const char *path,
    struct nb_user_preferences *preferences,
    char *error,
    size_t error_capacity);

/* Write through a same-directory temporary file and atomically rename it. */
bool nb_user_config_save(const char *path,
                         const struct nb_user_preferences *preferences,
                         char *error,
                         size_t error_capacity);

#endif
