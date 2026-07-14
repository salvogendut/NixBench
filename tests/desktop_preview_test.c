#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "desktop_preview.h"
#include "menu.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static uint64_t frame_hash(const struct nb_host_frame *frame)
{
    const unsigned char *bytes = frame->pixels;
    const size_t size = frame->stride * (size_t)frame->height;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static size_t sampled_color_count(const struct nb_host_frame *frame)
{
    uint32_t colors[32];
    size_t count = 0;
    int y;

    for (y = 0; y < frame->height; y += 4) {
        const uint32_t *row = (const uint32_t *)
            ((const unsigned char *)frame->pixels +
             (size_t)y * frame->stride);
        int x;

        for (x = 0; x < frame->width; x += 4) {
            size_t index;

            for (index = 0; index < count; ++index) {
                if (colors[index] == row[x]) {
                    break;
                }
            }
            if (index == count && count < sizeof(colors) / sizeof(colors[0])) {
                colors[count++] = row[x];
            }
        }
    }
    return count;
}

static struct nb_host_event pointer_motion(int x, int y)
{
    struct nb_host_event event;

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_MOTION;
    event.data.pointer_motion.x = x;
    event.data.pointer_motion.y = y;
    return event;
}

static struct nb_host_event left_button(int x, int y, bool pressed)
{
    struct nb_host_event event;

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_BUTTON;
    event.data.pointer_button.x = x;
    event.data.pointer_button.y = y;
    event.data.pointer_button.button = NB_HOST_POINTER_BUTTON_LEFT;
    event.data.pointer_button.pressed = pressed;
    return event;
}

static void test_preview_frame(void)
{
    struct nb_desktop_preview *preview;
    struct nb_host_frame first;
    struct nb_host_frame second;
    struct nb_host_output output = {1024, 640, 1024, 640, 60000};
    uint64_t first_hash;

    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
    preview = nb_desktop_preview_create();
    CHECK(preview != NULL);
    if (preview == NULL) {
        return;
    }
    memset(&first, 0, sizeof(first));
    CHECK(!nb_desktop_preview_render(preview, "12:34", 1, &first));
    CHECK(!nb_desktop_preview_set_output(NULL, &output));
    output.logical_width = 0;
    CHECK(!nb_desktop_preview_set_output(preview, &output));
    output.logical_width = 1024;
    output.logical_height = NB_MENU_BAR_HEIGHT;
    CHECK(!nb_desktop_preview_set_output(preview, &output));
    output.logical_height = 640;
    CHECK(nb_desktop_preview_set_output(preview, &output));
    CHECK(!nb_desktop_preview_render(preview, "12:34", 0, &first));
    CHECK(!nb_desktop_preview_render(preview, "", 1, &first));
    CHECK(nb_desktop_preview_render(preview, "12:34", 1, &first));
    CHECK(first.width == 1024);
    CHECK(first.height == 640);
    CHECK(first.format == NB_HOST_PIXEL_FORMAT_XRGB8888);
    CHECK(first.serial == 1);
    CHECK(nb_host_frame_is_valid(&first));
    CHECK(sampled_color_count(&first) >= 8);
    first_hash = frame_hash(&first);

    output.logical_width = 0;
    CHECK(!nb_desktop_preview_set_output(preview, &output));
    memset(&second, 0, sizeof(second));
    CHECK(nb_desktop_preview_render(preview, "12:34", 2, &second));
    CHECK(second.serial == 2);
    CHECK(frame_hash(&second) == first_hash);

    output = (struct nb_host_output){320, 200, 640, 400, 0};
    CHECK(nb_desktop_preview_set_output(preview, &output));
    CHECK(nb_desktop_preview_render(preview, "12:34", 3, &first));
    CHECK(nb_host_frame_is_valid(&first));
    CHECK(first.width == 640);
    CHECK(first.height == 400);
    nb_desktop_preview_destroy(preview);
    nb_desktop_preview_destroy(NULL);
    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
}

static void test_preview_input(void)
{
    struct nb_desktop_preview *preview = nb_desktop_preview_create();
    const struct nb_host_output output = {640, 480, 640, 480, 60000};
    struct nb_desktop_preview_update update;
    struct nb_host_event event;
    struct nb_host_frame frame;
    struct nb_rect initial;
    struct nb_rect moved;
    struct nb_rect preserved;
    uint64_t without_cursor;

    CHECK(preview != NULL);
    if (preview == NULL) {
        return;
    }
    CHECK(nb_desktop_preview_set_output(preview, &output));
    CHECK(nb_desktop_preview_window_frame(preview, &initial));
    CHECK(nb_desktop_preview_render(preview, "12:34", 1, &frame));
    without_cursor = frame_hash(&frame);

    event = pointer_motion(initial.x + 40, initial.y + 8);
    CHECK(nb_desktop_preview_handle_input(preview, &event, &update));
    CHECK(update.redraw);
    CHECK(!update.exit_requested);
    CHECK(nb_desktop_preview_render(preview, "12:34", 2, &frame));
    CHECK(frame_hash(&frame) != without_cursor);

    event = left_button(initial.x + 40, initial.y + 8, true);
    CHECK(nb_desktop_preview_handle_input(preview, &event, &update));
    event = pointer_motion(initial.x + 90, initial.y + 38);
    CHECK(nb_desktop_preview_handle_input(preview, &event, &update));
    event = left_button(initial.x + 90, initial.y + 38, false);
    CHECK(nb_desktop_preview_handle_input(preview, &event, &update));
    CHECK(nb_desktop_preview_window_frame(preview, &moved));
    CHECK(moved.x == initial.x + 50);
    CHECK(moved.y == initial.y + 30);

    CHECK(nb_desktop_preview_set_output(preview, &output));
    CHECK(nb_desktop_preview_window_frame(preview, &preserved));
    CHECK(memcmp(&moved, &preserved, sizeof(moved)) == 0);

    event = left_button(moved.x + 30, moved.y + 8, true);
    CHECK(nb_desktop_preview_handle_input(preview, &event, &update));
    CHECK(nb_desktop_preview_cancel_input(preview));
    event = pointer_motion(moved.x + 100, moved.y + 80);
    CHECK(nb_desktop_preview_handle_input(preview, &event, &update));
    CHECK(nb_desktop_preview_window_frame(preview, &preserved));
    CHECK(memcmp(&moved, &preserved, sizeof(moved)) == 0);

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_KEY;
    memcpy(event.data.key.xkb_key_name, "ESC", 4);
    event.data.key.pressed = true;
    CHECK(nb_desktop_preview_handle_input(preview, &event, &update));
    CHECK(update.exit_requested);

    CHECK(nb_desktop_preview_set_pointer(preview, 9999, 9999, true));
    CHECK(nb_desktop_preview_render(preview, "12:34", 3, &frame));
    CHECK(nb_host_frame_is_valid(&frame));
    CHECK(!nb_desktop_preview_handle_input(preview, NULL, &update));
    CHECK(!nb_desktop_preview_cancel_input(NULL));
    nb_desktop_preview_destroy(preview);
}

int main(void)
{
    test_preview_frame();
    test_preview_input();
    if (failures != 0) {
        fprintf(stderr, "desktop preview tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("desktop preview tests: ok");
    return 0;
}
