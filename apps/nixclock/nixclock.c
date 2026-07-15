#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "nixbench-application-menu-v1-client-protocol.h"
#include "nixclock_ui.h"
#include "xdg-shell-client-protocol.h"

#ifndef NIXCLOCK_VERSION
#define NIXCLOCK_VERSION "development"
#endif

enum {
    NIXCLOCK_DEFAULT_WIDTH = 560,
    NIXCLOCK_DEFAULT_HEIGHT = 300,
    NIXCLOCK_MAX_DIMENSION = 4096,
    NIXCLOCK_MAX_BUFFERS = 3,
    NIXCLOCK_BYTES_PER_PIXEL = 4,
    NIXCLOCK_COMPOSITOR_VERSION = 4,
    NIXCLOCK_COMMAND_QUIT = 1,
    NIXCLOCK_COMMAND_SHOW_SECONDS = 2
};

struct nixclock_options {
    bool show_seconds;
    bool exit_after_first_frame;
};

struct nixclock_app;

struct nixclock_buffer {
    struct nixclock_app *app;
    struct wl_buffer *proxy;
    uint32_t *pixels;
    size_t size;
    int width;
    int height;
    bool busy;
    bool retired;
    struct nixclock_buffer *next;
};

struct nixclock_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct nixbench_application_menu_manager_v1 *menu_manager;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct nixbench_application_menu_v1 *menu;
    struct wl_callback *frame_callback;
    struct nixclock_buffer *buffers;
    size_t buffer_count;
    struct nb_nixclock_ui ui;
    uint32_t shm_format;
    int width;
    int height;
    int pending_width;
    int pending_height;
    unsigned int presented_frames;
    bool saw_argb8888;
    bool saw_xrgb8888;
    bool configured;
    bool menu_published;
    bool dirty;
    bool frame_pending;
    bool running;
    bool failed;
    bool exit_after_first_frame;
};

static void print_usage(const char *program)
{
    printf("Usage: %s [OPTION]\n", program);
    puts("Open the NixClock analog clock on a NixBench desktop.\n");
    puts("  --show-seconds            show the seconds hand initially");
    puts("  --exit-after-first-frame  exit after one presented frame");
    puts("  --help                    show this help");
    puts("  --version                 show the NixClock version");
}

static bool parse_options(int argc,
                          char *argv[],
                          struct nixclock_options *options,
                          int *exit_status)
{
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--show-seconds") == 0) {
            options->show_seconds = true;
        } else if (strcmp(argv[index], "--exit-after-first-frame") == 0) {
            options->exit_after_first_frame = true;
        } else if (strcmp(argv[index], "--help") == 0) {
            print_usage(argv[0]);
            return false;
        } else if (strcmp(argv[index], "--version") == 0) {
            printf("NixClock %s\n", NIXCLOCK_VERSION);
            return false;
        } else {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[index]);
            fprintf(stderr, "Try '%s --help' for more information.\n",
                    argv[0]);
            *exit_status = 2;
            return false;
        }
    }
    return true;
}

static void fail_app(struct nixclock_app *app, const char *message)
{
    if (!app->failed) {
        fprintf(stderr, "nixclock: %s\n", message);
    }
    app->failed = true;
    app->running = false;
}

static int create_anonymous_file(size_t size)
{
    static const char suffix[] = "/nixclock-XXXXXX";
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
    if (unlink(path) != 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
        ftruncate(fd, (off_t)size) != 0) {
        const int saved_errno = errno;

        (void)close(fd);
        free(path);
        errno = saved_errno;
        return -1;
    }
    free(path);
    return fd;
}

static void unlink_buffer(struct nixclock_buffer *buffer)
{
    struct nixclock_buffer **link = &buffer->app->buffers;

    while (*link != NULL && *link != buffer) {
        link = &(*link)->next;
    }
    if (*link == buffer) {
        *link = buffer->next;
        --buffer->app->buffer_count;
    }
}

static void destroy_buffer(struct nixclock_buffer *buffer)
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
    struct nixclock_buffer *buffer = data;

    (void)proxy;
    buffer->busy = false;
    if (buffer->retired) {
        destroy_buffer(buffer);
    }
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release
};

