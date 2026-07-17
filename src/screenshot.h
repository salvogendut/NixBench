#ifndef NIXBENCH_SCREENSHOT_H
#define NIXBENCH_SCREENSHOT_H

#include <stdbool.h>
#include <stddef.h>

#include "host.h"

bool nb_screenshot_save_home(const struct nb_host_frame *frame,
                             char *saved_path,
                             size_t saved_path_capacity,
                             char *error,
                             size_t error_capacity);

#endif
