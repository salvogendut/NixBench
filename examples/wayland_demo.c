#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wayland_demo_ui.h"
#include "xdg-shell-client-protocol.h"

enum {
    DEMO_DEFAULT_WIDTH = 560,
    DEMO_DEFAULT_HEIGHT = 300,
    DEMO_MAX_DIMENSION = 4096,
    DEMO_MAX_BUFFERS = 4,
    DEMO_COMPOSITOR_VERSION = 4,
    DEMO_SEAT_VERSION = 5,
    DEMO_BYTES_PER_PIXEL = 4,
    DEMO_POINTER_BUTTON_LEFT = 0x110
};

struct demo_options {
    bool exit_after_first_frame;
};

struct demo_app;

struct demo_buffer {
    struct demo_app *app;
    struct wl_buffer *proxy;
    uint32_t *pixels;
    size_t size;
    int width;
    int height;
    int stride;
    bool busy;
    bool retired;
    struct demo_buffer *next;
};

struct demo_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_surface *surface;
    struct xdg_wm_base *wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_callback *frame_callback;
    struct demo_buffer *buffers;
    size_t buffer_count;
    struct nb_wayland_demo_ui ui;
    uint32_t seat_global_name;
    uint32_t shm_format;
    int width;
    int height;
    int pending_width;
    int pending_height;
    bool saw_argb8888;
    bool saw_xrgb8888;
    bool configured;
    bool dirty;
    bool frame_pending;
    bool running;
    bool failed;
    bool exit_after_first_frame;
};

static void maybe_draw(struct demo_app *app);

static void fail_app(struct demo_app *app, const char *message)
{
    if (!app->failed) {
        fprintf(stderr, "%s\n", message);
    }
    app->failed = true;
    app->running = false;
}

static int create_anonymous_file(size_t size)
{
    static const char suffix[] = "/nixbench-wayland-demo-XXXXXX";
    const char *directory = getenv("XDG_RUNTIME_DIR");
    size_t directory_length;
    size_t path_size;
    char *path;
    int fd;

    if (directory == NULL || directory[0] == '\0') {
        directory = "/tmp";
    }
    directory_length = strlen(directory);
    if (directory_length > SIZE_MAX - sizeof(suffix)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    path_size = directory_length + sizeof(suffix);
    path = malloc(path_size);
    if (path == NULL) {
        return -1;
    }
    memcpy(path, directory, directory_length);
    memcpy(path + directory_length, suffix, sizeof(suffix));

    fd = mkstemp(path);
    if (fd < 0) {
        free(path);
        return -1;
    }
    if (unlink(path) != 0) {
        const int saved_errno = errno;

        (void)close(fd);
        free(path);
        errno = saved_errno;
        return -1;
    }
    free(path);
    if (ftruncate(fd, (off_t)size) != 0) {
        const int saved_errno = errno;

        (void)close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}

static void unlink_buffer(struct demo_buffer *buffer)
{
    struct demo_buffer **link = &buffer->app->buffers;

    while (*link != NULL && *link != buffer) {
        link = &(*link)->next;
    }
    if (*link == buffer) {
        *link = buffer->next;
        --buffer->app->buffer_count;
    }
}

static void destroy_buffer(struct demo_buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }
    unlink_buffer(buffer);
    if (buffer->proxy != NULL) {
        wl_buffer_destroy(buffer->proxy);
    }
    if (buffer->pixels != MAP_FAILED) {
        (void)munmap(buffer->pixels, buffer->size);
    }
    free(buffer);
}

static void buffer_release(void *data, struct wl_buffer *proxy)
{
    struct demo_buffer *buffer = data;
    struct demo_app *app = buffer->app;

    (void)proxy;
    buffer->busy = false;
    if (buffer->retired) {
        destroy_buffer(buffer);
    }
    if (app->running && app->dirty && !app->frame_pending) {
        maybe_draw(app);
    }
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release
};

static struct demo_buffer *create_buffer(struct demo_app *app,
                                         int width,
                                         int height)
{
    struct demo_buffer *buffer;
    struct wl_shm_pool *pool;
    size_t size;
    int stride;
    int fd;

    if (width <= 0 || height <= 0 ||
        width > INT_MAX / DEMO_BYTES_PER_PIXEL) {
        errno = EINVAL;
        return NULL;
    }
    stride = width * DEMO_BYTES_PER_PIXEL;
    if ((size_t)stride > SIZE_MAX / (size_t)height) {
        errno = EOVERFLOW;
        return NULL;
    }
    size = (size_t)stride * (size_t)height;
    if (size > (size_t)INT32_MAX) {
        errno = EFBIG;
        return NULL;
    }

    buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        return NULL;
    }
    buffer->pixels = MAP_FAILED;
    fd = create_anonymous_file(size);
    if (fd < 0) {
        free(buffer);
        return NULL;
    }
    buffer->pixels = mmap(NULL,
                          size,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED,
                          fd,
                          0);
    if (buffer->pixels == MAP_FAILED) {
        const int saved_errno = errno;

        (void)close(fd);
        free(buffer);
        errno = saved_errno;
        return NULL;
    }
    pool = wl_shm_create_pool(app->shm, fd, (int32_t)size);
    (void)close(fd);
    if (pool == NULL) {
        (void)munmap(buffer->pixels, size);
        free(buffer);
        return NULL;
    }
    buffer->proxy = wl_shm_pool_create_buffer(pool,
                                               0,
                                               width,
                                               height,
                                               stride,
                                               app->shm_format);
    wl_shm_pool_destroy(pool);
    if (buffer->proxy == NULL ||
        wl_buffer_add_listener(buffer->proxy,
                               &buffer_listener,
                               buffer) < 0) {
        if (buffer->proxy != NULL) {
            wl_buffer_destroy(buffer->proxy);
        }
        (void)munmap(buffer->pixels, size);
        free(buffer);
        return NULL;
    }

    buffer->app = app;
    buffer->size = size;
    buffer->width = width;
    buffer->height = height;
    buffer->stride = stride;
    buffer->next = app->buffers;
    app->buffers = buffer;
    ++app->buffer_count;
    return buffer;
}

