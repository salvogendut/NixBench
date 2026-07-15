#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static volatile sig_atomic_t termination_requested;

static void handle_sigterm(int signal_number)
{
    (void)signal_number;
    termination_requested = 1;
}

static bool write_all(int descriptor, const char *text)
{
    size_t remaining = strlen(text);

    while (remaining != 0) {
        const ssize_t written = write(descriptor, text, remaining);

        if (written > 0) {
            text += (size_t)written;
            remaining -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool write_result(const char *path, const char *text)
{
    int descriptor = open(path,
                          O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRUSR | S_IWUSR);
    bool written;

    if (descriptor < 0) {
        return false;
    }
    written = write_all(descriptor, text);
    if (close(descriptor) != 0) {
        written = false;
    }
    return written;
}

int main(void)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *display = getenv("WAYLAND_DISPLAY");
    const char *result_path =
        getenv("NIXBENCH_TEST_APPLICATION_RESULT");
    struct sigaction action;
    struct stat status;
    sigset_t blocked;
    sigset_t previous;
    char socket_path[PATH_MAX];
    int length;
    bool socket_alive;

    if (runtime == NULL || runtime[0] == '\0' ||
        display == NULL || display[0] == '\0' ||
        result_path == NULL || result_path[0] == '\0') {
        return 2;
    }
    length = snprintf(socket_path,
                      sizeof(socket_path),
                      "%s/%s",
                      runtime,
                      display);
    if (length < 0 || (size_t)length >= sizeof(socket_path)) {
        return 2;
    }
    if (sigemptyset(&blocked) != 0 ||
        sigaddset(&blocked, SIGTERM) != 0 ||
        sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        return 2;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_sigterm;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGTERM, &action, NULL) != 0) {
        return 2;
    }
    if (!write_result(result_path, "ready\n")) {
        return 2;
    }
    while (!termination_requested) {
        (void)sigsuspend(&previous);
    }
    if (sigprocmask(SIG_SETMASK, &previous, NULL) != 0) {
        return 2;
    }

    socket_alive = lstat(socket_path, &status) == 0 &&
                   S_ISSOCK(status.st_mode);
    if (!write_result(result_path,
                      socket_alive ? "socket-alive\n"
                                   : "socket-missing\n")) {
        return 2;
    }
    return socket_alive ? 0 : 1;
}
