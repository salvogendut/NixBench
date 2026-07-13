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

static bool render_title(SDL_Renderer *renderer,
                         const struct nb_window *window)
{
    const struct nb_rect title = nb_window_title_rect(window);
    const struct nb_rect close = nb_window_close_rect(window);
    const char *text = window->title != NULL ? window->title : "";
    const float text_x = (float)(close.x + close.width + 8);
    const float text_y = (float)(title.y + ((title.height - 8) / 2));
    char clipped_text[64];
    size_t maximum_characters;
    size_t index;

    if (!fill_rect(renderer,
                   title,
                   window->active ? active_title_color : inactive_title_color) ||
        !render_close_gadget(renderer, window) ||
        !set_color(renderer, title_text_color)) {
        return false;
    }

    if (text[0] == '\0' || text_x + 8.0f >= (float)(title.x + title.width)) {
        return true;
    }

    maximum_characters =
        (size_t)(((float)(title.x + title.width) - text_x - 4.0f) / 8.0f);
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

static bool render_content(SDL_Renderer *renderer,
                           const struct nb_window *window)
{
    const struct nb_rect content = nb_window_content_rect(window);

    if (!fill_rect(renderer, content, content_color) ||
        !set_color(renderer, text_color)) {
        return false;
    }

    return SDL_RenderDebugText(renderer,
                               (float)(content.x + 18),
                               (float)(content.y + 24),
                               "Welcome to NixBench") &&
           SDL_RenderDebugText(renderer,
                               (float)(content.x + 18),
                               (float)(content.y + 44),
                               "Drag this window by its title bar.");
}

bool nb_window_render(SDL_Renderer *renderer, const struct nb_window *window)
{
    if (!window->visible) {
        return true;
    }

    return fill_rect(renderer, window->frame, frame_color) &&
           render_bevel(renderer, window->frame, false) &&
           render_title(renderer, window) &&
           render_content(renderer, window);
}
