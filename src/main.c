#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "desktop_runtime.h"
#include "host.h"
#include "host_sdl.h"

#ifndef NIXBENCH_HAS_HTML_THEMES
#define NIXBENCH_HAS_HTML_THEMES 0
#endif

#if NIXBENCH_HAS_HTML_THEMES
#include "theme_bundle.h"

#ifndef NIXBENCH_INSTALLED_HTML_RENDERER_PATH
#define NIXBENCH_INSTALLED_HTML_RENDERER_PATH ""
#endif

#ifndef NIXBENCH_INSTALLED_THEME_DIRECTORY
#define NIXBENCH_INSTALLED_THEME_DIRECTORY ""
#endif
#endif

#ifndef NIXBENCH_VERSION
#define NIXBENCH_VERSION "development"
#endif

#ifndef NIXBENCH_INSTALLED_HTML_APP_PATH
#define NIXBENCH_INSTALLED_HTML_APP_PATH ""
#endif

enum {
    NIXBENCH_WINDOW_WIDTH = 1024,
    NIXBENCH_WINDOW_HEIGHT = 640,
    NIXBENCH_WINDOW_MIN_WIDTH = 640,
    NIXBENCH_WINDOW_MIN_HEIGHT = 400,
    NIXBENCH_CLOCK_CAPACITY = 6,
    NIXBENCH_CLOCK_FALLBACK_WAIT_MS = 1000,
    NIXBENCH_WAYLAND_WAIT_MS = 16,
    NIXBENCH_HOST_ERROR_CAPACITY = 256,
    NIXBENCH_HOSTED_MAX_APPLICATIONS = 16,
    NIXBENCH_HOSTED_APPLICATION_STOP_WAIT_MS = 2000,
    NIXBENCH_HOSTED_APPLICATION_WAIT_SLICE_MS = 20,
#if NIXBENCH_HAS_HTML_THEMES
    NIXBENCH_HTML_TOKEN_BYTES = 32,
    NIXBENCH_HTML_TOKEN_CAPACITY = NIXBENCH_HTML_TOKEN_BYTES * 2 + 1,
    NIXBENCH_HTML_RENDERER_STOP_WAIT_MS = 2000,
    NIXBENCH_HTML_RENDERER_WAIT_SLICE_MS = 20,
#endif
};

struct hosted_application {
    pid_t pid;
    char name[64];
};

struct options {
    bool fullscreen;
    bool exit_after_first_frame;
};

#if NIXBENCH_HAS_HTML_THEMES
struct hosted_html_theme {
    struct nb_theme_bundle bundle;
    char token[NIXBENCH_HTML_TOKEN_CAPACITY];
    char renderer_path[PATH_MAX];
    pid_t renderer_pid;
    bool enabled;
};
#endif

struct frontend {
    struct nb_desktop_runtime *desktop;
    struct nb_host *host;
    uint64_t next_frame_serial;
    uint64_t pending_frame_serial;
    bool capture_active;
    bool capture_attempt_failed;
    bool redraw_needed;
    bool running;
    struct hosted_application
        applications[NIXBENCH_HOSTED_MAX_APPLICATIONS];
#if NIXBENCH_HAS_HTML_THEMES
    struct hosted_html_theme html_theme;
#endif
};

