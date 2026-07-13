#include "shell_renderer.h"

#include "desktop_renderer.h"
#include "menu_renderer.h"

bool nb_shell_render(SDL_Renderer *renderer,
                     const struct nb_shell *shell,
                     struct nb_rect viewport,
                     const char *clock_text)
{
    return nb_desktop_render(renderer, &shell->desktop) &&
           nb_menu_render(renderer, &shell->menu, viewport, clock_text);
}