static struct nixclock_buffer *create_buffer(struct nixclock_app *app,
                                             int width,
                                             int height)
{
    struct nixclock_buffer *buffer;
    struct wl_shm_pool *pool;
    size_t size;
    int stride;
    int fd;

    if (width <= 0 || height <= 0 ||
        width > NIXCLOCK_MAX_DIMENSION ||
        height > NIXCLOCK_MAX_DIMENSION ||
        width > INT_MAX / NIXCLOCK_BYTES_PER_PIXEL) {
        errno = EINVAL;
        return NULL;
    }
    stride = width * NIXCLOCK_BYTES_PER_PIXEL;
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
    buffer->next = app->buffers;
    app->buffers = buffer;
    ++app->buffer_count;
    return buffer;
}

static void retire_wrong_size_buffers(struct nixclock_app *app)
{
    struct nixclock_buffer *buffer = app->buffers;

    while (buffer != NULL) {
        struct nixclock_buffer *next = buffer->next;

        if (buffer->width != app->width || buffer->height != app->height) {
            if (buffer->busy) {
                buffer->retired = true;
            } else {
                destroy_buffer(buffer);
            }
        }
        buffer = next;
    }
}

static struct nixclock_buffer *acquire_buffer(struct nixclock_app *app)
{
    struct nixclock_buffer *buffer;

    for (buffer = app->buffers; buffer != NULL; buffer = buffer->next) {
        if (!buffer->busy && !buffer->retired &&
            buffer->width == app->width && buffer->height == app->height) {
            return buffer;
        }
    }
    if (app->buffer_count >= NIXCLOCK_MAX_BUFFERS) {
        return NULL;
    }
    return create_buffer(app, app->width, app->height);
}

static bool read_local_time(struct nb_nixclock_local_time *local_time)
{
    struct timespec now;
    struct tm converted;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
        localtime_r(&now.tv_sec, &converted) == NULL) {
        return false;
    }
    local_time->hour = (unsigned int)converted.tm_hour;
    local_time->minute = (unsigned int)converted.tm_min;
    local_time->second = (unsigned int)converted.tm_sec;
    local_time->millisecond = (unsigned int)(now.tv_nsec / 1000000L);
    return nb_nixclock_local_time_is_valid(local_time);
}

static void frame_done(void *data,
                       struct wl_callback *callback,
                       uint32_t milliseconds)
{
    struct nixclock_app *app = data;

    (void)milliseconds;
    if (app->frame_callback == callback) {
        app->frame_callback = NULL;
        app->frame_pending = false;
    }
    wl_callback_destroy(callback);
    ++app->presented_frames;
    if (app->exit_after_first_frame && app->presented_frames >= 1U) {
        app->running = false;
    }
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done
};

static bool draw_if_possible(struct nixclock_app *app)
{
    struct nb_nixclock_local_time local_time;
    struct nixclock_buffer *buffer;

    if (!app->running || !app->dirty || !app->configured ||
        app->frame_pending ||
        app->width <= 0 || app->height <= 0) {
        return true;
    }
    buffer = acquire_buffer(app);
    if (buffer == NULL) {
        if (app->buffer_count >= NIXCLOCK_MAX_BUFFERS) {
            return true;
        }
        fail_app(app, "could not allocate a clock drawing buffer");
        return false;
    }
    if (!read_local_time(&local_time) ||
        !nb_nixclock_ui_set_local_time(&app->ui, &local_time) ||
        !nb_nixclock_ui_render(&app->ui,
                               buffer->pixels,
                               (size_t)app->width)) {
        fail_app(app, "could not render the current clock face");
        return false;
    }

    app->frame_callback = wl_surface_frame(app->surface);
    if (app->frame_callback == NULL ||
        wl_callback_add_listener(app->frame_callback,
                                 &frame_listener,
                                 app) < 0) {
        if (app->frame_callback != NULL) {
            wl_callback_destroy(app->frame_callback);
            app->frame_callback = NULL;
        }
        fail_app(app, "could not request a clock frame callback");
        return false;
    }
    wl_surface_attach(app->surface, buffer->proxy, 0, 0);
    if (wl_proxy_get_version((struct wl_proxy *)app->surface) >= 4) {
        wl_surface_damage_buffer(app->surface,
                                 0,
                                 0,
                                 app->width,
                                 app->height);
    } else {
        wl_surface_damage(app->surface, 0, 0, app->width, app->height);
    }
    wl_surface_commit(app->surface);
    buffer->busy = true;
    app->frame_pending = true;
    app->dirty = false;
    return true;
}

