#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "nixbench-application-menu-v1-client-protocol.h"
#include "nixbench-html-theme-v1-client-protocol.h"
#include "wayland_server.h"
#include "wscons_input.h"
#include "xdg-shell-client-protocol.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

#define REQUIRE(expression)                                                   \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: requirement failed: %s\n",              \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
            goto cleanup;                                                     \
        }                                                                     \
    } while (false)

enum {
    DESKTOP_MENU_SOURCE = 1,
    WAYLAND_MENU_SOURCE = 2,
    OUTPUT_WIDTH = 1024,
    OUTPUT_HEIGHT = 640,
    INITIAL_WIDTH = 560,
    INITIAL_HEIGHT = 300,
    POPUP_WIDTH = 160,
    POPUP_HEIGHT = 72,
    POPUP_EXPECTED_X = 27,
    POPUP_EXPECTED_Y = 69,
    BYTES_PER_PIXEL = 4,
    WAYLAND_POINTER_BUTTON_LEFT = 0x110,
    WAYLAND_POINTER_BUTTON_RIGHT = 0x111,
    PUMP_ATTEMPTS = 80,
    PUMP_TIMEOUT_MILLISECONDS = 25,
    SEAT_NAME_CAPACITY = 64,
    OUTPUT_TEXT_CAPACITY = 64,
    CLIPBOARD_MIME_CAPACITY = 128,
    KEYBOARD_KEY_CAPACITY = 256
};

enum {
    APPLICATION_COMMAND_QUIT = 1,
    APPLICATION_COMMAND_SHOW_SECONDS = 2,
    APPLICATION_COMMAND_DISABLED = 3
};

static const struct nb_menu_model empty_menu_model = {NULL, 0};

struct barrier_state {
    bool done;
};

struct clipboard_owner_state {
    unsigned int calls;
    bool available;
};

static bool record_clipboard_owner(void *context, bool available)
{
    struct clipboard_owner_state *state = context;

    if (state == NULL) {
        return false;
    }
    ++state->calls;
    state->available = available;
    return true;
}

struct output_state {
    struct wl_output *proxy;
    int32_t geometry_x;
    int32_t geometry_y;
    int32_t physical_width;
    int32_t physical_height;
    int32_t subpixel;
    int32_t transform;
    int32_t mode_width;
    int32_t mode_height;
    int32_t refresh;
    int32_t scale;
    uint32_t mode_flags;
    char make[OUTPUT_TEXT_CAPACITY];
    char model[OUTPUT_TEXT_CAPACITY];
    unsigned int geometry_count;
    unsigned int mode_count;
    unsigned int done_count;
    unsigned int scale_count;
};

struct client_state {
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_data_device_manager *data_device_manager;
    struct wl_data_device *data_device;
    struct wl_data_source *data_source;
    struct wl_data_offer *selection_offer;
    struct output_state output;
    struct output_state late_output;
    struct xdg_wm_base *wm_base;
    struct nixbench_application_menu_manager_v1 *application_menu_manager;
    struct nixbench_application_menu_v1 *application_menu;
    struct nixbench_html_theme_manager_v1 *html_theme_manager;
    struct nixbench_html_theme_atlas_v1 *html_theme_atlas;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_callback *frame_callback;
    bool saw_argb8888;
    bool buffer_released;
    bool frame_done;
    bool close_requested;
    uint32_t configure_serial;
    uint32_t frame_milliseconds;
    int32_t configured_width;
    int32_t configured_height;
    unsigned int toplevel_configure_count;
    unsigned int surface_configure_count;
    uint32_t seat_capabilities;
    uint32_t seat_version;
    uint32_t seat_global_name;
    uint32_t data_device_manager_version;
    uint32_t output_global_name;
    uint32_t output_global_version;
    char seat_name[SEAT_NAME_CAPACITY];
    struct wl_output *surface_enter_output;
    struct wl_output *surface_leave_output;
    unsigned int surface_enter_count;
    unsigned int surface_leave_count;
    unsigned int initial_output_enter_count;
    unsigned int initial_output_leave_count;
    unsigned int late_output_enter_count;
    unsigned int late_output_leave_count;
    struct wl_surface *pointer_enter_surface;
    struct wl_surface *pointer_leave_surface;
    wl_fixed_t pointer_enter_x;
    wl_fixed_t pointer_enter_y;
    wl_fixed_t pointer_x;
    wl_fixed_t pointer_y;
    uint32_t pointer_serial;
    uint32_t pointer_time;
    uint32_t pointer_button;
    uint32_t pointer_button_state;
    unsigned int seat_capability_count;
    unsigned int seat_name_count;
    unsigned int seat_event_sequence;
    unsigned int seat_capability_sequence;
    unsigned int seat_name_sequence;
    unsigned int data_source_cancelled_count;
    unsigned int data_source_send_count;
    unsigned int data_offer_count;
    unsigned int data_offer_mime_count;
    const char *data_source_payload;
    size_t data_source_payload_size;
    char data_offer_mime[CLIPBOARD_MIME_CAPACITY];
    bool seat_waited_for_data_device_manager;
    unsigned int input_event_sequence;
    unsigned int data_device_selection_count;
    unsigned int data_device_selection_sequence;
    unsigned int pointer_enter_count;
    unsigned int pointer_leave_count;
    unsigned int pointer_motion_count;
    unsigned int pointer_button_count;
    unsigned int pointer_frame_count;
    uint32_t keyboard_keymap_format;
    uint32_t keyboard_keymap_size;
    bool keyboard_keymap_mapped;
    bool keyboard_keymap_terminated;
    bool keyboard_keymap_has_header;
    bool keyboard_keycodes_valid;
    bool keyboard_key_a_is_lowercase;
    bool keyboard_shift_a_is_uppercase;
    bool keyboard_pc_xt_profile_resolves;
    uint32_t keyboard_key_a;
    uint32_t keyboard_key_left_shift;
    uint32_t keyboard_key_space;
    uint32_t keyboard_key_f4;
    uint32_t keyboard_key_f10;
    uint32_t keyboard_key_f12;
    int32_t keyboard_repeat_rate;
    int32_t keyboard_repeat_delay;
    struct wl_surface *keyboard_enter_surface;
    struct wl_surface *keyboard_leave_surface;
    uint32_t keyboard_serial;
    uint32_t keyboard_time;
    uint32_t keyboard_key;
    uint32_t keyboard_key_state;
    uint32_t keyboard_mods_depressed;
    uint32_t keyboard_mods_latched;
    uint32_t keyboard_mods_locked;
    uint32_t keyboard_group;
    uint32_t keyboard_enter_keys[KEYBOARD_KEY_CAPACITY];
    size_t keyboard_enter_key_count;
    unsigned int keyboard_keymap_count;
    unsigned int keyboard_repeat_count;
    unsigned int keyboard_enter_sequence;
    unsigned int keyboard_enter_count;
    unsigned int keyboard_leave_count;
    unsigned int keyboard_key_count;
    unsigned int keyboard_modifiers_count;
    unsigned int keyboard_press_counts[KEYBOARD_KEY_CAPACITY];
    unsigned int keyboard_release_counts[KEYBOARD_KEY_CAPACITY];
    uint32_t application_menu_command;
    unsigned int application_menu_command_count;
    uint32_t html_theme_state_serial;
    unsigned int html_theme_configure_count;
    unsigned int html_theme_window_count;
    unsigned int html_theme_state_done_count;
};

struct popup_state {
    uint32_t configure_serial;
    int32_t configured_x;
    int32_t configured_y;
    int32_t configured_width;
    int32_t configured_height;
    unsigned int event_sequence;
    unsigned int popup_configure_sequence;
    unsigned int surface_configure_sequence;
    unsigned int popup_configure_count;
    unsigned int surface_configure_count;
    unsigned int done_count;
};

