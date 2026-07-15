#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "desktop_runtime.h"
#include "host.h"
#include "host_sdl.h"

#ifndef NIXBENCH_VERSION
#define NIXBENCH_VERSION "development"
#endif

enum {
    NIXBENCH_WINDOW_WIDTH = 1024,
    NIXBENCH_WINDOW_HEIGHT = 640,
    NIXBENCH_WINDOW_MIN_WIDTH = 640,
    NIXBENCH_WINDOW_MIN_HEIGHT = 400,
    NIXBENCH_CLOCK_CAPACITY = 6,
    NIXBENCH_CLOCK_FALLBACK_WAIT_MS = 1000,
    NIXBENCH_WAYLAND_WAIT_MS = 16,
    NIXBENCH_HOST_ERROR_CAPACITY = 256
};

struct options {
    bool fullscreen;
    bool exit_after_first_frame;
};

struct frontend {
    struct nb_desktop_runtime *desktop;
    struct nb_host *host;
    uint64_t next_frame_serial;
    uint64_t pending_frame_serial;
    bool capture_active;
    bool redraw_needed;
    bool running;
};

static void print_usage(const char *program_name)
{
    printf("Usage: %s [OPTION]\n", program_name);
    puts("Open the initial NixBench desktop screen.\n");
    puts("  --fullscreen              occupy the current display");
    puts("  --exit-after-first-frame  render once and exit (smoke test)");
    puts("  --help                    show this help");
    puts("  --version                 show the NixBench version");
}

static bool parse_options(int argc,
                          char *argv[],
                          struct options *options,
                          int *exit_status)
{
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--fullscreen") == 0) {
            options->fullscreen = true;
        } else if (strcmp(argv[index], "--exit-after-first-frame") == 0) {
            options->exit_after_first_frame = true;
        } else if (strcmp(argv[index], "--help") == 0) {
            print_usage(argv[0]);
            return false;
        } else if (strcmp(argv[index], "--version") == 0) {
            printf("NixBench %s\n", NIXBENCH_VERSION);
            return false;
        } else {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[index]);
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            *exit_status = 2;
            return false;
        }
    }
    return true;
}

static void format_clock(char clock_text[NIXBENCH_CLOCK_CAPACITY])
{
    const time_t now = time(NULL);
    const struct tm *local_time = localtime(&now);

    if (local_time == NULL ||
        strftime(clock_text, NIXBENCH_CLOCK_CAPACITY, "%H:%M", local_time) ==
            0) {
        memcpy(clock_text, "--:--", NIXBENCH_CLOCK_CAPACITY);
    }
}

static uint32_t clock_refresh_timeout(void)
{
    const time_t now = time(NULL);
    const struct tm *local_time = localtime(&now);
    int seconds_until_next_minute;

    if (local_time == NULL) {
        return NIXBENCH_CLOCK_FALLBACK_WAIT_MS;
    }
    seconds_until_next_minute = 60 - local_time->tm_sec;
    if (seconds_until_next_minute < 1 || seconds_until_next_minute > 60) {
        return NIXBENCH_CLOCK_FALLBACK_WAIT_MS;
    }
    return (uint32_t)(seconds_until_next_minute * 1000);
}

static uint32_t event_wait_timeout(
    const struct nb_desktop_runtime *desktop)
{
    uint32_t timeout = clock_refresh_timeout();

    if (nb_desktop_runtime_wayland_display_name(desktop) != NULL &&
        timeout > NIXBENCH_WAYLAND_WAIT_MS) {
        timeout = NIXBENCH_WAYLAND_WAIT_MS;
    }
    return timeout;
}

static void log_host_error(const struct frontend *frontend,
                           const char *operation)
{
    char message[NIXBENCH_HOST_ERROR_CAPACITY];
    int system_error;

    if (frontend->host != NULL &&
        nb_host_get_last_error(frontend->host,
                               &system_error,
                               message,
                               sizeof(message))) {
        if (system_error != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "%s: %s (system error %d)",
                         operation,
                         message,
                         system_error);
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "%s: %s",
                         operation,
                         message);
        }
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", operation);
    }
}

