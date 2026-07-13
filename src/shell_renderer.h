#ifndef NIXBENCH_SHELL_RENDERER_H
#define NIXBENCH_SHELL_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "shell.h"

bool nb_shell_render(SDL_Renderer *renderer,
                     const struct nb_shell *shell,
                     struct nb_rect viewport,
                     const char *clock_text);

#endif
