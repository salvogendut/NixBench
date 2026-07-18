#include "shell_renderer.h"

#include "desktop_renderer.h"
#include "menu_renderer.h"

static bool render_minimized_windows(SDL_Renderer *renderer,
                                     const struct nb_shell *shell,
                                     struct nb_rect viewport)
{
    size_t index;

    for (index = 0;
         index < nb_desktop_window_count(&shell->desktop);
         ++index) {
        const nb_window_id id =
            nb_desktop_window_id_at(&shell->desktop, index);
        const struct nb_window *window =
            nb_desktop_window_at(&shell->desktop, index);

        if (window != NULL && window->visible && window->minimized &&
            !nb_menu_render_window_button(
                renderer,
                nb_shell_minimized_window_rect(shell, viewport, id),
                window->title,
                shell->pointer_owner ==
                        NB_SHELL_POINTER_MINIMIZED_WINDOW &&
                    shell->minimized_pointer_window == id &&
                    shell->minimized_pointer_pressed)) {
            return false;
        }
    }
    return true;
}

bool nb_shell_render_with_callbacks(
    SDL_Renderer *renderer,
    const struct nb_shell *shell,
    struct nb_rect viewport,
    const char *clock_text,
    nb_window_decoration_render_callback render_decoration,
    nb_window_content_render_callback render_content,
    void *context)
{
    const struct nb_window *active = nb_desktop_find_window(
        &shell->desktop,
        nb_desktop_active_window_id(&shell->desktop));

    if (!nb_desktop_render_with_callbacks(renderer,
                                          &shell->desktop,
                                          render_decoration,
                                          render_content,
                                          context)) {
        return false;
    }
    if (active != NULL && active->fullscreen) {
        return true;
    }
    return nb_menu_render(renderer, &shell->menu, viewport, clock_text) &&
           render_minimized_windows(renderer, shell, viewport);
}

bool nb_shell_render_with_content(
    SDL_Renderer *renderer,
    const struct nb_shell *shell,
    struct nb_rect viewport,
    const char *clock_text,
    nb_window_content_render_callback render_content,
    void *context)
{
    return nb_shell_render_with_callbacks(renderer,
                                           shell,
                                           viewport,
                                           clock_text,
                                           NULL,
                                           render_content,
                                           context);
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