static void print_usage(const char *program_name)
{
    printf("Usage: %s [OPTION]\n", program_name);
    puts("Open the initial NixBench desktop screen.\n");
    puts("  --fullscreen              occupy the current display");
    puts("  --exit-after-first-frame  render once and exit (smoke test)");
    puts("  --help                    show this help");
    puts("  --version                 show the NixBench version");
#if NIXBENCH_HAS_HTML_THEMES
    puts("\nEnvironment:");
    puts("  NIXBENCH_HTML_THEME       select classic, fantasy, cde, or beos");
#endif
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

#if NIXBENCH_HAS_HTML_THEMES
static const char *canonical_html_theme_id(const char *id)
{
    return id != NULL && strcmp(id, "motif") == 0 ? "cde" : id;
}

static const char *html_theme_directory_name(const char *id)
{
    if (id == NULL) {
        return NULL;
    }
    if (strcmp(id, "fantasy") == 0) {
        return "Fantasy";
    }
    if (strcmp(id, "cde") == 0) {
        return "CDE";
    }
    if (strcmp(id, "beos") == 0) {
        return "BeOS";
    }
    return NULL;
}

static bool fill_html_theme_token(
    char token[NIXBENCH_HTML_TOKEN_CAPACITY])
{
    static const char hexadecimal[] = "0123456789abcdef";
    unsigned char bytes[NIXBENCH_HTML_TOKEN_BYTES];
    size_t used = 0;
    int descriptor = open("/dev/urandom", O_RDONLY);

    if (descriptor < 0) {
        return false;
    }
    while (used < sizeof(bytes)) {
        const ssize_t count = read(descriptor,
                                   bytes + used,
                                   sizeof(bytes) - used);

        if (count > 0) {
            used += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            const int saved_error = count == 0 ? EIO : errno;

            (void)close(descriptor);
            errno = saved_error;
            return false;
        }
    }
    if (close(descriptor) != 0) {
        return false;
    }
    for (used = 0; used < sizeof(bytes); ++used) {
        token[used * 2] = hexadecimal[bytes[used] >> 4U];
        token[used * 2 + 1] = hexadecimal[bytes[used] & 0x0fU];
    }
    token[sizeof(bytes) * 2] = '\0';
    return true;
}

static bool load_hosted_html_theme(const char *base_path,
                                   const char *id,
                                   struct nb_theme_bundle *bundle,
                                   char *error,
                                   size_t error_capacity)
{
    const char *name = html_theme_directory_name(id);
    char directory[PATH_MAX];
    int length;

    if (name == NULL) {
        (void)snprintf(error,
                       error_capacity,
                       "unknown HTML theme id: %s",
                       id != NULL ? id : "(null)");
        return false;
    }
    length = snprintf(directory,
                      sizeof(directory),
                      "%s../themes/%s",
                      base_path,
                      name);
    if (length >= 0 && (size_t)length < sizeof(directory) &&
        nb_theme_bundle_load(directory, bundle, error, error_capacity) &&
        strcmp(bundle->id, id) == 0) {
        return true;
    }
    length = snprintf(directory,
                      sizeof(directory),
                      "%s/%s",
                      NIXBENCH_INSTALLED_THEME_DIRECTORY,
                      name);
    if (NIXBENCH_INSTALLED_THEME_DIRECTORY[0] != '\0' && length >= 0 &&
        (size_t)length < sizeof(directory) &&
        nb_theme_bundle_load(directory, bundle, error, error_capacity) &&
        strcmp(bundle->id, id) == 0) {
        return true;
    }
    if (error != NULL && error_capacity > 0 && error[0] == '\0') {
        (void)snprintf(error,
                       error_capacity,
                       "theme bundle %s is unavailable",
                       id);
    }
    return false;
}

static bool resolve_html_theme_renderer(const char *base_path,
                                        char *path,
                                        size_t path_capacity)
{
    int length = snprintf(path,
                          path_capacity,
                          "%snixbench-html-theme-renderer",
                          base_path);

    if (length >= 0 && (size_t)length < path_capacity &&
        access(path, X_OK) == 0) {
        return true;
    }
    length = snprintf(path,
                      path_capacity,
                      "%s",
                      NIXBENCH_INSTALLED_HTML_RENDERER_PATH);
    return NIXBENCH_INSTALLED_HTML_RENDERER_PATH[0] != '\0' && length >= 0 &&
           (size_t)length < path_capacity && access(path, X_OK) == 0;
}

static void prepare_hosted_html_theme(struct hosted_html_theme *theme)
{
    const char *requested = canonical_html_theme_id(
        getenv("NIXBENCH_HTML_THEME"));
    const char *base_path;
    char error[256] = {0};

    memset(theme, 0, sizeof(*theme));
    theme->renderer_pid = -1;
    if (requested == NULL || requested[0] == '\0' ||
        strcmp(requested, "classic") == 0) {
        return;
    }
    base_path = SDL_GetBasePath();
    if (base_path == NULL || base_path[0] == '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not locate the hosted executable directory; "
                    "using Classic decorations");
        return;
    }
    if (!load_hosted_html_theme(base_path,
                                requested,
                                &theme->bundle,
                                error,
                                sizeof(error))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not enable requested HTML theme %s: %s; "
                    "using Classic decorations",
                    requested,
                    error[0] != '\0' ? error : "bundle unavailable");
        return;
    }
    if (!resolve_html_theme_renderer(base_path,
                                     theme->renderer_path,
                                     sizeof(theme->renderer_path))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "The HTML theme renderer is unavailable; using Classic "
                    "decorations");
        return;
    }
    if (!fill_html_theme_token(theme->token)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not create an HTML renderer authentication token: "
                    "%s; using Classic decorations",
                    strerror(errno != 0 ? errno : EIO));
        return;
    }
    theme->enabled = true;
}

