#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "desktop_runtime.h"
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

static const struct nb_host_output initial_output = {
    1024, 640, 1024, 640, 60000
};

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

static struct nb_host_event pointer_motion(int x,
                                           int y,
                                           uint64_t milliseconds)
{
    struct nb_host_event event;

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_MOTION;
    event.milliseconds = milliseconds;
    event.data.pointer_motion.x = x;
    event.data.pointer_motion.y = y;
    return event;
}

static struct nb_host_event left_button(int x,
                                        int y,
                                        bool pressed,
                                        uint64_t milliseconds)
{
    struct nb_host_event event;

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_BUTTON;
    event.milliseconds = milliseconds;
    event.data.pointer_button.x = x;
    event.data.pointer_button.y = y;
    event.data.pointer_button.button = NB_HOST_POINTER_BUTTON_LEFT;
    event.data.pointer_button.pressed = pressed;
    return event;
}

static struct nb_host_event key_event(const char *name,
                                      bool pressed,
                                      uint64_t milliseconds)
{
    struct nb_host_event event;

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_KEY;
    event.milliseconds = milliseconds;
    event.data.key.pressed = pressed;
    (void)snprintf(event.data.key.xkb_key_name,
                   sizeof(event.data.key.xkb_key_name),
                   "%s",
                   name);
    return event;
}

static struct nb_desktop_runtime_update click_runtime(
    struct nb_desktop_runtime *runtime,
    int x,
    int y,
    uint64_t milliseconds)
{
    struct nb_desktop_runtime_update update;
    struct nb_host_event event = left_button(x, y, true, milliseconds);

    memset(&update, 0, sizeof(update));
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = left_button(x, y, false, milliseconds + 1);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    return update;
}

static struct nb_desktop_runtime *create_runtime_with_launcher(
    bool software_pointer,
    bool application_launcher)
{
    struct nb_desktop_runtime_options options;

    nb_desktop_runtime_options_init(&options);
    options.enable_wayland = false;
    options.publish_wayland_socket = false;
    options.software_pointer = software_pointer;
    options.enable_application_launcher = application_launcher;
    return nb_desktop_runtime_create(&options, &initial_output);
}

static struct nb_desktop_runtime *create_runtime(bool software_pointer)
{
    return create_runtime_with_launcher(software_pointer, false);
}