static void retire_mismatched_buffers(struct demo_app *app)
{
    struct demo_buffer *buffer = app->buffers;

    while (buffer != NULL) {
        struct demo_buffer *next = buffer->next;

        if (buffer->width != app->width ||
            buffer->height != app->height) {
            buffer->retired = true;
            if (!buffer->busy) {
                destroy_buffer(buffer);
            }
        }
        buffer = next;
    }
}

static struct demo_buffer *acquire_buffer(struct demo_app *app)
{
    struct demo_buffer *buffer;

    for (buffer = app->buffers; buffer != NULL; buffer = buffer->next) {
        if (!buffer->busy && !buffer->retired &&
            buffer->width == app->width &&
            buffer->height == app->height) {
            return buffer;
        }
    }
    if (app->buffer_count >= DEMO_MAX_BUFFERS) {
        return NULL;
    }
    return create_buffer(app, app->width, app->height);
}

static void frame_done(void *data,
                       struct wl_callback *callback,
                       uint32_t milliseconds)
{
    struct demo_app *app = data;

    (void)milliseconds;
    wl_callback_destroy(callback);
    app->frame_callback = NULL;
    app->frame_pending = false;
    if (app->exit_after_first_frame) {
        app->running = false;
        return;
    }
    maybe_draw(app);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done
};

static void maybe_draw(struct demo_app *app)
{
    struct demo_buffer *buffer;
    struct wl_callback *callback;

    if (!app->running || !app->configured || !app->dirty ||
        app->frame_pending) {
        return;
    }
    retire_mismatched_buffers(app);
    buffer = acquire_buffer(app);
    if (buffer == NULL) {
        if (app->buffer_count < DEMO_MAX_BUFFERS) {
            fail_app(app, "Could not allocate a Wayland shared-memory buffer");
        }
        return;
    }
    if (!nb_wayland_demo_ui_render(&app->ui,
                                    buffer->pixels,
                                    (size_t)(buffer->stride /
                                             DEMO_BYTES_PER_PIXEL))) {
        fail_app(app, "Could not render the Wayland demo interface");
        return;
    }

    callback = wl_surface_frame(app->surface);
    if (callback == NULL ||
        wl_callback_add_listener(callback, &frame_listener, app) < 0) {
        if (callback != NULL) {
            wl_callback_destroy(callback);
        }
        fail_app(app, "Could not create a Wayland frame callback");
        return;
    }

    buffer->busy = true;
    app->frame_callback = callback;
    app->frame_pending = true;
    app->dirty = false;
    xdg_surface_set_window_geometry(app->xdg_surface,
                                    0,
                                    0,
                                    app->width,
                                    app->height);
    wl_surface_attach(app->surface, buffer->proxy, 0, 0);
    if (wl_proxy_get_version((struct wl_proxy *)app->surface) >= 4) {
        wl_surface_damage_buffer(app->surface,
                                 0,
                                 0,
                                 app->width,
                                 app->height);
    } else {
        wl_surface_damage(app->surface,
                          0,
                          0,
                          app->width,
                          app->height);
    }
    wl_surface_commit(app->surface);
}