static bool launch_hosted_html_theme(
    struct hosted_html_theme *theme,
    const struct nb_desktop_runtime *desktop)
{
    const char *display_name;
    pid_t child;

    if (!theme->enabled) {
        return true;
    }
    display_name = nb_desktop_runtime_wayland_display_name(desktop);
    if (display_name == NULL || display_name[0] == '\0') {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "The hosted desktop did not publish a Wayland display; "
                    "using Classic decorations");
        theme->enabled = false;
        return true;
    }
    child = fork();
    if (child < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not start the HTML theme renderer: %s; using "
                    "Classic decorations",
                    strerror(errno));
        theme->enabled = false;
        return true;
    }
    if (child == 0) {
        if (setenv("WAYLAND_DISPLAY", display_name, 1) != 0 ||
            setenv("GDK_BACKEND", "wayland", 1) != 0 ||
            setenv("EGL_PLATFORM", "wayland", 1) != 0 ||
            setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1) != 0 ||
            unsetenv("GTK3_MODULES") != 0) {
            fprintf(stderr,
                    "Could not prepare the hosted HTML renderer environment: "
                    "%s\n",
                    strerror(errno));
            _exit(127);
        }
        execl(theme->renderer_path,
              theme->renderer_path,
              "--theme",
              theme->bundle.directory,
              "--atlas-token",
              theme->token,
              (char *)NULL);
        fprintf(stderr,
                "Could not execute hosted HTML theme renderer %s: %s\n",
                theme->renderer_path,
                strerror(errno));
        _exit(127);
    }
    theme->renderer_pid = child;
    SDL_Log("Started %s HTML decorations in hosted renderer pid %ld",
            theme->bundle.name,
            (long)child);
    return true;
}

static void reap_hosted_html_theme(struct hosted_html_theme *theme)
{
    int child_status;
    pid_t result;

    if (theme->renderer_pid <= 0) {
        return;
    }
    do {
        result = waitpid(theme->renderer_pid, &child_status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        return;
    }
    if (result == theme->renderer_pid) {
        if (WIFEXITED(child_status)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Hosted HTML theme renderer pid %ld exited with "
                        "code %d; Classic decorations remain active",
                        (long)theme->renderer_pid,
                        WEXITSTATUS(child_status));
        } else if (WIFSIGNALED(child_status)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Hosted HTML theme renderer pid %ld terminated by "
                        "signal %d; Classic decorations remain active",
                        (long)theme->renderer_pid,
                        WTERMSIG(child_status));
        }
    }
    if (result == theme->renderer_pid ||
        (result < 0 && errno == ECHILD)) {
        theme->renderer_pid = -1;
    }
}

