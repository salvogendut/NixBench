#define _POSIX_C_SOURCE 200809L

#include "session_core.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef NB_SESSION_INSTALLED_NIXCLOCK_PATH
#define NB_SESSION_INSTALLED_NIXCLOCK_PATH ""
#endif

#ifndef NB_SESSION_INSTALLED_GTK_MODULE_PATH
#define NB_SESSION_INSTALLED_GTK_MODULE_PATH ""
#endif

#include "desktop_runtime.h"
#include "host.h"
#include "host_privsep_client.h"
#include "session_frame_pacing.h"
#include "user_config.h"

#ifndef NIXBENCH_HAS_ROOTLESS_XWAYLAND
#define NIXBENCH_HAS_ROOTLESS_XWAYLAND 0
#endif

#if NIXBENCH_HAS_ROOTLESS_XWAYLAND
#include "xwayland_rootless.h"
#endif

enum {
    NB_SESSION_CORE_RUNTIME_PATH_CAPACITY = 512,
    NB_SESSION_CORE_DISPLAY_NAME_CAPACITY = 128,
    NB_SESSION_CORE_CLOCK_CAPACITY = 6,
    NB_SESSION_CORE_HOST_ERROR_CAPACITY = 256,
    NB_SESSION_CORE_HANDSHAKE_TIMEOUT_MS = 10000,
    NB_SESSION_CORE_SHUTDOWN_TIMEOUT_MS = 5000,
    NB_SESSION_CORE_APPLICATION_EXIT_TIMEOUT_MS = 2000,
    NB_SESSION_CORE_APPLICATION_WAIT_SLICE_MS = 20,
    NB_SESSION_CORE_CLOCK_FALLBACK_WAIT_MS = 1000,
    NB_SESSION_CORE_MAX_APPLICATIONS = 16
};

#define NB_SESSION_CORE_SHUTDOWN_TOKEN UINT64_C(0x4e4253485554444e)

struct nb_session_runtime_directory {
    char path[NB_SESSION_CORE_RUNTIME_PATH_CAPACITY];
    char display_name[NB_SESSION_CORE_DISPLAY_NAME_CAPACITY];
    bool created;
    bool owned;
};

struct nb_session_application {
    pid_t pid;
    bool initial;
    char path[PATH_MAX];
};

struct nb_session_core {
    struct nb_host *host;
    struct nb_desktop_runtime *desktop;
    struct nb_session_runtime_directory runtime_directory;
    struct nb_session_frame_pacing frame_pacing;
    uint64_t next_frame_serial;
    uint64_t pending_frame_serial;
    bool redraw_damage_valid;
    struct nb_rect redraw_damage;
    uint64_t shutdown_deadline;
    struct nb_session_application applications[NB_SESSION_CORE_MAX_APPLICATIONS];
    char program_directory[PATH_MAX];
    char user_config_path[PATH_MAX];
    char clock_text[NB_SESSION_CORE_CLOCK_CAPACITY];
    bool redraw_needed;
    bool shutdown_pending;
    bool shutdown_requested;
    bool running;
#if NIXBENCH_HAS_ROOTLESS_XWAYLAND
    struct nb_xwayland_rootless *xwayland;
#endif
};

static volatile sig_atomic_t session_core_sigterm_requested;

static void request_session_core_shutdown(int signal_number)
{
    (void)signal_number;
    session_core_sigterm_requested = 1;
}

static uint64_t add_milliseconds(uint64_t start, uint64_t duration)
{
    return duration > UINT64_MAX - start ? UINT64_MAX : start + duration;
}

static void report_host_error(const struct nb_session_core *core,
                              const char *operation)
{
    char message[NB_SESSION_CORE_HOST_ERROR_CAPACITY];
    int system_error;

    if (core->host != NULL &&
        nb_host_get_last_error(core->host,
                               &system_error,
                               message,
                               sizeof(message))) {
        if (system_error != 0) {
            fprintf(stderr,
                    "%s: %s (%s)\n",
                    operation,
                    message,
                    strerror(system_error));
        } else {
            fprintf(stderr, "%s: %s\n", operation, message);
        }
    } else {
        fprintf(stderr, "%s\n", operation);
    }
}

static bool runtime_directory_is_private(const char *path)
{
    struct stat status;
    const mode_t permissions = S_IRUSR | S_IWUSR | S_IXUSR;

    return lstat(path, &status) == 0 && S_ISDIR(status.st_mode) &&
           status.st_uid == geteuid() &&
           (status.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) == permissions;
}

static bool create_runtime_directory(
    struct nb_session_runtime_directory *directory)
{
    mode_t previous_umask;
    int length;

    memset(directory, 0, sizeof(*directory));
    length = snprintf(directory->path,
                      sizeof(directory->path),
                      "/tmp/nixbench-runtime-%" PRIuMAX "-XXXXXX",
                      (uintmax_t)geteuid());
    if (length < 0 || (size_t)length >= sizeof(directory->path)) {
        fputs("Could not form the private Wayland runtime path\n", stderr);
        return false;
    }
    previous_umask = umask(077);
    if (mkdtemp(directory->path) == NULL) {
        const int saved_error = errno;

        (void)umask(previous_umask);
        fprintf(stderr,
                "Could not create the private Wayland runtime directory: "
                "%s\n",
                strerror(saved_error));
        directory->path[0] = '\0';
        return false;
    }
    (void)umask(previous_umask);
    directory->created = true;
    directory->owned = true;
    if (!runtime_directory_is_private(directory->path)) {
        fputs("The new Wayland runtime directory failed its ownership or "
              "mode check\n",
              stderr);
        (void)rmdir(directory->path);
        memset(directory, 0, sizeof(*directory));
        return false;
    }
    if (setenv("XDG_RUNTIME_DIR", directory->path, 1) != 0) {
        const int saved_error = errno;

        fprintf(stderr,
                "Could not select the private Wayland runtime directory: "
                "%s\n",
                strerror(saved_error));
        (void)rmdir(directory->path);
        memset(directory, 0, sizeof(*directory));
        return false;
    }
    return true;
}