static void redraw_if_changed(struct demo_app *app, bool changed)
{
    if (changed) {
        app->dirty = true;
        maybe_draw(app);
    }
}

static void pointer_enter(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface,
                          wl_fixed_t surface_x,
                          wl_fixed_t surface_y)
{
    struct demo_app *app = data;

    (void)pointer;
    (void)serial;
    if (surface == app->surface) {
        redraw_if_changed(
            app,
            nb_wayland_demo_ui_pointer_motion(&app->ui,
                                               wl_fixed_to_int(surface_x),
                                               wl_fixed_to_int(surface_y)));
    }
}

static void pointer_leave(void *data,
                          struct wl_pointer *pointer,
                          uint32_t serial,
                          struct wl_surface *surface)
{
    struct demo_app *app = data;

    (void)pointer;
    (void)serial;
    if (surface == app->surface) {
        redraw_if_changed(app,
                          nb_wayland_demo_ui_pointer_leave(&app->ui));
    }
}

static void pointer_motion(void *data,
                           struct wl_pointer *pointer,
                           uint32_t time,
                           wl_fixed_t surface_x,
                           wl_fixed_t surface_y)
{
    struct demo_app *app = data;

    (void)pointer;
    (void)time;
    redraw_if_changed(
        app,
        nb_wayland_demo_ui_pointer_motion(&app->ui,
                                           wl_fixed_to_int(surface_x),
                                           wl_fixed_to_int(surface_y)));
}

static void pointer_button(void *data,
                           struct wl_pointer *pointer,
                           uint32_t serial,
                           uint32_t time,
                           uint32_t button,
                           uint32_t state)
{
    struct demo_app *app = data;

    (void)pointer;
    (void)serial;
    (void)time;
    if (button != DEMO_POINTER_BUTTON_LEFT) {
        return;
    }
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        redraw_if_changed(
            app,
            nb_wayland_demo_ui_pointer_button(&app->ui, true));
    } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
        redraw_if_changed(
            app,
            nb_wayland_demo_ui_pointer_button(&app->ui, false));
    }
}

static void pointer_axis(void *data,
                         struct wl_pointer *pointer,
                         uint32_t time,
                         uint32_t axis,
                         wl_fixed_t value)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data;
    (void)pointer;
}

static void pointer_axis_source(void *data,
                                struct wl_pointer *pointer,
                                uint32_t source)
{
    (void)data;
    (void)pointer;
    (void)source;
}

static void pointer_axis_stop(void *data,
                              struct wl_pointer *pointer,
                              uint32_t time,
                              uint32_t axis)
{
    (void)data;
    (void)pointer;
    (void)time;
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

static void release_pointer(struct demo_app *app)
{
    if (app->pointer == NULL) {
        return;
    }
    if (wl_proxy_get_version((struct wl_proxy *)app->pointer) >=
        WL_POINTER_RELEASE_SINCE_VERSION) {
        wl_pointer_release(app->pointer);
    } else {
        wl_pointer_destroy(app->pointer);
    }
    app->pointer = NULL;
    redraw_if_changed(app, nb_wayland_demo_ui_pointer_leave(&app->ui));
}

static void seat_capabilities(void *data,
                              struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct demo_app *app = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 &&
        app->pointer == NULL) {
        app->pointer = wl_seat_get_pointer(seat);
        if (app->pointer == NULL ||
            wl_pointer_add_listener(app->pointer,
                                    &pointer_listener,
                                    app) < 0) {
            release_pointer(app);
            fail_app(app, "Could not bind the Wayland pointer");
        }
    } else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0) {
        release_pointer(app);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name
};