static void stop_hosted_html_theme(struct hosted_html_theme *theme)
{
    unsigned int waited = 0;
    int child_status;

    if (theme->renderer_pid <= 0) {
        return;
    }
    if (kill(theme->renderer_pid, SIGTERM) != 0 && errno != ESRCH) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not stop hosted HTML renderer pid %ld: %s",
                    (long)theme->renderer_pid,
                    strerror(errno));
    }
    while (waited < NIXBENCH_HTML_RENDERER_STOP_WAIT_MS) {
        pid_t result;

        do {
            result = waitpid(theme->renderer_pid, &child_status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == theme->renderer_pid ||
            (result < 0 && errno == ECHILD)) {
            theme->renderer_pid = -1;
            return;
        }
        SDL_Delay(NIXBENCH_HTML_RENDERER_WAIT_SLICE_MS);
        waited += NIXBENCH_HTML_RENDERER_WAIT_SLICE_MS;
    }
    if (kill(theme->renderer_pid, SIGKILL) != 0 && errno != ESRCH) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not kill hosted HTML renderer pid %ld: %s",
                    (long)theme->renderer_pid,
                    strerror(errno));
    }
    while (waitpid(theme->renderer_pid, &child_status, 0) < 0 &&
           errno == EINTR) {
    }
    theme->renderer_pid = -1;
}
#endif

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
    const struct nb_desktop_runtime *desktop,
    uint64_t milliseconds)
{
    uint32_t timeout = clock_refresh_timeout();
    const uint32_t runtime_timeout =
        nb_desktop_runtime_timer_timeout(desktop, milliseconds);

    if (runtime_timeout < timeout) {
        timeout = runtime_timeout;
    }

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

static bool copy_executable_path(const char *candidate,
                                 char path[PATH_MAX])
{
    const int length = candidate != NULL
                           ? snprintf(path, PATH_MAX, "%s", candidate)
                           : -1;

    return length >= 0 && length < PATH_MAX && path[0] == '/' &&
           access(path, X_OK) == 0;
}

static bool find_executable_in_path(const char *name,
                                    char path[PATH_MAX])
{
    const char *search = getenv("PATH");
    const char *entry;

    if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL ||
        search == NULL) {
        return false;
    }
    entry = search;
    while (true) {
        const char *separator = strchr(entry, ':');
        const size_t directory_length =
            separator != NULL ? (size_t)(separator - entry) : strlen(entry);
        int length;

        if (directory_length == 0) {
            length = snprintf(path, PATH_MAX, "./%s", name);
        } else if (directory_length < PATH_MAX) {
            length = snprintf(path,
                              PATH_MAX,
                              "%.*s/%s",
                              (int)directory_length,
                              entry,
                              name);
        } else {
            length = -1;
        }
        if (length >= 0 && length < PATH_MAX && access(path, X_OK) == 0) {
            return true;
        }
        if (separator == NULL) {
            break;
        }
        entry = separator + 1;
    }
    path[0] = '\0';
    return false;
}

static bool resolve_hosted_application(
    enum nb_desktop_launch_request request,
    char path[PATH_MAX],
    const char **name,
    bool *software_webkit)
{
    const char *program = NULL;

    *name = NULL;
    *software_webkit = false;
    path[0] = '\0';
    switch (request) {
    case NB_DESKTOP_LAUNCH_NIXCLOCK:
    {
        const char *base_path = SDL_GetBasePath();

        *name = "NixClock";
        *software_webkit = true;
        if (base_path != NULL) {
            const int length = snprintf(path,
                                        PATH_MAX,
                                        "%snixbench-html-app",
                                        base_path);

            if (length >= 0 && length < PATH_MAX &&
                access(path, X_OK) == 0) {
                return true;
            }
        }
        if (copy_executable_path(NIXBENCH_INSTALLED_HTML_APP_PATH, path)) {
            return true;
        }
        program = "nixbench-html-app";
        break;
    }
    case NB_DESKTOP_LAUNCH_SAKURA:
        *name = "Sakura Terminal";
        program = "sakura";
        break;
    case NB_DESKTOP_LAUNCH_MIDORI:
        *name = "Midori Web Browser";
        *software_webkit = true;
        program = "midori";
        break;
    case NB_DESKTOP_LAUNCH_THUNAR:
        *name = "Thunar File Manager";
        program = "thunar";
        break;
    case NB_DESKTOP_LAUNCH_NONE:
    default:
        return false;
    }
    return find_executable_in_path(program, path);
}