static bool use_runtime_directory(
    struct nb_session_runtime_directory *directory,
    const char *path)
{
    const size_t length = path != NULL ? strlen(path) : 0;

    if (path == NULL) {
        return create_runtime_directory(directory);
    }
    memset(directory, 0, sizeof(*directory));
    if (length == 0 || length >= sizeof(directory->path) ||
        path[0] != '/' || path[length - 1] == '/') {
        fputs("The supplied Wayland runtime directory path is invalid\n",
              stderr);
        return false;
    }
    (void)memcpy(directory->path, path, length + 1);
    directory->created = true;
    if (!runtime_directory_is_private(directory->path)) {
        fputs("The supplied Wayland runtime directory failed its ownership "
              "or mode check\n",
              stderr);
        memset(directory, 0, sizeof(*directory));
        return false;
    }
    if (setenv("XDG_RUNTIME_DIR", directory->path, 1) != 0) {
        fprintf(stderr,
                "Could not select the supplied Wayland runtime directory: "
                "%s\n",
                strerror(errno));
        memset(directory, 0, sizeof(*directory));
        return false;
    }
    return true;
}

static bool display_name_is_safe(const char *name)
{
    return name != NULL && name[0] != '\0' && strcmp(name, ".") != 0 &&
           strcmp(name, "..") != 0 && strchr(name, '/') == NULL &&
           strlen(name) < NB_SESSION_CORE_DISPLAY_NAME_CAPACITY;
}

