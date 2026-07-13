#include "desktop_renderer.h"

#include <stddef.h>

#include "window_renderer.h"

bool nb_desktop_render(SDL_Renderer *renderer,
                       const struct nb_desktop *desktop)
{
    size_t index;

    for (index = 0; index < nb_desktop_window_count(desktop); ++index) {
        const struct nb_window *window = nb_desktop_window_at(desktop, index);

        if (window == NULL || !nb_window_render(renderer, window)) {
            return false;
        }
    }

    return true;
}