static struct hosted_application *free_hosted_application_slot(
    struct frontend *frontend)
{
    size_t index;

    for (index = 0; index < NIXBENCH_HOSTED_MAX_APPLICATIONS; ++index) {
        if (frontend->applications[index].pid <= 0) {
            return &frontend->applications[index];
        }
    }
    return NULL;
}

static bool set_close_on_exec(int descriptor)
{
    const int flags = fcntl(descriptor, F_GETFD);

    return flags >= 0 && fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

static void reap_hosted_applications(struct frontend *frontend)
{
    size_t index;

    for (index = 0; index < NIXBENCH_HOSTED_MAX_APPLICATIONS; ++index) {
        struct hosted_application *application =
            &frontend->applications[index];
        int child_status;
        pid_t result;

        if (application->pid <= 0) {
            continue;
        }
        do {
            result = waitpid(application->pid, &child_status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == application->pid) {
            if (WIFEXITED(child_status)) {
                SDL_Log("%s pid %ld exited with code %d",
                        application->name,
                        (long)application->pid,
                        WEXITSTATUS(child_status));
            } else if (WIFSIGNALED(child_status)) {
                SDL_Log("%s pid %ld terminated by signal %d",
                        application->name,
                        (long)application->pid,
                        WTERMSIG(child_status));
            }
            memset(application, 0, sizeof(*application));
        } else if (result < 0 && errno == ECHILD) {
            memset(application, 0, sizeof(*application));
        }
    }
}

static void launch_hosted_application(
    struct frontend *frontend,
    enum nb_desktop_launch_request request)
{
    struct hosted_application *slot;
    const char *display_name;
    const char *name;
    bool software_webkit;
    char path[PATH_MAX];
    int exec_pipe[2] = {-1, -1};
    int exec_error = 0;
    unsigned char *error_bytes = (unsigned char *)&exec_error;
    size_t received = 0;
    pid_t child;

    if (request == NB_DESKTOP_LAUNCH_NONE) {
        return;
    }
    if (!resolve_hosted_application(request,
                                    path,
                                    &name,
                                    &software_webkit)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "%s is not available in this hosted environment",
                    name != NULL ? name : "Requested application");
        return;
    }
    display_name = nb_desktop_runtime_wayland_display_name(frontend->desktop);
    slot = free_hosted_application_slot(frontend);
    if (display_name == NULL || display_name[0] == '\0' || slot == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not launch %s: %s",
                    name,
                    slot == NULL ? "the hosted application limit was reached"
                                 : "the nested Wayland display is unavailable");
        return;
    }
    if (pipe(exec_pipe) != 0 || !set_close_on_exec(exec_pipe[0]) ||
        !set_close_on_exec(exec_pipe[1])) {
        const int saved_error = errno;

        if (exec_pipe[0] >= 0) {
            (void)close(exec_pipe[0]);
        }
        if (exec_pipe[1] >= 0) {
            (void)close(exec_pipe[1]);
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not prepare %s startup: %s",
                    name,
                    strerror(saved_error));
        return;
    }
    child = fork();
    if (child == 0) {
        const unsigned char *child_error_bytes;
        size_t remaining;

        (void)close(exec_pipe[0]);
        if (setenv("WAYLAND_DISPLAY", display_name, 1) != 0 ||
            setenv("GDK_BACKEND", "wayland", 1) != 0 ||
            setenv("EGL_PLATFORM", "wayland", 1) != 0 ||
            setenv("XDG_CURRENT_DESKTOP", "NixBench", 1) != 0 ||
            setenv("XDG_SESSION_DESKTOP", "NixBench", 1) != 0 ||
            setenv("XDG_SESSION_TYPE", "wayland", 1) != 0 ||
            setenv("GTK_CSD", "0", 1) != 0 ||
            unsetenv("GTK3_MODULES") != 0 ||
            (software_webkit &&
             setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1) != 0)) {
            exec_error = errno;
        } else {
            execl(path, path, (char *)NULL);
            exec_error = errno;
        }
        child_error_bytes = (const unsigned char *)&exec_error;
        remaining = sizeof(exec_error);
        while (remaining != 0) {
            const ssize_t written = write(
                exec_pipe[1],
                child_error_bytes + sizeof(exec_error) - remaining,
                remaining);

            if (written > 0) {
                remaining -= (size_t)written;
            } else if (written < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        _exit(127);
    }
    if (child < 0) {
        const int saved_error = errno;

        (void)close(exec_pipe[0]);
        (void)close(exec_pipe[1]);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not launch %s: %s",
                    name,
                    strerror(saved_error));
        return;
    }
    (void)close(exec_pipe[1]);
    while (received < sizeof(exec_error)) {
        const ssize_t count = read(exec_pipe[0],
                                   error_bytes + received,
                                   sizeof(exec_error) - received);

        if (count > 0) {
            received += (size_t)count;
        } else if (count == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            exec_error = errno;
            received = sizeof(exec_error);
            (void)kill(child, SIGKILL);
            break;
        }
    }
    (void)close(exec_pipe[0]);
    if (received != 0) {
        int child_status;

        while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Could not execute %s: %s",
                    path,
                    strerror(received == sizeof(exec_error) &&
                                     exec_error != 0
                                 ? exec_error
                                 : EIO));
        return;
    }
    slot->pid = child;
    (void)snprintf(slot->name, sizeof(slot->name), "%s", name);
    SDL_Log("Launched %s as hosted application pid %ld",
            name,
            (long)child);
}

