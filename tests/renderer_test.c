#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <png.h>

#include "desktop_renderer.h"
#include "backdrop_renderer.h"
#include "menu_renderer.h"
#include "settings_ui.h"

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

struct callback_pair {
    struct callback_state content;
    nb_window_id decoration_ids[NB_DESKTOP_MAX_WINDOWS];
    SDL_Rect decoration_clips[NB_DESKTOP_MAX_WINDOWS];
    size_t decoration_count;
    nb_window_id overlay_ids[NB_DESKTOP_MAX_WINDOWS];
    SDL_Rect overlay_clips[NB_DESKTOP_MAX_WINDOWS];
    size_t overlay_count;
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

static bool pixel_equals(SDL_Surface *surface,
                         int x,
                         int y,
                         Uint8 expected_red,
                         Uint8 expected_green,
                         Uint8 expected_blue)
{
    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    Uint8 alpha = 0;

    return SDL_ReadSurfacePixel(surface,
                                x,
                                y,
                                &red,
                                &green,
                                &blue,
                                &alpha) &&
           red == expected_red && green == expected_green &&
           blue == expected_blue && alpha == SDL_ALPHA_OPAQUE;
}

static bool pixel_near(SDL_Surface *surface,
                       int x,
                       int y,
                       Uint8 expected_red,
                       Uint8 expected_green,
                       Uint8 expected_blue,
                       Uint8 tolerance)
{
    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    Uint8 alpha = 0;

    return SDL_ReadSurfacePixel(surface,
                                x,
                                y,
                                &red,
                                &green,
                                &blue,
                                &alpha) &&
           red >= expected_red - SDL_min(expected_red, tolerance) &&
           red <= expected_red + SDL_min((Uint8)(255 - expected_red),
                                         tolerance) &&
           green >= expected_green - SDL_min(expected_green, tolerance) &&
           green <= expected_green + SDL_min((Uint8)(255 - expected_green),
                                             tolerance) &&
           blue >= expected_blue - SDL_min(expected_blue, tolerance) &&
           blue <= expected_blue + SDL_min((Uint8)(255 - expected_blue),
                                           tolerance) &&
           alpha == SDL_ALPHA_OPAQUE;
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

static bool record_decoration(SDL_Renderer *renderer,
                              nb_window_id id,
                              const struct nb_window *window,
                              void *context)
{
    struct callback_pair *pair = context;
    SDL_Rect clip;
    const size_t call = pair->decoration_count;

    CHECK(window != NULL);
    CHECK(call < NB_DESKTOP_MAX_WINDOWS);
    CHECK(SDL_RenderClipEnabled(renderer));
    CHECK(SDL_GetRenderClipRect(renderer, &clip));
    if (call < NB_DESKTOP_MAX_WINDOWS) {
        pair->decoration_ids[call] = id;
        pair->decoration_clips[call] = clip;
    }
    ++pair->decoration_count;
    return true;
}

static bool record_overlay(SDL_Renderer *renderer,
                           nb_window_id id,
                           const struct nb_window *window,
                           void *context)
{
    struct callback_pair *pair = context;
    SDL_Rect clip;
    const size_t call = pair->overlay_count;

    CHECK(window != NULL);
    CHECK(call < NB_DESKTOP_MAX_WINDOWS);
    CHECK(SDL_RenderClipEnabled(renderer));
    CHECK(SDL_GetRenderClipRect(renderer, &clip));
    if (call < NB_DESKTOP_MAX_WINDOWS) {
        pair->overlay_ids[call] = id;
        pair->overlay_clips[call] = clip;
    }
    ++pair->overlay_count;
    return true;
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

    {
        struct callback_pair pair = {0};

        reset_callback(&pair.content, 0);
        CHECK(nb_desktop_render_with_callbacks(renderer,
                                                &desktop,
                                                record_decoration,
                                                record_content,
                                                &pair));
        CHECK(pair.decoration_count == 2);
        CHECK(pair.content.call_count == 2);
        CHECK(pair.decoration_ids[0] == first);
        CHECK(pair.decoration_ids[1] == second);
        CHECK(rects_equal(pair.decoration_clips[0], prior_clip));
        CHECK(rects_equal(pair.decoration_clips[1], prior_clip));
        CHECK(rects_equal(pair.content.clips[0], expected_first_clip));
        CHECK(rects_equal(pair.content.clips[1], expected_second_clip));
    }

    {
        struct callback_pair pair = {0};

        reset_callback(&pair.content, 0);
        CHECK(nb_desktop_render_with_layer_callbacks(renderer,
                                                      &desktop,
                                                      NULL,
                                                      record_decoration,
                                                      record_content,
                                                      record_overlay,
                                                      &pair));
        CHECK(pair.decoration_count == 2);
        CHECK(pair.content.call_count == 2);
        CHECK(pair.overlay_count == 2);
        CHECK(pair.overlay_ids[0] == first);
        CHECK(pair.overlay_ids[1] == second);
        CHECK(rects_equal(pair.overlay_clips[0], prior_clip));
        CHECK(rects_equal(pair.overlay_clips[1], prior_clip));
        CHECK(SDL_RenderClipEnabled(renderer));
        CHECK(SDL_GetRenderClipRect(renderer, &restored));
        CHECK(rects_equal(restored, prior_clip));
    }

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

    CHECK(nb_desktop_toggle_window_minimized(&desktop, first));
    reset_callback(&state, 0);
    CHECK(nb_desktop_render_with_content(renderer,
                                         &desktop,
                                         record_content,
                                         &state));
    CHECK(state.call_count == 1);
    CHECK(state.ids[0] == second);
    CHECK(nb_desktop_toggle_window_minimized(&desktop, first));

    CHECK(nb_desktop_render_with_content(renderer,
                                         &desktop,
                                         NULL,
                                         NULL));
    CHECK(nb_desktop_render(renderer, &desktop));

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

static void test_checked_menu_item_gutter(void)
{
    const struct nb_rect viewport = {0, 0, 220, 100};
    struct nb_menu_item_spec items[] = {
        {"Show seconds", 1, NB_MENU_ITEM_COMMAND, true, false}
    };
    const struct nb_menu_spec menus[] = {
        {"Settings", items, 1}
    };
    const struct nb_menu_model model = {menus, 1};
    struct nb_menu menu;
    struct nb_rect label;
    struct nb_rect item;
    SDL_Surface *surface = SDL_CreateSurface(220,
                                             100,
                                             SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *renderer = NULL;
    int mark_x;
    int mark_y;

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

    nb_menu_init(&menu);
    nb_menu_set_model(&menu, &model);
    label = nb_menu_label_rect(&menu, viewport, 0);
    CHECK(nb_menu_pointer_down(&menu, label.x + 1, label.y + 1, viewport));
    CHECK(nb_menu_pointer_up(&menu, label.x + 1, label.y + 1, viewport) ==
          NB_MENU_COMMAND_NONE);
    CHECK(nb_menu_is_open(&menu));
    item = nb_menu_item_rect(&menu, viewport, 0);
    mark_x = item.x + 4;
    mark_y = item.y + (item.height / 2);

    CHECK(nb_menu_render(renderer, &menu, viewport, ""));
    CHECK(SDL_RenderPresent(renderer));
    CHECK(pixel_equals(surface, mark_x, mark_y, 213, 219, 211));

    items[0].checked = true;
    CHECK(nb_menu_render(renderer, &menu, viewport, ""));
    CHECK(SDL_RenderPresent(renderer));
    CHECK(pixel_equals(surface, mark_x, mark_y, 18, 28, 33));

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

static void test_settings_panel_is_opaque(void)
{
    const struct nb_rect content = {10, 10, 640, 574};
    SDL_Surface *surface = SDL_CreateSurface(660,
                                             594,
                                             SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *renderer = NULL;
    struct nb_user_preferences preferences;

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

    nb_user_preferences_init(&preferences);
    CHECK(SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_TRANSPARENT));
    CHECK(SDL_RenderClear(renderer));
    CHECK(nb_settings_render(renderer,
                             content,
                             &preferences,
                             NB_SETTINGS_COLOR_PRIMARY));
    CHECK(SDL_RenderPresent(renderer));
    CHECK(pixel_equals(surface,
                       content.x + 1,
                       content.y + 1,
                       225,
                       230,
                       222));
    CHECK(pixel_equals(surface,
                       content.x + content.width - 2,
                       content.y + content.height - 2,
                       225,
                       230,
                       222));

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

static void test_backdrop_gradients(void)
{
    SDL_Surface *surface =
        SDL_CreateSurface(5, 5, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer *renderer = NULL;
    struct nb_user_preferences preferences;
    const struct nb_rect viewport = {0, 0, 5, 5};

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
    nb_user_preferences_init(&preferences);
    preferences.backdrop_primary = (struct nb_color){10, 20, 30};
    preferences.backdrop_secondary = (struct nb_color){110, 120, 130};

    CHECK(nb_backdrop_render(renderer, viewport, &preferences));
    CHECK(SDL_RenderPresent(renderer));
    CHECK(pixel_equals(surface, 0, 0, 10, 20, 30));
    CHECK(pixel_equals(surface, 4, 4, 10, 20, 30));

    preferences.backdrop_gradient_enabled = true;
    preferences.backdrop_gradient_direction =
        NB_BACKDROP_GRADIENT_VERTICAL;
    CHECK(nb_backdrop_render(renderer, viewport, &preferences));
    CHECK(SDL_RenderPresent(renderer));
    CHECK(pixel_equals(surface, 2, 0, 10, 20, 30));
    CHECK(pixel_equals(surface, 2, 4, 110, 120, 130));

    preferences.backdrop_gradient_direction =
        NB_BACKDROP_GRADIENT_HORIZONTAL;
    CHECK(nb_backdrop_render(renderer, viewport, &preferences));
    CHECK(SDL_RenderPresent(renderer));
    CHECK(pixel_equals(surface, 0, 2, 10, 20, 30));
    CHECK(pixel_equals(surface, 4, 2, 110, 120, 130));

    preferences.backdrop_gradient_direction =
        NB_BACKDROP_GRADIENT_DIAGONAL;
    CHECK(nb_backdrop_render(renderer, viewport, &preferences));
    CHECK(SDL_RenderPresent(renderer));
    CHECK(pixel_near(surface, 0, 0, 10, 20, 30, 15));
    CHECK(pixel_near(surface, 4, 4, 110, 120, 130, 15));

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

static void test_console_sized_diagonal_gradient(void)
{
    const int width = 1366;
    const int height = 768;
    SDL_Surface *surface =
        SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer *renderer = NULL;
    struct nb_backdrop_cache *cache = NULL;
    struct nb_user_preferences preferences;
    const struct nb_rect viewport = {0, 0, width, height};
    int frame;

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
    cache = nb_backdrop_cache_create();
    CHECK(cache != NULL);
    if (cache == NULL) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        return;
    }
    nb_user_preferences_init(&preferences);
    preferences.backdrop_primary = (struct nb_color){24, 54, 76};
    preferences.backdrop_secondary = (struct nb_color){142, 47, 39};
    preferences.backdrop_gradient_enabled = true;
    preferences.backdrop_gradient_direction =
        NB_BACKDROP_GRADIENT_DIAGONAL;

    for (frame = 0; frame < 32; ++frame) {
        CHECK(nb_backdrop_cache_render(cache,
                                       renderer,
                                       viewport,
                                       &preferences));
        CHECK(SDL_RenderPresent(renderer));
    }
    CHECK(pixel_near(surface, 0, 0, 24, 54, 76, 1));
    CHECK(pixel_near(surface,
                     width - 1,
                     height - 1,
                     142,
                     47,
                     39,
                     1));

    preferences.backdrop_secondary = (struct nb_color){40, 80, 120};
    CHECK(nb_backdrop_cache_render(cache,
                                   renderer,
                                   viewport,
                                   &preferences));
    CHECK(SDL_RenderPresent(renderer));
    CHECK(pixel_near(surface,
                     width - 1,
                     height - 1,
                     40,
                     80,
                     120,
                     1));

    nb_backdrop_cache_invalidate(cache);
    CHECK(nb_backdrop_cache_render(cache,
                                   renderer,
                                   viewport,
                                   &preferences));
    CHECK(SDL_RenderPresent(renderer));

    nb_backdrop_cache_destroy(cache);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

static void test_wallpaper_modes(void)
{
    char directory[] = "/tmp/nixbench-renderer-wallpaper-XXXXXX";
    char path[512];
    const unsigned char pixels[] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255
    };
    png_image output;
    SDL_Surface *surface = NULL;
    SDL_Renderer *renderer = NULL;
    struct nb_backdrop_cache *cache = NULL;
    struct nb_user_preferences preferences;
    const struct nb_rect viewport = {0, 0, 8, 6};
    enum nb_wallpaper_mode mode;

    CHECK(mkdtemp(directory) != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/wallpaper.png", directory) > 0);
    (void)memset(&output, 0, sizeof(output));
    output.version = PNG_IMAGE_VERSION;
    output.width = 2;
    output.height = 2;
    output.format = PNG_FORMAT_RGBA;
    CHECK(png_image_write_to_file(&output, path, false, pixels, 0, NULL));
    surface = SDL_CreateSurface(viewport.width,
                                viewport.height,
                                SDL_PIXELFORMAT_XRGB8888);
    CHECK(surface != NULL);
    if (surface != NULL) {
        renderer = SDL_CreateSoftwareRenderer(surface);
    }
    CHECK(renderer != NULL);
    cache = nb_backdrop_cache_create();
    CHECK(cache != NULL);
    nb_user_preferences_init(&preferences);
    preferences.backdrop_primary = (struct nb_color){8, 12, 16};
    (void)snprintf(preferences.wallpaper,
                   sizeof(preferences.wallpaper),
                   "%s",
                   path);
    if (renderer != NULL && cache != NULL) {
        for (mode = NB_WALLPAPER_CENTER;
             mode <= NB_WALLPAPER_FILL;
             mode = (enum nb_wallpaper_mode)(mode + 1)) {
            preferences.wallpaper_mode = mode;
            CHECK(nb_backdrop_cache_render(cache,
                                           renderer,
                                           viewport,
                                           &preferences));
            CHECK(SDL_RenderPresent(renderer));
        }
        preferences.wallpaper_mode = NB_WALLPAPER_TILE;
        CHECK(nb_backdrop_cache_render(cache,
                                       renderer,
                                       viewport,
                                       &preferences));
        CHECK(SDL_RenderPresent(renderer));
        CHECK(pixel_near(surface, 0, 0, 255, 0, 0, 2));
        CHECK(pixel_near(surface, 2, 0, 255, 0, 0, 2));
    }
    nb_backdrop_cache_destroy(cache);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
}

int main(void)
{
    test_content_callback_and_clip_restoration();
    test_checked_menu_item_gutter();
    test_settings_panel_is_opaque();
    test_backdrop_gradients();
    test_console_sized_diagonal_gradient();
    test_wallpaper_modes();

    if (failures != 0) {
        fprintf(stderr, "%d renderer check(s) failed\n", failures);
        return 1;
    }

    puts("renderer callback checks passed");
    return 0;
}
