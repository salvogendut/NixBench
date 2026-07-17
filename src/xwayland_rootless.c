#define _POSIX_C_SOURCE 200809L

#include "xwayland_rootless.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <xcb/composite.h>
#include <xcb/xcb.h>

#include "desktop_runtime.h"
#include "window.h"

enum {
    NB_XWAYLAND_FIRST_DISPLAY = 98,
    NB_XWAYLAND_LAST_DISPLAY = 109,
    NB_XWAYLAND_MAX_WINDOWS = 32,
    NB_XWAYLAND_EXIT_WAIT_MS = 2000,
    NB_XWAYLAND_EXIT_WAIT_SLICE_MS = 20
};

struct nb_xwayland_window {
    xcb_window_t window;
    uint32_t surface_id;
    uint64_t surface_serial;
    bool occupied;
    bool associated;
    bool association_pending_reported;
    char title[NB_WINDOW_TITLE_CAPACITY];
};

struct nb_xwayland_rootless {
    struct nb_desktop_runtime *desktop;
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    pid_t process;
    xcb_atom_t wl_surface_id;
    xcb_atom_t wl_surface_serial;
    xcb_atom_t net_wm_name;
    xcb_atom_t utf8_string;
    xcb_atom_t wm_name;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_atom_t wm_selection;
    xcb_window_t wm_window;
    xcb_connection_t *pending_connection;
    pthread_t connector_thread;
    int startup_pipe[2];
    int connector_descriptor;
    bool connector_running;
    bool ready;
    char display_name[16];
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct nb_xwayland_window windows[NB_XWAYLAND_MAX_WINDOWS];
};

static bool set_close_on_exec(int descriptor, bool close_on_exec)
{
    int flags = fcntl(descriptor, F_GETFD);

    if (flags < 0) {
        return false;
    }
    if (close_on_exec) {
        flags |= FD_CLOEXEC;
    } else {
        flags &= ~FD_CLOEXEC;
    }
    return fcntl(descriptor, F_SETFD, flags) == 0;
}

static bool suppress_socket_sigpipe(int descriptor)
{
#if defined(SO_NOSIGPIPE)
    const int enabled = 1;

    return setsockopt(descriptor,
                      SOL_SOCKET,
                      SO_NOSIGPIPE,
                      &enabled,
                      sizeof(enabled)) == 0;
#else
    (void)descriptor;
    return true;
#endif
}

static void *connect_xwm(void *context)
{
    struct nb_xwayland_rootless *service = context;
    const char notification = 'X';
    ssize_t written;

    service->pending_connection =
        xcb_connect_to_fd(service->connector_descriptor, NULL);
    do {
        written = write(service->startup_pipe[1],
                        &notification,
                        sizeof(notification));
    } while (written < 0 && errno == EINTR);
    return NULL;
}

static int create_listen_socket(char *path,
                                size_t path_size,
                                char *display_name,
                                size_t display_name_size)
{
    struct stat directory_status;
    int display;

    if (lstat("/tmp/.X11-unix", &directory_status) != 0 ||
        !S_ISDIR(directory_status.st_mode) ||
        (directory_status.st_mode & (S_IWGRP | S_IWOTH)) == 0) {
        fprintf(stderr,
                "Rootless Xwayland is unavailable: /tmp/.X11-unix is "
                "missing or not writable\n");
        return -1;
    }
    for (display = NB_XWAYLAND_FIRST_DISPLAY;
         display <= NB_XWAYLAND_LAST_DISPLAY;
         ++display) {
        struct sockaddr_un address;
        int descriptor;
        int length;

        length = snprintf(path, path_size, "/tmp/.X11-unix/X%d", display);
        if (length < 0 || (size_t)length >= path_size) {
            return -1;
        }
        if (lstat(path, &directory_status) == 0 || errno != ENOENT) {
            continue;
        }
        descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
        if (descriptor < 0) {
            return -1;
        }
        memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        (void)snprintf(address.sun_path,
                       sizeof(address.sun_path),
                       "%s",
                       path);
        if (bind(descriptor,
                 (const struct sockaddr *)&address,
                 sizeof(address)) == 0 &&
            chmod(path, 0600) == 0 &&
            listen(descriptor, 16) == 0 &&
            set_close_on_exec(descriptor, true)) {
            length = snprintf(display_name,
                              display_name_size,
                              ":%d",
                              display);
            if (length >= 0 && (size_t)length < display_name_size) {
                return descriptor;
            }
        }
        (void)close(descriptor);
        (void)unlink(path);
    }
    fprintf(stderr,
            "Rootless Xwayland is unavailable: no free display from :%d "
            "through :%d\n",
            NB_XWAYLAND_FIRST_DISPLAY,
            NB_XWAYLAND_LAST_DISPLAY);
    return -1;
}