static void release_seat(struct demo_app *app)
{
    release_pointer(app);
    if (app->seat == NULL) {
        return;
    }
    if (wl_proxy_get_version((struct wl_proxy *)app->seat) >=
        WL_SEAT_RELEASE_SINCE_VERSION) {
        wl_seat_release(app->seat);
    } else {
        wl_seat_destroy(app->seat);
    }
    app->seat = NULL;
    app->seat_global_name = 0;
}

static void shm_format(void *data, struct wl_shm *shm, uint32_t format)
{
    struct demo_app *app = data;

    (void)shm;
    if (format == WL_SHM_FORMAT_XRGB8888) {
        app->saw_xrgb8888 = true;
        app->shm_format = WL_SHM_FORMAT_XRGB8888;
    } else if (format == WL_SHM_FORMAT_ARGB8888) {
        app->saw_argb8888 = true;
        if (!app->saw_xrgb8888) {
            app->shm_format = WL_SHM_FORMAT_ARGB8888;
        }
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
    struct demo_app *app = data;

    xdg_surface_ack_configure(xdg_surface, serial);
    if (app->pending_width <= 0 || app->pending_height <= 0 ||
        app->pending_width > DEMO_MAX_DIMENSION ||
        app->pending_height > DEMO_MAX_DIMENSION) {
        fail_app(app, "The compositor configured an unsupported window size");
        return;
    }
    app->width = app->pending_width;
    app->height = app->pending_height;
    app->configured = true;
    nb_wayland_demo_ui_resize(&app->ui, app->width, app->height);
    retire_mismatched_buffers(app);
    app->dirty = true;
    maybe_draw(app);
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
    struct demo_app *app = data;

    (void)toplevel;
    (void)states;
    if (width > 0) {
        app->pending_width = width;
    }
    if (height > 0) {
        app->pending_height = height;
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct demo_app *app = data;

    (void)toplevel;
    app->running = false;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close
};

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
    struct demo_app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0 &&
        app->compositor == NULL) {
        const uint32_t bind_version =
            version < DEMO_COMPOSITOR_VERSION
                ? version
                : DEMO_COMPOSITOR_VERSION;

        app->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, bind_version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0 &&
               app->shm == NULL) {
        app->shm = wl_registry_bind(registry,
                                    name,
                                    &wl_shm_interface,
                                    1);
        if (app->shm == NULL ||
            wl_shm_add_listener(app->shm, &shm_listener, app) < 0) {
            fail_app(app, "Could not bind Wayland shared memory");
        }
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0 &&
               app->wm_base == NULL) {
        app->wm_base = wl_registry_bind(registry,
                                        name,
                                        &xdg_wm_base_interface,
                                        1);
        if (app->wm_base == NULL ||
            xdg_wm_base_add_listener(app->wm_base,
                                     &wm_base_listener,
                                     app) < 0) {
            fail_app(app, "Could not bind xdg_wm_base");
        }
    } else if (strcmp(interface, wl_seat_interface.name) == 0 &&
               app->seat == NULL) {
        const uint32_t bind_version = version < DEMO_SEAT_VERSION
                                          ? version
                                          : DEMO_SEAT_VERSION;

        app->seat = wl_registry_bind(registry,
                                     name,
                                     &wl_seat_interface,
                                     bind_version);
        app->seat_global_name = name;
        if (app->seat == NULL ||
            wl_seat_add_listener(app->seat, &seat_listener, app) < 0) {
            fail_app(app, "Could not bind the Wayland seat");
        }
    }
}