static bool publish_menu(struct nixclock_app *app)
{
    uint32_t seconds_flags =
        NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED;

    if (app->menu_published) {
        nixbench_application_menu_v1_reset(app->menu);
    }
    if (app->ui.show_seconds) {
        seconds_flags |=
            NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_CHECKED;
    }
    nixbench_application_menu_v1_append_menu(app->menu, "NixClock");
    nixbench_application_menu_v1_append_item(
        app->menu,
        "Quit",
        NIXCLOCK_COMMAND_QUIT,
        NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED);
    nixbench_application_menu_v1_append_menu(app->menu, "Settings");
    nixbench_application_menu_v1_append_item(
        app->menu,
        "Show seconds",
        NIXCLOCK_COMMAND_SHOW_SECONDS,
        seconds_flags);
    nixbench_application_menu_v1_commit(app->menu);
    app->menu_published = true;
    return true;
}

static void menu_command(void *data,
                         struct nixbench_application_menu_v1 *menu,
                         uint32_t command)
{
    struct nixclock_app *app = data;

    (void)menu;
    if (command == NIXCLOCK_COMMAND_QUIT) {
        app->running = false;
    } else if (command == NIXCLOCK_COMMAND_SHOW_SECONDS) {
        (void)nb_nixclock_ui_set_show_seconds(
            &app->ui,
            !app->ui.show_seconds);
        (void)publish_menu(app);
        app->dirty = true;
    }
}

static const struct nixbench_application_menu_v1_listener menu_listener = {
    .command = menu_command
};