static void barrier_done(void *data,
                         struct wl_callback *callback,
                         uint32_t callback_data)
{
    struct barrier_state *state = data;

    (void)callback_data;
    state->done = true;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener barrier_listener = {
    .done = barrier_done
};

static bool add_server_client(struct nb_wayland_server *server, int fd)
{
    int saved_errno;

    errno = 0;
    if (nb_wayland_server_add_client_fd(server, fd)) {
        return true;
    }
    saved_errno = errno;
    fprintf(stderr,
            "could not attach Wayland test client: %s\n",
            saved_errno == 0 ? "unknown error" : strerror(saved_errno));
    return false;
}

/*
 * A client and its compositor share this thread, so a blocking round trip
 * would deadlock. Instead, put a sync request at the end of the client queue
 * and alternately flush the client, dispatch the server, and consume readable
 * client events until the sync callback arrives.
 */
static bool pump_barrier(struct nb_wayland_server *server,
                         struct wl_display *display)
{
    struct barrier_state state = {false};
    struct wl_callback *callback = wl_display_sync(display);
    const int display_fd = wl_display_get_fd(display);
    int attempt;

    if (callback == NULL || display_fd < 0 ||
        wl_callback_add_listener(callback, &barrier_listener, &state) < 0) {
        if (callback != NULL) {
            wl_callback_destroy(callback);
        }
        return false;
    }

    for (attempt = 0; attempt < PUMP_ATTEMPTS; ++attempt) {
        struct pollfd poll_fd;
        int result;

        errno = 0;
        if (wl_display_flush(display) < 0 && errno != EAGAIN) {
            return false;
        }
        if (!nb_wayland_server_dispatch(server) ||
            wl_display_dispatch_pending(display) < 0) {
            return false;
        }
        if (state.done) {
            return true;
        }

        poll_fd.fd = display_fd;
        poll_fd.events = POLLIN | POLLOUT;
        poll_fd.revents = 0;
        result = poll(&poll_fd, 1, PUMP_TIMEOUT_MILLISECONDS);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (result == 0) {
            continue;
        }
        if ((poll_fd.revents & POLLIN) != 0) {
            if (wl_display_dispatch(display) < 0) {
                return false;
            }
            if (state.done) {
                return true;
            }
        }
        if ((poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return false;
        }
    }

    return false;
}

static void output_geometry(void *data,
                            struct wl_output *output,
                            int32_t x,
                            int32_t y,
                            int32_t physical_width,
                            int32_t physical_height,
                            int32_t subpixel,
                            const char *make,
                            const char *model,
                            int32_t transform)
{
    struct output_state *state = data;

    (void)output;
    state->geometry_x = x;
    state->geometry_y = y;
    state->physical_width = physical_width;
    state->physical_height = physical_height;
    state->subpixel = subpixel;
    state->transform = transform;
    (void)snprintf(state->make, sizeof(state->make), "%s", make);
    (void)snprintf(state->model, sizeof(state->model), "%s", model);
    ++state->geometry_count;
}

static void output_mode(void *data,
                        struct wl_output *output,
                        uint32_t flags,
                        int32_t width,
                        int32_t height,
                        int32_t refresh)
{
    struct output_state *state = data;

    (void)output;
    state->mode_flags = flags;
    state->mode_width = width;
    state->mode_height = height;
    state->refresh = refresh;
    ++state->mode_count;
}

static void output_done(void *data, struct wl_output *output)
{
    struct output_state *state = data;

    (void)output;
    ++state->done_count;
}

static void output_scale(void *data,
                         struct wl_output *output,
                         int32_t factor)
{
    struct output_state *state = data;

    (void)output;
    state->scale = factor;
    ++state->scale_count;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale
};

static void surface_enter(void *data,
                          struct wl_surface *surface,
                          struct wl_output *output)
{
    struct client_state *state = data;

    (void)surface;
    state->surface_enter_output = output;
    ++state->surface_enter_count;
    if (output == state->output.proxy) {
        ++state->initial_output_enter_count;
    }
    if (output == state->late_output.proxy) {
        ++state->late_output_enter_count;
    }
}

static void surface_leave(void *data,
                          struct wl_surface *surface,
                          struct wl_output *output)
{
    struct client_state *state = data;

    (void)surface;
    state->surface_leave_output = output;
    ++state->surface_leave_count;
    if (output == state->output.proxy) {
        ++state->initial_output_leave_count;
    }
    if (output == state->late_output.proxy) {
        ++state->late_output_leave_count;
    }
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave
};

static void pointer_enter(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface,
                          wl_fixed_t surface_x,
                          wl_fixed_t surface_y)
{
    struct client_state *state = data;

    (void)pointer;
    state->pointer_enter_surface = surface;
    state->pointer_serial = serial;
    state->pointer_enter_x = surface_x;
    state->pointer_enter_y = surface_y;
    ++state->pointer_enter_count;
}

static void pointer_leave(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface)
{
    struct client_state *state = data;

    (void)pointer;
    state->pointer_leave_surface = surface;
    state->pointer_serial = serial;
    ++state->pointer_leave_count;
}

static void pointer_motion(void *data,
                           struct wl_pointer *pointer,
                           uint32_t milliseconds,
                           wl_fixed_t surface_x,
                           wl_fixed_t surface_y)
{
    struct client_state *state = data;

    (void)pointer;
    state->pointer_time = milliseconds;
    state->pointer_x = surface_x;
    state->pointer_y = surface_y;
    ++state->pointer_motion_count;
}

static void pointer_button(void *data,
                           struct wl_pointer *pointer,
                           uint32_t serial,
                           uint32_t milliseconds,
                           uint32_t button,
                           uint32_t button_state)
{
    struct client_state *state = data;

    (void)pointer;
    state->pointer_serial = serial;
    state->pointer_time = milliseconds;
    state->pointer_button = button;
    state->pointer_button_state = button_state;
    ++state->pointer_button_count;
}

static void pointer_axis(void *data,
                         struct wl_pointer *pointer,
                         uint32_t milliseconds,
                         uint32_t axis,
                         wl_fixed_t value)
{
    (void)data;
    (void)pointer;
    (void)milliseconds;
    (void)axis;
    (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    struct client_state *state = data;

    (void)pointer;
    ++state->pointer_frame_count;
}

static void pointer_axis_source(void *data,
                                struct wl_pointer *pointer,
                                uint32_t axis_source)
{
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void pointer_axis_stop(void *data,
                              struct wl_pointer *pointer,
                              uint32_t milliseconds,
                              uint32_t axis)
{
    (void)data;
    (void)pointer;
    (void)milliseconds;
    (void)axis;
}

static void pointer_axis_discrete(void *data,
                                  struct wl_pointer *pointer,
                                  uint32_t axis,
                                  int32_t discrete)
{
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete
};

static void keyboard_keymap(void *data,
                            struct wl_keyboard *keyboard,
                            uint32_t format,
                            int fd,
                            uint32_t size)
{
    struct client_state *state = data;
    const char *mapping = MAP_FAILED;

    (void)keyboard;
    state->keyboard_keymap_format = format;
    state->keyboard_keymap_size = size;
    if (size > 0) {
        mapping = mmap(NULL,
                       (size_t)size,
                       PROT_READ,
                       MAP_PRIVATE,
                       fd,
                       0);
    }
    if (mapping != MAP_FAILED) {
        struct xkb_context *context = NULL;
        struct xkb_keymap *keymap = NULL;

        state->keyboard_keymap_mapped = true;
        state->keyboard_keymap_terminated = mapping[size - 1] == '\0';
        if (state->keyboard_keymap_terminated) {
            xkb_keycode_t key_a;
            xkb_keycode_t key_left_shift;
            xkb_keycode_t key_space;
            xkb_keycode_t key_f4;
            xkb_keycode_t key_f10;
            xkb_keycode_t key_f12;

            state->keyboard_keymap_has_header =
                strstr(mapping, "xkb_keymap") != NULL;
            context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            if (context != NULL) {
                keymap = xkb_keymap_new_from_string(
                    context,
                    mapping,
                    XKB_KEYMAP_FORMAT_TEXT_V1,
                    XKB_KEYMAP_COMPILE_NO_FLAGS);
            }
            if (keymap != NULL) {
                struct nb_wscons_input_reducer pc_xt_profile;
                struct xkb_state *xkb_state;
                char text[8];
                size_t profile_index;

                key_a = xkb_keymap_key_by_name(keymap, "AC01");
                key_left_shift =
                    xkb_keymap_key_by_name(keymap, "LFSH");
                key_space = xkb_keymap_key_by_name(keymap, "SPCE");
                key_f4 = xkb_keymap_key_by_name(keymap, "FK04");
                key_f10 = xkb_keymap_key_by_name(keymap, "FK10");
                key_f12 = xkb_keymap_key_by_name(keymap, "FK12");
                if (key_a >= UINT32_C(8) &&
                    key_left_shift >= UINT32_C(8) &&
                    key_space >= UINT32_C(8) &&
                    key_f4 >= UINT32_C(8) &&
                    key_f10 >= UINT32_C(8) &&
                    key_f12 >= UINT32_C(8) &&
                    key_a - UINT32_C(8) <
                        (xkb_keycode_t)KEYBOARD_KEY_CAPACITY &&
                    key_left_shift - UINT32_C(8) <
                        (xkb_keycode_t)KEYBOARD_KEY_CAPACITY &&
                    key_space - UINT32_C(8) <
                        (xkb_keycode_t)KEYBOARD_KEY_CAPACITY &&
                    key_f4 - UINT32_C(8) <
                        (xkb_keycode_t)KEYBOARD_KEY_CAPACITY &&
                    key_f10 - UINT32_C(8) <
                        (xkb_keycode_t)KEYBOARD_KEY_CAPACITY &&
                    key_f12 - UINT32_C(8) <
                        (xkb_keycode_t)KEYBOARD_KEY_CAPACITY) {
                    state->keyboard_key_a =
                        (uint32_t)(key_a - UINT32_C(8));
                    state->keyboard_key_left_shift =
                        (uint32_t)(key_left_shift - UINT32_C(8));
                    state->keyboard_key_space =
                        (uint32_t)(key_space - UINT32_C(8));
                    state->keyboard_key_f4 =
                        (uint32_t)(key_f4 - UINT32_C(8));
                    state->keyboard_key_f10 =
                        (uint32_t)(key_f10 - UINT32_C(8));
                    state->keyboard_key_f12 =
                        (uint32_t)(key_f12 - UINT32_C(8));
                    state->keyboard_keycodes_valid = true;
                }
                xkb_state = state->keyboard_keycodes_valid
                                ? xkb_state_new(keymap)
                                : NULL;
                if (xkb_state != NULL) {
                    state->keyboard_key_a_is_lowercase =
                        xkb_state_key_get_utf8(xkb_state,
                                               key_a,
                                               text,
                                               sizeof(text)) == 1 &&
                        strcmp(text, "a") == 0;
                    (void)xkb_state_update_key(xkb_state,
                                               key_left_shift,
                                               XKB_KEY_DOWN);
                    state->keyboard_shift_a_is_uppercase =
                        xkb_state_key_get_utf8(xkb_state,
                                               key_a,
                                               text,
                                               sizeof(text)) == 1 &&
                        strcmp(text, "A") == 0;
                    xkb_state_unref(xkb_state);
                }
                state->keyboard_pc_xt_profile_resolves =
                    nb_wscons_input_reducer_init(&pc_xt_profile, 1, 1) &&
                    nb_wscons_input_reducer_set_pc_xt_keycodes(
                        &pc_xt_profile);
                for (profile_index = 0;
                     state->keyboard_pc_xt_profile_resolves &&
                     profile_index < NB_WSCONS_KEYCODE_CAPACITY;
                     ++profile_index) {
                    const char *const name =
                        pc_xt_profile.keyboard_keys[profile_index]
                            .xkb_key_name;

                    if (name[0] != '\0' &&
                        xkb_keymap_key_by_name(keymap, name) ==
                            XKB_KEYCODE_INVALID) {
                        state->keyboard_pc_xt_profile_resolves = false;
                    }
                }
            }
        }
        if (keymap != NULL) {
            xkb_keymap_unref(keymap);
        }
        if (context != NULL) {
            xkb_context_unref(context);
        }
        (void)munmap((void *)mapping, (size_t)size);
    }
    (void)close(fd);
    ++state->keyboard_keymap_count;
}

static void keyboard_enter(void *data,
                           struct wl_keyboard *keyboard,
                           uint32_t serial,
                           struct wl_surface *surface,
                           struct wl_array *keys)
{
    struct client_state *state = data;
    size_t key_count = keys->size / sizeof(uint32_t);

    (void)keyboard;
    if (key_count > KEYBOARD_KEY_CAPACITY) {
        key_count = KEYBOARD_KEY_CAPACITY;
    }
    state->keyboard_serial = serial;
    state->keyboard_enter_surface = surface;
    state->keyboard_enter_key_count = key_count;
    if (key_count > 0) {
        memcpy(state->keyboard_enter_keys,
               keys->data,
               key_count * sizeof(uint32_t));
    }
    ++state->keyboard_enter_count;
    state->keyboard_enter_sequence = ++state->input_event_sequence;
}

static void keyboard_leave(void *data,
                           struct wl_keyboard *keyboard,
                           uint32_t serial,
                           struct wl_surface *surface)
{
    struct client_state *state = data;

    (void)keyboard;
    state->keyboard_serial = serial;
    state->keyboard_leave_surface = surface;
    ++state->keyboard_leave_count;
}

static void keyboard_key(void *data,
                         struct wl_keyboard *keyboard,
                         uint32_t serial,
                         uint32_t milliseconds,
                         uint32_t key,
                         uint32_t key_state)
{
    struct client_state *state = data;

    (void)keyboard;
    state->keyboard_serial = serial;
    state->keyboard_time = milliseconds;
    state->keyboard_key = key;
    state->keyboard_key_state = key_state;
    if (key < KEYBOARD_KEY_CAPACITY) {
        if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            ++state->keyboard_press_counts[key];
        } else if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
            ++state->keyboard_release_counts[key];
        }
    }
    ++state->keyboard_key_count;
}

static void keyboard_modifiers(void *data,
                               struct wl_keyboard *keyboard,
                               uint32_t serial,
                               uint32_t depressed,
                               uint32_t latched,
                               uint32_t locked,
                               uint32_t group)
{
    struct client_state *state = data;

    (void)keyboard;
    state->keyboard_serial = serial;
    state->keyboard_mods_depressed = depressed;
    state->keyboard_mods_latched = latched;
    state->keyboard_mods_locked = locked;
    state->keyboard_group = group;
    ++state->keyboard_modifiers_count;
}

static void keyboard_repeat_info(void *data,
                                 struct wl_keyboard *keyboard,
                                 int32_t rate,
                                 int32_t delay)
{
    struct client_state *state = data;

    (void)keyboard;
    state->keyboard_repeat_rate = rate;
    state->keyboard_repeat_delay = delay;
    ++state->keyboard_repeat_count;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info
};

static void seat_capabilities(void *data,
                              struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct client_state *state = data;

    state->seat_capabilities = capabilities;
    ++state->seat_capability_count;
    state->seat_capability_sequence = ++state->seat_event_sequence;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 &&
        state->pointer == NULL) {
        state->pointer = wl_seat_get_pointer(seat);
        if (state->pointer != NULL &&
            wl_pointer_add_listener(state->pointer,
                                    &pointer_listener,
                                    state) < 0) {
            wl_pointer_release(state->pointer);
            state->pointer = NULL;
        }
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 &&
        state->keyboard == NULL) {
        state->keyboard = wl_seat_get_keyboard(seat);
        if (state->keyboard != NULL &&
            wl_keyboard_add_listener(state->keyboard,
                                     &keyboard_listener,
                                     state) < 0) {
            wl_keyboard_release(state->keyboard);
            state->keyboard = NULL;
        }
    }
}

static void seat_name(void *data,
                      struct wl_seat *seat,
                      const char *name)
{
    struct client_state *state = data;

    (void)seat;
    (void)snprintf(state->seat_name,
                   sizeof(state->seat_name),
                   "%s",
                   name);
    ++state->seat_name_count;
    state->seat_name_sequence = ++state->seat_event_sequence;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name
};

static void application_menu_command(
    void *data,
    struct nixbench_application_menu_v1 *menu,
    uint32_t command)
{
    struct client_state *state = data;

    (void)menu;
    state->application_menu_command = command;
    ++state->application_menu_command_count;
}

static const struct nixbench_application_menu_v1_listener
application_menu_listener = {
    .command = application_menu_command
};

static void html_theme_configure(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial,
    uint32_t maximum_width,
    uint32_t maximum_height,
    uint32_t scale)
{
    struct client_state *state = data;

    (void)atlas;
    CHECK(serial != 0);
    CHECK(maximum_width == NB_THEME_ATLAS_MAX_DIMENSION);
    CHECK(maximum_height == NB_THEME_ATLAS_MAX_DIMENSION);
    CHECK(scale == 1);
    state->html_theme_state_serial = serial;
    ++state->html_theme_configure_count;
}

static void html_theme_theme(void *data,
                             struct nixbench_html_theme_atlas_v1 *atlas,
                             uint32_t serial,
                             const char *theme_id,
                             const char *directory)
{
    struct client_state *state = data;

    (void)atlas;
    CHECK(serial == state->html_theme_state_serial);
    CHECK(strcmp(theme_id, "motif") == 0);
    CHECK(strcmp(directory, "/tmp/nixbench-test-theme") == 0);
}

static void html_theme_clear_windows(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial)
{
    struct client_state *state = data;

    (void)atlas;
    CHECK(serial == state->html_theme_state_serial);
    state->html_theme_window_count = 0;
}

static void html_theme_window(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial,
    uint32_t window_hi,
    uint32_t window_lo,
    uint32_t width,
    uint32_t height,
    uint32_t window_state,
    const char *title)
{
    struct client_state *state = data;

    (void)atlas;
    (void)window_hi;
    (void)window_lo;
    (void)window_state;
    CHECK(serial == state->html_theme_state_serial);
    CHECK(width >= NB_WINDOW_MIN_WIDTH);
    CHECK(height >= NB_WINDOW_MIN_HEIGHT);
    CHECK(title != NULL && title[0] != '\0');
    ++state->html_theme_window_count;
}

static void html_theme_state_done(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial)
{
    struct client_state *state = data;

    (void)atlas;
    CHECK(serial == state->html_theme_state_serial);
    ++state->html_theme_state_done_count;
}

static const struct nixbench_html_theme_atlas_v1_listener
html_theme_atlas_listener = {
    .configure = html_theme_configure,
    .theme = html_theme_theme,
    .clear_windows = html_theme_clear_windows,
    .window = html_theme_window,
    .state_done = html_theme_state_done
};

static void data_source_target(void *data,
                               struct wl_data_source *source,
                               const char *mime_type)
{
    (void)data;
    (void)source;
    (void)mime_type;
}

static void data_source_send(void *data,
                             struct wl_data_source *source,
                             const char *mime_type,
                             int32_t fd)
{
    struct client_state *state = data;
    size_t offset = 0;

    (void)source;
    (void)mime_type;
    ++state->data_source_send_count;
    if (fd >= 0) {
        while (offset < state->data_source_payload_size) {
            ssize_t count = write(
                fd,
                state->data_source_payload + offset,
                state->data_source_payload_size - offset);

            if (count > 0) {
                offset += (size_t)count;
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        (void)close(fd);
    }
}

static void data_source_cancelled(void *data,
                                  struct wl_data_source *source)
{
    struct client_state *state = data;

    (void)source;
    ++state->data_source_cancelled_count;
}

static void data_source_dnd_drop_performed(void *data,
                                           struct wl_data_source *source)
{
    (void)data;
    (void)source;
}

static void data_source_dnd_finished(void *data,
                                     struct wl_data_source *source)
{
    (void)data;
    (void)source;
}

static void data_source_action(void *data,
                               struct wl_data_source *source,
                               uint32_t dnd_action)
{
    (void)data;
    (void)source;
    (void)dnd_action;
}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_target,
    .send = data_source_send,
    .cancelled = data_source_cancelled,
    .dnd_drop_performed = data_source_dnd_drop_performed,
    .dnd_finished = data_source_dnd_finished,
    .action = data_source_action
};

static void data_offer_mime(void *data,
                            struct wl_data_offer *offer,
                            const char *mime_type)
{
    struct client_state *state = data;

    (void)offer;
    (void)snprintf(state->data_offer_mime,
                   sizeof(state->data_offer_mime),
                   "%s",
                   mime_type);
    ++state->data_offer_mime_count;
}

static void data_offer_source_actions(void *data,
                                      struct wl_data_offer *offer,
                                      uint32_t source_actions)
{
    (void)data;
    (void)offer;
    (void)source_actions;
}

static void data_offer_action(void *data,
                              struct wl_data_offer *offer,
                              uint32_t dnd_action)
{
    (void)data;
    (void)offer;
    (void)dnd_action;
}

static const struct wl_data_offer_listener data_offer_listener = {
    .offer = data_offer_mime,
    .source_actions = data_offer_source_actions,
    .action = data_offer_action
};

static void data_device_data_offer(void *data,
                                   struct wl_data_device *device,
                                   struct wl_data_offer *offer)
{
    struct client_state *state = data;

    (void)device;
    ++state->data_offer_count;
    CHECK(wl_data_offer_add_listener(offer,
                                     &data_offer_listener,
                                     state) == 0);
}

static void data_device_enter(void *data,
                              struct wl_data_device *device,
                              uint32_t serial,
                              struct wl_surface *surface,
                              wl_fixed_t x,
                              wl_fixed_t y,
                              struct wl_data_offer *offer)
{
    (void)data;
    (void)device;
    (void)serial;
    (void)surface;
    (void)x;
    (void)y;
    (void)offer;
}

static void data_device_leave(void *data,
                              struct wl_data_device *device)
{
    (void)data;
    (void)device;
}

static void data_device_motion(void *data,
                               struct wl_data_device *device,
                               uint32_t time,
                               wl_fixed_t x,
                               wl_fixed_t y)
{
    (void)data;
    (void)device;
    (void)time;
    (void)x;
    (void)y;
}

static void data_device_drop(void *data,
                             struct wl_data_device *device)
{
    (void)data;
    (void)device;
}

static void data_device_selection(void *data,
                                  struct wl_data_device *device,
                                  struct wl_data_offer *offer)
{
    struct client_state *state = data;

    (void)device;
    if (state->selection_offer != NULL &&
        state->selection_offer != offer) {
        wl_data_offer_destroy(state->selection_offer);
    }
    state->selection_offer = offer;
    ++state->data_device_selection_count;
    state->data_device_selection_sequence = ++state->input_event_sequence;
}

static const struct wl_data_device_listener data_device_listener = {
    .data_offer = data_device_data_offer,
    .enter = data_device_enter,
    .leave = data_device_leave,
    .motion = data_device_motion,
    .drop = data_device_drop,
    .selection = data_device_selection
};

static void publish_application_menu(
    struct nixbench_application_menu_v1 *menu,
    bool show_seconds)
{
    uint32_t show_seconds_flags =
        NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED;

    if (show_seconds) {
        show_seconds_flags |=
            NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_CHECKED;
    }
    nixbench_application_menu_v1_append_menu(menu, "NixClock");
    nixbench_application_menu_v1_append_item(
        menu,
        "Quit",
        APPLICATION_COMMAND_QUIT,
        NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED);
    nixbench_application_menu_v1_append_menu(menu, "Settings");
    nixbench_application_menu_v1_append_item(
        menu,
        "Show seconds",
        APPLICATION_COMMAND_SHOW_SECONDS,
        show_seconds_flags);
    nixbench_application_menu_v1_append_item(
        menu,
        "Unavailable",
        APPLICATION_COMMAND_DISABLED,
        0);
    nixbench_application_menu_v1_commit(menu);
}

static void bind_seat_after_gtk_globals(struct client_state *state,
                                        struct wl_registry *registry)
{
    uint32_t bind_version;

    if (state->seat != NULL || state->seat_global_name == 0) {
        return;
    }
    if (state->compositor == NULL ||
        state->data_device_manager == NULL) {
        if (state->data_device_manager == NULL) {
            state->seat_waited_for_data_device_manager = true;
        }
        return;
    }
    bind_version = state->seat_version < 5 ? state->seat_version : 5;
    state->seat = wl_registry_bind(registry,
                                   state->seat_global_name,
                                   &wl_seat_interface,
                                   bind_version);
}

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
    struct client_state *state = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        const uint32_t bind_version = version < 4 ? version : 4;

        state->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, bind_version);
        bind_seat_after_gtk_globals(state, registry);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(
            registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        state->subcompositor = wl_registry_bind(
            registry, name, &wl_subcompositor_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        const uint32_t bind_version = version < 2 ? version : 2;

        state->output_global_name = name;
        state->output_global_version = version;
        state->output.proxy = wl_registry_bind(
            registry, name, &wl_output_interface, bind_version);
        if (state->output.proxy != NULL &&
            wl_output_add_listener(state->output.proxy,
                                   &output_listener,
                                   &state->output) < 0) {
            wl_output_destroy(state->output.proxy);
            state->output.proxy = NULL;
        }
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat_version = version;
        state->seat_global_name = name;
        bind_seat_after_gtk_globals(state, registry);
    } else if (strcmp(interface,
                      wl_data_device_manager_interface.name) == 0) {
        const uint32_t bind_version = version < 1 ? version : 1;

        state->data_device_manager_version = version;
        state->data_device_manager = wl_registry_bind(
            registry,
            name,
            &wl_data_device_manager_interface,
            bind_version);
        bind_seat_after_gtk_globals(state, registry);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface, 1);
    } else if (strcmp(
                   interface,
                   nixbench_application_menu_manager_v1_interface.name) ==
               0) {
        state->application_menu_manager = wl_registry_bind(
            registry,
            name,
            &nixbench_application_menu_manager_v1_interface,
            1);
    } else if (strcmp(
                   interface,
                   nixbench_html_theme_manager_v1_interface.name) == 0) {
        state->html_theme_manager = wl_registry_bind(
            registry,
            name,
            &nixbench_html_theme_manager_v1_interface,
            1);
    }
}

