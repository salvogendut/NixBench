#include "software_canvas.h"

#include <stddef.h>
#include <stdlib.h>

struct nb_software_canvas {
    SDL_Surface *surface;
    SDL_Renderer *renderer;
};

struct nb_software_canvas *nb_software_canvas_create(int width, int height)
{
    struct nb_software_canvas *canvas;

    if (width <= 0 || height <= 0) {
        return NULL;
    }
    canvas = calloc(1, sizeof(*canvas));
    if (canvas == NULL) {
        return NULL;
    }
    canvas->surface = SDL_CreateSurface(width,
                                        height,
                                        SDL_PIXELFORMAT_XRGB8888);
    if (canvas->surface == NULL) {
        free(canvas);
        return NULL;
    }
    canvas->renderer = SDL_CreateSoftwareRenderer(canvas->surface);
    if (canvas->renderer == NULL) {
        SDL_DestroySurface(canvas->surface);
        free(canvas);
        return NULL;
    }
    return canvas;
}

void nb_software_canvas_destroy(struct nb_software_canvas *canvas)
{
    if (canvas == NULL) {
        return;
    }
    SDL_DestroyRenderer(canvas->renderer);
    SDL_DestroySurface(canvas->surface);
    free(canvas);
}

SDL_Renderer *nb_software_canvas_renderer(
    struct nb_software_canvas *canvas)
{
    return canvas == NULL ? NULL : canvas->renderer;
}

bool nb_software_canvas_get_size(const struct nb_software_canvas *canvas,
                                 int *width,
                                 int *height)
{
    if (canvas == NULL || width == NULL || height == NULL) {
        return false;
    }
    *width = canvas->surface->w;
    *height = canvas->surface->h;
    return true;
}

bool nb_software_canvas_finish(struct nb_software_canvas *canvas,
                               uint64_t serial,
                               struct nb_host_frame *frame)
{
    if (canvas == NULL || frame == NULL || serial == 0 ||
        canvas->surface->pixels == NULL || canvas->surface->pitch <= 0 ||
        !SDL_RenderPresent(canvas->renderer)) {
        return false;
    }

    frame->pixels = canvas->surface->pixels;
    frame->width = canvas->surface->w;
    frame->height = canvas->surface->h;
    frame->stride = (size_t)canvas->surface->pitch;
    frame->format = NB_HOST_PIXEL_FORMAT_XRGB8888;
    frame->serial = serial;
    frame->damage_x = 0;
    frame->damage_y = 0;
    frame->damage_width = 0;
    frame->damage_height = 0;
    return nb_host_frame_is_valid(frame);
}
