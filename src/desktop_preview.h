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
struct nb_rect;

struct nb_desktop_preview_update {
    bool redraw;
    bool exit_requested;
};

struct nb_desktop_preview *nb_desktop_preview_create(void);
void nb_desktop_preview_destroy(struct nb_desktop_preview *preview);

bool nb_desktop_preview_set_output(
    struct nb_desktop_preview *preview,
    const struct nb_host_output *output);

/*
 * Route normalized standalone input through the same shell model used by the
 * hosted desktop. Pointer coordinates are clamped to the current logical
 * output. Escape and the preview's Exit menu request an orderly worker exit.
 */
bool nb_desktop_preview_handle_input(
    struct nb_desktop_preview *preview,
    const struct nb_host_event *event,
    struct nb_desktop_preview_update *update);
bool nb_desktop_preview_cancel_input(
    struct nb_desktop_preview *preview);

/* Seed or restore the software-cursor position without synthesizing input. */
bool nb_desktop_preview_set_pointer(
    struct nb_desktop_preview *preview,
    int x,
    int y,
    bool visible);

/* Read-only geometry seam used by deterministic input tests. */
bool nb_desktop_preview_window_frame(
    const struct nb_desktop_preview *preview,
    struct nb_rect *frame);

bool nb_desktop_preview_render(
    struct nb_desktop_preview *preview,
    const char *clock_text,
    uint64_t serial,
    struct nb_host_frame *frame);

#endif
