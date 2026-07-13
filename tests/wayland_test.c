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

#include "wayland_server.h"
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
    INITIAL_WIDTH = 560,
    INITIAL_HEIGHT = 300,
    BYTES_PER_PIXEL = 4,
    WAYLAND_POINTER_BUTTON_LEFT = 0x110,
    WAYLAND_POINTER_BUTTON_RIGHT = 0x111,
    PUMP_ATTEMPTS = 80,
    PUMP_TIMEOUT_MILLISECONDS = 25,
    SEAT_NAME_CAPACITY = 64
};

static const struct nb_menu_model empty_menu_model = {NULL, 0};

struct barrier_state {
    bool done;
};

struct client_state {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct xdg_wm_base *wm_base;
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
    char seat_name[SEAT_NAME_CAPACITY];
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
    unsigned int pointer_enter_count;
    unsigned int pointer_leave_count;
    unsigned int pointer_motion_count;
    unsigned int pointer_button_count;
    unsigned int pointer_frame_count;
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

static void seat_capabilities(void *data,
                              struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct client_state *state = data;

    state->seat_capabilities = capabilities;
    ++state->seat_capability_count;
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
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name
};

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
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(
            registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        const uint32_t bind_version = version < 5 ? version : 5;

        state->seat_version = version;
        state->seat = wl_registry_bind(
            registry, name, &wl_seat_interface, bind_version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface, 1);
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

static void test_wayland_surface_lifecycle(void)
{
    struct nb_shell shell;
    struct nb_wayland_server *server = NULL;
    struct wl_display *display = NULL;
    struct wl_registry *registry = NULL;
    struct wl_surface *surface = NULL;
    struct wl_shm_pool *pool = NULL;
    struct wl_buffer *buffer = NULL;
    struct client_state client = {0};
    struct nb_wayland_surface_snapshot snapshot;
    const struct nb_window *host_window;
    struct nb_window *resized_host_window;
    struct nb_rect content;
    nb_window_id window = NB_WINDOW_ID_NONE;
    int sockets[2] = {-1, -1};
    int shm_fd = -1;
    uint32_t *pixels = MAP_FAILED;
    const size_t pixel_count =
        (size_t)INITIAL_WIDTH * (size_t)INITIAL_HEIGHT;
    const size_t buffer_size = pixel_count * sizeof(*pixels);
    char shm_path[] = "/tmp/nixbench-wayland-test-XXXXXX";

    nb_shell_init(&shell, DESKTOP_MENU_SOURCE, &empty_menu_model);
    server = nb_wayland_server_create(&shell,
                                      WAYLAND_MENU_SOURCE,
                                      &empty_menu_model);
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

    /* First fence receives globals; the second processes bind requests. */
    REQUIRE(pump_barrier(server, display));
    REQUIRE(client.compositor != NULL);
    REQUIRE(client.shm != NULL);
    REQUIRE(client.seat != NULL);
    REQUIRE(client.wm_base != NULL);
    CHECK(client.seat_version >= 5);
    CHECK(wl_seat_get_version(client.seat) == 5);
    REQUIRE(wl_shm_add_listener(client.shm, &shm_listener, &client) == 0);
    REQUIRE(wl_seat_add_listener(client.seat,
                                 &seat_listener,
                                 &client) == 0);
    REQUIRE(xdg_wm_base_add_listener(client.wm_base,
                                     &wm_base_listener,
                                     &client) == 0);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.saw_argb8888);
    CHECK(client.seat_capability_count == 1);
    CHECK((client.seat_capabilities & WL_SEAT_CAPABILITY_POINTER) != 0);
    CHECK(client.seat_name_count == 1);
    CHECK(strcmp(client.seat_name, "nixbench-seat0") == 0);
    REQUIRE(client.pointer != NULL);
    /* Process get_pointer, which was queued by the capability callback. */
    REQUIRE(pump_barrier(server, display));

    surface = wl_compositor_create_surface(client.compositor);
    REQUIRE(surface != NULL);
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

    /* A bufferless initial commit requests the compositor configure. */
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(client.toplevel_configure_count == 1);
    CHECK(client.surface_configure_count == 1);
    CHECK(client.configure_serial != 0);
    CHECK(client.configured_width == INITIAL_WIDTH);
    CHECK(client.configured_height == INITIAL_HEIGHT);
    CHECK(nb_wayland_server_window_count(server) == 0);

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
    window = nb_wayland_server_window_at(server, 0);
    REQUIRE(window != NB_WINDOW_ID_NONE);
    CHECK(nb_wayland_server_owns_window(server, window));
    REQUIRE(nb_wayland_server_surface_snapshot(server,
                                               window,
                                               &snapshot));
    CHECK(snapshot.width == INITIAL_WIDTH);
    CHECK(snapshot.height == INITIAL_HEIGHT);
    CHECK(snapshot.stride == INITIAL_WIDTH * BYTES_PER_PIXEL);
    CHECK(snapshot.revision == 1);
    CHECK(memcmp(snapshot.pixels, pixels, buffer_size) == 0);
    pixels[0] = 0;
    CHECK(snapshot.pixels[0] != pixels[0]);

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

    CHECK(nb_wayland_server_pointer_motion(server,
                                           window,
                                           content.x + 280,
                                           content.y + 150,
                                           UINT32_C(1000)));
    REQUIRE(pump_barrier(server, display));
    CHECK(client.pointer_enter_count == 1);
    CHECK(client.pointer_enter_surface == surface);
    CHECK(client.pointer_serial != 0);
    CHECK(client.pointer_enter_x == wl_fixed_from_int(140));
    CHECK(client.pointer_enter_y == wl_fixed_from_int(75));

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
    CHECK(client.pointer_x == wl_fixed_from_int(280));
    CHECK(client.pointer_y == wl_fixed_from_int(150));
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
    CHECK(client.pointer_x == wl_fixed_from_int(-56));
    CHECK(client.pointer_y == wl_fixed_from_int(-30));
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

    /* A committed NULL attachment explicitly unmaps the host window. */
    wl_surface_attach(surface, NULL, 0, 0);
    wl_surface_commit(surface);
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 0);
    CHECK(client.pointer_leave_count == 3);
    CHECK(client.pointer_leave_surface == surface);
    CHECK(nb_wayland_server_pointer_grab_window(server) ==
          NB_WINDOW_ID_NONE);
    CHECK(!nb_wayland_server_owns_window(server, window));
    CHECK(!nb_wayland_server_surface_snapshot(server,
                                              window,
                                              &snapshot));

    wl_buffer_destroy(buffer);
    buffer = NULL;
    wl_shm_pool_destroy(pool);
    pool = NULL;
    xdg_toplevel_destroy(client.toplevel);
    client.toplevel = NULL;
    xdg_surface_destroy(client.xdg_surface);
    client.xdg_surface = NULL;
    wl_surface_destroy(surface);
    surface = NULL;
    wl_pointer_release(client.pointer);
    client.pointer = NULL;
    wl_seat_release(client.seat);
    client.seat = NULL;
    REQUIRE(pump_barrier(server, display));
    CHECK(nb_wayland_server_window_count(server) == 0);

cleanup:
    if (display != NULL) {
        wl_display_disconnect(display);
    }
    if (server != NULL) {
        nb_wayland_server_destroy(server);
    }
    if (pixels != MAP_FAILED) {
        (void)munmap(pixels, buffer_size);
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
    test_wayland_surface_lifecycle();

    if (failures != 0) {
        fprintf(stderr, "wayland tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("wayland tests: ok");
    return 0;
}