static void test_creation_render_and_nixinfo(void)
{
    struct nb_desktop_runtime_options options = {0};
    struct nb_desktop_runtime *runtime;
    struct nb_host_output output;
    struct nb_host_frame frame;
    struct nb_rect window_frame;
    nb_window_id window = NB_WINDOW_ID_NONE;
    uint64_t first_hash;

    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
    nb_desktop_runtime_options_init(&options);
    CHECK(!options.enable_wayland);
    CHECK(!options.publish_wayland_socket);
    CHECK(!options.software_pointer);
    CHECK(!options.enable_application_launcher);

    runtime = nb_desktop_runtime_create(&options, &initial_output);
    CHECK(runtime != NULL);
    if (runtime == NULL) {
        return;
    }
    CHECK(nb_desktop_runtime_get_output(runtime, &output));
    CHECK(memcmp(&output, &initial_output, sizeof(output)) == 0);
    CHECK(nb_desktop_runtime_window_count(runtime) == 1);
    CHECK(nb_desktop_runtime_active_window_frame(runtime,
                                                  &window,
                                                  &window_frame));
    CHECK(window != NB_WINDOW_ID_NONE);
    CHECK(window_frame.width > 0);
    CHECK(window_frame.height > 0);
    CHECK(nb_desktop_runtime_wayland_display_name(runtime) == NULL);
    CHECK(!nb_desktop_runtime_quit_requested(runtime));

    memset(&frame, 0, sizeof(frame));
    CHECK(nb_desktop_runtime_render(runtime, "12:34", 1, &frame));
    CHECK(nb_host_frame_is_valid(&frame));
    CHECK(frame.width == initial_output.pixel_width);
    CHECK(frame.height == initial_output.pixel_height);
    CHECK(frame.format == NB_HOST_PIXEL_FORMAT_XRGB8888);
    CHECK(frame.serial == 1);
    first_hash = frame_hash(&frame);

    CHECK(nb_desktop_runtime_render(runtime, "12:34", 2, &frame));
    CHECK(frame.serial == 2);
    CHECK(frame_hash(&frame) == first_hash);
    nb_desktop_runtime_frame_presented(runtime, 100);

    nb_desktop_runtime_destroy(runtime);
    CHECK((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0);
}

static void test_drag_cancel_and_resize(void)
{
    struct nb_desktop_runtime *runtime = create_runtime(false);
    const struct nb_host_output resized = {
        480, 320, 960, 640, 0
    };
    struct nb_desktop_runtime_update update;
    struct nb_host_event event;
    struct nb_host_output actual_output;
    struct nb_host_frame frame;
    struct nb_rect initial;
    struct nb_rect moved;
    struct nb_rect cancelled;
    struct nb_rect clamped;
    nb_window_id initial_window = NB_WINDOW_ID_NONE;
    nb_window_id current_window = NB_WINDOW_ID_NONE;

    CHECK(runtime != NULL);
    if (runtime == NULL) {
        return;
    }
    CHECK(nb_desktop_runtime_active_window_frame(runtime,
                                                  &initial_window,
                                                  &initial));

    event = left_button(initial.x + 40,
                        initial.y + 8,
                        true,
                        10);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.redraw);
    CHECK(!update.quit_requested);
    CHECK(nb_desktop_runtime_wants_pointer_capture(runtime));

    event = pointer_motion(initial.x + 65, initial.y + 25, 11);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(nb_desktop_runtime_active_window_frame(runtime,
                                                  &current_window,
                                                  &moved));
    CHECK(current_window == initial_window);
    CHECK(moved.x == initial.x + 25);
    CHECK(moved.y == initial.y + 17);

    nb_desktop_runtime_cancel_input(runtime, 12);
    CHECK(!nb_desktop_runtime_wants_pointer_capture(runtime));
    event = pointer_motion(initial.x + 200, initial.y + 120, 13);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(nb_desktop_runtime_active_window_frame(runtime,
                                                  &current_window,
                                                  &cancelled));
    CHECK(memcmp(&cancelled, &moved, sizeof(moved)) == 0);

    CHECK(nb_desktop_runtime_set_output(runtime, &resized));
    CHECK(nb_desktop_runtime_get_output(runtime, &actual_output));
    CHECK(memcmp(&actual_output, &resized, sizeof(resized)) == 0);
    CHECK(nb_desktop_runtime_active_window_frame(runtime,
                                                  &current_window,
                                                  &clamped));
    CHECK(current_window == initial_window);
    CHECK(clamped.x == 0);
    CHECK(clamped.y == NB_MENU_BAR_HEIGHT);
    CHECK(clamped.width == resized.logical_width);
    CHECK(clamped.height == resized.logical_height - NB_MENU_BAR_HEIGHT);
    CHECK(nb_desktop_runtime_window_count(runtime) == 1);

    CHECK(nb_desktop_runtime_render(runtime, "12:34", 3, &frame));
    CHECK(nb_host_frame_is_valid(&frame));
    CHECK(frame.width == resized.pixel_width);
    CHECK(frame.height == resized.pixel_height);
    CHECK(frame.format == NB_HOST_PIXEL_FORMAT_XRGB8888);
    nb_desktop_runtime_destroy(runtime);
}

static void test_software_pointer_and_escape(void)
{
    struct nb_desktop_runtime *runtime = create_runtime(true);
    struct nb_desktop_runtime_update update;
    struct nb_host_event event;
    struct nb_host_frame frame;
    uint64_t without_pointer;

    CHECK(runtime != NULL);
    if (runtime == NULL) {
        return;
    }
    CHECK(nb_desktop_runtime_render(runtime, "12:34", 1, &frame));
    without_pointer = frame_hash(&frame);

    CHECK(nb_desktop_runtime_set_pointer(runtime, 8, 100, true));
    CHECK(nb_desktop_runtime_render(runtime, "12:34", 2, &frame));
    CHECK(frame_hash(&frame) != without_pointer);
    CHECK(nb_desktop_runtime_set_pointer(runtime, 8, 100, false));
    CHECK(nb_desktop_runtime_render(runtime, "12:34", 3, &frame));
    CHECK(frame_hash(&frame) == without_pointer);

    event = key_event("ESC", true, 20);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.redraw);
    CHECK(update.quit_requested);
    CHECK(nb_desktop_runtime_quit_requested(runtime));
    nb_desktop_runtime_destroy(runtime);
}

