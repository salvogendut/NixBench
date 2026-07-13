#ifndef NIXBENCH_HOST_SDL_H
#define NIXBENCH_HOST_SDL_H

#include <stdbool.h>

#include "host.h"

struct nb_host_sdl_options {
    const char *title;
    int window_width;
    int window_height;
    int minimum_width;
    int minimum_height;
    bool fullscreen;
    bool resizable;
    bool high_pixel_density;
};

/* Initialize options for the current hosted NixBench development window. */
void nb_host_sdl_options_init(struct nb_host_sdl_options *options);

/*
 * The adapter owns one SDL video-subsystem reference, window, renderer, and
 * persistent streaming texture. It must be created, used, and destroyed on
 * the SDL video thread.
 */
struct nb_host *nb_host_sdl_create(
    const struct nb_host_sdl_options *options);

/* Borrowed creation error text; invalidated by the next create attempt. */
const char *nb_host_sdl_creation_error(void);

#endif