static void shm_format(void *data, struct wl_shm *shm, uint32_t format)
{
    struct nixclock_app *app = data;

    (void)shm;
    if (format == WL_SHM_FORMAT_XRGB8888) {
        app->saw_xrgb8888 = true;
    } else if (format == WL_SHM_FORMAT_ARGB8888) {
        app->saw_argb8888 = true;
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

static void toplevel_configure(void *data,
                               struct xdg_toplevel *toplevel,
                               int32_t width,
                               int32_t height,
                               struct wl_array *states)
{
    struct nixclock_app *app = data;

    (void)toplevel;
    (void)states;
    if (width > 0 && height > 0 &&
        width <= NIXCLOCK_MAX_DIMENSION &&
        height <= NIXCLOCK_MAX_DIMENSION) {
        app->pending_width = width;
        app->pending_height = height;
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct nixclock_app *app = data;

    (void)toplevel;
    app->running = false;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close
};

static void surface_configure(void *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t serial)
{
    struct nixclock_app *app = data;

    xdg_surface_ack_configure(xdg_surface, serial);
    if (app->pending_width > 0 && app->pending_height > 0 &&
        (app->width != app->pending_width ||
         app->height != app->pending_height)) {
        app->width = app->pending_width;
        app->height = app->pending_height;
        nb_nixclock_ui_resize(&app->ui, app->width, app->height);
        retire_wrong_size_buffers(app);
    }
    app->configured = true;
    app->dirty = true;
}

static const struct xdg_surface_listener surface_listener = {
    .configure = surface_configure
};

static void registry_global(void *data,
                            struct wl_registry *registry,
                            uint32_t name,
                            const char *interface,
                            uint32_t version)
{
    struct nixclock_app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0 &&
        app->compositor == NULL) {
        const uint32_t bind_version =
            version < NIXCLOCK_COMPOSITOR_VERSION
                ? version
                : NIXCLOCK_COMPOSITOR_VERSION;

        app->compositor = wl_registry_bind(registry,
                                           name,
                                           &wl_compositor_interface,
                                           bind_version);
        if (app->compositor == NULL) {
            fail_app(app, "could not bind the Wayland compositor");
        }
    } else if (strcmp(interface, wl_shm_interface.name) == 0 &&
               app->shm == NULL) {
        app->shm = wl_registry_bind(registry,
                                    name,
                                    &wl_shm_interface,
                                    1);
        if (app->shm == NULL ||
            wl_shm_add_listener(app->shm, &shm_listener, app) < 0) {
            fail_app(app, "could not bind Wayland shared memory");
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
            fail_app(app, "could not bind xdg_wm_base");
        }
    } else if (strcmp(
                   interface,
                   nixbench_application_menu_manager_v1_interface.name) ==
                   0 &&
               app->menu_manager == NULL) {
        app->menu_manager = wl_registry_bind(
            registry,
            name,
            &nixbench_application_menu_manager_v1_interface,
            1);
        if (app->menu_manager == NULL) {
            fail_app(app, "could not bind the NixBench menu manager");
        }
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

static bool initialize_app(struct nixclock_app *app,
                           const struct nixclock_options *options)
{
    app->width = NIXCLOCK_DEFAULT_WIDTH;
    app->height = NIXCLOCK_DEFAULT_HEIGHT;
    app->pending_width = app->width;
    app->pending_height = app->height;
    app->running = true;
    app->dirty = true;
    app->exit_after_first_frame = options->exit_after_first_frame;
    nb_nixclock_ui_init(&app->ui, app->width, app->height);
    (void)nb_nixclock_ui_set_show_seconds(&app->ui,
                                          options->show_seconds);

    app->display = wl_display_connect(NULL);
    if (app->display == NULL) {
        fail_app(app, "could not connect to WAYLAND_DISPLAY");
        return false;
    }
    app->registry = wl_display_get_registry(app->display);
    if (app->registry == NULL ||
        wl_registry_add_listener(app->registry,
                                 &registry_listener,
                                 app) < 0 ||
        wl_display_roundtrip(app->display) < 0 || app->failed) {
        fail_app(app, "could not discover Wayland globals");
        return false;
    }
    if (app->compositor == NULL || app->shm == NULL ||
        app->wm_base == NULL || app->menu_manager == NULL) {
        fail_app(app,
                 "the compositor lacks a required NixClock protocol");
        return false;
    }
    if (wl_display_roundtrip(app->display) < 0) {
        fail_app(app, "could not initialize Wayland globals");
        return false;
    }
    if (!app->saw_xrgb8888 && !app->saw_argb8888) {
        fail_app(app, "the compositor offers no 32-bit wl_shm format");
        return false;
    }
    app->shm_format = app->saw_xrgb8888
                          ? WL_SHM_FORMAT_XRGB8888
                          : WL_SHM_FORMAT_ARGB8888;

    app->surface = wl_compositor_create_surface(app->compositor);
    if (app->surface == NULL) {
        fail_app(app, "could not create the clock surface");
        return false;
    }
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base,
                                                   app->surface);
    if (app->xdg_surface == NULL ||
        xdg_surface_add_listener(app->xdg_surface,
                                 &surface_listener,
                                 app) < 0) {
        fail_app(app, "could not create the xdg surface");
        return false;
    }
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    if (app->toplevel == NULL ||
        xdg_toplevel_add_listener(app->toplevel,
                                  &toplevel_listener,
                                  app) < 0) {
        fail_app(app, "could not create the NixClock toplevel");
        return false;
    }
    xdg_toplevel_set_title(app->toplevel, "NixClock");
    xdg_toplevel_set_app_id(app->toplevel, "org.nixbench.NixClock");

    app->menu = nixbench_application_menu_manager_v1_get_menu(
        app->menu_manager,
        app->surface);
    if (app->menu == NULL ||
        nixbench_application_menu_v1_add_listener(app->menu,
                                                   &menu_listener,
                                                   app) < 0 ||
        !publish_menu(app)) {
        fail_app(app, "could not publish the NixClock application menu");
        return false;
    }

    wl_surface_commit(app->surface);
    if (wl_display_roundtrip(app->display) < 0) {
        fail_app(app, "the compositor rejected NixClock setup");
        return false;
    }
    return true;
}

static uint32_t next_update_delay(const struct nixclock_app *app)
{
    struct nb_nixclock_local_time local_time;
    uint32_t delay_ms;

    if (!read_local_time(&local_time) ||
        !nb_nixclock_next_update_delay_ms(&local_time,
                                          app->ui.show_seconds,
                                          &delay_ms)) {
        return UINT32_C(1000);
    }
    return delay_ms;
}

static bool dispatch_pending(struct nixclock_app *app)
{
    if (wl_display_dispatch_pending(app->display) < 0) {
        fail_app(app, "the Wayland connection failed");
        return false;
    }
    return draw_if_possible(app);
}

static bool run_app(struct nixclock_app *app)
{
    const int display_fd = wl_display_get_fd(app->display);

    if (display_fd < 0) {
        fail_app(app, "could not obtain the Wayland connection descriptor");
        return false;
    }

    while (app->running) {
        struct pollfd descriptor;
        uint32_t delay_ms;
        int flush_result;
        int poll_result;
        bool write_blocked;

        if (!dispatch_pending(app)) {
            break;
        }
        if (!app->running) {
            break;
        }
        while (wl_display_prepare_read(app->display) != 0) {
            if (!dispatch_pending(app)) {
                return false;
            }
            if (!app->running) {
                return !app->failed;
            }
        }

        errno = 0;
        flush_result = wl_display_flush(app->display);
        write_blocked = flush_result < 0 && errno == EAGAIN;
        if (flush_result < 0 && !write_blocked) {
            wl_display_cancel_read(app->display);
            fail_app(app, "could not flush the Wayland connection");
            break;
        }

        delay_ms = next_update_delay(app);
        descriptor.fd = display_fd;
        descriptor.events = POLLIN;
        if (write_blocked) {
            descriptor.events |= POLLOUT;
        }
        descriptor.revents = 0;
        poll_result = poll(&descriptor,
                           1,
                           delay_ms > (uint32_t)INT_MAX
                               ? INT_MAX
                               : (int)delay_ms);
        if (poll_result < 0) {
            const int saved_errno = errno;

            wl_display_cancel_read(app->display);
            if (saved_errno == EINTR) {
                continue;
            }
            fail_app(app, "could not wait for Wayland events");
            break;
        }

        if (poll_result > 0 && (descriptor.revents & POLLIN) != 0) {
            if (wl_display_read_events(app->display) < 0) {
                fail_app(app, "could not read Wayland events");
                break;
            }
        } else {
            wl_display_cancel_read(app->display);
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fail_app(app, "the Wayland connection closed unexpectedly");
            break;
        }
        if ((descriptor.revents & POLLOUT) != 0 &&
            wl_display_flush(app->display) < 0 && errno != EAGAIN) {
            fail_app(app, "could not resume the Wayland connection");
            break;
        }
        if (poll_result == 0) {
            app->dirty = true;
        }
    }
    return !app->failed;
}

static void destroy_app(struct nixclock_app *app)
{
    while (app->buffers != NULL) {
        destroy_buffer(app->buffers);
    }
    if (app->frame_callback != NULL) {
        wl_callback_destroy(app->frame_callback);
    }
    if (app->menu != NULL) {
        nixbench_application_menu_v1_destroy(app->menu);
    }
    if (app->toplevel != NULL) {
        xdg_toplevel_destroy(app->toplevel);
    }
    if (app->xdg_surface != NULL) {
        xdg_surface_destroy(app->xdg_surface);
    }
    if (app->surface != NULL) {
        wl_surface_destroy(app->surface);
    }
    if (app->menu_manager != NULL) {
        nixbench_application_menu_manager_v1_destroy(app->menu_manager);
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
    memset(app, 0, sizeof(*app));
}

int main(int argc, char *argv[])
{
    struct nixclock_options options = {false, false};
    struct nixclock_app app;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }
    memset(&app, 0, sizeof(app));
    if (!initialize_app(&app, &options) || !run_app(&app)) {
        exit_status = 1;
    }
    destroy_app(&app);
    return exit_status;
}