static void test_keyboard_menu_path(void)
{
    struct nb_desktop_runtime *runtime = create_runtime(false);
    struct nb_desktop_runtime_update update;
    struct nb_host_event event;
    struct nb_host_frame frame;
    uint64_t closed_hash;
    uint64_t open_hash;

    CHECK(runtime != NULL);
    if (runtime == NULL) {
        return;
    }
    CHECK(nb_desktop_runtime_window_count(runtime) == 1);
    CHECK(nb_desktop_runtime_render(runtime, "12:34", 1, &frame));
    closed_hash = frame_hash(&frame);

    event = key_event("FK10", true, 30);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.redraw);
    CHECK(!update.quit_requested);
    CHECK(nb_desktop_runtime_render(runtime, "12:34", 2, &frame));
    open_hash = frame_hash(&frame);
    CHECK(open_hash != closed_hash);

    event = key_event("DOWN", true, 31);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(!update.quit_requested);
    event.data.key.repeat = true;
    event.milliseconds = 32;
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(!update.quit_requested);

    event = key_event("ESC", true, 33);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(!update.quit_requested);
    CHECK(!nb_desktop_runtime_quit_requested(runtime));
    CHECK(nb_desktop_runtime_render(runtime, "12:34", 3, &frame));
    CHECK(frame_hash(&frame) == closed_hash);

    event = key_event("FK10", true, 34);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RTRN", true, 35);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(!update.quit_requested);
    CHECK(nb_desktop_runtime_dispatch(runtime, &update));
    CHECK(nb_desktop_runtime_window_count(runtime) == 2);

    event = key_event("FK10", true, 36);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("KPEN", true, 37);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(!update.quit_requested);

    event = key_event("ESC", true, 38);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.quit_requested);
    CHECK(nb_desktop_runtime_quit_requested(runtime));
    nb_desktop_runtime_destroy(runtime);
}

static void test_application_launcher_menu(void)
{
    struct nb_desktop_runtime *runtime =
        create_runtime_with_launcher(false, true);
    struct nb_desktop_runtime_update update;
    struct nb_host_event event;

    CHECK(runtime != NULL);
    if (runtime == NULL) {
        return;
    }

    event = key_event("FK10", true, 40);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.launch_request == NB_DESKTOP_LAUNCH_NONE);
    event = key_event("RGHT", true, 41);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.launch_request == NB_DESKTOP_LAUNCH_NONE);
    event = key_event("RGHT", true, 42);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RGHT", true, 43);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RTRN", true, 44);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.launch_request == NB_DESKTOP_LAUNCH_NIXCLOCK);

    event = key_event("FK10", true, 45);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RTRN", true, 46);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(nb_desktop_runtime_window_count(runtime) == 2);

    event = key_event("FK10", true, 47);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RGHT", true, 48);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RGHT", true, 49);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RGHT", true, 50);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("DOWN", true, 51);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RTRN", true, 52);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.launch_request == NB_DESKTOP_LAUNCH_SAKURA);

    event = key_event("FK10", true, 53);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RGHT", true, 54);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RGHT", true, 55);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RGHT", true, 56);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("DOWN", true, 57);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("DOWN", true, 58);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RTRN", true, 59);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.launch_request == NB_DESKTOP_LAUNCH_MIDORI);

    nb_desktop_runtime_destroy(runtime);
}