static xcb_atom_t intern_atom(xcb_connection_t *connection,
                              const char *name,
                              bool only_if_exists)
{
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        connection,
        xcb_intern_atom(connection,
                        only_if_exists ? 1 : 0,
                        (uint16_t)strlen(name),
                        name),
        NULL);
    xcb_atom_t atom = XCB_ATOM_NONE;

    if (reply != NULL) {
        atom = reply->atom;
        free(reply);
    }
    return atom;
}

static bool checked_request_succeeded(xcb_connection_t *connection,
                                      xcb_void_cookie_t cookie,
                                      const char *operation)
{
    xcb_generic_error_t *error = xcb_request_check(connection, cookie);

    if (error == NULL) {
        return true;
    }
    fprintf(stderr,
            "Rootless XWM could not %s (X11 error %u)\n",
            operation,
            (unsigned int)error->error_code);
    free(error);
    return false;
}

static struct nb_xwayland_window *find_window(
    struct nb_xwayland_rootless *service,
    xcb_window_t window)
{
    size_t index;

    for (index = 0; index < NB_XWAYLAND_MAX_WINDOWS; ++index) {
        if (service->windows[index].occupied &&
            service->windows[index].window == window) {
            return &service->windows[index];
        }
    }
    return NULL;
}

static struct nb_xwayland_window *remember_window(
    struct nb_xwayland_rootless *service,
    xcb_window_t window)
{
    struct nb_xwayland_window *entry = find_window(service, window);
    size_t index;

    if (entry != NULL) {
        return entry;
    }
    for (index = 0; index < NB_XWAYLAND_MAX_WINDOWS; ++index) {
        if (!service->windows[index].occupied) {
            entry = &service->windows[index];
            memset(entry, 0, sizeof(*entry));
            entry->occupied = true;
            entry->window = window;
            (void)snprintf(entry->title,
                           sizeof(entry->title),
                           "%s",
                           "X11 Application");
            return entry;
        }
    }
    return NULL;
}

static bool read_title_property(struct nb_xwayland_rootless *service,
                                xcb_window_t window,
                                xcb_atom_t property,
                                xcb_atom_t expected_type,
                                char *title,
                                size_t title_size)
{
    xcb_get_property_reply_t *reply;
    const unsigned char *value;
    int value_length;
    size_t copied = 0;

    if (property == XCB_ATOM_NONE || title_size == 0) {
        return false;
    }
    reply = xcb_get_property_reply(
        service->connection,
        xcb_get_property(service->connection,
                         0,
                         window,
                         property,
                         expected_type,
                         0,
                         1024),
        NULL);
    if (reply == NULL || reply->format != 8 ||
        (expected_type != XCB_GET_PROPERTY_TYPE_ANY &&
         reply->type != expected_type)) {
        free(reply);
        return false;
    }
    value = xcb_get_property_value(reply);
    value_length = xcb_get_property_value_length(reply);
    while (copied + 1U < title_size && copied < (size_t)value_length &&
           value[copied] != '\0') {
        const unsigned char character = value[copied];

        title[copied] = character < 32 ? ' ' : (char)character;
        ++copied;
    }
    title[copied] = '\0';
    free(reply);
    return copied != 0;
}

static void refresh_window_title(struct nb_xwayland_rootless *service,
                                 struct nb_xwayland_window *entry)
{
    char title[NB_WINDOW_TITLE_CAPACITY];