static void registry_global_remove(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove
};

static bool bind_late_output(struct client_state *state,
                             struct wl_registry *registry)
{
    const uint32_t bind_version =
        state->output_global_version < 1
            ? state->output_global_version
            : 1;

    if (state->output_global_name == 0 || bind_version == 0 ||
        state->late_output.proxy != NULL) {
        return false;
    }
    state->late_output.proxy = wl_registry_bind(
        registry,
        state->output_global_name,
        &wl_output_interface,
        bind_version);
    if (state->late_output.proxy == NULL) {
        return false;
    }
    if (wl_output_add_listener(state->late_output.proxy,
                               &output_listener,
                               &state->late_output) < 0) {
        wl_output_destroy(state->late_output.proxy);
        state->late_output.proxy = NULL;
        return false;
    }
    return true;
}

static void shm_format(void *data,
                       struct wl_shm *shm,
                       uint32_t format)
{
    struct client_state *state = data;

    (void)shm;
    if (format == WL_SHM_FORMAT_ARGB8888) {
        state->saw_argb8888 = true;
    }
}

static const struct wl_shm_listener shm_listener = {
    .format = shm_format
};

static void wm_base_ping(void *data,
                         struct xdg_wm_base *wm_base,
                         uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping
};

static void xdg_surface_configure(void *data,
                                  struct xdg_surface *xdg_surface,
                                  uint32_t serial)
{
    struct client_state *state = data;

    state->configure_serial = serial;
    ++state->surface_configure_count;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure
};

static void popup_surface_configure(void *data,
                                    struct xdg_surface *xdg_surface,
                                    uint32_t serial)
{
    struct popup_state *state = data;

    state->configure_serial = serial;
    ++state->surface_configure_count;
    state->surface_configure_sequence = ++state->event_sequence;
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener popup_surface_listener = {
    .configure = popup_surface_configure
};

static void popup_configure(void *data,
                            struct xdg_popup *popup,
                            int32_t x,
                            int32_t y,
                            int32_t width,
                            int32_t height)
{
    struct popup_state *state = data;

    (void)popup;
    state->configured_x = x;
    state->configured_y = y;
    state->configured_width = width;
    state->configured_height = height;
    ++state->popup_configure_count;
    state->popup_configure_sequence = ++state->event_sequence;
}

static void popup_done(void *data, struct xdg_popup *popup)
{
    struct popup_state *state = data;

    (void)popup;
    ++state->done_count;
}

static const struct xdg_popup_listener popup_listener = {
    .configure = popup_configure,
    .popup_done = popup_done
};

static void toplevel_configure(void *data,
                               struct xdg_toplevel *toplevel,
                               int32_t width,
                               int32_t height,
                               struct wl_array *states)
{
    struct client_state *state = data;

    (void)toplevel;
    (void)states;
    state->configured_width = width;
    state->configured_height = height;
    ++state->toplevel_configure_count;
}

static void toplevel_close(void *data,
                           struct xdg_toplevel *toplevel)
{
    struct client_state *state = data;

    (void)toplevel;
    state->close_requested = true;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close
};

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    struct client_state *state = data;

    (void)buffer;
    state->buffer_released = true;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release
};

static void frame_done(void *data,
                       struct wl_callback *callback,
                       uint32_t milliseconds)
{
    struct client_state *state = data;

    state->frame_done = true;
    state->frame_milliseconds = milliseconds;
    state->frame_callback = NULL;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done
};

static void fill_pixels(uint32_t *pixels, size_t pixel_count)
{
    size_t index;

    for (index = 0; index < pixel_count; ++index) {
        const uint32_t red = (uint32_t)((index * 37U) & 0xffU);
        const uint32_t green = (uint32_t)((index * 17U) & 0xffU);
        const uint32_t blue = (uint32_t)((index * 7U) & 0xffU);

        pixels[index] = UINT32_C(0xff000000) |
                        (red << 16) | (green << 8) | blue;
    }
}

static void test_menu_source_reservation(void)
{
    struct nb_shell shell;
    struct nb_wayland_server *server;
    const nb_menu_source_id last_valid_base =
        UINT64_MAX - NB_WAYLAND_MAX_SURFACES;

    nb_shell_init(&shell, DESKTOP_MENU_SOURCE, &empty_menu_model);
    CHECK(nb_wayland_server_create(&shell,
                                   NB_MENU_SOURCE_NONE,
                                   &empty_menu_model,
                                   OUTPUT_WIDTH,
                                   OUTPUT_HEIGHT) == NULL);
    CHECK(nb_wayland_server_create(&shell,
                                   DESKTOP_MENU_SOURCE,
                                   &empty_menu_model,
                                   OUTPUT_WIDTH,
                                   OUTPUT_HEIGHT) == NULL);
    CHECK(nb_wayland_server_create(&shell,
                                   last_valid_base + UINT64_C(1),
                                   &empty_menu_model,
                                   OUTPUT_WIDTH,
                                   OUTPUT_HEIGHT) == NULL);
    server = nb_wayland_server_create(&shell,
                                      last_valid_base,
                                      &empty_menu_model,
                                      OUTPUT_WIDTH,
                                      OUTPUT_HEIGHT);
    CHECK(server != NULL);
    nb_wayland_server_destroy(server);

    CHECK(nb_shell_open_window(&shell,
                               "Reserved source",
                               (struct nb_rect){20, 40, 200, 120},
                               WAYLAND_MENU_SOURCE + 2,
                               &empty_menu_model) != NB_WINDOW_ID_NONE);
    CHECK(nb_wayland_server_create(&shell,
                                   WAYLAND_MENU_SOURCE,
                                   &empty_menu_model,
                                   OUTPUT_WIDTH,
                                   OUTPUT_HEIGHT) == NULL);
}