static void test_settings_and_application_pins(void)
{
    struct nb_user_preferences initial_preferences;
    struct nb_user_preferences preferences;
    struct nb_desktop_runtime_options options;
    struct nb_desktop_runtime *runtime;
    struct nb_desktop_runtime_update update;
    struct nb_host_event event;
    struct nb_host_frame rendered_frame;
    struct nb_rect frame;
    struct nb_rect content;
    nb_window_id window;
    uint64_t milliseconds = 100;

    nb_user_preferences_init(&initial_preferences);
    nb_desktop_runtime_options_init(&options);
    options.enable_application_launcher = true;
    options.preferences = &initial_preferences;
    runtime = nb_desktop_runtime_create(&options, &initial_output);
    CHECK(runtime != NULL);
    if (runtime == NULL) {
        return;
    }

    update = click_runtime(runtime, 900, 600, milliseconds);
    milliseconds += 2;
    event = key_event("FK10", true, milliseconds++);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("DOWN", true, milliseconds++);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("DOWN", true, milliseconds++);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RTRN", true, milliseconds++);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(nb_desktop_runtime_window_count(runtime) == 2);
    CHECK(nb_desktop_runtime_active_window_frame(runtime, &window, &frame));
    CHECK(frame.width == 640);
    CHECK(frame.height == 550);
    content = (struct nb_rect){
        frame.x + NB_WINDOW_BORDER_WIDTH,
        frame.y + NB_WINDOW_BORDER_WIDTH + NB_WINDOW_TITLE_HEIGHT,
        frame.width - (2 * NB_WINDOW_BORDER_WIDTH),
        frame.height - (2 * NB_WINDOW_BORDER_WIDTH) -
            NB_WINDOW_TITLE_HEIGHT - NB_WINDOW_FOOTER_HEIGHT
    };

    update = click_runtime(runtime,
                           content.x + 20,
                           content.y + 70,
                           milliseconds);
    milliseconds += 2;
    CHECK(!update.preferences_changed);
    update = click_runtime(runtime,
                           content.x + 16 + 5 * 37 + 2,
                           content.y + 114,
                           milliseconds);
    milliseconds += 2;
    CHECK(update.preferences_changed);
    CHECK(nb_color_equal(update.preferences.backdrop_secondary,
                         (struct nb_color){196, 112, 45}));

    update = click_runtime(runtime,
                           content.x + 20,
                           content.y + 175,
                           milliseconds);
    milliseconds += 2;
    CHECK(update.preferences_changed);
    CHECK(update.preferences.backdrop_gradient_enabled);

    update = click_runtime(runtime,
                           content.x + 20,
                           content.y + 275,
                           milliseconds);
    milliseconds += 2;
    CHECK(update.preferences_changed);
    CHECK(!update.preferences.pinned_applications[
        NB_PINNED_APPLICATION_SAKURA]);

    update = click_runtime(runtime,
                           content.x + 20,
                           content.y + 351,
                           milliseconds);
    milliseconds += 2;
    CHECK(update.preferences_changed);
    CHECK(!update.preferences.minimize_gadget_visible);

    update = click_runtime(runtime,
                           content.x + 20,
                           content.y + 375,
                           milliseconds);
    milliseconds += 2;
    CHECK(update.preferences_changed);
    CHECK(!update.preferences.maximize_gadget_visible);
    CHECK(nb_desktop_runtime_get_preferences(runtime, &preferences));
    CHECK(!preferences.minimize_gadget_visible);
    CHECK(!preferences.maximize_gadget_visible);
    CHECK(preferences.backdrop_gradient_enabled);

    /* Settings has one base menu; its overlay is the rebuilt Applications. */
    event = key_event("FK10", true, milliseconds++);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RGHT", true, milliseconds++);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("DOWN", true, milliseconds++);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RTRN", true, milliseconds++);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.launch_request == NB_DESKTOP_LAUNCH_MIDORI);

    update = click_runtime(runtime,
                           content.x + 20,
                           content.y + 475,
                           milliseconds);
    milliseconds += 2;
    CHECK(update.preferences_changed);
    CHECK(update.preferences.wallpaper_mode == NB_WALLPAPER_FILL);

    update = click_runtime(runtime,
                           content.x + 20,
                           content.y + 451,
                           milliseconds);
    milliseconds += 2;
    CHECK(nb_desktop_runtime_window_count(runtime) == 3);
    CHECK(nb_desktop_runtime_active_window_frame(runtime, &window, &frame));
    CHECK(nb_desktop_runtime_render(runtime,
                                    "12:34",
                                    900,
                                    &rendered_frame));
    content = (struct nb_rect){
        frame.x + NB_WINDOW_BORDER_WIDTH,
        frame.y + NB_WINDOW_BORDER_WIDTH + NB_WINDOW_TITLE_HEIGHT,
        frame.width - (2 * NB_WINDOW_BORDER_WIDTH),
        frame.height - (2 * NB_WINDOW_BORDER_WIDTH) -
            NB_WINDOW_TITLE_HEIGHT - NB_WINDOW_FOOTER_HEIGHT
    };
    update = click_runtime(runtime,
                           content.x + 12 + 312 + 10,
                           content.y + content.height - 30,
                           milliseconds);
    CHECK(nb_desktop_runtime_window_count(runtime) == 2);

    nb_desktop_runtime_destroy(runtime);
}