    if (!read_title_property(service,
                             entry->window,
                             service->net_wm_name,
                             service->utf8_string,
                             title,
                             sizeof(title)) &&
        !read_title_property(service,
                             entry->window,
                             service->wm_name,
                             XCB_GET_PROPERTY_TYPE_ANY,
                             title,
                             sizeof(title))) {
        (void)snprintf(title, sizeof(title), "%s", "X11 Application");
    }
    (void)snprintf(entry->title, sizeof(entry->title), "%s", title);
    if (entry->associated) {
        (void)nb_desktop_runtime_update_xwayland_title(service->desktop,
                                                       entry->window,
                                                       entry->title);
    }
}

static void try_associate_window(struct nb_xwayland_rootless *service,
                                 struct nb_xwayland_window *entry)
{
    if (entry->associated ||
        (entry->surface_serial == 0 && entry->surface_id == 0)) {
        return;
    }
    refresh_window_title(service, entry);
    if (entry->surface_serial != 0) {
        entry->associated = nb_desktop_runtime_associate_xwayland_serial(
            service->desktop,
            entry->surface_serial,
            entry->window,
            entry->title);
    } else {
        entry->associated = nb_desktop_runtime_associate_xwayland_surface(
            service->desktop,
            entry->surface_id,
            entry->window,
            entry->title);
    }
    if (entry->associated) {
        if (entry->surface_serial != 0) {
            fprintf(stderr,
                    "Rootless XWM associated X window %#x with Wayland "
                    "surface serial %llu\n",
                    (unsigned int)entry->window,
                    (unsigned long long)entry->surface_serial);
        } else {
            fprintf(stderr,
                    "Rootless XWM associated X window %#x with Wayland "
                    "surface %u\n",
                    (unsigned int)entry->window,
                    entry->surface_id);
        }
    } else if (!entry->association_pending_reported) {
        if (entry->surface_serial != 0) {
            fprintf(stderr,
                    "Rootless XWM is waiting for Wayland surface serial "
                    "%llu for X window %#x\n",
                    (unsigned long long)entry->surface_serial,
                    (unsigned int)entry->window);
        } else {
            fprintf(stderr,
                    "Rootless XWM is waiting for Wayland surface %u for X "
                    "window %#x\n",
                    entry->surface_id,
                    (unsigned int)entry->window);
        }
        entry->association_pending_reported = true;
    }
}

static bool configure_native_window(void *context,
                                    uint32_t xwindow,
                                    int width,
                                    int height)
{
    struct nb_xwayland_rootless *service = context;
    const uint16_t mask = XCB_CONFIG_WINDOW_WIDTH |
                          XCB_CONFIG_WINDOW_HEIGHT;
    const uint32_t values[2] = {(uint32_t)width, (uint32_t)height};

    if (service == NULL || service->connection == NULL ||
        width <= 0 || height <= 0) {
        return false;
    }
    xcb_configure_window(service->connection,
                         (xcb_window_t)xwindow,
                         mask,
                         values);
    return xcb_flush(service->connection) > 0;
}

static bool close_native_window(void *context, uint32_t xwindow)
{
    struct nb_xwayland_rootless *service = context;
    xcb_client_message_event_t message;

    if (service == NULL || service->connection == NULL ||
        service->wm_protocols == XCB_ATOM_NONE ||
        service->wm_delete_window == XCB_ATOM_NONE) {
        return false;
    }
    memset(&message, 0, sizeof(message));
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = (xcb_window_t)xwindow;
    message.type = service->wm_protocols;
    message.data.data32[0] = service->wm_delete_window;
    message.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(service->connection,
                   0,
                   (xcb_window_t)xwindow,
                   XCB_EVENT_MASK_NO_EVENT,
                   (const char *)&message);
    return xcb_flush(service->connection) > 0;
}