static void test_wayland_surface_lifecycle(void)
{
    struct nb_shell shell;
    struct nb_wayland_server *server = NULL;
    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;
    struct wl_surface *surface = NULL;
    struct wl_surface *html_theme_surface = NULL;
    struct wl_surface *popup_surface = NULL;
    struct wl_surface *child_popup_surface = NULL;
    struct wl_shm_pool *pool = NULL;
    struct wl_buffer *buffer = NULL;
    struct wl_buffer *html_theme_buffer = NULL;
    struct wl_buffer *popup_buffer = NULL;
    struct wl_data_source *drag_source = NULL;
    struct xdg_surface *popup_xdg_surface = NULL;
    struct xdg_surface *child_popup_xdg_surface = NULL;
    struct xdg_popup *popup = NULL;
    struct xdg_popup *child_popup = NULL;
    struct xdg_positioner *positioner = NULL;
    struct xdg_positioner *child_positioner = NULL;
    struct client_state client = {0};
    struct clipboard_owner_state clipboard_owner = {0};
    struct nb_wayland_xwayland_interface xwayland_interface = {0};
    struct client_state legacy_input = {0};
    struct popup_state popup_client = {0};
    struct popup_state child_popup_client = {0};
    struct nb_wayland_surface_snapshot snapshot;
    struct nb_wayland_html_theme_snapshot html_theme_snapshot;
    const struct nb_window *host_window;
    struct nb_window *resized_host_window;
    struct nb_rect content;
    struct nb_damage_region redraw_region;
    nb_window_id window = NB_WINDOW_ID_NONE;
    uint32_t parent_pixel_under_popup = 0;
    uint32_t first_html_theme_state_serial = 0;
    uint64_t root_revision_before_popup = 0;
    uint64_t popup_tree_revision = 0;
    int sockets[2] = {-1, -1};
    int clipboard_pipe[2] = {-1, -1};
    int shm_fd = -1;
    const char wayland_clipboard[] = "Wayland clipboard text";
    const char external_clipboard[] = "X11 clipboard text";
    const char html_theme_token[] =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    char clipboard_buffer[64];
    const char *cached_clipboard = NULL;
    size_t cached_clipboard_size = 0;
    uint32_t *pixels = MAP_FAILED;
    const size_t pixel_count =
        (size_t)INITIAL_WIDTH * (size_t)INITIAL_HEIGHT;
    const size_t buffer_size = pixel_count * sizeof(*pixels);
    char shm_path[] = "/tmp/nixbench-wayland-test-XXXXXX";

    nb_shell_init(&shell, DESKTOP_MENU_SOURCE, &empty_menu_model);
    server = nb_wayland_server_create(&shell,
                                      WAYLAND_MENU_SOURCE,
                                      &empty_menu_model,
                                      OUTPUT_WIDTH,
                                      OUTPUT_HEIGHT);
    REQUIRE(server != NULL);
    CHECK(!nb_wayland_server_enable_html_theme(
        server, "short", "motif", "/tmp/nixbench-test-theme"));
    REQUIRE(nb_wayland_server_enable_html_theme(
        server,
        html_theme_token,
        "motif",
        "/tmp/nixbench-test-theme"));
    CHECK(nb_wayland_server_event_descriptor(NULL) == -1);
    CHECK(nb_wayland_server_event_descriptor(server) >= 0);
    CHECK(!nb_wayland_server_take_redraw(server));
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    REQUIRE(add_server_client(server, sockets[0]));
    sockets[0] = -1;

    display = wl_display_connect_to_fd(sockets[1]);
    sockets[1] = -1;
    REQUIRE(display != NULL);
    registry = wl_display_get_registry(display);
    REQUIRE(registry != NULL);
    REQUIRE(wl_registry_add_listener(registry,
                                     &registry_listener,
                                     &client) == 0);

    /* First fence receives globals; the second processes bind requests. */
    REQUIRE(pump_barrier(server, display));
    REQUIRE(client.compositor != NULL);
    REQUIRE(client.shm != NULL);
    REQUIRE(client.output.proxy != NULL);
    REQUIRE(client.seat != NULL);
    REQUIRE(client.data_device_manager != NULL);
    REQUIRE(client.wm_base != NULL);
    REQUIRE(client.application_menu_manager != NULL);
    REQUIRE(client.html_theme_manager != NULL);
    CHECK(client.output_global_version == 2);
    CHECK(wl_output_get_version(client.output.proxy) == 2);
    CHECK(client.seat_version >= 5);
    CHECK(wl_seat_get_version(client.seat) == 5);
    CHECK(client.data_device_manager_version == 1);
    CHECK(wl_data_device_manager_get_version(
              client.data_device_manager) == 1);
    CHECK(client.seat_waited_for_data_device_manager);
    REQUIRE(wl_shm_add_listener(client.shm, &shm_listener, &client) == 0);
    REQUIRE(wl_seat_add_listener(client.seat,
                                 &seat_listener,
                                 &client) == 0);
    REQUIRE(xdg_wm_base_add_listener(client.wm_base,
                                     &wm_base_listener,
                                     &client) == 0);
    client.data_device = wl_data_device_manager_get_data_device(
        client.data_device_manager,
        client.seat);
    REQUIRE(client.data_device != NULL);
    REQUIRE(wl_data_device_get_version(client.data_device) == 1);
    REQUIRE(wl_data_device_add_listener(client.data_device,
                                        &data_device_listener,
                                        &client) == 0);
    client.data_source = wl_data_device_manager_create_data_source(
        client.data_device_manager);
    REQUIRE(client.data_source != NULL);
    REQUIRE(wl_data_source_get_version(client.data_source) == 1);
    REQUIRE(wl_data_source_add_listener(client.data_source,
                                        &data_source_listener,
                                        &client) == 0);
    wl_data_source_offer(client.data_source,
                         "text/plain;charset=utf-8");
    wl_data_device_set_selection(client.data_device,
                                 client.data_source,
                                 UINT32_C(0));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.saw_argb8888);
    CHECK(client.output.geometry_count == 1);
    CHECK(client.output.geometry_x == 0);
    CHECK(client.output.geometry_y == 0);
    CHECK(client.output.physical_width == 0);
    CHECK(client.output.physical_height == 0);
    CHECK(client.output.subpixel == WL_OUTPUT_SUBPIXEL_UNKNOWN);
    CHECK(strcmp(client.output.make, "NixBench") == 0);
    CHECK(strcmp(client.output.model, "Hosted output") == 0);
    CHECK(client.output.transform == WL_OUTPUT_TRANSFORM_NORMAL);
    CHECK(client.output.mode_count == 1);
    CHECK((client.output.mode_flags & WL_OUTPUT_MODE_CURRENT) != 0);
    CHECK((client.output.mode_flags & WL_OUTPUT_MODE_PREFERRED) != 0);
    CHECK(client.output.mode_width == OUTPUT_WIDTH);
    CHECK(client.output.mode_height == OUTPUT_HEIGHT);
    CHECK(client.output.refresh == 60000);
    CHECK(client.output.scale_count == 1);
    CHECK(client.output.scale == 1);
    CHECK(client.output.done_count == 1);
    CHECK(client.seat_capability_count == 1);
    CHECK((client.seat_capabilities & WL_SEAT_CAPABILITY_POINTER) != 0);
    CHECK((client.seat_capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0);
    CHECK(client.seat_name_count == 1);
    CHECK(strcmp(client.seat_name, "nixbench-seat0") == 0);
    CHECK(client.seat_name_sequence < client.seat_capability_sequence);
    CHECK(client.data_source_cancelled_count == 1);
    REQUIRE(client.pointer != NULL);
    REQUIRE(client.keyboard != NULL);
    wl_data_device_set_selection(client.data_device, NULL, UINT32_C(0));
    wl_data_source_destroy(client.data_source);
    client.data_source = NULL;
    /* Process the input resources queued by the capability callback. */
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_keymap_count == 1);
    CHECK(client.keyboard_keymap_format ==
          WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
    CHECK(client.keyboard_keymap_size > 1);
    CHECK(client.keyboard_keymap_mapped);
    CHECK(client.keyboard_keymap_terminated);
    CHECK(client.keyboard_keymap_has_header);
    CHECK(client.keyboard_keycodes_valid);
    CHECK(client.keyboard_key_a_is_lowercase);
    CHECK(client.keyboard_shift_a_is_uppercase);
    CHECK(client.keyboard_pc_xt_profile_resolves);
    CHECK(client.keyboard_repeat_count == 1);
    CHECK(client.keyboard_repeat_rate == 25);
    CHECK(client.keyboard_repeat_delay == 600);

    surface = wl_compositor_create_surface(client.compositor);
    REQUIRE(surface != NULL);
    REQUIRE(wl_surface_add_listener(surface,
                                    &surface_listener,
                                    &client) == 0);
    drag_source = wl_data_device_manager_create_data_source(
        client.data_device_manager);
    REQUIRE(drag_source != NULL);
    REQUIRE(wl_data_source_add_listener(drag_source,
                                        &data_source_listener,
                                        &client) == 0);
    wl_data_source_offer(drag_source, "text/uri-list");
    wl_data_device_start_drag(client.data_device,
                              drag_source,
                              surface,
                              NULL,
                              UINT32_C(0));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.data_source_cancelled_count == 2);
    wl_data_source_destroy(drag_source);
    drag_source = NULL;
    client.xdg_surface = xdg_wm_base_get_xdg_surface(client.wm_base,
                                                     surface);
    REQUIRE(client.xdg_surface != NULL);
    REQUIRE(xdg_surface_add_listener(client.xdg_surface,
                                     &xdg_surface_listener,
                                     &client) == 0);
    client.toplevel = xdg_surface_get_toplevel(client.xdg_surface);
    REQUIRE(client.toplevel != NULL);
    REQUIRE(xdg_toplevel_add_listener(client.toplevel,
                                      &toplevel_listener,
                                      &client) == 0);
    xdg_toplevel_set_title(client.toplevel, "Wayland Test Window");
    xdg_toplevel_set_app_id(client.toplevel, "org.nixbench.Test");
    client.application_menu =
        nixbench_application_menu_manager_v1_get_menu(
            client.application_menu_manager,
            surface);
    REQUIRE(client.application_menu != NULL);
    REQUIRE(nixbench_application_menu_v1_add_listener(
                client.application_menu,
                &application_menu_listener,
                &client) == 0);
    publish_application_menu(client.application_menu, true);

    /* A bufferless initial commit requests the compositor configure. */
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.toplevel_configure_count == 1);
    CHECK(client.surface_configure_count == 1);
    CHECK(client.configure_serial != 0);
    CHECK(client.configured_width == 0);
    CHECK(client.configured_height == 0);
    CHECK(nb_wayland_server_window_count(server) == 0);
    CHECK(!nb_wayland_server_take_redraw(server));

    /* The configure handler queued ack_configure; process it before attach. */
    REQUIRE(pump_barrier(server, display));

    shm_fd = mkstemp(shm_path);
    REQUIRE(shm_fd >= 0);
    REQUIRE(unlink(shm_path) == 0);
    REQUIRE(ftruncate(shm_fd, (off_t)buffer_size) == 0);
    pixels = mmap(NULL,
                  buffer_size,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm_fd,
                  0);
    REQUIRE(pixels != MAP_FAILED);
    fill_pixels(pixels, pixel_count);
    pool = wl_shm_create_pool(client.shm,
                              shm_fd,
                              (int32_t)buffer_size);
    REQUIRE(pool != NULL);
    buffer = wl_shm_pool_create_buffer(pool,
                                       0,
                                       INITIAL_WIDTH,
                                       INITIAL_HEIGHT,
                                       INITIAL_WIDTH * BYTES_PER_PIXEL,
                                       WL_SHM_FORMAT_ARGB8888);
    REQUIRE(buffer != NULL);
    REQUIRE(wl_buffer_add_listener(buffer,
                                   &buffer_listener,
                                   &client) == 0);

    client.frame_callback = wl_surface_frame(surface);
    REQUIRE(client.frame_callback != NULL);
    REQUIRE(wl_callback_add_listener(client.frame_callback,
                                     &frame_listener,
                                     &client) == 0);
    xdg_surface_set_window_geometry(client.xdg_surface,
                                    0,
                                    0,
                                    INITIAL_WIDTH,
                                    INITIAL_HEIGHT);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, INITIAL_WIDTH, INITIAL_HEIGHT);
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));

    CHECK(client.buffer_released);
    CHECK(!client.frame_done);
    CHECK(nb_wayland_server_window_count(server) == 1);
    CHECK(nb_wayland_server_take_redraw(server));
    CHECK(!nb_wayland_server_take_redraw(server));
    CHECK(client.surface_enter_count == 1);
    CHECK(client.initial_output_enter_count == 1);
    CHECK(client.surface_enter_output == client.output.proxy);
    CHECK(client.surface_leave_count == 0);
    window = nb_wayland_server_window_at(server, 0);
    REQUIRE(window != NB_WINDOW_ID_NONE);
    CHECK(nb_wayland_server_owns_window(server, window));

    /*
     * The authenticated browser atlas carries pixels and atomic metadata but
     * never appears as a managed application window.
     */
    html_theme_surface = wl_compositor_create_surface(client.compositor);
    REQUIRE(html_theme_surface != NULL);
    client.html_theme_atlas =
        nixbench_html_theme_manager_v1_register_atlas(
            client.html_theme_manager,
            html_theme_surface,
            html_theme_token);
    REQUIRE(client.html_theme_atlas != NULL);
    REQUIRE(nixbench_html_theme_atlas_v1_add_listener(
                client.html_theme_atlas,
                &html_theme_atlas_listener,
                &client) == 0);
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_html_theme_connected(server));
    CHECK(nb_wayland_server_window_count(server) == 1);
    CHECK(client.html_theme_configure_count == 1);
    CHECK(client.html_theme_window_count == 1);
    CHECK(client.html_theme_state_done_count == 1);
    first_html_theme_state_serial = client.html_theme_state_serial;
    nb_wayland_server_html_theme_state_changed(server);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.html_theme_configure_count == 2);
    CHECK(client.html_theme_state_serial != first_html_theme_state_serial);
    CHECK(client.html_theme_window_count == 1);
    CHECK(client.html_theme_state_done_count == 2);
    nixbench_html_theme_atlas_v1_ack_state(
        client.html_theme_atlas,
        first_html_theme_state_serial);
    nixbench_html_theme_atlas_v1_ack_state(
        client.html_theme_atlas,
        client.html_theme_state_serial);
    REQUIRE(pump_barrier(server, display));

    /*
     * A shell update can overtake an atlas transaction at either side of its
     * begin request. Both obsolete layouts must be drained without turning a
     * normal renderer/compositor race into a fatal protocol error.
     */
    nb_wayland_server_html_theme_state_changed(server);
    nixbench_html_theme_atlas_v1_begin_layout(
        client.html_theme_atlas, 0, 42, 64, 32);
    nixbench_html_theme_atlas_v1_tile(
        client.html_theme_atlas,
        NIXBENCH_HTML_THEME_ATLAS_V1_TILE_KIND_WINDOW,
        (uint32_t)((uint64_t)window >> 32U),
        (uint32_t)window,
        0,
        0,
        64,
        32);
    nixbench_html_theme_atlas_v1_commit_layout(
        client.html_theme_atlas, 0, 42);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.html_theme_configure_count == 3);
    nixbench_html_theme_atlas_v1_ack_state(
        client.html_theme_atlas,
        client.html_theme_state_serial);
    REQUIRE(pump_barrier(server, display));

    nixbench_html_theme_atlas_v1_begin_layout(
        client.html_theme_atlas, 0, 43, 64, 32);
    REQUIRE(pump_barrier(server, display));
    nb_wayland_server_html_theme_state_changed(server);
    nixbench_html_theme_atlas_v1_tile(
        client.html_theme_atlas,
        NIXBENCH_HTML_THEME_ATLAS_V1_TILE_KIND_WINDOW,
        (uint32_t)((uint64_t)window >> 32U),
        (uint32_t)window,
        0,
        0,
        64,
        32);
    nixbench_html_theme_atlas_v1_commit_layout(
        client.html_theme_atlas, 0, 43);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.html_theme_configure_count == 4);
    nixbench_html_theme_atlas_v1_ack_state(
        client.html_theme_atlas,
        client.html_theme_state_serial);
    REQUIRE(pump_barrier(server, display));

    html_theme_buffer = wl_shm_pool_create_buffer(
        pool,
        0,
        64,
        32,
        64 * BYTES_PER_PIXEL,
        WL_SHM_FORMAT_ARGB8888);
    REQUIRE(html_theme_buffer != NULL);
    nixbench_html_theme_atlas_v1_begin_layout(
        client.html_theme_atlas, 0, 1, 64, 32);
    wl_surface_attach(html_theme_surface, html_theme_buffer, 0, 0);
    wl_surface_damage(html_theme_surface, 0, 0, 64, 32);
    wl_surface_commit(html_theme_surface);
    nixbench_html_theme_atlas_v1_tile(
        client.html_theme_atlas,
        NIXBENCH_HTML_THEME_ATLAS_V1_TILE_KIND_WINDOW,
        (uint32_t)((uint64_t)window >> 32U),
        (uint32_t)window,
        0,
        0,
        64,
        32);
    nixbench_html_theme_atlas_v1_action_region(
        client.html_theme_atlas,
        NIXBENCH_HTML_THEME_ATLAS_V1_TILE_KIND_WINDOW,
        (uint32_t)((uint64_t)window >> 32U),
        (uint32_t)window,
        NIXBENCH_HTML_THEME_ATLAS_V1_ACTION_CLOSE,
        48,
        4,
        12,
        12);
    nixbench_html_theme_atlas_v1_commit_layout(
        client.html_theme_atlas, 0, 1);
    REQUIRE(pump_barrier(server, display));
    REQUIRE(nb_wayland_server_html_theme_snapshot(
        server, &html_theme_snapshot));
    CHECK(html_theme_snapshot.surface.width == 64);
    CHECK(html_theme_snapshot.surface.height == 32);
    REQUIRE(html_theme_snapshot.layout != NULL);
    CHECK(html_theme_snapshot.layout->generation == 1);
    CHECK(nb_theme_atlas_find_tile(html_theme_snapshot.layout,
                                   NB_THEME_TILE_WINDOW,
                                   (uint64_t)window) != NULL);
    CHECK(nb_theme_atlas_hit_test(html_theme_snapshot.layout,
                                  NB_THEME_TILE_WINDOW,
                                  (uint64_t)window,
                                  50,
                                  8) == NB_THEME_ACTION_CLOSE);
    CHECK(nb_wayland_server_take_redraw(server));
    CHECK(!nb_wayland_server_take_redraw(server));

    CHECK(nb_shell_toggle_window_minimized(&shell, window));
    CHECK(nb_desktop_find_window(&shell.desktop, window)->minimized);
    nb_wayland_server_frame_presented(server, UINT32_C(4241));
    REQUIRE(pump_barrier(server, display));
    CHECK(!client.frame_done);
    CHECK(nb_shell_toggle_window_minimized(&shell, window));
    CHECK(!nb_desktop_find_window(&shell.desktop, window)->minimized);
    CHECK(shell.active_menu_source == WAYLAND_MENU_SOURCE + 1);
    REQUIRE(shell.menu.model != NULL);
    CHECK(shell.menu.model->menu_count == 2);
    CHECK(strcmp(shell.menu.model->menus[0].label, "NixClock") == 0);
    CHECK(strcmp(shell.menu.model->menus[0].items[0].label, "Quit") == 0);
    CHECK(strcmp(shell.menu.model->menus[1].label, "Settings") == 0);
    CHECK(strcmp(shell.menu.model->menus[1].items[0].label,
                 "Show seconds") == 0);
    CHECK(shell.menu.model->menus[1].items[0].checked);
    CHECK(!shell.menu.model->menus[1].items[1].enabled);
    CHECK(!nb_wayland_server_dispatch_menu_command(
        NULL,
        window,
        shell.active_menu_source,
        APPLICATION_COMMAND_QUIT));
    CHECK(!nb_wayland_server_dispatch_menu_command(
        server,
        NB_WINDOW_ID_NONE,
        shell.active_menu_source,
        APPLICATION_COMMAND_QUIT));
    CHECK(!nb_wayland_server_dispatch_menu_command(
        server,
        window,
        NB_MENU_SOURCE_NONE,
        APPLICATION_COMMAND_QUIT));
    CHECK(!nb_wayland_server_dispatch_menu_command(
        server,
        window,
        shell.active_menu_source,
        NB_MENU_COMMAND_NONE));
    CHECK(!nb_wayland_server_dispatch_menu_command(
        server,
        window,
        shell.active_menu_source,
        UINT32_C(999)));
    CHECK(!nb_wayland_server_dispatch_menu_command(
        server,
        window,
        shell.active_menu_source,
        APPLICATION_COMMAND_DISABLED));
    CHECK(nb_wayland_server_dispatch_menu_command(
        server,
        window,
        shell.active_menu_source,
        APPLICATION_COMMAND_SHOW_SECONDS));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.application_menu_command_count == 1);
    CHECK(client.application_menu_command ==
          APPLICATION_COMMAND_SHOW_SECONDS);

    nixbench_application_menu_v1_reset(client.application_menu);
    nixbench_application_menu_v1_append_menu(client.application_menu,
                                              "Pending");
    nixbench_application_menu_v1_append_item(
        client.application_menu,
        "Not committed",
        UINT32_C(99),
        NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED);
    REQUIRE(pump_barrier(server, display));
    REQUIRE(shell.menu.model != NULL);
    CHECK(shell.menu.model->menu_count == 2);
    CHECK(strcmp(shell.menu.model->menus[0].label, "NixClock") == 0);
    CHECK(shell.menu.model->menus[1].items[0].checked);

    nixbench_application_menu_v1_reset(client.application_menu);
    publish_application_menu(client.application_menu, false);
    REQUIRE(pump_barrier(server, display));
    REQUIRE(shell.menu.model != NULL);
    CHECK(shell.menu.model->menu_count == 2);
    CHECK(!shell.menu.model->menus[1].items[0].checked);
    CHECK(!nb_wayland_server_dispatch_menu_command(
        server,
        window,
        WAYLAND_MENU_SOURCE,
        APPLICATION_COMMAND_QUIT));
    CHECK(nb_wayland_server_dispatch_menu_command(
        server,
        window,
        shell.active_menu_source,
        APPLICATION_COMMAND_QUIT));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.application_menu_command_count == 2);
    CHECK(client.application_menu_command == APPLICATION_COMMAND_QUIT);
    REQUIRE(nb_wayland_server_surface_snapshot(server,
                                               window,
                                               &snapshot));
    CHECK(snapshot.width == INITIAL_WIDTH);
    CHECK(snapshot.height == INITIAL_HEIGHT);
    CHECK(snapshot.stride == INITIAL_WIDTH * BYTES_PER_PIXEL);
    CHECK(snapshot.revision == 1);
    CHECK(memcmp(snapshot.pixels, pixels, buffer_size) == 0);
    /* Discard menu-publication damage before isolating the hidden commit. */
    (void)nb_wayland_server_take_redraw(server);
    CHECK(!nb_wayland_server_take_redraw(server));
    CHECK(nb_shell_toggle_window_minimized(&shell, window));
    client.buffer_released = false;
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, INITIAL_WIDTH, INITIAL_HEIGHT);
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.buffer_released);
    CHECK(!nb_wayland_server_take_redraw(server));
    CHECK(nb_shell_toggle_window_minimized(&shell, window));
    REQUIRE(nb_wayland_server_surface_snapshot(server,
                                               window,
                                               &snapshot));
    CHECK(snapshot.revision == 2);
    host_window = nb_desktop_find_window(&shell.desktop, window);
    REQUIRE(host_window != NULL);
    content = nb_window_content_rect(host_window);
    {
        const size_t damaged_index =
            (size_t)12 * (size_t)INITIAL_WIDTH + (size_t)10;
        const size_t second_damaged_index =
            (size_t)40 * (size_t)INITIAL_WIDTH + (size_t)50;
        const uint32_t retained_pixel = snapshot.pixels[0];

        pixels[0] ^= UINT32_C(0x00ffffff);
        pixels[damaged_index] = UINT32_C(0xff123456);
        pixels[second_damaged_index] = UINT32_C(0xff654321);
        client.buffer_released = false;
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage(surface, 10, 12, 3, 2);
        wl_surface_damage(surface, 50, 40, 2, 3);
        wl_surface_commit(surface);
        REQUIRE(pump_barrier(server, display));
        CHECK(client.buffer_released);
        CHECK(nb_wayland_server_take_redraw_region(server,
                                                    &redraw_region));
        CHECK(!redraw_region.full);
        CHECK(redraw_region.count == 2);
        CHECK(redraw_region.rects[0].x == content.x);
        CHECK(redraw_region.rects[0].y == content.y);
        CHECK(redraw_region.rects[0].width == 32);
        CHECK(redraw_region.rects[0].height == 32);
        CHECK(redraw_region.rects[1].x == content.x + 32);
        CHECK(redraw_region.rects[1].y == content.y + 32);
        CHECK(redraw_region.rects[1].width == 32);
        CHECK(redraw_region.rects[1].height == 32);
        CHECK(!nb_wayland_server_take_redraw(server));
        REQUIRE(nb_wayland_server_surface_snapshot(server,
                                                   window,
                                                   &snapshot));
        CHECK(snapshot.revision == 3);
        CHECK(snapshot.pixels[0] == retained_pixel);
        CHECK(snapshot.pixels[damaged_index] == UINT32_C(0xff123456));
        CHECK(snapshot.pixels[second_damaged_index] ==
              UINT32_C(0xff654321));
    }
    parent_pixel_under_popup =
        snapshot.pixels[(size_t)POPUP_EXPECTED_Y * (size_t)INITIAL_WIDTH +
                        (size_t)POPUP_EXPECTED_X];
    root_revision_before_popup = snapshot.revision;
    pixels[0] = 0;
    CHECK(snapshot.pixels[0] != pixels[0]);
    (void)nb_wayland_server_take_redraw(server);

    /*
     * A completed positioner is copied by get_popup.  Destroying it before
     * the popup's initial commit must not affect the resulting configure.
     * The popup is part of the parent's surface tree, not another decorated
     * NixBench shell window.
     */
    popup_surface = wl_compositor_create_surface(client.compositor);
    REQUIRE(popup_surface != NULL);
    popup_xdg_surface = xdg_wm_base_get_xdg_surface(client.wm_base,
                                                    popup_surface);
    REQUIRE(popup_xdg_surface != NULL);
    REQUIRE(xdg_surface_add_listener(popup_xdg_surface,
                                     &popup_surface_listener,
                                     &popup_client) == 0);
    positioner = xdg_wm_base_create_positioner(client.wm_base);
    REQUIRE(positioner != NULL);
    xdg_positioner_set_size(positioner, POPUP_WIDTH, POPUP_HEIGHT);
    xdg_positioner_set_anchor_rect(positioner, 24, 36, 120, 28);
    xdg_positioner_set_anchor(positioner,
                              XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
    xdg_positioner_set_gravity(positioner,
                               XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_constraint_adjustment(
        positioner,
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
            XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
    xdg_positioner_set_offset(positioner, 3, 5);
    popup = xdg_surface_get_popup(popup_xdg_surface,
                                  client.xdg_surface,
                                  positioner);
    REQUIRE(popup != NULL);
    REQUIRE(xdg_popup_add_listener(popup,
                                   &popup_listener,
                                   &popup_client) == 0);
    /* GTK requests its completion/menu grab before the initial commit. */
    xdg_popup_grab(popup, client.seat, UINT32_C(1));
    xdg_positioner_destroy(positioner);
    positioner = NULL;

    wl_surface_commit(popup_surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(popup_client.popup_configure_count == 1);
    CHECK(popup_client.surface_configure_count == 1);
    CHECK(popup_client.popup_configure_sequence != 0);
    CHECK(popup_client.popup_configure_sequence <
          popup_client.surface_configure_sequence);
    CHECK(popup_client.configure_serial != 0);
    CHECK(popup_client.configured_x == POPUP_EXPECTED_X);
    CHECK(popup_client.configured_y == POPUP_EXPECTED_Y);
    CHECK(popup_client.configured_width == POPUP_WIDTH);
    CHECK(popup_client.configured_height == POPUP_HEIGHT);
    CHECK(popup_client.done_count == 0);
    CHECK(nb_wayland_server_window_count(server) == 1);
    CHECK(!nb_wayland_server_take_redraw(server));

    /* Process the popup ack before attaching its first shared-memory buffer. */
    REQUIRE(pump_barrier(server, display));
    popup_buffer = wl_shm_pool_create_buffer(
        pool,
        0,
        POPUP_WIDTH,
        POPUP_HEIGHT,
        POPUP_WIDTH * BYTES_PER_PIXEL,
        WL_SHM_FORMAT_ARGB8888);
    REQUIRE(popup_buffer != NULL);
    REQUIRE(wl_buffer_add_listener(popup_buffer,
                                   &buffer_listener,
                                   &client) == 0);
    {
        const size_t popup_pixel_count =
            (size_t)POPUP_WIDTH * (size_t)POPUP_HEIGHT;
        size_t pixel_index;

        for (pixel_index = 0; pixel_index < popup_pixel_count;
             ++pixel_index) {
            pixels[pixel_index] = UINT32_C(0xff386ca8);
        }
    }
    client.buffer_released = false;
    xdg_surface_set_window_geometry(popup_xdg_surface,
                                    0,
                                    0,
                                    POPUP_WIDTH,
                                    POPUP_HEIGHT);
    wl_surface_attach(popup_surface, popup_buffer, 0, 0);
    wl_surface_damage(popup_surface, 0, 0, POPUP_WIDTH, POPUP_HEIGHT);
    wl_surface_commit(popup_surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.buffer_released);
    CHECK(nb_wayland_server_window_count(server) == 1);
    CHECK(nb_wayland_server_window_at(server, 0) == window);
    CHECK(nb_wayland_server_owns_window(server, window));
    CHECK(nb_wayland_server_take_redraw(server));
    CHECK(!nb_wayland_server_take_redraw(server));
    REQUIRE(nb_wayland_server_surface_snapshot(server,
                                               window,
                                               &snapshot));
    CHECK(snapshot.width == INITIAL_WIDTH);
    CHECK(snapshot.height == INITIAL_HEIGHT);
    CHECK(snapshot.pixels[
              (size_t)POPUP_EXPECTED_Y * (size_t)INITIAL_WIDTH +
              (size_t)POPUP_EXPECTED_X] == UINT32_C(0xff386ca8));
    CHECK(snapshot.revision > root_revision_before_popup);
    popup_tree_revision = snapshot.revision;

    /*
     * A popup is composited into its parent's NixBench window, but pointer
     * focus and button grabs belong to the popup's own wl_surface.  This is
     * the path used by GTK context menus.
     */
    CHECK(nb_wayland_server_pointer_motion(
        server,
        window,
        content.x + POPUP_EXPECTED_X + 10,
        content.y + POPUP_EXPECTED_Y + 11,
        UINT32_C(900)));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_enter_count == 1);
    CHECK(client.pointer_enter_surface == popup_surface);
    CHECK(client.pointer_enter_x == wl_fixed_from_int(10));
    CHECK(client.pointer_enter_y == wl_fixed_from_int(11));
    CHECK(nb_wayland_server_pointer_button(
        server,
        window,
        content.x + POPUP_EXPECTED_X + 10,
        content.y + POPUP_EXPECTED_Y + 11,
        UINT32_C(901),
        NB_WAYLAND_POINTER_BUTTON_LEFT,
        true));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_button_count == 1);
    CHECK(client.pointer_button == WAYLAND_POINTER_BUTTON_LEFT);
    CHECK(client.pointer_button_state == WL_POINTER_BUTTON_STATE_PRESSED);
    CHECK(nb_wayland_server_pointer_grab_window(server) == window);
    CHECK(nb_wayland_server_pointer_button(
        server,
        window,
        content.x + POPUP_EXPECTED_X + 10,
        content.y + POPUP_EXPECTED_Y + 11,
        UINT32_C(902),
        NB_WAYLAND_POINTER_BUTTON_LEFT,
        false));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_button_count == 2);
    CHECK(client.pointer_button_state == WL_POINTER_BUTTON_STATE_RELEASED);
    CHECK(nb_wayland_server_pointer_grab_window(server) ==
          NB_WINDOW_ID_NONE);
    CHECK(nb_wayland_server_pointer_motion(server,
                                           window,
                                           content.x + INITIAL_WIDTH - 4,
                                           content.y + INITIAL_HEIGHT - 4,
                                           UINT32_C(903)));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_leave_count == 1);
    CHECK(client.pointer_leave_surface == popup_surface);
    CHECK(client.pointer_enter_count == 2);
    CHECK(client.pointer_enter_surface == surface);
    nb_wayland_server_pointer_cancel(server, UINT32_C(904));
    REQUIRE(pump_barrier(server, display));
    client.pointer_enter_surface = NULL;
    client.pointer_leave_surface = NULL;
    client.pointer_enter_count = 0;
    client.pointer_leave_count = 0;
    client.pointer_motion_count = 0;
    client.pointer_button_count = 0;
    client.pointer_frame_count = 0;

    /* Client-driven popup destruction unmaps only the child surface. */
    xdg_popup_destroy(popup);
    popup = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(popup_client.done_count == 0);
    CHECK(nb_wayland_server_window_count(server) == 1);
    CHECK(nb_wayland_server_window_at(server, 0) == window);
    CHECK(nb_wayland_server_owns_window(server, window));
    CHECK(nb_wayland_server_take_redraw(server));
    CHECK(!nb_wayland_server_take_redraw(server));
    REQUIRE(nb_wayland_server_surface_snapshot(server,
                                               window,
                                               &snapshot));
    CHECK(snapshot.pixels[
              (size_t)POPUP_EXPECTED_Y * (size_t)INITIAL_WIDTH +
              (size_t)POPUP_EXPECTED_X] == parent_pixel_under_popup);
    CHECK(snapshot.revision > popup_tree_revision);
    xdg_surface_destroy(popup_xdg_surface);
    popup_xdg_surface = NULL;
    wl_surface_destroy(popup_surface);
    popup_surface = NULL;
    wl_buffer_destroy(popup_buffer);
    popup_buffer = NULL;
    fill_pixels(pixels, pixel_count);
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 1);

    CHECK(!nb_wayland_server_set_output_size(NULL,
                                              OUTPUT_WIDTH,
                                              OUTPUT_HEIGHT));
    CHECK(!nb_wayland_server_set_output_size(server,
                                              0,
                                              OUTPUT_HEIGHT));
    CHECK(!nb_wayland_server_set_output_size(server,
                                              OUTPUT_WIDTH,
                                              -1));
    {
        const unsigned int previous_mode_count =
            client.output.mode_count;
        const unsigned int previous_done_count =
            client.output.done_count;

        CHECK(nb_wayland_server_set_output_size(server,
                                                 OUTPUT_WIDTH,
                                                 OUTPUT_HEIGHT));
        REQUIRE(pump_barrier(server, display));
        CHECK(client.output.mode_count == previous_mode_count);
        CHECK(client.output.done_count == previous_done_count);

        CHECK(nb_wayland_server_set_output_size(server, 1280, 720));
        REQUIRE(pump_barrier(server, display));
        CHECK(client.output.mode_count == previous_mode_count + 1);
        CHECK(client.output.done_count == previous_done_count + 1);
    }
    CHECK(client.output.geometry_count == 1);
    CHECK(client.output.scale_count == 1);
    CHECK(client.output.mode_width == 1280);
    CHECK(client.output.mode_height == 720);
    CHECK(client.output.refresh == 60000);
    CHECK((client.output.mode_flags & WL_OUTPUT_MODE_CURRENT) != 0);
    CHECK((client.output.mode_flags & WL_OUTPUT_MODE_PREFERRED) != 0);

    REQUIRE(bind_late_output(&client, registry));
    REQUIRE(pump_barrier(server, display));
    CHECK(wl_output_get_version(client.late_output.proxy) == 1);
    CHECK(client.late_output.geometry_count == 1);
    CHECK(strcmp(client.late_output.make, "NixBench") == 0);
    CHECK(strcmp(client.late_output.model, "Hosted output") == 0);
    CHECK(client.late_output.mode_count == 1);
    CHECK(client.late_output.mode_width == 1280);
    CHECK(client.late_output.mode_height == 720);
    CHECK(client.late_output.refresh == 60000);
    CHECK((client.late_output.mode_flags & WL_OUTPUT_MODE_CURRENT) != 0);
    CHECK((client.late_output.mode_flags & WL_OUTPUT_MODE_PREFERRED) != 0);
    CHECK(client.late_output.scale_count == 0);
    CHECK(client.late_output.done_count == 0);
    CHECK(client.surface_enter_count == 2);
    CHECK(client.initial_output_enter_count == 1);
    CHECK(client.late_output_enter_count == 1);
    CHECK(client.surface_enter_output == client.late_output.proxy);

    host_window = nb_desktop_find_window(&shell.desktop, window);
    REQUIRE(host_window != NULL);
    CHECK(strcmp(host_window->title, "Wayland Test Window") == 0);

    /* The host scales the committed buffer to a resized content area. */
    resized_host_window = (struct nb_window *)host_window;
    resized_host_window->frame.width =
        (2 * INITIAL_WIDTH) + (2 * NB_WINDOW_BORDER_WIDTH);
    resized_host_window->frame.height =
        (2 * INITIAL_HEIGHT) + (2 * NB_WINDOW_BORDER_WIDTH) +
        NB_WINDOW_TITLE_HEIGHT + NB_WINDOW_FOOTER_HEIGHT;
    content = nb_window_content_rect(host_window);
    CHECK(content.width == 2 * INITIAL_WIDTH);
    CHECK(content.height == 2 * INITIAL_HEIGHT);
    xdg_surface_set_window_geometry(client.xdg_surface,
                                    8,
                                    12,
                                    INITIAL_WIDTH,
                                    INITIAL_HEIGHT);
    REQUIRE(pump_barrier(server, display));

    CHECK(nb_wayland_server_pointer_motion(server,
                                           window,
                                           content.x + 280,
                                           content.y + 150,
                                           UINT32_C(1000)));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_enter_count == 1);
    CHECK(client.pointer_enter_surface == surface);
    CHECK(client.pointer_serial != 0);
    CHECK(client.pointer_enter_x == wl_fixed_from_int(148));
    CHECK(client.pointer_enter_y == wl_fixed_from_int(87));

    {
        const unsigned int previous_motion_count =
            client.pointer_motion_count;

        CHECK(nb_wayland_server_pointer_motion(server,
                                               window,
                                               content.x + 560,
                                               content.y + 300,
                                               UINT32_C(1010)));
        REQUIRE(pump_barrier(server, display));
        CHECK(client.pointer_motion_count == previous_motion_count + 1);
    }
    CHECK(client.pointer_time == UINT32_C(1010));
    CHECK(client.pointer_x == wl_fixed_from_int(288));
    CHECK(client.pointer_y == wl_fixed_from_int(162));
    CHECK(client.pointer_frame_count >= 2);

    CHECK(nb_wayland_server_pointer_button(
        server,
        window,
        content.x + 560,
        content.y + 300,
        UINT32_C(1020),
        NB_WAYLAND_POINTER_BUTTON_LEFT,
        true));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_button_count == 1);
    CHECK(client.pointer_serial != 0);
    CHECK(client.pointer_time == UINT32_C(1020));
    CHECK(client.pointer_button == WAYLAND_POINTER_BUTTON_LEFT);
    CHECK(client.pointer_button_state == WL_POINTER_BUTTON_STATE_PRESSED);
    CHECK(nb_wayland_server_pointer_grab_window(server) == window);

    {
        const unsigned int previous_motion_count =
            client.pointer_motion_count;

        CHECK(nb_wayland_server_pointer_motion(server,
                                               NB_WINDOW_ID_NONE,
                                               content.x - 112,
                                               content.y - 60,
                                               UINT32_C(1030)));
        REQUIRE(pump_barrier(server, display));
        CHECK(client.pointer_motion_count == previous_motion_count + 1);
    }
    CHECK(client.pointer_leave_count == 0);
    CHECK(client.pointer_time == UINT32_C(1030));
    CHECK(client.pointer_x == wl_fixed_from_int(-48));
    CHECK(client.pointer_y == wl_fixed_from_int(-18));
    CHECK(nb_wayland_server_pointer_grab_window(server) == window);

    CHECK(nb_wayland_server_pointer_button(
        server,
        NB_WINDOW_ID_NONE,
        content.x - 112,
        content.y - 60,
        UINT32_C(1040),
        NB_WAYLAND_POINTER_BUTTON_LEFT,
        false));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_button_count == 2);
    CHECK(client.pointer_time == UINT32_C(1040));
    CHECK(client.pointer_button == WAYLAND_POINTER_BUTTON_LEFT);
    CHECK(client.pointer_button_state == WL_POINTER_BUTTON_STATE_RELEASED);
    CHECK(client.pointer_leave_count == 1);
    CHECK(client.pointer_leave_surface == surface);
    CHECK(nb_wayland_server_pointer_grab_window(server) ==
          NB_WINDOW_ID_NONE);

    /* A multi-button grab lasts until every button has been released. */
    CHECK(nb_wayland_server_pointer_motion(server,
                                           window,
                                           content.x + 280,
                                           content.y + 150,
                                           UINT32_C(1041)));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_enter_count == 2);
    {
        const unsigned int previous_button_count =
            client.pointer_button_count;

        CHECK(nb_wayland_server_pointer_button(
            server,
            window,
            content.x + 280,
            content.y + 150,
            UINT32_C(1042),
            NB_WAYLAND_POINTER_BUTTON_LEFT,
            true));
        CHECK(nb_wayland_server_pointer_button(
            server,
            window,
            content.x + 280,
            content.y + 150,
            UINT32_C(1043),
            NB_WAYLAND_POINTER_BUTTON_RIGHT,
            true));
        CHECK(nb_wayland_server_pointer_button(
            server,
            NB_WINDOW_ID_NONE,
            content.x - 1,
            content.y - 1,
            UINT32_C(1044),
            NB_WAYLAND_POINTER_BUTTON_LEFT,
            false));
        REQUIRE(pump_barrier(server, display));
        CHECK(client.pointer_button_count == previous_button_count + 3);

        CHECK(nb_wayland_server_pointer_grab_window(server) == window);
        nb_wayland_server_pointer_cancel(server, UINT32_C(1045));
        REQUIRE(pump_barrier(server, display));
        CHECK(client.pointer_button_count == previous_button_count + 4);
    }
    CHECK(client.pointer_time == UINT32_C(1045));
    CHECK(client.pointer_button == WAYLAND_POINTER_BUTTON_RIGHT);
    CHECK(client.pointer_button_state == WL_POINTER_BUTTON_STATE_RELEASED);
    CHECK(client.pointer_leave_count == 2);
    CHECK(nb_wayland_server_pointer_grab_window(server) ==
          NB_WINDOW_ID_NONE);

    /* Keyboard focus carries the current pressed-key and modifier state. */
    CHECK(!nb_wayland_server_keyboard_key(server,
                                           NULL,
                                           UINT32_C(1100),
                                           true));
    CHECK(nb_wayland_server_keyboard_focus(server, window));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_enter_count == 1);
    CHECK(client.data_device_selection_count == 1);
    CHECK(client.selection_offer == NULL);
    CHECK(client.data_device_selection_sequence != 0);
    CHECK(client.data_device_selection_sequence <
          client.keyboard_enter_sequence);
    CHECK(client.keyboard_enter_surface == surface);
    CHECK(client.keyboard_serial != 0);
    CHECK(client.keyboard_enter_key_count == 0);
    CHECK(client.keyboard_modifiers_count == 1);
    CHECK(client.keyboard_mods_depressed == 0);
    CHECK(client.keyboard_mods_latched == 0);
    CHECK(client.keyboard_mods_locked == 0);
    CHECK(client.keyboard_group == 0);

    /* A focused client can own, offer, and transfer bounded text. */
    client.data_source = wl_data_device_manager_create_data_source(
        client.data_device_manager);
    REQUIRE(client.data_source != NULL);
    REQUIRE(wl_data_source_add_listener(client.data_source,
                                        &data_source_listener,
                                        &client) == 0);
    client.data_source_payload = wayland_clipboard;
    client.data_source_payload_size = sizeof(wayland_clipboard) - 1;
    wl_data_source_offer(client.data_source,
                         "text/plain;charset=utf-8");
    wl_data_device_set_selection(client.data_device,
                                 client.data_source,
                                 client.keyboard_serial);
    REQUIRE(pump_barrier(server, display));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.data_source_cancelled_count == 2);
    CHECK(client.data_device_selection_count == 2);
    REQUIRE(client.selection_offer != NULL);
    CHECK(client.data_offer_count == 1);
    CHECK(client.data_offer_mime_count == 1);
    CHECK(strcmp(client.data_offer_mime,
                 "text/plain;charset=utf-8") == 0);
    CHECK(client.data_source_send_count == 1);
    CHECK(nb_wayland_server_clipboard_text(server,
                                           &cached_clipboard,
                                           &cached_clipboard_size));
    CHECK(cached_clipboard_size == sizeof(wayland_clipboard) - 1);
    CHECK(memcmp(cached_clipboard,
                 wayland_clipboard,
                 cached_clipboard_size) == 0);

    /*
     * GTK claims the clipboard before appending SAVE_TARGETS, repeats text
     * aliases, and then repeats set_selection for the same source.  Late and
     * duplicate offers are valid and must not disconnect the client.
     */
    wl_data_source_offer(client.data_source, "SAVE_TARGETS");
    wl_data_source_offer(client.data_source,
                         "text/plain;charset=utf-8");
    wl_data_device_set_selection(client.data_device,
                                 client.data_source,
                                 client.keyboard_serial);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.data_offer_mime_count == 2);
    CHECK(strcmp(client.data_offer_mime, "SAVE_TARGETS") == 0);
    xwayland_interface.set_clipboard_owner = record_clipboard_owner;
    nb_wayland_server_set_xwayland_interface(server,
                                             &xwayland_interface,
                                             &clipboard_owner);
    CHECK(clipboard_owner.calls == 1);
    CHECK(clipboard_owner.available);
    REQUIRE(pipe(clipboard_pipe) == 0);
    wl_data_offer_receive(client.selection_offer,
                          "text/plain;charset=utf-8",
                          clipboard_pipe[1]);
    (void)close(clipboard_pipe[1]);
    clipboard_pipe[1] = -1;
    REQUIRE(pump_barrier(server, display));
    memset(clipboard_buffer, 0, sizeof(clipboard_buffer));
    CHECK(read(clipboard_pipe[0],
               clipboard_buffer,
               sizeof(clipboard_buffer)) ==
          (ssize_t)(sizeof(wayland_clipboard) - 1));
    CHECK(memcmp(clipboard_buffer,
                 wayland_clipboard,
                 sizeof(wayland_clipboard) - 1) == 0);
    (void)close(clipboard_pipe[0]);
    clipboard_pipe[0] = -1;
    CHECK(client.data_source_send_count == 2);

    /* Imported X11 text becomes a regular Wayland selection offer. */
    CHECK(nb_wayland_server_set_external_clipboard_text(
        server,
        external_clipboard,
        sizeof(external_clipboard) - 1));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.data_source_cancelled_count == 3);
    CHECK(client.data_device_selection_count == 3);
    REQUIRE(client.selection_offer != NULL);
    CHECK(client.data_offer_count == 2);
    CHECK(client.data_offer_mime_count == 5);
    REQUIRE(pipe(clipboard_pipe) == 0);
    wl_data_offer_receive(client.selection_offer,
                          "text/plain;charset=utf-8",
                          clipboard_pipe[1]);
    (void)close(clipboard_pipe[1]);
    clipboard_pipe[1] = -1;
    REQUIRE(pump_barrier(server, display));
    REQUIRE(pump_barrier(server, display));
    memset(clipboard_buffer, 0, sizeof(clipboard_buffer));
    CHECK(read(clipboard_pipe[0],
               clipboard_buffer,
               sizeof(clipboard_buffer)) ==
          (ssize_t)(sizeof(external_clipboard) - 1));
    CHECK(memcmp(clipboard_buffer,
                 external_clipboard,
                 sizeof(external_clipboard) - 1) == 0);
    (void)close(clipboard_pipe[0]);
    clipboard_pipe[0] = -1;
    nb_wayland_server_clear_external_clipboard(server);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.data_device_selection_count == 4);
    CHECK(client.selection_offer == NULL);
    wl_data_source_destroy(client.data_source);
    client.data_source = NULL;
    REQUIRE(pump_barrier(server, display));

    /* A late version-3 keyboard gets focus but no version-4 repeat event. */
    REQUIRE(client.seat_global_name != 0);
    legacy_input.seat = wl_registry_bind(registry,
                                         client.seat_global_name,
                                         &wl_seat_interface,
                                         3);
    REQUIRE(legacy_input.seat != NULL);
    REQUIRE(wl_seat_add_listener(legacy_input.seat,
                                 &seat_listener,
                                 &legacy_input) == 0);
    REQUIRE(pump_barrier(server, display));
    REQUIRE(legacy_input.pointer != NULL);
    REQUIRE(legacy_input.keyboard != NULL);
    REQUIRE(pump_barrier(server, display));
    CHECK(wl_keyboard_get_version(legacy_input.keyboard) == 3);
    CHECK(legacy_input.keyboard_keymap_count == 1);
    CHECK(legacy_input.keyboard_keymap_mapped);
    CHECK(legacy_input.keyboard_repeat_count == 0);
    CHECK(legacy_input.keyboard_enter_count == 1);
    CHECK(legacy_input.keyboard_enter_surface == surface);
    CHECK(legacy_input.keyboard_enter_key_count == 0);
    CHECK(legacy_input.keyboard_modifiers_count == 1);

    CHECK(nb_wayland_server_keyboard_key(server,
                                          "SPCE",
                                          UINT32_C(1110),
                                          true));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_key_count == 1);
    CHECK(client.keyboard_key == client.keyboard_key_space);
    CHECK(client.keyboard_key_state == WL_KEYBOARD_KEY_STATE_PRESSED);
    CHECK(client.keyboard_time == UINT32_C(1110));
    CHECK(client.keyboard_press_counts[client.keyboard_key_space] == 1);

    /* Host key repeat must not duplicate protocol key-down transitions. */
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "SPCE",
                                          UINT32_C(1111),
                                          true));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_key_count == 1);
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "SPCE",
                                          UINT32_C(1120),
                                          false));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_key_count == 2);
    CHECK(client.keyboard_key == client.keyboard_key_space);
    CHECK(client.keyboard_key_state == WL_KEYBOARD_KEY_STATE_RELEASED);
    CHECK(client.keyboard_time == UINT32_C(1120));
    CHECK(client.keyboard_release_counts[client.keyboard_key_space] == 1);

    CHECK(nb_wayland_server_keyboard_key(server,
                                          "AC01",
                                          UINT32_C(1130),
                                          true));
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "LFSH",
                                          UINT32_C(1131),
                                          true));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_key_count == 4);
    CHECK(client.keyboard_press_counts[client.keyboard_key_a] == 1);
    CHECK(client.keyboard_press_counts[client.keyboard_key_left_shift] == 1);
    CHECK(client.keyboard_key == client.keyboard_key_left_shift);
    CHECK(client.keyboard_key_state == WL_KEYBOARD_KEY_STATE_PRESSED);
    CHECK(client.keyboard_modifiers_count == 2);
    CHECK(client.keyboard_mods_depressed != 0);

    CHECK(nb_wayland_server_keyboard_focus(server, NB_WINDOW_ID_NONE));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_leave_count == 1);
    CHECK(client.keyboard_leave_surface == surface);
    CHECK(nb_wayland_server_keyboard_focus(server, window));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_enter_count == 2);
    CHECK(client.keyboard_enter_surface == surface);
    CHECK(client.keyboard_enter_key_count == 2);
    CHECK((client.keyboard_enter_keys[0] == client.keyboard_key_a &&
           client.keyboard_enter_keys[1] ==
               client.keyboard_key_left_shift) ||
          (client.keyboard_enter_keys[0] ==
               client.keyboard_key_left_shift &&
           client.keyboard_enter_keys[1] == client.keyboard_key_a));
    CHECK(client.keyboard_modifiers_count == 3);
    CHECK(client.keyboard_mods_depressed != 0);

    CHECK(nb_wayland_server_keyboard_key(server,
                                          "LFSH",
                                          UINT32_C(1140),
                                          false));
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "AC01",
                                          UINT32_C(1141),
                                          false));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_key_count == 6);
    CHECK(client.keyboard_release_counts[client.keyboard_key_a] == 1);
    CHECK(client.keyboard_release_counts[client.keyboard_key_left_shift] ==
          1);
    CHECK(client.keyboard_key == client.keyboard_key_a);
    CHECK(client.keyboard_key_state == WL_KEYBOARD_KEY_STATE_RELEASED);
    CHECK(client.keyboard_time == UINT32_C(1141));
    CHECK(client.keyboard_modifiers_count == 4);
    CHECK(client.keyboard_mods_depressed == 0);

    CHECK(nb_wayland_server_keyboard_key(server,
                                          "AC01",
                                          UINT32_C(1150),
                                          true));
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "LFSH",
                                          UINT32_C(1151),
                                          true));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_key_count == 8);
    CHECK(client.keyboard_mods_depressed != 0);
    nb_wayland_server_keyboard_cancel(server, UINT32_C(1160));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_key_count == 10);
    CHECK(client.keyboard_release_counts[client.keyboard_key_a] == 2);
    CHECK(client.keyboard_release_counts[client.keyboard_key_left_shift] ==
          2);
    CHECK(client.keyboard_key_state == WL_KEYBOARD_KEY_STATE_RELEASED);
    CHECK(client.keyboard_time == UINT32_C(1160));
    CHECK(client.keyboard_mods_depressed == 0);
    CHECK(client.keyboard_leave_count == 2);

    /* Keep focus on the surface so unmapping can prove focus cleanup. */
    CHECK(nb_wayland_server_keyboard_focus(server, window));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_enter_count == 3);
    CHECK(client.keyboard_enter_key_count == 0);

    /* Plain function keys, including the shell's F10 fallback, reach clients. */
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "FK04",
                                          UINT32_C(1170),
                                          true));
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "FK04",
                                          UINT32_C(1171),
                                          false));
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "FK10",
                                          UINT32_C(1172),
                                          true));
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "FK10",
                                          UINT32_C(1173),
                                          false));
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "FK12",
                                          UINT32_C(1174),
                                          true));
    CHECK(nb_wayland_server_keyboard_key(server,
                                          "FK12",
                                          UINT32_C(1175),
                                          false));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.keyboard_key_count == 16);
    CHECK(client.keyboard_press_counts[client.keyboard_key_f4] == 1);
    CHECK(client.keyboard_release_counts[client.keyboard_key_f4] == 1);
    CHECK(client.keyboard_press_counts[client.keyboard_key_f10] == 1);
    CHECK(client.keyboard_release_counts[client.keyboard_key_f10] == 1);
    CHECK(client.keyboard_press_counts[client.keyboard_key_f12] == 1);
    CHECK(client.keyboard_release_counts[client.keyboard_key_f12] == 1);
    CHECK(client.keyboard_key == client.keyboard_key_f12);
    CHECK(client.keyboard_key_state == WL_KEYBOARD_KEY_STATE_RELEASED);
    CHECK(client.keyboard_time == UINT32_C(1175));

    nb_wayland_server_frame_presented(server, UINT32_C(4242));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.frame_done);
    CHECK(client.frame_milliseconds == UINT32_C(4242));

    REQUIRE(nb_wayland_server_request_close(server, window));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.close_requested);

    /* Unmapping a focused surface also removes compositor pointer focus. */
    CHECK(nb_wayland_server_pointer_motion(server,
                                           window,
                                           content.x + 560,
                                           content.y + 300,
                                           UINT32_C(1050)));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_enter_count == 3);

    nixbench_application_menu_v1_destroy(client.application_menu);
    client.application_menu = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(shell.active_menu_source == WAYLAND_MENU_SOURCE);
    CHECK(shell.menu.model == &empty_menu_model);
    CHECK(!nb_wayland_server_dispatch_menu_command(
        server,
        window,
        WAYLAND_MENU_SOURCE + 1,
        APPLICATION_COMMAND_QUIT));

    /* A manager may die before the menu object it created. */
    client.application_menu =
        nixbench_application_menu_manager_v1_get_menu(
            client.application_menu_manager,
            surface);
    REQUIRE(client.application_menu != NULL);
    REQUIRE(nixbench_application_menu_v1_add_listener(
                client.application_menu,
                &application_menu_listener,
                &client) == 0);
    publish_application_menu(client.application_menu, true);
    nixbench_application_menu_manager_v1_destroy(
        client.application_menu_manager);
    client.application_menu_manager = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(shell.active_menu_source == WAYLAND_MENU_SOURCE + 1);
    REQUIRE(shell.menu.model != NULL);
    CHECK(strcmp(shell.menu.model->menus[0].label, "NixClock") == 0);

    /*
     * A root unmap dismisses a complete popup grab tree before the root
     * surface can later be remapped or its storage slot reused.
     */
    popup_client = (struct popup_state){0};
    child_popup_client = (struct popup_state){0};
    popup_surface = wl_compositor_create_surface(client.compositor);
    REQUIRE(popup_surface != NULL);
    popup_xdg_surface = xdg_wm_base_get_xdg_surface(client.wm_base,
                                                    popup_surface);
    REQUIRE(popup_xdg_surface != NULL);
    REQUIRE(xdg_surface_add_listener(popup_xdg_surface,
                                     &popup_surface_listener,
                                     &popup_client) == 0);
    positioner = xdg_wm_base_create_positioner(client.wm_base);
    REQUIRE(positioner != NULL);
    xdg_positioner_set_size(positioner, POPUP_WIDTH, POPUP_HEIGHT);
    xdg_positioner_set_anchor_rect(positioner, 8, 8, 16, 16);
    xdg_positioner_set_anchor(positioner,
                              XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
    xdg_positioner_set_gravity(positioner,
                               XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    popup = xdg_surface_get_popup(popup_xdg_surface,
                                  client.xdg_surface,
                                  positioner);
    REQUIRE(popup != NULL);
    REQUIRE(xdg_popup_add_listener(popup,
                                   &popup_listener,
                                   &popup_client) == 0);
    xdg_popup_grab(popup, client.seat, UINT32_C(2));
    xdg_positioner_destroy(positioner);
    positioner = NULL;
    wl_surface_commit(popup_surface);
    REQUIRE(pump_barrier(server, display));
    REQUIRE(pump_barrier(server, display));
    CHECK(popup_client.popup_configure_count == 1);
    CHECK(popup_client.surface_configure_count == 1);
    CHECK(popup_client.done_count == 0);

    popup_buffer = wl_shm_pool_create_buffer(
        pool,
        0,
        POPUP_WIDTH,
        POPUP_HEIGHT,
        POPUP_WIDTH * BYTES_PER_PIXEL,
        WL_SHM_FORMAT_ARGB8888);
    REQUIRE(popup_buffer != NULL);
    xdg_surface_set_window_geometry(popup_xdg_surface,
                                    0,
                                    0,
                                    POPUP_WIDTH,
                                    POPUP_HEIGHT);
    wl_surface_attach(popup_surface, popup_buffer, 0, 0);
    wl_surface_damage(popup_surface, 0, 0, POPUP_WIDTH, POPUP_HEIGHT);
    wl_surface_commit(popup_surface);
    REQUIRE(pump_barrier(server, display));

    child_popup_surface = wl_compositor_create_surface(client.compositor);
    REQUIRE(child_popup_surface != NULL);
    child_popup_xdg_surface = xdg_wm_base_get_xdg_surface(
        client.wm_base,
        child_popup_surface);
    REQUIRE(child_popup_xdg_surface != NULL);
    REQUIRE(xdg_surface_add_listener(child_popup_xdg_surface,
                                     &popup_surface_listener,
                                     &child_popup_client) == 0);
    child_positioner = xdg_wm_base_create_positioner(client.wm_base);
    REQUIRE(child_positioner != NULL);
    xdg_positioner_set_size(child_positioner, 16, 16);
    xdg_positioner_set_anchor_rect(child_positioner, 0, 0, 16, 16);
    xdg_positioner_set_anchor(child_positioner,
                              XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
    xdg_positioner_set_gravity(child_positioner,
                               XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    child_popup = xdg_surface_get_popup(child_popup_xdg_surface,
                                        popup_xdg_surface,
                                        child_positioner);
    REQUIRE(child_popup != NULL);
    REQUIRE(xdg_popup_add_listener(child_popup,
                                   &popup_listener,
                                   &child_popup_client) == 0);
    xdg_popup_grab(child_popup, client.seat, UINT32_C(3));
    xdg_positioner_destroy(child_positioner);
    child_positioner = NULL;
    wl_surface_commit(child_popup_surface);
    REQUIRE(pump_barrier(server, display));
    REQUIRE(pump_barrier(server, display));
    CHECK(child_popup_client.popup_configure_count == 1);
    CHECK(child_popup_client.surface_configure_count == 1);
    CHECK(child_popup_client.done_count == 0);
    CHECK(nb_wayland_server_window_count(server) == 1);

    /* A committed NULL attachment explicitly unmaps the host window. */
    wl_surface_attach(surface, NULL, 0, 0);
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 0);
    CHECK(nb_wayland_server_take_redraw(server));
    CHECK(!nb_wayland_server_take_redraw(server));
    CHECK(client.pointer_leave_count == 3);
    CHECK(client.pointer_leave_surface == surface);
    CHECK(client.keyboard_leave_count == 3);
    CHECK(client.keyboard_leave_surface == surface);
    CHECK(client.surface_leave_count == 2);
    CHECK(client.initial_output_leave_count == 1);
    CHECK(client.late_output_leave_count == 1);
    CHECK(client.surface_leave_output == client.output.proxy ||
          client.surface_leave_output == client.late_output.proxy);
    CHECK(nb_wayland_server_pointer_grab_window(server) ==
          NB_WINDOW_ID_NONE);
    CHECK(!nb_wayland_server_owns_window(server, window));
    CHECK(!nb_wayland_server_surface_snapshot(server,
                                              window,
                                              &snapshot));
    CHECK(popup_client.done_count == 1);
    CHECK(child_popup_client.done_count == 1);

    xdg_popup_destroy(child_popup);
    child_popup = NULL;
    xdg_surface_destroy(child_popup_xdg_surface);
    child_popup_xdg_surface = NULL;
    wl_surface_destroy(child_popup_surface);
    child_popup_surface = NULL;
    xdg_popup_destroy(popup);
    popup = NULL;
    xdg_surface_destroy(popup_xdg_surface);
    popup_xdg_surface = NULL;
    wl_surface_destroy(popup_surface);
    popup_surface = NULL;
    wl_buffer_destroy(popup_buffer);
    popup_buffer = NULL;
    REQUIRE(pump_barrier(server, display));

    /* Remap, then destroy the role while its committed menu remains alive. */
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    REQUIRE(pump_barrier(server, display));
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, INITIAL_WIDTH, INITIAL_HEIGHT);
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 1);
    window = nb_wayland_server_window_at(server, 0);
    REQUIRE(window != NB_WINDOW_ID_NONE);
    CHECK(shell.active_menu_source == WAYLAND_MENU_SOURCE + 1);

    xdg_toplevel_destroy(client.toplevel);
    client.toplevel = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 0);
    CHECK(shell.active_menu_source == DESKTOP_MENU_SOURCE);
    CHECK(!nb_wayland_server_dispatch_menu_command(
        server,
        window,
        WAYLAND_MENU_SOURCE + 1,
        APPLICATION_COMMAND_QUIT));

    nixbench_application_menu_v1_destroy(client.application_menu);
    client.application_menu = NULL;
    REQUIRE(pump_barrier(server, display));

    nixbench_html_theme_atlas_v1_destroy(client.html_theme_atlas);
    client.html_theme_atlas = NULL;
    wl_buffer_destroy(html_theme_buffer);
    html_theme_buffer = NULL;
    wl_surface_destroy(html_theme_surface);
    html_theme_surface = NULL;
    nixbench_html_theme_manager_v1_destroy(client.html_theme_manager);
    client.html_theme_manager = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(!nb_wayland_server_html_theme_connected(server));
    CHECK(!nb_wayland_server_html_theme_snapshot(
        server, &html_theme_snapshot));

    wl_buffer_destroy(buffer);
    buffer = NULL;
    wl_shm_pool_destroy(pool);
    pool = NULL;
    xdg_surface_destroy(client.xdg_surface);
    client.xdg_surface = NULL;
    wl_surface_destroy(surface);
    surface = NULL;
    wl_data_device_destroy(client.data_device);
    client.data_device = NULL;
    wl_data_device_manager_destroy(client.data_device_manager);
    client.data_device_manager = NULL;
    wl_pointer_release(legacy_input.pointer);
    legacy_input.pointer = NULL;
    wl_keyboard_release(legacy_input.keyboard);
    legacy_input.keyboard = NULL;
    wl_seat_destroy(legacy_input.seat);
    legacy_input.seat = NULL;
    wl_pointer_release(client.pointer);
    client.pointer = NULL;
    wl_keyboard_release(client.keyboard);
    client.keyboard = NULL;
    wl_seat_release(client.seat);
    client.seat = NULL;
    wl_output_destroy(client.late_output.proxy);
    client.late_output.proxy = NULL;
    wl_output_destroy(client.output.proxy);
    client.output.proxy = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 0);