static void test_screenshot_countdown(void)
{
    struct nb_desktop_runtime *runtime = create_runtime(false);
    struct nb_desktop_runtime_update update;
    struct nb_host_event event;
    struct nb_host_frame frame;
    uint64_t initial_hash;
    uint64_t second_hash;
    uint64_t milliseconds = 100;
    const uint64_t runtime_clock = UINT64_C(1000000);

    CHECK(runtime != NULL);
    if (runtime == NULL) {
        return;
    }
    CHECK(nb_desktop_runtime_tick(runtime, milliseconds, &update));
    update = click_runtime(runtime, 900, 600, ++milliseconds);
    milliseconds += 2;

    event = key_event("FK10", true, ++milliseconds);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("DOWN", true, ++milliseconds);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event.milliseconds = ++milliseconds;
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event.milliseconds = ++milliseconds;
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    event = key_event("RTRN", true, ++milliseconds);
    CHECK(nb_desktop_runtime_handle_input(runtime, &event, &update));
    CHECK(update.redraw);
    CHECK(nb_desktop_runtime_timer_timeout(runtime, runtime_clock) == 0);
    CHECK(nb_desktop_runtime_tick(runtime, runtime_clock, &update));
    CHECK(update.redraw);
    CHECK(nb_desktop_runtime_timer_timeout(runtime, runtime_clock) <= 1000);

    CHECK(nb_desktop_runtime_render(runtime, "12:34", 1, &frame));
    initial_hash = frame_hash(&frame);
    CHECK(nb_desktop_runtime_tick(runtime,
                                  runtime_clock + 1000,
                                  &update));
    CHECK(update.redraw);
    CHECK(nb_desktop_runtime_render(runtime, "12:34", 2, &frame));
    second_hash = frame_hash(&frame);
    CHECK(second_hash != initial_hash);

    CHECK(nb_desktop_runtime_tick(runtime,
                                  runtime_clock + 5000,
                                  &update));
    CHECK(update.redraw);
    nb_desktop_runtime_destroy(runtime);
}