static bool handle_map_request(struct nb_xwayland_rootless *service,
                               const xcb_map_request_event_t *event)
{
    const uint32_t event_mask = XCB_EVENT_MASK_PROPERTY_CHANGE |
                                XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    struct nb_xwayland_window *entry =
        remember_window(service, event->window);

    if (entry == NULL) {
        return false;
    }
    xcb_change_window_attributes(service->connection,
                                 event->window,
                                 XCB_CW_EVENT_MASK,
                                 &event_mask);
    refresh_window_title(service, entry);
    if (!checked_request_succeeded(
            service->connection,
            xcb_composite_redirect_window_checked(
                service->connection,
                event->window,
                XCB_COMPOSITE_REDIRECT_MANUAL),
            "redirect an X11 top-level") ||
        !checked_request_succeeded(
            service->connection,
            xcb_map_window_checked(service->connection, event->window),
            "map an X11 top-level")) {
        return false;
    }
    fprintf(stderr,
            "Rootless XWM accepted map request for X window %#x (%s)\n",
            (unsigned int)event->window,
            entry->title);
    return true;
}

static void handle_configure_request(
    struct nb_xwayland_rootless *service,
    const xcb_configure_request_event_t *event)
{
    uint32_t values[7];
    size_t count = 0;

    if ((event->value_mask & XCB_CONFIG_WINDOW_X) != 0) {
        values[count++] = (uint32_t)event->x;
    }
    if ((event->value_mask & XCB_CONFIG_WINDOW_Y) != 0) {
        values[count++] = (uint32_t)event->y;
    }
    if ((event->value_mask & XCB_CONFIG_WINDOW_WIDTH) != 0) {
        values[count++] = event->width;
    }
    if ((event->value_mask & XCB_CONFIG_WINDOW_HEIGHT) != 0) {
        values[count++] = event->height;
    }
    if ((event->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) != 0) {
        values[count++] = event->border_width;
    }
    if ((event->value_mask & XCB_CONFIG_WINDOW_SIBLING) != 0) {
        values[count++] = event->sibling;
    }
    if ((event->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) != 0) {
        values[count++] = event->stack_mode;
    }
    (void)count;
    xcb_configure_window(service->connection,
                         event->window,
                         event->value_mask,
                         values);
}

static void forget_window(struct nb_xwayland_rootless *service,
                          xcb_window_t window)
{
    struct nb_xwayland_window *entry = find_window(service, window);

    if (entry == NULL) {
        return;
    }
    if (entry->associated) {
        (void)nb_desktop_runtime_unmap_xwayland_window(service->desktop,
                                                       entry->window);
    }
    memset(entry, 0, sizeof(*entry));
}