cleanup:
    if (server != NULL) {
        nb_wayland_server_destroy(server);
    }
    if (pixels != MAP_FAILED) {
        (void)munmap(pixels, buffer_size);
    }
    if (shm_fd >= 0) {
        (void)close(shm_fd);
    }
    if (clipboard_pipe[0] >= 0) {
        (void)close(clipboard_pipe[0]);
    }
    if (clipboard_pipe[1] >= 0) {
        (void)close(clipboard_pipe[1]);
    }
    if (sockets[0] >= 0) {
        (void)close(sockets[0]);
    }
    if (sockets[1] >= 0) {
        (void)close(sockets[1]);
    }
}

static void test_wayland_subsurface_lifecycle(void)
{
    struct nb_shell shell;
    struct nb_wayland_server *server = NULL;
    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;
    struct wl_surface *surface = NULL;
    struct wl_surface *child_surface = NULL;
    struct wl_shm_pool *pool = NULL;
    struct wl_buffer *root_buffer = NULL;
    struct wl_buffer *child_buffer = NULL;
    struct wl_subsurface *subsurface = NULL;
    struct xdg_surface *xdg_surface = NULL;
    struct xdg_toplevel *toplevel = NULL;
    struct client_state client = {0};
    struct nb_wayland_surface_snapshot snapshot;
    uint32_t *pixels = MAP_FAILED;
    uint32_t *root_pixels = NULL;
    uint32_t *child_pixels = NULL;
    const int root_width = 256;
    const int root_height = 160;
    const int child_width = 48;
    const int child_height = 32;
    const int child_x = 37;
    const int child_y = 29;
    const uint32_t child_pixel = UINT32_C(0xff224466);
    const size_t root_pixel_count =
        (size_t)root_width * (size_t)root_height;
    const size_t child_pixel_count =
        (size_t)child_width * (size_t)child_height;
    const size_t root_bytes = root_pixel_count * sizeof(*pixels);
    const size_t child_bytes = child_pixel_count * sizeof(*pixels);
    const size_t total_bytes = root_bytes + child_bytes;
    int sockets[2] = {-1, -1};
    int shm_fd = -1;
    char shm_path[] = "/tmp/nixbench-wayland-subsurface-XXXXXX";

    nb_shell_init(&shell, DESKTOP_MENU_SOURCE, &empty_menu_model);
    server = nb_wayland_server_create(&shell,
                                      WAYLAND_MENU_SOURCE,
                                      &empty_menu_model,
                                      OUTPUT_WIDTH,
                                      OUTPUT_HEIGHT);
    REQUIRE(server != NULL);
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    REQUIRE(add_server_client(server, sockets[0]));
    sockets[0] = -1;

    display = wl_display_connect_to_fd(sockets[1]);
    sockets[1] = -1;
    REQUIRE(display != NULL);
    registry = wl_display_get_registry(display);
    REQUIRE(registry != NULL);
    REQUIRE(wl_registry_add_listener(registry,
                                     &registry_listener,
                                     &client) == 0);

    REQUIRE(pump_barrier(server, display));
    REQUIRE(client.compositor != NULL);
    REQUIRE(client.subcompositor != NULL);
    REQUIRE(client.shm != NULL);
    REQUIRE(client.wm_base != NULL);

    surface = wl_compositor_create_surface(client.compositor);
    REQUIRE(surface != NULL);
    REQUIRE(wl_surface_add_listener(surface,
                                    &surface_listener,
                                    &client) == 0);
    xdg_surface = xdg_wm_base_get_xdg_surface(client.wm_base, surface);
    REQUIRE(xdg_surface != NULL);
    REQUIRE(xdg_surface_add_listener(xdg_surface,
                                     &xdg_surface_listener,
                                     &client) == 0);
    toplevel = xdg_surface_get_toplevel(xdg_surface);
    REQUIRE(toplevel != NULL);
    REQUIRE(xdg_toplevel_add_listener(toplevel,
                                      &toplevel_listener,
                                      &client) == 0);

    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 0);
    REQUIRE(pump_barrier(server, display));

    shm_fd = mkstemp(shm_path);
    REQUIRE(shm_fd >= 0);
    REQUIRE(unlink(shm_path) == 0);
    REQUIRE(ftruncate(shm_fd, (off_t)total_bytes) == 0);
    pixels = mmap(NULL,
                  total_bytes,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,
                  shm_fd,
                  0);
    REQUIRE(pixels != MAP_FAILED);
    root_pixels = pixels;
    child_pixels = (uint32_t *)((unsigned char *)pixels + root_bytes);
    fill_pixels(root_pixels, root_pixel_count);
    for (size_t index = 0; index < child_pixel_count; ++index) {
        child_pixels[index] = child_pixel;
    }

    pool = wl_shm_create_pool(client.shm, shm_fd, (int32_t)total_bytes);
    REQUIRE(pool != NULL);
    root_buffer = wl_shm_pool_create_buffer(pool,
                                            0,
                                            root_width,
                                            root_height,
                                            root_width * BYTES_PER_PIXEL,
                                            WL_SHM_FORMAT_ARGB8888);
    REQUIRE(root_buffer != NULL);
    child_buffer = wl_shm_pool_create_buffer(pool,
                                             (int32_t)root_bytes,
                                             child_width,
                                             child_height,
                                             child_width * BYTES_PER_PIXEL,
                                             WL_SHM_FORMAT_ARGB8888);
    REQUIRE(child_buffer != NULL);
    REQUIRE(wl_buffer_add_listener(root_buffer,
                                   &buffer_listener,
                                   &client) == 0);
    REQUIRE(wl_buffer_add_listener(child_buffer,
                                   &buffer_listener,
                                   &client) == 0);

    xdg_surface_set_window_geometry(xdg_surface,
                                    0,
                                    0,
                                    root_width,
                                    root_height);
    wl_surface_attach(surface, root_buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, root_width, root_height);
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 1);
    CHECK(nb_wayland_server_take_redraw(server));
    CHECK(!nb_wayland_server_take_redraw(server));
    REQUIRE(nb_wayland_server_surface_snapshot(server,
                                               nb_wayland_server_window_at(
                                                   server, 0),
                                               &snapshot));
    CHECK(snapshot.width == root_width);
    CHECK(snapshot.height == root_height);
    CHECK(memcmp(snapshot.pixels, root_pixels, root_bytes) == 0);

    child_surface = wl_compositor_create_surface(client.compositor);
    REQUIRE(child_surface != NULL);
    subsurface = wl_subcompositor_get_subsurface(client.subcompositor,
                                                 child_surface,
                                                 surface);
    REQUIRE(subsurface != NULL);
    wl_subsurface_set_desync(subsurface);
    wl_subsurface_set_position(subsurface, child_x, child_y);
    wl_surface_attach(child_surface, child_buffer, 0, 0);
    wl_surface_damage(child_surface, 0, 0, child_width, child_height);
    wl_surface_commit(child_surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 1);
    CHECK(nb_wayland_server_take_redraw(server));
    CHECK(!nb_wayland_server_take_redraw(server));
    REQUIRE(nb_wayland_server_surface_snapshot(server,
                                               nb_wayland_server_window_at(
                                                   server, 0),
                                               &snapshot));
    CHECK(snapshot.pixels[(size_t)child_y * (size_t)root_width +
                          (size_t)child_x] == child_pixel);
    CHECK(snapshot.pixels[(size_t)(child_y + child_height - 1) *
                              (size_t)root_width +
                          (size_t)(child_x + child_width - 1)] ==
          child_pixel);

    wl_subsurface_destroy(subsurface);
    subsurface = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 1);
    CHECK(nb_wayland_server_take_redraw(server));
    CHECK(!nb_wayland_server_take_redraw(server));
    REQUIRE(nb_wayland_server_surface_snapshot(server,
                                               nb_wayland_server_window_at(
                                                   server, 0),
                                               &snapshot));
    CHECK(snapshot.pixels[(size_t)child_y * (size_t)root_width +
                          (size_t)child_x] ==
          root_pixels[(size_t)child_y * (size_t)root_width +
                      (size_t)child_x]);

    wl_surface_destroy(child_surface);
    child_surface = NULL;
    xdg_toplevel_destroy(toplevel);
    toplevel = NULL;
    xdg_surface_destroy(xdg_surface);
    xdg_surface = NULL;
    wl_surface_destroy(surface);
    surface = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 0);