static void set_pointer_capture(struct frontend *frontend, bool captured)
{
    if (frontend->host == NULL || frontend->capture_active == captured) {
        return;
    }
    if (nb_host_set_pointer_capture(frontend->host, captured)) {
        frontend->capture_active = captured;
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "The host could not %s pointer capture",
                    captured ? "begin" : "release");
        if (!captured) {
            frontend->capture_active = false;
        }
    }
}

static void reconcile_pointer_capture(struct frontend *frontend)
{
    set_pointer_capture(
        frontend,
        nb_desktop_runtime_wants_pointer_capture(frontend->desktop));
}

static void apply_runtime_update(
    struct frontend *frontend,
    const struct nb_desktop_runtime_update *update)
{
    frontend->redraw_needed = frontend->redraw_needed || update->redraw;
    if (update->quit_requested ||
        nb_desktop_runtime_quit_requested(frontend->desktop)) {
        frontend->running = false;
    }
    reconcile_pointer_capture(frontend);
}

static bool set_output(struct frontend *frontend,
                       const struct nb_host_output *output)
{
    if (!nb_desktop_runtime_set_output(frontend->desktop, output)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not configure the desktop output: %s",
                     SDL_GetError());
        return false;
    }
    frontend->redraw_needed = true;
    reconcile_pointer_capture(frontend);
    return true;
}

static bool present_desktop(struct frontend *frontend)
{
    struct nb_host_frame frame;
    char clock_text[NIXBENCH_CLOCK_CAPACITY];
    enum nb_host_result result;

    if (frontend->pending_frame_serial != 0) {
        return true;
    }
    if (frontend->next_frame_serial == UINT64_MAX) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Desktop frame serial exhausted");
        return false;
    }
    format_clock(clock_text);
    if (!nb_desktop_runtime_render(frontend->desktop,
                                   clock_text,
                                   frontend->next_frame_serial,
                                   &frame)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not draw the desktop screen: %s",
                     SDL_GetError());
        return false;
    }
    result = nb_host_present(frontend->host, &frame);
    if (result == NB_HOST_RESULT_WOULD_BLOCK ||
        result == NB_HOST_RESULT_SUSPENDED) {
        return true;
    }
    if (result != NB_HOST_RESULT_OK) {
        log_host_error(frontend, "Could not present the desktop screen");
        return false;
    }
    frontend->pending_frame_serial = frontend->next_frame_serial;
    ++frontend->next_frame_serial;
    frontend->redraw_needed = false;
    return true;
}

static bool process_host_event(struct frontend *frontend,
                               const struct nb_host_event *event)
{
    struct nb_desktop_runtime_update update;
    enum nb_host_result result;

    switch (event->type) {
    case NB_HOST_EVENT_QUIT:
        frontend->running = false;
        return true;
    case NB_HOST_EVENT_OUTPUT_CHANGED:
        return set_output(frontend, &event->data.output);
    case NB_HOST_EVENT_FOCUS_CHANGED:
        if (!nb_desktop_runtime_set_focus(frontend->desktop,
                                          event->data.focus.focused,
                                          event->milliseconds,
                                          &update)) {
            return false;
        }
        apply_runtime_update(frontend, &update);
        return true;
    case NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED:
        nb_desktop_runtime_cancel_input(frontend->desktop,
                                        event->milliseconds);
        set_pointer_capture(frontend, false);
        frontend->redraw_needed = true;
        result = nb_host_complete_console_release(frontend->host);
        if (result != NB_HOST_RESULT_OK) {
            log_host_error(frontend, "Could not release the console");
            return false;
        }
        return true;
    case NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED:
    {
        struct nb_host_output output;

        result = nb_host_complete_console_acquire(frontend->host);
        if (result != NB_HOST_RESULT_OK ||
            !nb_host_get_output(frontend->host, &output)) {
            log_host_error(frontend, "Could not reacquire the console");
            return false;
        }
        frontend->pending_frame_serial = 0;
        return set_output(frontend, &output);
    }
    case NB_HOST_EVENT_POINTER_MOTION:
    case NB_HOST_EVENT_POINTER_BUTTON:
    case NB_HOST_EVENT_KEY:
        if (!nb_desktop_runtime_handle_input(frontend->desktop,
                                             event,
                                             &update)) {
            return false;
        }
        apply_runtime_update(frontend, &update);
        return true;
    case NB_HOST_EVENT_POINTER_LEAVE:
        if (!nb_desktop_runtime_pointer_leave(frontend->desktop,
                                              frontend->capture_active,
                                              event->milliseconds,
                                              &update)) {
            return false;
        }
        apply_runtime_update(frontend, &update);
        return true;
    case NB_HOST_EVENT_FRAME_COMPLETE:
        if (event->data.frame_complete.frame_serial ==
            frontend->pending_frame_serial) {
            frontend->pending_frame_serial = 0;
            nb_desktop_runtime_frame_presented(frontend->desktop,
                                                event->milliseconds);
        }
        return true;
    case NB_HOST_EVENT_FAILED:
        log_host_error(frontend, "The display host failed");
        return false;
    case NB_HOST_EVENT_NONE:
    default:
        return false;
    }
}

