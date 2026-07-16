#include "window_renderer.h"

#include <stddef.h>

struct color {
    Uint8 red;
    Uint8 green;
    Uint8 blue;
};

static const struct color frame_color = {172, 184, 184};
static const struct color frame_highlight = {239, 246, 240};
static const struct color frame_shadow = {49, 61, 68};
static const struct color active_title_color = {43, 113, 137};
static const struct color inactive_title_color = {96, 116, 123};
static const struct color content_color = {213, 219, 211};
static const struct color text_color = {18, 28, 33};
static const struct color title_text_color = {245, 247, 238};

static bool set_color(SDL_Renderer *renderer, struct color color)
{
    return SDL_SetRenderDrawColor(renderer,
                                  color.red,
                                  color.green,
                                  color.blue,
                                  SDL_ALPHA_OPAQUE);
}

static bool fill_rect(SDL_Renderer *renderer,
                      struct nb_rect rect,
                      struct color color)
{
    const SDL_FRect destination = {
        (float)rect.x,
        (float)rect.y,
        (float)rect.width,
        (float)rect.height
    };

    if (rect.width <= 0 || rect.height <= 0) {
        return true;
    }

    return set_color(renderer, color) &&
           SDL_RenderFillRect(renderer, &destination);
}

static bool render_bevel(SDL_Renderer *renderer,
                         struct nb_rect rect,
                         bool pressed)
{
    const struct color top_left = pressed ? frame_shadow : frame_highlight;
    const struct color bottom_right = pressed ? frame_highlight : frame_shadow;
    const float left = (float)rect.x;
    const float top = (float)rect.y;
    const float right = (float)(rect.x + rect.width - 1);
    const float bottom = (float)(rect.y + rect.height - 1);

    if (rect.width <= 0 || rect.height <= 0) {
        return true;
    }

    return set_color(renderer, top_left) &&
           SDL_RenderLine(renderer, left, bottom, left, top) &&
           SDL_RenderLine(renderer, left, top, right, top) &&
           set_color(renderer, bottom_right) &&
           SDL_RenderLine(renderer, right, top, right, bottom) &&
           SDL_RenderLine(renderer, right, bottom, left, bottom);
}

static bool render_close_gadget(SDL_Renderer *renderer,
                                const struct nb_window *window)
{
    const struct nb_rect close = nb_window_close_rect(window);
    const float inset = window->close_pressed ? 1.0f : 0.0f;

    if (!fill_rect(renderer, close, frame_color) ||
        !render_bevel(renderer, close, window->close_pressed)) {
        return false;
    }

    if (close.width < 8 || close.height < 8) {
        return true;
    }

    if (!set_color(renderer, text_color)) {
        return false;
    }

    return SDL_RenderLine(renderer,
                          (float)(close.x + 4) + inset,
                          (float)(close.y + 4) + inset,
                          (float)(close.x + close.width - 5) + inset,
                          (float)(close.y + close.height - 5) + inset) &&
           SDL_RenderLine(renderer,
                          (float)(close.x + close.width - 5) + inset,
                          (float)(close.y + 4) + inset,
                          (float)(close.x + 4) + inset,
                          (float)(close.y + close.height - 5) + inset);
}

static bool render_maximize_gadget(SDL_Renderer *renderer,
                                   const struct nb_window *window)
{
    const struct nb_rect maximize = nb_window_maximize_rect(window);
    const bool pressed =
        window->pointer_mode == NB_WINDOW_POINTER_MAXIMIZE;
    const float inset = pressed ? 1.0f : 0.0f;
    const float left = (float)maximize.x + inset;
    const float top = (float)maximize.y + inset;
    const float right = (float)(maximize.x + maximize.width - 1) + inset;
    const float bottom = (float)(maximize.y + maximize.height - 1) + inset;

    if (!fill_rect(renderer, maximize, frame_color) ||
        !render_bevel(renderer, maximize, pressed)) {
        return false;
    }

    if (maximize.width < 8 || maximize.height < 8) {
        return true;
    }

    if (!set_color(renderer, text_color)) {
        return false;
    }

    if (window->maximized) {
        return SDL_RenderLine(renderer, left + 2.0f, top + 2.0f, right - 2.0f,
                              top + 2.0f) &&
               SDL_RenderLine(renderer, left + 2.0f, top + 2.0f, left + 2.0f,
                              bottom - 2.0f) &&
               SDL_RenderLine(renderer, right - 2.0f, top + 2.0f,
                              right - 2.0f, bottom - 2.0f) &&
               SDL_RenderLine(renderer, left + 2.0f, bottom - 2.0f,
                              right - 2.0f, bottom - 2.0f);
    }

    return SDL_RenderLine(renderer,
                          left + 3.0f,
                          top + 3.0f,
                          right - 3.0f,
                          top + 3.0f) &&
           SDL_RenderLine(renderer,
                          left + 3.0f,
                          top + 3.0f,
                          left + 3.0f,
                          bottom - 3.0f) &&
           SDL_RenderLine(renderer,
                          right - 3.0f,
                          top + 3.0f,
                          right - 3.0f,
                          bottom - 3.0f) &&
           SDL_RenderLine(renderer,
                          left + 3.0f,
                          bottom - 3.0f,
                          right - 3.0f,
                          bottom - 3.0f);
}