static bool initialize_xwm(struct nb_xwayland_rootless *service)
{
    xcb_screen_iterator_t screens;
    xcb_composite_query_version_reply_t *composite_reply;
    xcb_get_selection_owner_reply_t *selection_reply;
    const uint32_t root_event_mask =
        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
        XCB_EVENT_MASK_PROPERTY_CHANGE;
    const uint32_t support_event_mask = XCB_EVENT_MASK_PROPERTY_CHANGE;

    if (xcb_connection_has_error(service->connection) != 0) {
        return false;
    }
    screens = xcb_setup_roots_iterator(xcb_get_setup(service->connection));
    service->screen = screens.data;
    if (service->screen == NULL) {
        return false;
    }
    composite_reply = xcb_composite_query_version_reply(
        service->connection,
        xcb_composite_query_version(service->connection, 0, 4),
        NULL);
    if (composite_reply == NULL) {
        fputs("Rootless Xwayland does not provide the Composite extension\n",
              stderr);
        return false;
    }
    free(composite_reply);
    if (!checked_request_succeeded(
            service->connection,
            xcb_change_window_attributes_checked(service->connection,
                                                 service->screen->root,
                                                 XCB_CW_EVENT_MASK,
                                                 &root_event_mask),
            "claim SubstructureRedirect on the X root")) {
        return false;
    }
    service->wl_surface_id = intern_atom(service->connection,
                                         "WL_SURFACE_ID",
                                         false);
    service->wl_surface_serial = intern_atom(service->connection,
                                             "WL_SURFACE_SERIAL",
                                             false);
    service->net_wm_name = intern_atom(service->connection,
                                       "_NET_WM_NAME",
                                       false);
    service->utf8_string = intern_atom(service->connection,
                                       "UTF8_STRING",
                                       false);
    service->wm_name = intern_atom(service->connection,
                                   "WM_NAME",
                                   false);
    service->wm_protocols = intern_atom(service->connection,
                                        "WM_PROTOCOLS",
                                        false);
    service->wm_delete_window = intern_atom(service->connection,
                                            "WM_DELETE_WINDOW",
                                            false);
    service->wm_selection = intern_atom(service->connection,
                                        "WM_S0",
                                        false);
    service->wm_window = xcb_generate_id(service->connection);
    if (service->wm_selection == XCB_ATOM_NONE ||
        service->wm_window == XCB_WINDOW_NONE ||
        !checked_request_succeeded(
            service->connection,
            xcb_create_window_checked(service->connection,
                                      XCB_COPY_FROM_PARENT,
                                      service->wm_window,
                                      service->screen->root,
                                      0,
                                      0,
                                      1,
                                      1,
                                      0,
                                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                      service->screen->root_visual,
                                      XCB_CW_EVENT_MASK,
                                      &support_event_mask),
            "create the XWM support window") ||
        !checked_request_succeeded(
            service->connection,
            xcb_set_selection_owner_checked(service->connection,
                                            service->wm_window,
                                            service->wm_selection,
                                            XCB_CURRENT_TIME),
            "claim the WM_S0 selection")) {
        return false;
    }
    selection_reply = xcb_get_selection_owner_reply(
        service->connection,
        xcb_get_selection_owner(service->connection,
                                service->wm_selection),
        NULL);
    if (selection_reply == NULL ||
        selection_reply->owner != service->wm_window) {
        free(selection_reply);
        fputs("Rootless XWM did not retain the WM_S0 selection\n", stderr);
        return false;
    }
    free(selection_reply);
    return service->wl_surface_id != XCB_ATOM_NONE &&
           service->wl_surface_serial != XCB_ATOM_NONE &&
           service->wm_protocols != XCB_ATOM_NONE &&
           service->wm_delete_window != XCB_ATOM_NONE &&
           xcb_flush(service->connection) > 0;
}

