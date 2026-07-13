#include "shell_renderer.h"

#include "desktop_renderer.h"
#include "menu_renderer.h"

bool nb_shell_render_with_content(
    SDL_Renderer *renderer,
    const struct nb_shell *shell,
    struct nb_rect viewport,
    const char *clock_text,
    nb_window_content_render_callback render_content,
    void *context)
{
    return nb_desktop_render_with_content(renderer,
                                          &shell->desktop,
                                          render_content,
                                          context) &&
           nb_menu_render(renderer, &shell->menu, viewport, clock_text);
}

bool nb_shell_render(SDL_Renderer *renderer,
                     const struct nb_shell *shell,
                     struct nb_rect viewport,
                     const char *clock_text)
{
    return nb_shell_render_with_content(renderer,
                                        shell,
                                        viewport,
                                        clock_text,
                                        NULL,
                                        NULL);
}