static void stop_hosted_applications(struct frontend *frontend)
{
    unsigned int waited = 0;
    size_t index;

    reap_hosted_applications(frontend);
    for (index = 0; index < NIXBENCH_HOSTED_MAX_APPLICATIONS; ++index) {
        const pid_t pid = frontend->applications[index].pid;

        if (pid > 0 && kill(pid, SIGTERM) != 0 && errno != ESRCH) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Could not terminate hosted application pid %ld: %s",
                        (long)pid,
                        strerror(errno));
        }
    }
    while (waited < NIXBENCH_HOSTED_APPLICATION_STOP_WAIT_MS) {
        bool running = false;

        reap_hosted_applications(frontend);
        for (index = 0; index < NIXBENCH_HOSTED_MAX_APPLICATIONS; ++index) {
            running = running || frontend->applications[index].pid > 0;
        }
        if (!running) {
            return;
        }
        SDL_Delay(NIXBENCH_HOSTED_APPLICATION_WAIT_SLICE_MS);
        waited += NIXBENCH_HOSTED_APPLICATION_WAIT_SLICE_MS;
    }
    for (index = 0; index < NIXBENCH_HOSTED_MAX_APPLICATIONS; ++index) {
        struct hosted_application *application =
            &frontend->applications[index];
        int child_status;

        if (application->pid <= 0) {
            continue;
        }
        if (kill(application->pid, SIGKILL) != 0 && errno != ESRCH) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Could not kill hosted application pid %ld: %s",
                        (long)application->pid,
                        strerror(errno));
        }
        while (waitpid(application->pid, &child_status, 0) < 0 &&
               errno == EINTR) {
        }
        memset(application, 0, sizeof(*application));
    }
}