static void registry_global_remove(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name)
{
    struct demo_app *app = data;

    (void)registry;
    if (name == app->seat_global_name) {
        release_seat(app);
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove
};

static bool initialize_app(struct demo_app *app)
{
    app->display = wl_display_connect(NULL);
    if (app->display == NULL) {
        fail_app(app, "Could not connect to a Wayland compositor");
        return false;
    }
    app->registry = wl_display_get_registry(app->display);
    if (app->registry == NULL ||
        wl_registry_add_listener(app->registry,
                                 &registry_listener,
                                 app) < 0 ||
        wl_display_roundtrip(app->display) < 0 ||
        wl_display_roundtrip(app->display) < 0) {
        fail_app(app, "Could not discover Wayland globals");
        return false;
    }
    if (app->failed || app->compositor == NULL || app->shm == NULL ||
        app->wm_base == NULL) {
        fail_app(app,
                 "The compositor lacks wl_compositor, wl_shm, or xdg_wm_base");
        return false;
    }
    if (!app->saw_xrgb8888 && !app->saw_argb8888) {
        fail_app(app, "The compositor lacks a supported wl_shm pixel format");
        return false;
    }
    if (app->seat == NULL || app->pointer == NULL) {
        fputs("Wayland pointer input is unavailable; the demo remains "
              "viewable.\n",
              stderr);
    }

    app->surface = wl_compositor_create_surface(app->compositor);
    if (app->surface == NULL) {
        fail_app(app, "Could not create a Wayland surface");
        return false;
    }
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base,
                                                   app->surface);
    if (app->xdg_surface == NULL ||
        xdg_surface_add_listener(app->xdg_surface,
                                 &xdg_surface_listener,
                                 app) < 0) {
        fail_app(app, "Could not create an xdg_surface");
        return false;
    }
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    if (app->toplevel == NULL ||
        xdg_toplevel_add_listener(app->toplevel,
                                  &toplevel_listener,
                                  app) < 0) {
        fail_app(app, "Could not create an xdg_toplevel");
        return false;
    }
    xdg_toplevel_set_title(app->toplevel, "NixBench Wayland Demo");
    xdg_toplevel_set_app_id(app->toplevel,
                            "org.nixbench.WaylandDemo");
    wl_surface_commit(app->surface);
    return true;
}

static void destroy_app(struct demo_app *app)
{
    app->running = false;
    if (app->frame_callback != NULL) {
        wl_callback_destroy(app->frame_callback);
        app->frame_callback = NULL;
    }
    while (app->buffers != NULL) {
        destroy_buffer(app->buffers);
    }
    release_seat(app);
    if (app->toplevel != NULL) {
        xdg_toplevel_destroy(app->toplevel);
    }
    if (app->xdg_surface != NULL) {
        xdg_surface_destroy(app->xdg_surface);
    }
    if (app->surface != NULL) {
        wl_surface_destroy(app->surface);
    }
    if (app->wm_base != NULL) {
        xdg_wm_base_destroy(app->wm_base);
    }
    if (app->shm != NULL) {
        wl_shm_destroy(app->shm);
    }
    if (app->compositor != NULL) {
        wl_compositor_destroy(app->compositor);
    }
    if (app->registry != NULL) {
        wl_registry_destroy(app->registry);
    }
    if (app->display != NULL) {
        (void)wl_display_flush(app->display);
        wl_display_disconnect(app->display);
    }
}

static void print_usage(const char *program)
{
    printf("Usage: %s [--exit-after-first-frame] [--help]\n"
           "\n"
           "Open a small wl_shm/xdg-shell client on WAYLAND_DISPLAY.\n",
           program);
}

static bool parse_options(int argc,
                          char **argv,
                          struct demo_options *options,
                          int *exit_status)
{
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--exit-after-first-frame") == 0) {
            options->exit_after_first_frame = true;
        } else if (strcmp(argv[index], "--help") == 0) {
            print_usage(argv[0]);
            *exit_status = 0;
            return false;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[index]);
            print_usage(argv[0]);
            *exit_status = 2;
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    struct demo_options options = {false};
    struct demo_app app;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }

    memset(&app, 0, sizeof(app));
    app.width = DEMO_DEFAULT_WIDTH;
    app.height = DEMO_DEFAULT_HEIGHT;
    app.pending_width = DEMO_DEFAULT_WIDTH;
    app.pending_height = DEMO_DEFAULT_HEIGHT;
    app.running = true;
    app.exit_after_first_frame = options.exit_after_first_frame;
    nb_wayland_demo_ui_init(&app.ui, app.width, app.height);

    if (initialize_app(&app)) {
        while (app.running) {
            if (wl_display_dispatch(app.display) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (app.running) {
                    fail_app(&app, "The Wayland connection was lost");
                }
                break;
            }
        }
    }

    exit_status = app.failed ? 1 : 0;
    destroy_app(&app);
    return exit_status;
}