cleanup:
    if (child_buffer != NULL) {
        wl_buffer_destroy(child_buffer);
    }
    if (root_buffer != NULL) {
        wl_buffer_destroy(root_buffer);
    }
    if (pool != NULL) {
        wl_shm_pool_destroy(pool);
    }
    if (subsurface != NULL) {
        wl_subsurface_destroy(subsurface);
    }
    if (child_surface != NULL) {
        wl_surface_destroy(child_surface);
    }
    if (toplevel != NULL) {
        xdg_toplevel_destroy(toplevel);
    }
    if (xdg_surface != NULL) {
        xdg_surface_destroy(xdg_surface);
    }
    if (surface != NULL) {
        wl_surface_destroy(surface);
    }
    if (server != NULL) {
        nb_wayland_server_destroy(server);
    }
    if (pixels != MAP_FAILED) {
        (void)munmap(pixels, total_bytes);
    }
    if (shm_fd >= 0) {
        (void)close(shm_fd);
    }
    if (sockets[0] >= 0) {
        (void)close(sockets[0]);
    }
    if (sockets[1] >= 0) {
        (void)close(sockets[1]);
    }
}

int main(void)
{
    test_menu_source_reservation();
    test_wayland_surface_lifecycle();
    test_wayland_subsurface_lifecycle();

    if (failures != 0) {
        fprintf(stderr, "wayland tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("wayland tests: ok");
    return 0;
}