static void set_pointer_capture(struct frontend *frontend, bool captured)
{
    if (frontend->host == NULL) {
        return;
    }
    if (!captured) {
        frontend->capture_attempt_failed = false;
    }
    if (frontend->capture_active == captured ||
        (captured && frontend->capture_attempt_failed)) {
        return;
    }
    if (nb_host_set_pointer_capture(frontend->host, captured)) {
        frontend->capture_active = captured;
        frontend->capture_attempt_failed = false;
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "The host could not %s pointer capture",
                    captured ? "begin" : "release");
        if (captured) {
            frontend->capture_attempt_failed = true;
        } else {
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
    launch_hosted_application(frontend, update->launch_request);
    if (update->quit_requested ||
        nb_desktop_runtime_quit_requested(frontend->desktop)) {
        frontend->running = false;
    }
    reconcile_pointer_capture(frontend);
}

static bool update_runtime_timers(struct frontend *frontend)
{
    struct nb_desktop_runtime_update update;

    if (!nb_desktop_runtime_tick(
            frontend->desktop,
            nb_host_monotonic_milliseconds(frontend->host),
            &update)) {
        return false;
    }
    apply_runtime_update(frontend, &update);
    return true;
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

#if NIXBENCH_HAS_HTML_THEMES
    prepare_hosted_html_theme(&frontend.html_theme);
#else
    {
        const char *requested_theme = getenv("NIXBENCH_HTML_THEME");

        if (requested_theme != NULL && requested_theme[0] != '\0' &&
            strcmp(requested_theme, "classic") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "HTML theme support is unavailable in this build; "
                        "using Classic decorations");
        }
    }
#endif

    nb_desktop_runtime_options_init(&desktop_options);
    desktop_options.enable_wayland = true;
    desktop_options.publish_wayland_socket = true;
    desktop_options.enable_application_launcher = true;
#if NIXBENCH_HAS_HTML_THEMES
    if (frontend.html_theme.enabled) {
        desktop_options.html_theme_token = frontend.html_theme.token;
        desktop_options.html_theme_id = frontend.html_theme.bundle.id;
        desktop_options.html_theme_directory =
            frontend.html_theme.bundle.directory;
    }
#endif
    frontend.desktop = nb_desktop_runtime_create(&desktop_options, &output);
    if (frontend.desktop == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not initialize the NixBench desktop runtime: %s",
                     SDL_GetError());
        exit_status = 1;
        goto cleanup;
    }
#if NIXBENCH_HAS_HTML_THEMES
    if (!launch_hosted_html_theme(&frontend.html_theme, frontend.desktop)) {
        exit_status = 1;
        goto cleanup;
    }
#endif

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
                               event_wait_timeout(
                                   frontend.desktop,
                                   nb_host_monotonic_milliseconds(
                                       frontend.host)),
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
#if NIXBENCH_HAS_HTML_THEMES
        reap_hosted_html_theme(&frontend.html_theme);
#endif
        reap_hosted_applications(&frontend);

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

        if (!update_runtime_timers(&frontend)) {
            exit_status = 1;
            break;
        }

        if (frontend.running && frontend.redraw_needed &&
            !present_desktop(&frontend)) {
            exit_status = 1;
            break;
        }
    }

cleanup:
    set_pointer_capture(&frontend, false);
    stop_hosted_applications(&frontend);
#if NIXBENCH_HAS_HTML_THEMES
    stop_hosted_html_theme(&frontend.html_theme);
#endif
    nb_desktop_runtime_destroy(frontend.desktop);
    frontend.desktop = NULL;
    nb_host_destroy(frontend.host);
    frontend.host = NULL;
    return exit_status;
}