static void test_defensive_api(void)
{
    struct nb_desktop_runtime_options invalid_options;
    struct nb_desktop_runtime *runtime;
    struct nb_host_output invalid_output = initial_output;
    struct nb_host_output preserved;
    struct nb_desktop_runtime_update update;
    struct nb_host_event event;
    struct nb_host_frame frame;
    struct nb_rect window_frame;
    struct nb_user_preferences preferences;
    nb_window_id window;

    nb_desktop_runtime_options_init(NULL);
    nb_desktop_runtime_options_init(&invalid_options);
    invalid_options.enable_wayland = false;
    invalid_options.publish_wayland_socket = true;
    invalid_options.software_pointer = false;
    invalid_options.enable_application_launcher = false;
    CHECK(nb_desktop_runtime_create(&invalid_options,
                                    &initial_output) == NULL);
    invalid_output.logical_width = 0;
    CHECK(nb_desktop_runtime_create(NULL, &invalid_output) == NULL);
    CHECK(nb_desktop_runtime_create(NULL, NULL) == NULL);

    runtime = nb_desktop_runtime_create(NULL, &initial_output);
    CHECK(runtime != NULL);
    if (runtime == NULL) {
        return;
    }
    CHECK(!nb_desktop_runtime_set_output(NULL, &initial_output));
    CHECK(!nb_desktop_runtime_set_output(runtime, NULL));
    CHECK(!nb_desktop_runtime_set_output(runtime, &invalid_output));
    CHECK(nb_desktop_runtime_get_output(runtime, &preserved));
    CHECK(memcmp(&preserved, &initial_output, sizeof(preserved)) == 0);
    CHECK(!nb_desktop_runtime_get_output(NULL, &preserved));
    CHECK(!nb_desktop_runtime_get_output(runtime, NULL));
    CHECK(!nb_desktop_runtime_set_pointer(NULL, 0, 0, true));

    event = pointer_motion(10, 10, 1);
    CHECK(!nb_desktop_runtime_handle_input(NULL, &event, &update));
    CHECK(!nb_desktop_runtime_handle_input(runtime, NULL, &update));
    CHECK(!nb_desktop_runtime_handle_input(runtime, &event, NULL));
    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_QUIT;
    CHECK(!nb_desktop_runtime_handle_input(runtime, &event, &update));

    CHECK(!nb_desktop_runtime_set_focus(NULL, true, 0, &update));
    CHECK(!nb_desktop_runtime_set_focus(runtime, true, 0, NULL));
    CHECK(!nb_desktop_runtime_pointer_leave(NULL, false, 0, &update));
    CHECK(!nb_desktop_runtime_pointer_leave(runtime, false, 0, NULL));
    CHECK(!nb_desktop_runtime_dispatch(NULL, &update));
    CHECK(!nb_desktop_runtime_dispatch(runtime, NULL));
    CHECK(nb_desktop_runtime_dispatch(runtime, &update));
    CHECK(!update.redraw);
    CHECK(!update.quit_requested);
    CHECK(update.launch_request == NB_DESKTOP_LAUNCH_NONE);
    CHECK(!nb_desktop_runtime_tick(NULL, 0, &update));
    CHECK(!nb_desktop_runtime_tick(runtime, 0, NULL));
    CHECK(nb_desktop_runtime_timer_timeout(NULL, 0) == UINT32_MAX);

    CHECK(!nb_desktop_runtime_render(NULL, "12:34", 1, &frame));
    CHECK(!nb_desktop_runtime_render(runtime, NULL, 1, &frame));
    CHECK(!nb_desktop_runtime_render(runtime, "", 1, &frame));
    CHECK(!nb_desktop_runtime_render(runtime, "12:34", 0, &frame));
    CHECK(!nb_desktop_runtime_render(runtime, "12:34", 1, NULL));
    CHECK(!nb_desktop_runtime_wants_pointer_capture(NULL));
    CHECK(!nb_desktop_runtime_quit_requested(NULL));
    CHECK(nb_desktop_runtime_wayland_display_name(NULL) == NULL);
    CHECK(nb_desktop_runtime_window_count(NULL) == 0);
    CHECK(!nb_desktop_runtime_active_window_frame(NULL,
                                                   &window,
                                                   &window_frame));
    CHECK(!nb_desktop_runtime_active_window_frame(runtime,
                                                   NULL,
                                                   &window_frame));
    CHECK(!nb_desktop_runtime_active_window_frame(runtime,
                                                   &window,
                                                   NULL));
    CHECK(!nb_desktop_runtime_get_preferences(NULL, &preferences));
    nb_desktop_runtime_frame_presented(NULL, 0);
    nb_desktop_runtime_cancel_input(NULL, 0);
    nb_desktop_runtime_destroy(runtime);
    nb_desktop_runtime_destroy(NULL);
}

int main(void)
{
    test_creation_render_and_nixinfo();
    test_drag_cancel_and_resize();
    test_software_pointer_and_escape();
    test_keyboard_menu_path();
    test_application_launcher_menu();
    test_settings_and_application_pins();
    test_screenshot_countdown();
    test_defensive_api();

    if (failures != 0) {
        fprintf(stderr,
                "desktop runtime tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("desktop runtime tests: ok");
    return 0;
}