static bool dispatch_desktop(struct frontend *frontend)
{
    struct nb_desktop_runtime_update update;

    if (!nb_desktop_runtime_dispatch(frontend->desktop, &update)) {
        return false;
    }
    apply_runtime_update(frontend, &update);
    return true;
}

int main(int argc, char *argv[])
{
    struct options options = {false, false};
    struct nb_desktop_runtime_options desktop_options;
    struct nb_host_sdl_options host_options;
    struct nb_host_output output;
    struct nb_host_event event;
    struct frontend frontend;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }

    memset(&frontend, 0, sizeof(frontend));
    frontend.next_frame_serial = 1;
    frontend.redraw_needed = true;
    frontend.running = true;

    nb_host_sdl_options_init(&host_options);
    host_options.window_width = NIXBENCH_WINDOW_WIDTH;
    host_options.window_height = NIXBENCH_WINDOW_HEIGHT;
    host_options.minimum_width = NIXBENCH_WINDOW_MIN_WIDTH;
    host_options.minimum_height = NIXBENCH_WINDOW_MIN_HEIGHT;
    host_options.fullscreen = options.fullscreen;
    host_options.resizable = false;
    frontend.host = nb_host_sdl_create(&host_options);
    if (frontend.host == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not create the SDL display host: %s",
                     nb_host_sdl_creation_error());
        return 1;
    }
    if (!nb_host_get_output(frontend.host, &output)) {
        log_host_error(&frontend, "Could not initialize the display output");
        exit_status = 1;
        goto cleanup;
    }

    nb_desktop_runtime_options_init(&desktop_options);
    desktop_options.enable_wayland = true;
    desktop_options.publish_wayland_socket = true;
    frontend.desktop = nb_desktop_runtime_create(&desktop_options, &output);
    if (frontend.desktop == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not initialize the NixBench desktop runtime: %s",
                     SDL_GetError());
        exit_status = 1;
        goto cleanup;
    }

    if (!present_desktop(&frontend)) {
        exit_status = 1;
        goto cleanup;
    }
    if (options.exit_after_first_frame) {
        goto cleanup;
    }

    while (frontend.running) {
        enum nb_host_event_status event_status =
            nb_host_wait_event(frontend.host,
                               event_wait_timeout(frontend.desktop),
                               &event);

        if (event_status == NB_HOST_EVENT_STATUS_ERROR) {
            log_host_error(&frontend, "Could not wait for host events");
            exit_status = 1;
            break;
        }
        if (!dispatch_desktop(&frontend)) {
            exit_status = 1;
            break;
        }

        if (event_status == NB_HOST_EVENT_STATUS_AVAILABLE) {
            do {
                if (!process_host_event(&frontend, &event)) {
                    exit_status = 1;
                    frontend.running = false;
                    break;
                }
                event_status = nb_host_poll_event(frontend.host, &event);
                if (event_status == NB_HOST_EVENT_STATUS_ERROR) {
                    log_host_error(&frontend,
                                   "Could not poll host events");
                    exit_status = 1;
                    frontend.running = false;
                    break;
                }
            } while (frontend.running &&
                     event_status == NB_HOST_EVENT_STATUS_AVAILABLE);
        } else {
            frontend.redraw_needed = true;
        }

        if (frontend.running && frontend.redraw_needed &&
            !present_desktop(&frontend)) {
            exit_status = 1;
            break;
        }
    }

cleanup:
    set_pointer_capture(&frontend, false);
    nb_desktop_runtime_destroy(frontend.desktop);
    frontend.desktop = NULL;
    nb_host_destroy(frontend.host);
    frontend.host = NULL;
    return exit_status;
}
