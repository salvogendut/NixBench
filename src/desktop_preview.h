#ifndef NIXBENCH_DESKTOP_PREVIEW_H
#define NIXBENCH_DESKTOP_PREVIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "host.h"

/*
 * A deterministic shell scene rendered into an SDL software surface. It does
 * not initialize SDL video, open a host window, publish Wayland, or read input.
 * The returned frame borrows pixels from the preview until its next render or
 * destruction.
 */
struct nb_desktop_preview;

struct nb_desktop_preview *nb_desktop_preview_create(void);
void nb_desktop_preview_destroy(struct nb_desktop_preview *preview);

bool nb_desktop_preview_set_output(
    struct nb_desktop_preview *preview,
    const struct nb_host_output *output);

bool nb_desktop_preview_render(
    struct nb_desktop_preview *preview,
    const char *clock_text,
    uint64_t serial,
    struct nb_host_frame *frame);

#endif
