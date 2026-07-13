#ifndef NIXBENCH_MENU_RENDERER_H
#define NIXBENCH_MENU_RENDERER_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "menu.h"

bool nb_menu_render(SDL_Renderer *renderer,
                    const struct nb_menu *menu,
                    struct nb_rect viewport,
                    const char *clock_text);

#endif
