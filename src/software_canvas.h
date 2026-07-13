#ifndef NIXBENCH_SOFTWARE_CANVAS_H
#define NIXBENCH_SOFTWARE_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL.h>

#include "host.h"

/*
 * SDL draws the shell into this canonical CPU buffer. Physical host adapters
 * only consume the completed nb_host_frame and never participate in shell
 * rendering.
 */
struct nb_software_canvas;

struct nb_software_canvas *nb_software_canvas_create(int width, int height);
void nb_software_canvas_destroy(struct nb_software_canvas *canvas);

SDL_Renderer *nb_software_canvas_renderer(
    struct nb_software_canvas *canvas);
bool nb_software_canvas_get_size(const struct nb_software_canvas *canvas,
                                 int *width,
                                 int *height);

/* Flush drawing and borrow the canvas pixels until its next drawing call. */
bool nb_software_canvas_finish(struct nb_software_canvas *canvas,
                               uint64_t serial,
                               struct nb_host_frame *frame);

#endif
