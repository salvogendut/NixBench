#include <stdbool.h>
#include <stdio.h>

#include <SDL3/SDL.h>

#include "desktop_renderer.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

struct callback_state {
    nb_window_id ids[NB_DESKTOP_MAX_WINDOWS];
    struct nb_rect contents[NB_DESKTOP_MAX_WINDOWS];
    SDL_Rect clips[NB_DESKTOP_MAX_WINDOWS];
    size_t call_count;
    size_t fail_on_call;
};

static bool rects_equal(SDL_Rect left, SDL_Rect right)
{
    return left.x == right.x && left.y == right.y &&
           left.w == right.w && left.h == right.h;
}

static SDL_Rect sdl_rect(struct nb_rect rect)
{
    const SDL_Rect converted = {
        rect.x,
        rect.y,
        rect.width,
        rect.height
    };

    return converted;
}

static bool record_content(SDL_Renderer *renderer,
                           nb_window_id id,
                           const struct nb_window *window,
                           struct nb_rect content,
                           void *context)
{
    struct callback_state *state = context;
    SDL_Rect clip;
    const size_t call = state->call_count;

    CHECK(window != NULL);
    CHECK(call < NB_DESKTOP_MAX_WINDOWS);
    CHECK(SDL_RenderClipEnabled(renderer));
    CHECK(SDL_GetRenderClipRect(renderer, &clip));
    if (call < NB_DESKTOP_MAX_WINDOWS) {
        state->ids[call] = id;
        state->contents[call] = content;
        state->clips[call] = clip;
    }
    ++state->call_count;
    return state->fail_on_call == 0 ||
           state->call_count != state->fail_on_call;
}

static void reset_callback(struct callback_state *state,
                           size_t fail_on_call)
{
    size_t index;

    state->call_count = 0;
    state->fail_on_call = fail_on_call;
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        state->ids[index] = NB_WINDOW_ID_NONE;
        state->contents[index] = (struct nb_rect){0, 0, 0, 0};
        state->clips[index] = (SDL_Rect){0, 0, 0, 0};
    }
}

static void test_content_callback_and_clip_restoration(void)
{
    SDL_Surface *surface =
        SDL_CreateSurface(400, 300, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *renderer = NULL;
    struct nb_desktop desktop;
    struct callback_state state;
    const SDL_Rect prior_clip = {0, 0, 250, 200};
    SDL_Rect restored;
    nb_window_id first;
    nb_window_id second;
    const struct nb_window *first_window;
    const struct nb_window *second_window;
    SDL_Rect first_content;
    SDL_Rect second_content;
    SDL_Rect expected_first_clip;
    SDL_Rect expected_second_clip;

    CHECK(surface != NULL);
    if (surface == NULL) {
        return;
    }
    renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer != NULL);
    if (renderer == NULL) {
        SDL_DestroySurface(surface);
        return;
    }

    nb_desktop_init(&desktop);
    first = nb_desktop_open_window(&desktop,
                                   "First",
                                   (struct nb_rect){10, 20, 200, 140});
    second = nb_desktop_open_window(&desktop,
                                    "Second",
                                    (struct nb_rect){80, 70, 220, 160});
    first_window = nb_desktop_find_window(&desktop, first);
    second_window = nb_desktop_find_window(&desktop, second);
    CHECK(first_window != NULL);
    CHECK(second_window != NULL);
    CHECK(SDL_SetRenderClipRect(renderer, &prior_clip));

    reset_callback(&state, 0);
    CHECK(nb_desktop_render_with_content(renderer,
                                         &desktop,
                                         record_content,
                                         &state));
    CHECK(state.call_count == 2);
    CHECK(state.ids[0] == first);
    CHECK(state.ids[1] == second);
    CHECK(first_window != NULL &&
          state.contents[0].x == nb_window_content_rect(first_window).x &&
          state.contents[0].y == nb_window_content_rect(first_window).y &&
          state.contents[0].width ==
              nb_window_content_rect(first_window).width &&
          state.contents[0].height ==
              nb_window_content_rect(first_window).height);
    CHECK(second_window != NULL &&
          state.contents[1].x == nb_window_content_rect(second_window).x &&
          state.contents[1].y == nb_window_content_rect(second_window).y &&
          state.contents[1].width ==
              nb_window_content_rect(second_window).width &&
          state.contents[1].height ==
              nb_window_content_rect(second_window).height);
    first_content = sdl_rect(state.contents[0]);
    second_content = sdl_rect(state.contents[1]);
    CHECK(SDL_GetRectIntersection(&prior_clip,
                                  &first_content,
                                  &expected_first_clip));
    CHECK(SDL_GetRectIntersection(&prior_clip,
                                  &second_content,
                                  &expected_second_clip));
    CHECK(rects_equal(state.clips[0], expected_first_clip));
    CHECK(rects_equal(state.clips[1], expected_second_clip));
    CHECK(SDL_RenderClipEnabled(renderer));
    CHECK(SDL_GetRenderClipRect(renderer, &restored));
    CHECK(rects_equal(restored, prior_clip));

    reset_callback(&state, 2);
    CHECK(!nb_desktop_render_with_content(renderer,
                                          &desktop,
                                          record_content,
                                          &state));
    CHECK(state.call_count == 2);
    CHECK(SDL_RenderClipEnabled(renderer));
    CHECK(SDL_GetRenderClipRect(renderer, &restored));
    CHECK(rects_equal(restored, prior_clip));

    CHECK(SDL_SetRenderClipRect(renderer, NULL));
    reset_callback(&state, 0);
    CHECK(nb_desktop_render_with_content(renderer,
                                         &desktop,
                                         record_content,
                                         &state));
    CHECK(state.call_count == 2);
    CHECK(rects_equal(state.clips[0], sdl_rect(state.contents[0])));
    CHECK(rects_equal(state.clips[1], sdl_rect(state.contents[1])));
    CHECK(!SDL_RenderClipEnabled(renderer));
    CHECK(nb_desktop_render_with_content(renderer,
                                         &desktop,
                                         NULL,
                                         NULL));
    CHECK(nb_desktop_render(renderer, &desktop));

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

int main(void)
{
    test_content_callback_and_clip_restoration();

    if (failures != 0) {
        fprintf(stderr, "%d renderer check(s) failed\n", failures);
        return 1;
    }

    puts("renderer callback checks passed");
    return 0;
}