static bool render_title(SDL_Renderer *renderer,
                         const struct nb_window *window)
{
    const struct nb_rect title = nb_window_title_rect(window);
    const struct nb_rect close = nb_window_close_rect(window);
    const struct nb_rect maximize = nb_window_maximize_rect(window);
    const char *text = window->title;
    const float text_x = (float)(close.x + close.width + 8);
    const float text_y = (float)(title.y + ((title.height - 8) / 2));
    char clipped_text[64];
    size_t maximum_characters;
    size_t index;

    if (!fill_rect(renderer,
                   title,
                   window->active ? active_title_color : inactive_title_color) ||
        !render_close_gadget(renderer, window) ||
        !render_maximize_gadget(renderer, window) ||
        !set_color(renderer, title_text_color)) {
        return false;
    }

    if (text[0] == '\0' ||
        text_x + 8.0f >= (float)(maximize.x - NB_WINDOW_GADGET_MARGIN)) {
        return true;
    }

    maximum_characters =
        (size_t)(((float)maximize.x - text_x - 4.0f) / 8.0f);
    if (maximum_characters >= sizeof(clipped_text)) {
        maximum_characters = sizeof(clipped_text) - 1;
    }

    for (index = 0; index < maximum_characters && text[index] != '\0';
         ++index) {
        clipped_text[index] = text[index];
    }
    clipped_text[index] = '\0';

    return clipped_text[0] == '\0' ||
           SDL_RenderDebugText(renderer, text_x, text_y, clipped_text);
}

static bool render_content_background(SDL_Renderer *renderer,
                                      const struct nb_window *window)
{
    return fill_rect(renderer,
                     nb_window_content_rect(window),
                     content_color);
}

bool nb_window_render_default_content(SDL_Renderer *renderer,
                                      const struct nb_window *window)
{
    const struct nb_rect content = nb_window_content_rect(window);
    static const char first_line[] = "This window is managed by NixBench.";
    static const char second_line[] =
        "Drag the title; resize at bottom right.";
    const char *lines[] = {first_line, second_line};
    const int line_y[] = {content.y + 24, content.y + 44};
    size_t line_index;

    if (!set_color(renderer, text_color)) {
        return false;
    }

    for (line_index = 0; line_index < 2; ++line_index) {
        const int text_x = content.x + 18;
        const int available_width = content.x + content.width - text_x - 4;
        char clipped_text[64];
        size_t maximum_characters;
        size_t text_index;

        if (available_width < 8 || line_y[line_index] < content.y ||
            line_y[line_index] + 8 > content.y + content.height) {
            continue;
        }

        maximum_characters = (size_t)(available_width / 8);
        if (maximum_characters >= sizeof(clipped_text)) {
            maximum_characters = sizeof(clipped_text) - 1;
        }
        for (text_index = 0;
             text_index < maximum_characters &&
             lines[line_index][text_index] != '\0';
             ++text_index) {
            clipped_text[text_index] = lines[line_index][text_index];
        }
        clipped_text[text_index] = '\0';

        if (clipped_text[0] != '\0' &&
            !SDL_RenderDebugText(renderer,
                                 (float)text_x,
                                 (float)line_y[line_index],
                                 clipped_text)) {
            return false;
        }
    }

    return true;
}

static bool render_resize_gadget(SDL_Renderer *renderer,
                                 const struct nb_window *window)
{
    const struct nb_rect resize = nb_window_resize_rect(window);
    const bool pressed =
        window->pointer_mode == NB_WINDOW_POINTER_RESIZE;
    const float inset = pressed ? 1.0f : 0.0f;

    if (window->maximized) {
        return true;
    }

    if (!fill_rect(renderer, resize, frame_color) ||
        !render_bevel(renderer, resize, pressed)) {
        return false;
    }

    if (resize.width < 17 || resize.height < 17) {
        return true;
    }

    if (!set_color(renderer, frame_shadow)) {
        return false;
    }

    return SDL_RenderLine(renderer,
                          (float)(resize.x + 5) + inset,
                          (float)(resize.y + resize.height - 4) + inset,
                          (float)(resize.x + resize.width - 4) + inset,
                          (float)(resize.y + 5) + inset) &&
           SDL_RenderLine(renderer,
                          (float)(resize.x + 9) + inset,
                          (float)(resize.y + resize.height - 4) + inset,
                          (float)(resize.x + resize.width - 4) + inset,
                          (float)(resize.y + 9) + inset) &&
           SDL_RenderLine(renderer,
                          (float)(resize.x + 13) + inset,
                          (float)(resize.y + resize.height - 4) + inset,
                          (float)(resize.x + resize.width - 4) + inset,
                          (float)(resize.y + 13) + inset);
}

static bool render_footer(SDL_Renderer *renderer,
                          const struct nb_window *window)
{
    const struct nb_rect footer = nb_window_footer_rect(window);

    return fill_rect(renderer, footer, frame_color) &&
           render_bevel(renderer, footer, false);
}

bool nb_window_render_base(SDL_Renderer *renderer,
                           const struct nb_window *window)
{
    if (!window->visible) {
        return true;
    }

    return fill_rect(renderer, window->frame, frame_color) &&
           render_bevel(renderer, window->frame, false) &&
           render_title(renderer, window) &&
           render_content_background(renderer, window) &&
           render_footer(renderer, window) &&
           render_resize_gadget(renderer, window);
}

bool nb_window_render(SDL_Renderer *renderer, const struct nb_window *window)
{
    if (!window->visible) {
        return true;
    }

    return nb_window_render_base(renderer, window) &&
           nb_window_render_default_content(renderer, window);
}