static void sleep_milliseconds(unsigned int milliseconds)
{
    struct timespec request;

    request.tv_sec = (time_t)(milliseconds / 1000U);
    request.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

static void stop_xwayland(struct nb_xwayland_rootless *service)
{
    unsigned int waited = 0;
    int status;
    pid_t result;

    if (service->process <= 0) {
        return;
    }
    if (kill(service->process, SIGTERM) != 0 && errno != ESRCH) {
        fprintf(stderr,
                "Could not terminate rootless Xwayland pid %ld: %s\n",
                (long)service->process,
                strerror(errno));
    }
    do {
        result = waitpid(service->process, &status, WNOHANG);
        if (result == service->process || (result < 0 && errno == ECHILD)) {
            service->process = -1;
            return;
        }
        if (result < 0 && errno != EINTR) {
            break;
        }
        sleep_milliseconds(NB_XWAYLAND_EXIT_WAIT_SLICE_MS);
        waited += NB_XWAYLAND_EXIT_WAIT_SLICE_MS;
    } while (waited < NB_XWAYLAND_EXIT_WAIT_MS);

    if (kill(service->process, SIGKILL) != 0 && errno != ESRCH) {
        fprintf(stderr,
                "Could not kill rootless Xwayland pid %ld: %s\n",
                (long)service->process,
                strerror(errno));
    }
    while (waitpid(service->process, &status, 0) < 0 && errno == EINTR) {
    }
    service->process = -1;
}

static void report_startup_failure(struct nb_xwayland_rootless *service)
{
    int status;
    pid_t result;

    if (service->connection != NULL) {
        const int connection_error =
            xcb_connection_has_error(service->connection);

        if (connection_error != 0) {
            fprintf(stderr,
                    "Rootless XWM connection failed during startup "
                    "(XCB error %d)\n",
                    connection_error);
        }
    }
    if (service->process <= 0) {
        return;
    }
    result = waitpid(service->process, &status, WNOHANG);
    if (result != service->process) {
        return;
    }
    service->process = -1;
    if (WIFEXITED(status)) {
        fprintf(stderr,
                "Rootless Xwayland exited during startup with status %d\n",
                WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr,
                "Rootless Xwayland terminated during startup by signal %d\n",
                WTERMSIG(status));
    } else {
        fputs("Rootless Xwayland stopped unexpectedly during startup\n",
              stderr);
    }
}

struct nb_xwayland_rootless *nb_xwayland_rootless_create(
    struct nb_desktop_runtime *desktop,
    const char *xwayland_path)
{
    struct nb_xwayland_rootless *service;
    int wm_sockets[2] = {-1, -1};
    int listen_descriptor = -1;
    pid_t child;

    if (desktop == NULL || xwayland_path == NULL ||
        xwayland_path[0] != '/' || access(xwayland_path, X_OK) != 0) {
        return NULL;
    }
    service = calloc(1, sizeof(*service));
    if (service == NULL) {
        return NULL;
    }
    service->desktop = desktop;
    service->process = -1;
    service->startup_pipe[0] = -1;
    service->startup_pipe[1] = -1;
    service->connector_descriptor = -1;
    listen_descriptor = create_listen_socket(service->socket_path,
                                             sizeof(service->socket_path),
                                             service->display_name,
                                             sizeof(service->display_name));
    if (listen_descriptor < 0 ||
        socketpair(AF_UNIX, SOCK_STREAM, 0, wm_sockets) != 0 ||
        !set_close_on_exec(wm_sockets[0], true) ||
        !set_close_on_exec(wm_sockets[1], true) ||
        !suppress_socket_sigpipe(wm_sockets[0]) ||
        !suppress_socket_sigpipe(wm_sockets[1])) {
        goto fail;
    }
    child = fork();
    if (child < 0) {
        goto fail;
    }
    if (child == 0) {
        char listen_text[32];
        char wm_text[32];
        const char *trace_wayland = getenv("NIXBENCH_TRACE_WAYLAND");

        (void)close(wm_sockets[0]);
        if (!set_close_on_exec(listen_descriptor, false) ||
            !set_close_on_exec(wm_sockets[1], false)) {
            _exit(126);
        }
        if (trace_wayland != NULL && strcmp(trace_wayland, "1") == 0 &&
            setenv("WAYLAND_DEBUG", "client", 1) != 0) {
            _exit(126);
        }
        (void)snprintf(listen_text,
                       sizeof(listen_text),
                       "%d",
                       listen_descriptor);
        (void)snprintf(wm_text, sizeof(wm_text), "%d", wm_sockets[1]);
        execl(xwayland_path,
              xwayland_path,
              service->display_name,
              "-rootless",
              "-terminate",
              "-shm",
              "-ac",
              "-wm",
              wm_text,
              "-listenfd",
              listen_text,
              "-nolisten",
              "tcp",
              (char *)NULL);
        _exit(127);
    }
    service->process = child;
    if (!nb_desktop_runtime_authorize_xwayland_client(desktop, child)) {
        fputs("Could not authorize the rootless Xwayland Wayland client\n",
              stderr);
        goto fail;
    }
    (void)close(wm_sockets[1]);
    wm_sockets[1] = -1;
    (void)close(listen_descriptor);
    listen_descriptor = -1;
    if (pipe(service->startup_pipe) != 0 ||
        !set_close_on_exec(service->startup_pipe[0], true) ||
        !set_close_on_exec(service->startup_pipe[1], true)) {
        goto fail;
    }
    service->connector_descriptor = wm_sockets[0];
    {
        const int thread_error = pthread_create(&service->connector_thread,
                                                NULL,
                                                connect_xwm,
                                                service);

        if (thread_error != 0) {
            fprintf(stderr,
                    "Could not create the rootless XWM connector: %s\n",
                    strerror(thread_error));
            service->connector_descriptor = -1;
            goto fail;
        }
    }
    service->connector_running = true;
    wm_sockets[0] = -1;
    return service;

fail:
    report_startup_failure(service);
    if (wm_sockets[0] >= 0) {
        (void)close(wm_sockets[0]);
    }
    if (wm_sockets[1] >= 0) {
        (void)close(wm_sockets[1]);
    }
    if (listen_descriptor >= 0) {
        (void)close(listen_descriptor);
    }
    nb_xwayland_rootless_destroy(service);
    return NULL;
}

void nb_xwayland_rootless_destroy(struct nb_xwayland_rootless *service)
{
    if (service == NULL) {
        return;
    }
    if (service->desktop != NULL) {
        (void)nb_desktop_runtime_set_xwayland_interface(service->desktop,
                                                        NULL,
                                                        NULL);
        (void)nb_desktop_runtime_authorize_xwayland_client(service->desktop,
                                                           0);
    }
    if (service->connector_running) {
        stop_xwayland(service);
        (void)pthread_join(service->connector_thread, NULL);
        service->connector_running = false;
        service->connector_descriptor = -1;
    }
    if (service->pending_connection != NULL) {
        xcb_disconnect(service->pending_connection);
        service->pending_connection = NULL;
    }
    if (service->connection != NULL) {
        xcb_disconnect(service->connection);
        service->connection = NULL;
    }
    stop_xwayland(service);
    if (service->startup_pipe[0] >= 0) {
        (void)close(service->startup_pipe[0]);
    }
    if (service->startup_pipe[1] >= 0) {
        (void)close(service->startup_pipe[1]);
    }
    if (service->socket_path[0] != '\0') {
        (void)unlink(service->socket_path);
    }
    free(service);
}

bool nb_xwayland_rootless_dispatch(struct nb_xwayland_rootless *service)
{
    xcb_generic_event_t *event;
    int child_status;
    pid_t child_result;
    size_t index;

    if (service == NULL) {
        return false;
    }
    if (!service->ready) {
        struct nb_desktop_xwayland_interface interface;

        if (service->connection == NULL) {
            struct pollfd descriptor;
            char notification;
            ssize_t received;
            int poll_result;

            memset(&descriptor, 0, sizeof(descriptor));
            descriptor.fd = service->startup_pipe[0];
            descriptor.events = POLLIN;
            poll_result = poll(&descriptor, 1, 0);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    return true;
                }
                fprintf(stderr,
                        "Could not poll rootless XWM connector: %s\n",
                        strerror(errno));
                return false;
            }
            if (poll_result == 0) {
                return true;
            }
            do {
                received = read(service->startup_pipe[0],
                                &notification,
                                sizeof(notification));
            } while (received < 0 && errno == EINTR);
            if (received <= 0 ||
                pthread_join(service->connector_thread, NULL) != 0) {
                fputs("Rootless XWM connector did not complete\n", stderr);
                return false;
            }
            service->connector_running = false;
            service->connector_descriptor = -1;
            (void)close(service->startup_pipe[0]);
            (void)close(service->startup_pipe[1]);
            service->startup_pipe[0] = -1;
            service->startup_pipe[1] = -1;
            service->connection = service->pending_connection;
            service->pending_connection = NULL;
            if (service->connection == NULL) {
                fputs("Rootless XWM connector returned no connection\n",
                      stderr);
                return false;
            }
        }

        child_result = waitpid(service->process, &child_status, WNOHANG);
        if (child_result == service->process) {
            service->process = -1;
            if (WIFEXITED(child_status)) {
                fprintf(stderr,
                        "Rootless Xwayland exited during startup with "
                        "status %d\n",
                        WEXITSTATUS(child_status));
            } else if (WIFSIGNALED(child_status)) {
                fprintf(stderr,
                        "Rootless Xwayland terminated during startup by "
                        "signal %d\n",
                        WTERMSIG(child_status));
            }
            return false;
        }
        if (xcb_connection_has_error(service->connection) != 0) {
            report_startup_failure(service);
            return false;
        }
        if (!initialize_xwm(service)) {
            report_startup_failure(service);
            return false;
        }
        interface.configure_window = configure_native_window;
        interface.close_window = close_native_window;
        if (!nb_desktop_runtime_set_xwayland_interface(service->desktop,
                                                       &interface,
                                                       service) ||
            setenv("DISPLAY", service->display_name, 1) != 0) {
            fputs("Could not publish the rootless Xwayland environment\n",
                  stderr);
            return false;
        }
        service->ready = true;
        fprintf(stderr,
                "Rootless Xwayland is ready: DISPLAY=%s pid=%ld\n",
                service->display_name,
                (long)service->process);
    }
    while ((event = xcb_poll_for_event(service->connection)) != NULL) {
        const uint8_t type = event->response_type & UINT8_C(0x7f);

        switch (type) {
        case XCB_MAP_REQUEST:
            if (!handle_map_request(
                    service,
                    (const xcb_map_request_event_t *)event)) {
                free(event);
                return false;
            }
            break;
        case XCB_CONFIGURE_REQUEST:
            handle_configure_request(
                service,
                (const xcb_configure_request_event_t *)event);
            break;
        case XCB_CLIENT_MESSAGE:
        {
            const xcb_client_message_event_t *message =
                (const xcb_client_message_event_t *)event;

            if (message->type == service->wl_surface_serial &&
                message->format == 32 &&
                (message->data.data32[0] != 0 ||
                 message->data.data32[1] != 0)) {
                struct nb_xwayland_window *entry =
                    remember_window(service, message->window);

                if (entry != NULL) {
                    entry->surface_serial =
                        (uint64_t)message->data.data32[0] |
                        ((uint64_t)message->data.data32[1] << 32U);
                    fprintf(stderr,
                            "Rootless XWM received WL_SURFACE_SERIAL %llu "
                            "for X window %#x\n",
                            (unsigned long long)entry->surface_serial,
                            (unsigned int)entry->window);
                    try_associate_window(service, entry);
                }
            } else if (message->type == service->wl_surface_id &&
                message->format == 32 && message->data.data32[0] != 0) {
                struct nb_xwayland_window *entry =
                    remember_window(service, message->window);

                if (entry != NULL) {
                    entry->surface_id = message->data.data32[0];
                    fprintf(stderr,
                            "Rootless XWM received WL_SURFACE_ID %u for X "
                            "window %#x\n",
                            entry->surface_id,
                            (unsigned int)entry->window);
                    try_associate_window(service, entry);
                }
            }
            break;
        }
        case XCB_PROPERTY_NOTIFY:
        {
            const xcb_property_notify_event_t *property =
                (const xcb_property_notify_event_t *)event;
            struct nb_xwayland_window *entry =
                find_window(service, property->window);

            if (entry != NULL &&
                (property->atom == service->net_wm_name ||
                 property->atom == service->wm_name)) {
                refresh_window_title(service, entry);
            }
            break;
        }
        case XCB_DESTROY_NOTIFY:
            forget_window(
                service,
                ((const xcb_destroy_notify_event_t *)event)->window);
            break;
        case XCB_UNMAP_NOTIFY:
            forget_window(
                service,
                ((const xcb_unmap_notify_event_t *)event)->window);
            break;
        default:
            break;
        }
        free(event);
    }
    for (index = 0; index < NB_XWAYLAND_MAX_WINDOWS; ++index) {
        if (service->windows[index].occupied &&
            !service->windows[index].associated) {
            try_associate_window(service, &service->windows[index]);
        }
    }
    if (xcb_connection_has_error(service->connection) != 0) {
        fputs("Rootless XWM connection failed\n", stderr);
        return false;
    }
    child_result = waitpid(service->process, &child_status, WNOHANG);
    if (child_result == service->process) {
        fprintf(stderr,
                "Rootless Xwayland pid %ld exited unexpectedly\n",
                (long)service->process);
        service->process = -1;
        return false;
    }
    return xcb_flush(service->connection) > 0;
}

int nb_xwayland_rootless_event_descriptor(
    const struct nb_xwayland_rootless *service)
{
    if (service == NULL) {
        return -1;
    }
    if (service->connection != NULL) {
        return xcb_get_file_descriptor(service->connection);
    }
    return service->connector_running ? service->startup_pipe[0] : -1;
}

const char *nb_xwayland_rootless_display_name(
    const struct nb_xwayland_rootless *service)
{
    return service != NULL && service->ready &&
                   service->display_name[0] != '\0'
               ? service->display_name
               : NULL;
}

bool nb_xwayland_rootless_is_ready(
    const struct nb_xwayland_rootless *service)
{
    return service != NULL && service->ready;
}