static bool set_close_on_exec(int descriptor)
{
    const int flags = fcntl(descriptor, F_GETFD);

    return flags >= 0 &&
           fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

static bool remember_program_directory(struct nb_session_core *core,
                                       const char *program_path)
{
    const char *separator;
    size_t length;

    if (program_path == NULL || program_path[0] != '/') {
        return false;
    }
    separator = strrchr(program_path, '/');
    if (separator == NULL) {
        return false;
    }
    length = separator == program_path ? 1U : (size_t)(separator - program_path);
    if (length >= sizeof(core->program_directory)) {
        return false;
    }
    (void)memcpy(core->program_directory, program_path, length);
    core->program_directory[length] = '\0';
    return true;
}

static bool join_program_path(const struct nb_session_core *core,
                              const char *name,
                              char *path,
                              size_t path_size)
{
    const int length = snprintf(path,
                                path_size,
                                "%s%s%s",
                                core->program_directory,
                                strcmp(core->program_directory, "/") == 0
                                    ? ""
                                    : "/",
                                name);

    return length >= 0 && (size_t)length < path_size;
}

static bool resolve_program_file(const struct nb_session_core *core,
                                 const char *sibling,
                                 const char *installed,
                                 int access_mode,
                                 char *path,
                                 size_t path_size)
{
    if (join_program_path(core, sibling, path, path_size) &&
        access(path, access_mode) == 0) {
        return true;
    }
    if (installed == NULL || installed[0] != '/' ||
        strlen(installed) >= path_size) {
        return false;
    }
    (void)snprintf(path, path_size, "%s", installed);
    return access(path, access_mode) == 0;
}

static bool prepare_application_environment(struct nb_session_core *core)
{
    char module_path[PATH_MAX] = {0};
    const char *bridge = getenv("NIXBENCH_GTK_MENU_BRIDGE");

    if (setenv("WAYLAND_DISPLAY",
               core->runtime_directory.display_name,
               1) != 0 ||
        setenv("EGL_PLATFORM", "wayland", 1) != 0 ||
        setenv("XDG_CURRENT_DESKTOP", "NixBench", 1) != 0 ||
        setenv("XDG_SESSION_DESKTOP", "NixBench", 1) != 0 ||
        setenv("XDG_SESSION_TYPE", "wayland", 1) != 0 ||
        setenv("GDK_BACKEND", "wayland", 1) != 0 ||
        setenv("GTK_CSD", "0", 1) != 0 ||
        setenv("LANG", "C.UTF-8", 1) != 0) {
        fputs("Could not prepare the application environment\n", stderr);
        return false;
    }
    if (bridge == NULL || strcmp(bridge, "1") != 0) {
        return true;
    }
    if (!resolve_program_file(
            core,
            "gtk-modules/libnixbench_gtk_menu_bridge.so",
            NB_SESSION_INSTALLED_GTK_MODULE_PATH,
            R_OK,
            module_path,
            sizeof(module_path)) ||
        setenv("GTK3_MODULES", module_path, 1) != 0) {
        fprintf(stderr,
                "Could not enable the requested GTK menu bridge at %s: %s\n",
                module_path[0] != '\0' ? module_path : "(invalid path)",
                strerror(errno != 0 ? errno : EINVAL));
        return false;
    }
    return true;
}

static struct nb_session_application *free_application_slot(
    struct nb_session_core *core)
{
    size_t index;

    for (index = 0; index < NB_SESSION_CORE_MAX_APPLICATIONS; ++index) {
        if (core->applications[index].pid <= 0) {
            return &core->applications[index];
        }
    }
    return NULL;
}

static bool launch_application(struct nb_session_core *core,
                               const char *path,
                               bool initial,
                               bool software_webkit,
                               pid_t *launched_pid)
{
    struct nb_session_application *slot;
    int exec_pipe[2] = {-1, -1};
    int exec_error = 0;
    unsigned char *error_bytes = (unsigned char *)&exec_error;
    size_t received = 0;
    pid_t child;

    if (launched_pid != NULL) {
        *launched_pid = -1;
    }
    slot = free_application_slot(core);
    if (path == NULL || path[0] != '/' ||
        strlen(path) >= sizeof(core->applications[0].path)) {
        fputs("The application path is invalid\n", stderr);
        return false;
    }
    if (slot == NULL) {
        fputs("The NixBench application limit has been reached\n", stderr);
        return false;
    }
    if (access(path, X_OK) != 0) {
        fprintf(stderr,
                "Application is unavailable: %s (%s)\n",
                path,
                strerror(errno));
        return false;
    }
    if (pipe(exec_pipe) != 0 ||
        !set_close_on_exec(exec_pipe[0]) ||
        !set_close_on_exec(exec_pipe[1])) {
        const int saved_error = errno;

        if (exec_pipe[0] >= 0) {
            (void)close(exec_pipe[0]);
        }
        if (exec_pipe[1] >= 0) {
            (void)close(exec_pipe[1]);
        }
        fprintf(stderr,
                "Could not prepare initial application startup: %s\n",
                strerror(saved_error));
        return false;
    }
    child = fork();
    if (child < 0) {
        const int saved_error = errno;

        (void)close(exec_pipe[0]);
        (void)close(exec_pipe[1]);
        fprintf(stderr,
                "Could not launch the initial NixBench application: %s\n",
                strerror(saved_error));
        return false;
    }
    if (child == 0) {
        const unsigned char *child_error_bytes;
        size_t remaining;

        (void)close(exec_pipe[0]);
        if (software_webkit &&
            setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1) != 0) {
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
    (void)close(exec_pipe[1]);
    exec_pipe[1] = -1;
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
        fprintf(stderr,
                "Could not execute %s: %s\n",
                path,
                strerror(received == sizeof(exec_error) && exec_error != 0
                             ? exec_error
                             : EIO));
        return false;
    }
    slot->pid = child;
    slot->initial = initial;
    (void)snprintf(slot->path, sizeof(slot->path), "%s", path);
    if (launched_pid != NULL) {
        *launched_pid = child;
    }
    return true;
}

static void report_application_exit(
    const struct nb_session_application *application,
    int child_status)
{
    const char *kind =
        application->initial ? "Initial application" : "Application";

    if (WIFEXITED(child_status)) {
        fprintf(stderr,
                "%s pid %ld exited with code %d\n",
                kind,
                (long)application->pid,
                WEXITSTATUS(child_status));
    } else if (WIFSIGNALED(child_status)) {
        fprintf(stderr,
                "%s pid %ld terminated by signal %d\n",
                kind,
                (long)application->pid,
                WTERMSIG(child_status));
    }
}

static void clear_application(struct nb_session_application *application)
{
    (void)memset(application, 0, sizeof(*application));
}

static void reap_applications_nonblocking(struct nb_session_core *core)
{
    size_t index;

    for (index = 0; index < NB_SESSION_CORE_MAX_APPLICATIONS; ++index) {
        struct nb_session_application *application = &core->applications[index];
        int child_status;
        pid_t result;

        if (application->pid <= 0) {
            continue;
        }
        do {
            result = waitpid(application->pid, &child_status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == application->pid) {
            report_application_exit(application, child_status);
            clear_application(application);
        } else if (result < 0 && errno == ECHILD) {
            clear_application(application);
        }
    }
}

static void sleep_milliseconds(unsigned int milliseconds)
{
    struct timespec request;

    request.tv_sec = (time_t)(milliseconds / 1000U);
    request.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
}

static bool applications_are_running(const struct nb_session_core *core)
{
    size_t index;

    for (index = 0; index < NB_SESSION_CORE_MAX_APPLICATIONS; ++index) {
        if (core->applications[index].pid > 0) {
            return true;
        }
    }
    return false;
}

static void stop_applications(struct nb_session_core *core)
{
    uint64_t deadline;
    size_t index;

    reap_applications_nonblocking(core);
    if (!applications_are_running(core)) {
        return;
    }
    for (index = 0; index < NB_SESSION_CORE_MAX_APPLICATIONS; ++index) {
        const pid_t pid = core->applications[index].pid;

        if (pid > 0 && kill(pid, SIGTERM) != 0 && errno != ESRCH) {
            fprintf(stderr,
                    "Could not terminate application pid %ld: %s\n",
                    (long)pid,
                    strerror(errno));
        }
    }
    deadline = add_milliseconds(
        nb_host_monotonic_milliseconds(core->host),
        NB_SESSION_CORE_APPLICATION_EXIT_TIMEOUT_MS);
    while (applications_are_running(core) &&
           nb_host_monotonic_milliseconds(core->host) < deadline) {
        reap_applications_nonblocking(core);
        if (applications_are_running(core)) {
            sleep_milliseconds(NB_SESSION_CORE_APPLICATION_WAIT_SLICE_MS);
        }
    }
    for (index = 0; index < NB_SESSION_CORE_MAX_APPLICATIONS; ++index) {
        struct nb_session_application *application = &core->applications[index];
        int child_status;
        pid_t result;

        if (application->pid <= 0) {
            continue;
        }
        if (kill(application->pid, SIGKILL) != 0 && errno != ESRCH) {
            fprintf(stderr,
                    "Could not kill application pid %ld: %s\n",
                    (long)application->pid,
                    strerror(errno));
        }
        do {
            result = waitpid(application->pid, &child_status, 0);
        } while (result < 0 && errno == EINTR);
        if (result == application->pid) {
            report_application_exit(application, child_status);
        }
        clear_application(application);
    }
}

static bool remember_display_name(
    struct nb_session_runtime_directory *directory,
    const char *display_name)
{
    if (!display_name_is_safe(display_name)) {
        return false;
    }
    (void)snprintf(directory->display_name,
                   sizeof(directory->display_name),
                   "%s",
                   display_name);
    return true;
}

static bool remove_runtime_entry(const char *directory,
                                 const char *name,
                                 const char *suffix)
{
    char path[NB_SESSION_CORE_RUNTIME_PATH_CAPACITY +
              NB_SESSION_CORE_DISPLAY_NAME_CAPACITY + 8];
    const int length = snprintf(path,
                                sizeof(path),
                                "%s/%s%s",
                                directory,
                                name,
                                suffix);

    if (length < 0 || (size_t)length >= sizeof(path)) {
        return false;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        fprintf(stderr,
                "Could not remove session-owned runtime entry %s: %s\n",
                path,
                strerror(errno));
        return false;
    }
    return true;
}

static bool destroy_runtime_directory(
    struct nb_session_runtime_directory *directory)
{
    bool success = true;

    if (!directory->created) {
        return true;
    }
    if (!directory->owned) {
        memset(directory, 0, sizeof(*directory));
        return true;
    }
    if (directory->display_name[0] != '\0') {
        success = remove_runtime_entry(directory->path,
                                       directory->display_name,
                                       "") && success;
        success = remove_runtime_entry(directory->path,
                                       directory->display_name,
                                       ".lock") && success;
    }
    if (rmdir(directory->path) != 0 && errno != ENOENT) {
        fprintf(stderr,
                "Could not remove private Wayland runtime directory %s: "
                "%s\n",
                directory->path,
                strerror(errno));
        success = false;
    }
    memset(directory, 0, sizeof(*directory));
    return success;
}

static void format_clock(char clock_text[NB_SESSION_CORE_CLOCK_CAPACITY])
{
    const time_t now = time(NULL);
    struct tm local_time;

    if (now == (time_t)-1 || localtime_r(&now, &local_time) == NULL ||
        strftime(clock_text,
                 NB_SESSION_CORE_CLOCK_CAPACITY,
                 "%H:%M",
                 &local_time) == 0) {
        (void)snprintf(clock_text,
                       NB_SESSION_CORE_CLOCK_CAPACITY,
                       "--:--");
    }
}

static uint32_t clock_refresh_timeout(void)
{
    const time_t now = time(NULL);
    struct tm local_time;
    int seconds;

    if (now == (time_t)-1 || localtime_r(&now, &local_time) == NULL) {
        return NB_SESSION_CORE_CLOCK_FALLBACK_WAIT_MS;
    }
    seconds = 60 - local_time.tm_sec;
    if (seconds < 1 || seconds > 60) {
        return NB_SESSION_CORE_CLOCK_FALLBACK_WAIT_MS;
    }
    return (uint32_t)seconds * UINT32_C(1000);
}

static enum nb_host_event_status wait_for_activity(
    struct nb_session_core *core,
    struct nb_host_event *event)
{
    int descriptors[2];
    size_t descriptor_count = 0;
    const int wayland_descriptor =
        nb_desktop_runtime_event_descriptor(core->desktop);
    const uint32_t timeout = nb_session_frame_pacing_wait_timeout(
        &core->frame_pacing,
        nb_host_monotonic_milliseconds(core->host),
        core->redraw_needed,
        core->pending_frame_serial != 0,
        clock_refresh_timeout());

    if (wayland_descriptor >= 0) {
        descriptors[descriptor_count++] = wayland_descriptor;
    }
#if NIXBENCH_HAS_ROOTLESS_XWAYLAND
    if (core->xwayland != NULL) {
        const int xwayland_descriptor =
            nb_xwayland_rootless_event_descriptor(core->xwayland);

        if (xwayland_descriptor >= 0) {
            descriptors[descriptor_count++] = xwayland_descriptor;
        }
    }
#endif
    if (descriptor_count != 0) {
        return nb_host_privsep_client_wait_event_with_descriptors(
            core->host,
            descriptors,
            descriptor_count,
            timeout,
            event);
    }
    return nb_host_wait_event(core->host, timeout, event);
}

static bool request_helper_shutdown(struct nb_session_core *core)
{
    if (core->shutdown_requested) {
        return true;
    }
    if (!nb_host_privsep_client_request_shutdown(
            core->host,
            NB_SESSION_CORE_SHUTDOWN_TOKEN)) {
        fputs("Could not request orderly session shutdown\n", stderr);
        return false;
    }
    core->shutdown_requested = true;
    if (core->shutdown_deadline == 0) {
        core->shutdown_deadline = add_milliseconds(
            nb_host_monotonic_milliseconds(core->host),
            NB_SESSION_CORE_SHUTDOWN_TIMEOUT_MS);
    }
    core->redraw_needed = false;
    return true;
}

static bool begin_shutdown(struct nb_session_core *core)
{
    core->shutdown_pending = true;
    core->redraw_needed = false;
    if (core->shutdown_deadline == 0) {
        core->shutdown_deadline = add_milliseconds(
            nb_host_monotonic_milliseconds(core->host),
            NB_SESSION_CORE_SHUTDOWN_TIMEOUT_MS);
    }
    return core->pending_frame_serial != 0 ||
           request_helper_shutdown(core);
}

static bool advance_shutdown(struct nb_session_core *core)
{
    return !core->shutdown_pending || core->shutdown_requested ||
           core->pending_frame_serial != 0 ||
           request_helper_shutdown(core);
}

static bool apply_runtime_update(
    struct nb_session_core *core,
    const struct nb_desktop_runtime_update *update)
{
    char path[PATH_MAX];
    const char *name = NULL;
    bool software_webkit = false;

    if (update->redraw) {
        if (!core->redraw_needed) {
            core->redraw_needed = true;
            core->redraw_damage_valid = update->damage_valid;
            core->redraw_damage = update->damage;
        } else if (!update->damage_valid) {
            core->redraw_damage_valid = false;
        } else if (core->redraw_damage_valid) {
            const int current_right = core->redraw_damage.x +
                                      core->redraw_damage.width;
            const int current_bottom = core->redraw_damage.y +
                                       core->redraw_damage.height;
            const int update_right = update->damage.x +
                                     update->damage.width;
            const int update_bottom = update->damage.y +
                                      update->damage.height;
            const int left = update->damage.x < core->redraw_damage.x
                                 ? update->damage.x
                                 : core->redraw_damage.x;
            const int top = update->damage.y < core->redraw_damage.y
                                ? update->damage.y
                                : core->redraw_damage.y;
            const int right = update_right > current_right
                                  ? update_right
                                  : current_right;
            const int bottom = update_bottom > current_bottom
                                   ? update_bottom
                                   : current_bottom;

            core->redraw_damage =
                (struct nb_rect){left, top, right - left, bottom - top};
        }
    }
    if (update->preferences_changed) {
        char error[256];

        if (!nb_user_preferences_is_valid(&update->preferences)) {
            fputs("Desktop produced invalid user preferences\n", stderr);
            return false;
        }
        if (core->user_config_path[0] == '\0' ||
            !nb_user_config_save(core->user_config_path,
                                 &update->preferences,
                                 error,
                                 sizeof(error))) {
            fprintf(stderr,
                    "Could not save NixBench settings: %s\n",
                    core->user_config_path[0] != '\0'
                        ? error
                        : "configuration path is unavailable");
        }
    }
    switch (update->launch_request) {
    case NB_DESKTOP_LAUNCH_NONE:
        break;
    case NB_DESKTOP_LAUNCH_NIXCLOCK:
        name = "NixClock";
        if (!resolve_program_file(core,
                                  "nixclock",
                                  NB_SESSION_INSTALLED_NIXCLOCK_PATH,
                                  X_OK,
                                  path,
                                  sizeof(path))) {
            fputs("Could not resolve the NixClock launcher path\n", stderr);
            return true;
        }
        break;
    case NB_DESKTOP_LAUNCH_SAKURA:
        name = "Sakura Terminal";
        (void)snprintf(path, sizeof(path), "%s", "/usr/pkg/bin/sakura");
        break;
    case NB_DESKTOP_LAUNCH_MIDORI:
        name = "Midori Web Browser";
        software_webkit = true;
        (void)snprintf(path, sizeof(path), "%s", "/usr/pkg/bin/midori");
        break;
    default:
        fputs("Desktop requested an unknown application\n", stderr);
        return false;
    }
    if (name != NULL) {
        pid_t pid;

        if (launch_application(core,
                               path,
                               false,
                               software_webkit,
                               &pid)) {
            fprintf(stderr,
                    "Launched %s as application pid %ld\n",
                    name,
                    (long)pid);
        } else {
            fprintf(stderr, "Could not launch %s\n", name);
        }
    }
    if (update->quit_requested ||
        nb_desktop_runtime_quit_requested(core->desktop)) {
        return begin_shutdown(core);
    }
    return true;
}

static bool set_runtime_focus(struct nb_session_core *core,
                              bool focused,
                              uint64_t milliseconds)
{
    struct nb_desktop_runtime_update update;

    return nb_desktop_runtime_set_focus(core->desktop,
                                        focused,
                                        milliseconds,
                                        &update) &&
           apply_runtime_update(core, &update);
}

static bool configure_output(struct nb_session_core *core,
                             const struct nb_host_output *output,
                             uint64_t milliseconds)
{
    if (!nb_desktop_runtime_set_output(core->desktop, output) ||
        !set_runtime_focus(core, true, milliseconds)) {
        fputs("Could not configure the standalone desktop output\n", stderr);
        return false;
    }
    nb_session_frame_pacing_configure(&core->frame_pacing,
                                      output->refresh_millihertz);
    core->redraw_needed = true;
    core->redraw_damage_valid = false;
    return true;
}

static bool present_desktop(struct nb_session_core *core)
{
    struct nb_host_frame frame;
    enum nb_host_result result;
    uint64_t started;

    if (core->shutdown_pending || !core->redraw_needed ||
        core->pending_frame_serial != 0 ||
        nb_host_get_state(core->host) != NB_HOST_STATE_ACTIVE) {
        return true;
    }
    started = nb_host_monotonic_milliseconds(core->host);
    if (!nb_session_frame_pacing_ready(&core->frame_pacing, started)) {
        return true;
    }
    if (core->next_frame_serial == 0 ||
        core->next_frame_serial == UINT64_MAX) {
        fputs("Standalone desktop frame serial exhausted\n", stderr);
        return false;
    }
    if (!nb_desktop_runtime_render_damage(
            core->desktop,
            core->clock_text,
            core->next_frame_serial,
            core->redraw_damage_valid ? &core->redraw_damage : NULL,
            &frame)) {
        fputs("Could not render the standalone desktop\n", stderr);
        return false;
    }
    result = nb_host_present(core->host, &frame);
    if (result == NB_HOST_RESULT_WOULD_BLOCK ||
        result == NB_HOST_RESULT_SUSPENDED) {
        return true;
    }
    if (result != NB_HOST_RESULT_OK) {
        report_host_error(core,
                          "Could not submit a frame to the privileged helper");
        return false;
    }
    core->pending_frame_serial = core->next_frame_serial;
    ++core->next_frame_serial;
    nb_session_frame_pacing_presented(&core->frame_pacing, started);
    core->redraw_needed = false;
    core->redraw_damage_valid = false;
    return true;
}

static bool process_input(struct nb_session_core *core,
                          const struct nb_host_event *event)
{
    struct nb_desktop_runtime_update update;

    if (!nb_desktop_runtime_handle_input(core->desktop, event, &update)) {
        fputs("Could not route standalone input\n", stderr);
        return false;
    }
    return apply_runtime_update(core, &update);
}

static bool process_host_event(struct nb_session_core *core,
                               const struct nb_host_event *event)
{
    enum nb_host_result result;

    switch (event->type) {
    case NB_HOST_EVENT_QUIT:
        core->running = false;
        return true;
    case NB_HOST_EVENT_OUTPUT_CHANGED:
        return configure_output(core,
                                &event->data.output,
                                event->milliseconds);
    case NB_HOST_EVENT_CONSOLE_RELEASE_REQUESTED:
        nb_desktop_runtime_cancel_input(core->desktop,
                                        event->milliseconds);
        core->pending_frame_serial = 0;
        core->redraw_needed = true;
        core->redraw_damage_valid = false;
        result = nb_host_complete_console_release(core->host);
        if (result != NB_HOST_RESULT_OK) {
            report_host_error(core,
                              "Could not acknowledge helper suspension");
            return false;
        }
        return advance_shutdown(core);
    case NB_HOST_EVENT_CONSOLE_ACQUIRE_REQUESTED:
    {
        struct nb_host_output output;

        result = nb_host_complete_console_acquire(core->host);
        if (result != NB_HOST_RESULT_OK ||
            !nb_host_get_output(core->host, &output)) {
            report_host_error(core,
                              "Could not acknowledge helper resumption");
            return false;
        }
        core->pending_frame_serial = 0;
        return configure_output(core,
                                &output,
                                event->milliseconds);
    }
    case NB_HOST_EVENT_POINTER_MOTION:
    case NB_HOST_EVENT_POINTER_BUTTON:
    case NB_HOST_EVENT_KEY:
        return process_input(core, event);
    case NB_HOST_EVENT_FRAME_COMPLETE:
        if (event->data.frame_complete.frame_serial !=
            core->pending_frame_serial) {
            fputs("Helper completed an unexpected desktop frame\n", stderr);
            return false;
        }
        core->pending_frame_serial = 0;
        nb_desktop_runtime_frame_presented(core->desktop,
                                           event->milliseconds);
        return advance_shutdown(core);
    case NB_HOST_EVENT_FAILED:
        report_host_error(core, "The privileged session helper failed");
        return false;
    case NB_HOST_EVENT_FOCUS_CHANGED:
    case NB_HOST_EVENT_POINTER_LEAVE:
    case NB_HOST_EVENT_NONE:
    default:
        fputs("Unexpected standalone host event\n", stderr);
        return false;
    }
}

static bool dispatch_desktop(struct nb_session_core *core)
{
    struct nb_desktop_runtime_update update;

    if (!nb_desktop_runtime_dispatch(core->desktop, &update)) {
        fputs("Could not dispatch standalone Wayland clients\n", stderr);
        return false;
    }
    if (!apply_runtime_update(core, &update)) {
        return false;
    }
#if NIXBENCH_HAS_ROOTLESS_XWAYLAND
    if (core->xwayland != NULL &&
        !nb_xwayland_rootless_dispatch(core->xwayland)) {
        fputs("Could not dispatch rootless Xwayland clients\n", stderr);
        return false;
    }
#endif
    return true;
}

#if NIXBENCH_HAS_ROOTLESS_XWAYLAND
static const char *resolve_xwayland_path(void)
{
    const char *override = getenv("NIXBENCH_XWAYLAND");
    static const char *const candidates[] = {
        "/usr/pkg/bin/Xwayland",
        "/usr/X11R7/bin/Xwayland",
        "/usr/local/bin/Xwayland",
        "/usr/bin/Xwayland"
    };
    size_t index;

    if (override != NULL) {
        if (override[0] != '/' || access(override, X_OK) != 0) {
            fprintf(stderr,
                    "NIXBENCH_XWAYLAND does not name an executable absolute "
                    "path: %s\n",
                    override);
            return NULL;
        }
        return override;
    }
    for (index = 0; index < sizeof(candidates) / sizeof(candidates[0]);
         ++index) {
        if (access(candidates[index], X_OK) == 0) {
            return candidates[index];
        }
    }
    return NULL;
}

static void start_rootless_xwayland(struct nb_session_core *core)
{
    const char *enabled = getenv("NIXBENCH_XWAYLAND_ROOTLESS");
    const char *path;

    if (enabled == NULL || strcmp(enabled, "1") != 0) {
        return;
    }
    path = resolve_xwayland_path();
    if (path == NULL) {
        fputs("Rootless Xwayland was requested but no Xwayland executable "
              "was found\n",
              stderr);
        return;
    }
    core->xwayland = nb_xwayland_rootless_create(core->desktop, path);
    if (core->xwayland == NULL) {
        fputs("Rootless Xwayland could not be started; continuing with "
              "native Wayland applications only\n",
              stderr);
    }
}

static bool wait_for_rootless_xwayland(struct nb_session_core *core)
{
    const uint64_t deadline = add_milliseconds(
        nb_host_monotonic_milliseconds(core->host),
        NB_SESSION_CORE_HANDSHAKE_TIMEOUT_MS);

    while (core->xwayland != NULL &&
           !nb_xwayland_rootless_is_ready(core->xwayland)) {
        struct nb_host_event event;
        enum nb_host_event_status event_status =
            wait_for_activity(core, &event);

        if (event_status == NB_HOST_EVENT_STATUS_ERROR) {
            report_host_error(core,
                              "Could not wait for rootless Xwayland startup");
            return false;
        }
        if (!dispatch_desktop(core)) {
            return false;
        }
        while (event_status == NB_HOST_EVENT_STATUS_AVAILABLE) {
            if (!process_host_event(core, &event)) {
                return false;
            }
            event_status = nb_host_poll_event(core->host, &event);
            if (event_status == NB_HOST_EVENT_STATUS_ERROR) {
                report_host_error(core,
                                  "Could not drain host activity during "
                                  "rootless Xwayland startup");
                return false;
            }
        }
        if (!core->running || session_core_sigterm_requested) {
            fputs("Rootless Xwayland startup was cancelled\n", stderr);
            return false;
        }
        if (nb_host_monotonic_milliseconds(core->host) >= deadline) {
            fputs("Timed out waiting for rootless Xwayland startup\n",
                  stderr);
            return false;
        }
    }
    return true;
}
#endif

static void refresh_clock(struct nb_session_core *core)
{
    char current[NB_SESSION_CORE_CLOCK_CAPACITY];

    format_clock(current);
    if (strcmp(current, core->clock_text) != 0) {
        (void)memcpy(core->clock_text, current, sizeof(current));
        if (!core->shutdown_pending) {
            core->redraw_needed = true;
            core->redraw_damage_valid = false;
        }
    }
}

static bool wait_for_ready(struct nb_session_core *core,
                           struct nb_host_output *output)
{
    const uint64_t deadline = add_milliseconds(
        nb_host_monotonic_milliseconds(core->host),
        NB_SESSION_CORE_HANDSHAKE_TIMEOUT_MS);

    while (!nb_host_privsep_client_is_ready(core->host)) {
        struct nb_host_event event;
        const uint64_t now = nb_host_monotonic_milliseconds(core->host);
        uint64_t remaining;
        enum nb_host_event_status status;

        if (now >= deadline) {
            fputs("Timed out waiting for the privileged session helper\n",
                  stderr);
            return false;
        }
        remaining = deadline - now;
        status = nb_host_wait_event(
            core->host,
            remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining,
            &event);
        if (status == NB_HOST_EVENT_STATUS_ERROR) {
            report_host_error(core,
                              "Could not complete the helper handshake");
            return false;
        }
        if (status == NB_HOST_EVENT_STATUS_AVAILABLE &&
            event.type != NB_HOST_EVENT_OUTPUT_CHANGED) {
            fputs("Unexpected event during the helper handshake\n", stderr);
            return false;
        }
    }
    return nb_host_get_output(core->host, output);
}

static bool core_identity_is_unprivileged(void)
{
    return geteuid() != 0 && getuid() == geteuid() &&
           getgid() == getegid();
}

int nb_session_core_run(int protocol_descriptor,
                        const char *initial_application_path,
                        const char *runtime_directory_path,
                        const char *core_program_path,
                        const char *user_config_path)
{
    struct nb_session_core core;
    struct nb_desktop_runtime_options desktop_options;
    struct nb_user_preferences preferences;
    struct nb_desktop_runtime_update focus_update;
    struct nb_host_output output;
    struct sigaction previous_sigterm_action;
    struct sigaction sigterm_action;
    const char *display_name;
    pid_t initial_application_pid = -1;
    bool sigterm_action_installed = false;
    int status = 1;

    memset(&core, 0, sizeof(core));
    nb_session_frame_pacing_init(&core.frame_pacing);
    core.next_frame_serial = 1;
    core.redraw_needed = true;
    core.running = true;
    format_clock(core.clock_text);

    memset(&sigterm_action, 0, sizeof(sigterm_action));
    sigterm_action.sa_handler = request_session_core_shutdown;
    if (sigemptyset(&sigterm_action.sa_mask) != 0) {
        fprintf(stderr,
                "Could not initialize the session SIGTERM handler: %s\n",
                strerror(errno));
        return 1;
    }
    session_core_sigterm_requested = 0;
    if (sigaction(SIGTERM,
                  &sigterm_action,
                  &previous_sigterm_action) != 0) {
        fprintf(stderr,
                "Could not install the session SIGTERM handler: %s\n",
                strerror(errno));
        return 1;
    }
    sigterm_action_installed = true;

    /* Creation applies FD_CLOEXEC before allocation or protocol activity. */
    core.host = nb_host_privsep_client_create(protocol_descriptor);
    if (core.host == NULL) {
        fprintf(stderr,
                "Could not create the unprivileged helper proxy: %s\n",
                nb_host_privsep_client_creation_error());
        goto cleanup;
    }
    if (!core_identity_is_unprivileged()) {
        fputs("The NixBench session core refuses to run with privileged or "
              "mismatched credentials\n",
              stderr);
        goto cleanup;
    }
    if (initial_application_path != NULL &&
        initial_application_path[0] != '/') {
        fputs("The initial NixBench application path must be absolute\n",
              stderr);
        goto cleanup;
    }
    if (!remember_program_directory(&core, core_program_path)) {
        fputs("The session core executable path must be absolute\n", stderr);
        goto cleanup;
    }
    nb_user_preferences_init(&preferences);
    {
        char config_error[256];
        enum nb_user_config_load_result config_result;

        if (!nb_user_config_path(user_config_path,
                                 core.user_config_path,
                                 sizeof(core.user_config_path),
                                 config_error,
                                 sizeof(config_error))) {
            fprintf(stderr,
                    "Could not resolve NixBench user configuration: %s; "
                    "using session defaults\n",
                    config_error);
            core.user_config_path[0] = '\0';
        } else {
            config_result = nb_user_config_load_or_create(
                core.user_config_path,
                &preferences,
                config_error,
                sizeof(config_error));
            if (config_result == NB_USER_CONFIG_LOAD_ERROR) {
                fprintf(stderr,
                        "Could not load %s: %s; using session defaults\n",
                        core.user_config_path,
                        config_error);
                nb_user_preferences_init(&preferences);
            } else if (config_result == NB_USER_CONFIG_CREATED) {
                fprintf(stderr,
                        "Created NixBench user configuration: %s\n",
                        core.user_config_path);
            }
        }
    }
    if (!wait_for_ready(&core, &output) ||
        !use_runtime_directory(&core.runtime_directory,
                               runtime_directory_path)) {
        goto cleanup;
    }

    nb_desktop_runtime_options_init(&desktop_options);
    desktop_options.enable_wayland = true;
    desktop_options.publish_wayland_socket = true;
    desktop_options.software_pointer = true;
    desktop_options.enable_application_launcher = true;
    desktop_options.preferences = &preferences;
    core.desktop = nb_desktop_runtime_create(&desktop_options, &output);
    if (core.desktop == NULL) {
        fputs("Could not create the standalone desktop runtime\n", stderr);
        goto cleanup;
    }
    display_name = nb_desktop_runtime_wayland_display_name(core.desktop);
    if (!remember_display_name(&core.runtime_directory, display_name)) {
        fputs("The standalone desktop did not publish a safe Wayland "
              "display name\n",
              stderr);
        goto cleanup;
    }
#if NIXBENCH_HAS_ROOTLESS_XWAYLAND
    start_rootless_xwayland(&core);
    if (core.xwayland != NULL && !wait_for_rootless_xwayland(&core)) {
        goto cleanup;
    }
#endif
    if (!prepare_application_environment(&core) ||
        (initial_application_path != NULL &&
         !launch_application(&core,
                             initial_application_path,
                             true,
                             false,
                             &initial_application_pid))) {
        goto cleanup;
    }
    if (!nb_desktop_runtime_set_pointer(
            core.desktop,
            (output.logical_width - 1) / 2,
            (output.logical_height - 1) / 2,
            true) ||
        !nb_desktop_runtime_set_focus(core.desktop,
                                      true,
                                      nb_host_monotonic_milliseconds(core.host),
                                      &focus_update) ||
        !apply_runtime_update(&core, &focus_update)) {
        fputs("Could not initialize standalone desktop input\n", stderr);
        goto cleanup;
    }
    printf("Standalone NixBench Wayland display: "
           "XDG_RUNTIME_DIR=%s WAYLAND_DISPLAY=%s",
           core.runtime_directory.path,
           core.runtime_directory.display_name);
#if NIXBENCH_HAS_ROOTLESS_XWAYLAND
    if (core.xwayland != NULL) {
        printf(" DISPLAY=%s",
               nb_xwayland_rootless_display_name(core.xwayland));
    }
#endif
    if (initial_application_pid > 0) {
        printf(" application-pid=%ld", (long)initial_application_pid);
    } else {
        printf(" initial-application=none");
    }
    putchar('\n');
    (void)fflush(stdout);

    if (!present_desktop(&core)) {
        goto cleanup;
    }
    while (core.running) {
        struct nb_host_event event;
        enum nb_host_event_status event_status =
            wait_for_activity(&core, &event);

        reap_applications_nonblocking(&core);

        if (session_core_sigterm_requested) {
            session_core_sigterm_requested = 0;
            if (!core.shutdown_pending && !begin_shutdown(&core)) {
                goto cleanup;
            }
        }

        if (event_status == NB_HOST_EVENT_STATUS_ERROR) {
            report_host_error(&core,
                              "Could not wait for standalone host activity");
            goto cleanup;
        }
        if (!dispatch_desktop(&core)) {
            goto cleanup;
        }
        if (event_status == NB_HOST_EVENT_STATUS_AVAILABLE) {
            do {
                if (!process_host_event(&core, &event)) {
                    goto cleanup;
                }
                event_status = nb_host_poll_event(core.host, &event);
                if (event_status == NB_HOST_EVENT_STATUS_ERROR) {
                    report_host_error(&core,
                                      "Could not drain standalone host "
                                      "activity");
                    goto cleanup;
                }
            } while (core.running &&
                     event_status == NB_HOST_EVENT_STATUS_AVAILABLE);
        } else {
            refresh_clock(&core);
        }
        if (!advance_shutdown(&core)) {
            goto cleanup;
        }
        if (core.shutdown_pending &&
            nb_host_monotonic_milliseconds(core.host) >=
                core.shutdown_deadline) {
            fputs("Timed out waiting for orderly helper shutdown\n", stderr);
            goto cleanup;
        }
        if (core.running && !present_desktop(&core)) {
            goto cleanup;
        }
    }
    status = core.shutdown_requested ? 0 : 1;

cleanup:
    /* Keep the private Wayland display alive while the tracked client exits.
     * Destroying the compositor first turns an orderly session shutdown into
     * a misleading client-side connection error. */
    stop_applications(&core);
#if NIXBENCH_HAS_ROOTLESS_XWAYLAND
    nb_xwayland_rootless_destroy(core.xwayland);
    core.xwayland = NULL;
#endif
    nb_desktop_runtime_destroy(core.desktop);
    core.desktop = NULL;
    if (!destroy_runtime_directory(&core.runtime_directory)) {
        status = 1;
    }
    nb_host_destroy(core.host);
    core.host = NULL;
    session_core_sigterm_requested = 0;
    if (sigterm_action_installed &&
        sigaction(SIGTERM, &previous_sigterm_action, NULL) != 0) {
        fprintf(stderr,
                "Could not restore the prior SIGTERM disposition: %s\n",
                strerror(errno));
        status = 1;
    }
    return status;
}
