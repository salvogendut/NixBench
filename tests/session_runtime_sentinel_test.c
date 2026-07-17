#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "session_runtime_sentinel.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static bool wait_for_child(pid_t child, int *status)
{
    const uint64_t start = monotonic_milliseconds();
    const uint64_t deadline = start + UINT64_C(3000);

    for (;;) {
        const pid_t result = waitpid(child, status, WNOHANG);

        if (result == child) {
            return true;
        }
        if (result < 0 && errno != EINTR) {
            return false;
        }
        if (monotonic_milliseconds() >= deadline) {
            (void)kill(child, SIGKILL);
            while (waitpid(child, status, 0) < 0 && errno == EINTR) {
            }
            return false;
        }
        {
            const struct timespec pause = {0, 10000000L};

            (void)nanosleep(&pause, NULL);
        }
    }
}

static bool start_sentinel(pid_t *child, int *controller)
{
    int sockets[2] = {-1, -1};
    pid_t process;

    *child = -1;
    *controller = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        return false;
    }
    process = fork();
    if (process < 0) {
        (void)close(sockets[0]);
        (void)close(sockets[1]);
        return false;
    }
    if (process == 0) {
        int result;

        (void)close(sockets[0]);
        result = nb_session_runtime_sentinel_run(sockets[1]);
        (void)close(sockets[1]);
        _exit(result);
    }
    (void)close(sockets[1]);
    *child = process;
    *controller = sockets[0];
    return true;
}

static bool form_child_path(
    const char *directory,
    const char *name,
    char destination[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY])
{
    const int length = snprintf(destination,
                                NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY,
                                "%s/%s",
                                directory,
                                name);

    return length > 0 &&
           (size_t)length < NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY;
}

static bool wait_for_ready(
    int controller,
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY],
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY])
{
    const bool ready = nb_session_runtime_sentinel_wait_ready(controller,
                                                              2000,
                                                              path,
                                                              error);

    if (!ready) {
        fprintf(stderr, "runtime sentinel ready failed: %s\n", error);
    }
    return ready;
}

static bool create_regular_file(const char *path)
{
    int descriptor = open(path,
                          O_WRONLY | O_CREAT | O_EXCL,
                          S_IRUSR | S_IWUSR);

    if (descriptor < 0) {
        return false;
    }
    return close(descriptor) == 0;
}

static void test_explicit_cleanup(void)
{
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char socket_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char lock_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char link_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY];
    struct stat status;
    pid_t child;
    int controller;
    int child_status = 0;

    CHECK(start_sentinel(&child, &controller));
    if (child <= 0 || controller < 0) {
        return;
    }
    CHECK(wait_for_ready(controller, path, error));
    if (path[0] == '\0') {
        (void)close(controller);
        CHECK(wait_for_child(child, &child_status));
        return;
    }
    CHECK(strncmp(path,
                  "/tmp/nixbench-runtime-",
                  strlen("/tmp/nixbench-runtime-")) == 0);
    CHECK(lstat(path, &status) == 0);
    CHECK(S_ISDIR(status.st_mode));
    CHECK(status.st_uid == geteuid());
    CHECK((status.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) ==
          (S_IRUSR | S_IWUSR | S_IXUSR));

    CHECK(form_child_path(path, "wayland-0", socket_path));
    CHECK(form_child_path(path, "wayland-0.lock", lock_path));
    CHECK(form_child_path(path, "wayland-link", link_path));
    CHECK(mkfifo(socket_path, S_IRUSR | S_IWUSR) == 0);
    CHECK(create_regular_file(lock_path));
    CHECK(symlink("wayland-0", link_path) == 0);

    CHECK(nb_session_runtime_sentinel_request_cleanup(controller,
                                                      2000,
                                                      error));
    CHECK(lstat(path, &status) != 0 && errno == ENOENT);
    CHECK(close(controller) == 0);
    CHECK(wait_for_child(child, &child_status));
    CHECK(WIFEXITED(child_status));
    CHECK(WEXITSTATUS(child_status) == 0);
}

static void test_controller_eof_cleanup(void)
{
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char entry_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY];
    struct stat status;
    pid_t child;
    int controller;
    int child_status = 0;

    CHECK(start_sentinel(&child, &controller));
    if (child <= 0 || controller < 0) {
        return;
    }
    CHECK(wait_for_ready(controller, path, error));
    if (path[0] != '\0') {
        CHECK(form_child_path(path, "transient", entry_path));
        CHECK(create_regular_file(entry_path));
    }
    CHECK(close(controller) == 0);
    CHECK(wait_for_child(child, &child_status));
    CHECK(WIFEXITED(child_status));
    CHECK(WEXITSTATUS(child_status) == 0);
    if (path[0] != '\0') {
        CHECK(lstat(path, &status) != 0 && errno == ENOENT);
    }
}

static void test_nested_runtime_directories_cleanup(void)
{
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char dbus_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char services_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char at_spi_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char transient_path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY];
    struct stat status;
    pid_t child;
    int controller;
    int child_status = 0;

    CHECK(start_sentinel(&child, &controller));
    if (child <= 0 || controller < 0) {
        return;
    }
    CHECK(wait_for_ready(controller, path, error));
    if (path[0] == '\0') {
        (void)close(controller);
        CHECK(wait_for_child(child, &child_status));
        return;
    }
    CHECK(form_child_path(path, "dbus-1", dbus_path));
    CHECK(mkdir(dbus_path, S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    CHECK(form_child_path(dbus_path, "services", services_path));
    CHECK(mkdir(services_path, S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    CHECK(form_child_path(services_path, "transient", transient_path));
    CHECK(create_regular_file(transient_path));
    CHECK(form_child_path(path, "at-spi", at_spi_path));
    CHECK(mkdir(at_spi_path, S_IRUSR | S_IWUSR | S_IXUSR) == 0);
    CHECK(nb_session_runtime_sentinel_request_cleanup(controller,
                                                      2000,
                                                      error));
    CHECK(close(controller) == 0);
    CHECK(wait_for_child(child, &child_status));
    CHECK(WIFEXITED(child_status));
    CHECK(WEXITSTATUS(child_status) == 0);
    CHECK(lstat(path, &status) != 0 && errno == ENOENT);
}

static void test_defensive_arguments(void)
{
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY];

    CHECK(!nb_session_runtime_sentinel_wait_ready(-1,
                                                  10,
                                                  path,
                                                  error));
    CHECK(!nb_session_runtime_sentinel_wait_ready(-1,
                                                  10,
                                                  NULL,
                                                  error));
    CHECK(!nb_session_runtime_sentinel_request_cleanup(-1, 10, error));
    CHECK(nb_session_runtime_sentinel_run(-1) != 0);
}

int main(void)
{
    if (geteuid() == (uid_t)0) {
        puts("session runtime sentinel tests skipped for uid 0");
        return 0;
    }
    test_explicit_cleanup();
    test_controller_eof_cleanup();
    test_nested_runtime_directories_cleanup();
    test_defensive_arguments();

    if (failures != 0) {
        fprintf(stderr,
                "session runtime sentinel tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("session runtime sentinel tests: ok");
    return 0;
}
