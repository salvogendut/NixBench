#if defined(__linux__)
#define _GNU_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "session_runtime_sentinel.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

enum {
    NB_RUNTIME_SENTINEL_WIRE_VERSION = 1,
    NB_RUNTIME_SENTINEL_WIRE_READY = 1,
    NB_RUNTIME_SENTINEL_WIRE_CLEAN = 2,
    NB_RUNTIME_SENTINEL_WIRE_CLEANED = 3,
    NB_RUNTIME_SENTINEL_WIRE_FAILED = 4,
    NB_RUNTIME_SENTINEL_BASENAME_CAPACITY = 128
};

static const uint32_t runtime_sentinel_magic = UINT32_C(0x4e425253);
static const char runtime_parent_path[] = "/tmp";
static const char runtime_path_prefix[] = "/tmp/nixbench-runtime-";

struct runtime_sentinel_message {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint32_t reserved;
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
};

struct runtime_sentinel_directory {
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];
    char basename[NB_RUNTIME_SENTINEL_BASENAME_CAPACITY];
    struct stat identity;
    int parent_fd;
    int directory_fd;
    bool created;
};

enum sentinel_read_result {
    SENTINEL_READ_COMPLETE,
    SENTINEL_READ_EOF,
    SENTINEL_READ_ERROR
};

_Static_assert(sizeof(struct runtime_sentinel_message) ==
                   16U + NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY,
               "runtime sentinel wire record has unexpected padding");

static void set_error(char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY],
                      const char *format,
                      ...)
{
    va_list arguments;

    if (error == NULL) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error,
                    NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY,
                    format,
                    arguments);
    va_end(arguments);
}

static bool set_cloexec(int descriptor)
{
    const int flags = fcntl(descriptor, F_GETFD);

    return flags >= 0 &&
           fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

static bool configure_no_sigpipe(int descriptor)
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

static ssize_t write_without_sigpipe(int descriptor,
                                     const void *bytes,
                                     size_t size)
{
    const struct timespec no_wait = {0, 0};
    sigset_t blocked;
    sigset_t previous;
    sigset_t pending;
    ssize_t count;
    int saved_error;
    int was_pending;

    if (sigemptyset(&blocked) != 0 ||
        sigaddset(&blocked, SIGPIPE) != 0 ||
        sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        return -1;
    }
    if (sigpending(&pending) != 0 ||
        (was_pending = sigismember(&pending, SIGPIPE)) < 0) {
        saved_error = errno;
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        errno = saved_error;
        return -1;
    }

    count = write(descriptor, bytes, size);
    saved_error = errno;
    if (count < 0 && saved_error == EPIPE && was_pending == 0) {
        int result;

        do {
            result = sigtimedwait(&blocked, NULL, &no_wait);
        } while (result < 0 && errno == EINTR);
    }
    if (sigprocmask(SIG_SETMASK, &previous, NULL) != 0) {
        return -1;
    }
    errno = saved_error;
    return count;
}

static bool send_all(int descriptor, const void *data, size_t size)
{
    const unsigned char *bytes = data;

    while (size != 0U) {
        const ssize_t count = write_without_sigpipe(descriptor, bytes, size);

        if (count > 0) {
            bytes += (size_t)count;
            size -= (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static enum sentinel_read_result receive_all(int descriptor,
                                             void *data,
                                             size_t size)
{
    unsigned char *bytes = data;
    size_t received = 0;

    while (received < size) {
        const ssize_t count = recv(descriptor,
                                   bytes + received,
                                   size - received,
                                   0);

        if (count > 0) {
            received += (size_t)count;
        } else if (count == 0) {
            if (received == 0U) {
                return SENTINEL_READ_EOF;
            }
            errno = EPROTO;
            return SENTINEL_READ_ERROR;
        } else if (errno == EINTR) {
            continue;
        } else {
            return SENTINEL_READ_ERROR;
        }
    }
    return SENTINEL_READ_COMPLETE;
}

static void initialize_message(struct runtime_sentinel_message *message,
                               uint32_t type)
{
    memset(message, 0, sizeof(*message));
    message->magic = runtime_sentinel_magic;
    message->version = NB_RUNTIME_SENTINEL_WIRE_VERSION;
    message->type = type;
}

static bool message_header_is_valid(
    const struct runtime_sentinel_message *message,
    uint32_t expected_type)
{
    return message != NULL && message->magic == runtime_sentinel_magic &&
           message->version == NB_RUNTIME_SENTINEL_WIRE_VERSION &&
           message->type == expected_type && message->reserved == 0;
}

static bool empty_message_is_valid(
    const struct runtime_sentinel_message *message,
    uint32_t expected_type)
{
    static const char empty[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY];

    return message_header_is_valid(message, expected_type) &&
           memcmp(message->path, empty, sizeof(empty)) == 0;
}

static bool ready_path_is_valid(const char *path)
{
    const size_t prefix_size = sizeof(runtime_path_prefix) - 1U;
    const char *terminator;
    size_t used;
    size_t index;

    if (path == NULL ||
        memcmp(path, runtime_path_prefix, prefix_size) != 0) {
        return false;
    }
    terminator = memchr(path,
                        '\0',
                        NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY);
    if (terminator == NULL || terminator == path + prefix_size) {
        return false;
    }
    used = (size_t)(terminator - path);
    for (index = prefix_size; index < used; ++index) {
        const unsigned char byte = (unsigned char)path[index];

        if ((byte < (unsigned char)'0' || byte > (unsigned char)'9') &&
            (byte < (unsigned char)'A' || byte > (unsigned char)'Z') &&
            (byte < (unsigned char)'a' || byte > (unsigned char)'z') &&
            byte != (unsigned char)'-') {
            return false;
        }
    }
    for (index = used + 1U;
         index < NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY;
         ++index) {
        if (path[index] != '\0') {
            return false;
        }
    }
    return true;
}

static bool directory_identity_is_valid(const struct stat *status,
                                        uid_t owner)
{
    const mode_t permissions = S_IRUSR | S_IWUSR | S_IXUSR;

    return status != NULL && S_ISDIR(status->st_mode) &&
           status->st_uid == owner &&
           (status->st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)) == permissions;
}

static bool same_identity(const struct stat *left,
                          const struct stat *right)
{
    return left != NULL && right != NULL &&
           left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static int open_directory(const char *path)
{
    int flags = O_RDONLY;

#if defined(O_DIRECTORY)
    flags |= O_DIRECTORY;
#endif
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    return open(path, flags);
}

static bool named_directory_matches(
    const struct runtime_sentinel_directory *directory)
{
    struct stat named;

    return fstatat(directory->parent_fd,
                   directory->basename,
                   &named,
                   AT_SYMLINK_NOFOLLOW) == 0 &&
           directory_identity_is_valid(&named, directory->identity.st_uid) &&
           same_identity(&named, &directory->identity);
}

static void directory_init(struct runtime_sentinel_directory *directory)
{
    memset(directory, 0, sizeof(*directory));
    directory->parent_fd = -1;
    directory->directory_fd = -1;
}

static void directory_close(struct runtime_sentinel_directory *directory)
{
    if (directory->directory_fd >= 0) {
        (void)close(directory->directory_fd);
        directory->directory_fd = -1;
    }
    if (directory->parent_fd >= 0) {
        (void)close(directory->parent_fd);
        directory->parent_fd = -1;
    }
}

static bool directory_create(struct runtime_sentinel_directory *directory)
{
    const uid_t owner = geteuid();
    const char *basename;
    struct stat named;
    mode_t previous_umask;
    int length;

    directory_init(directory);
    length = snprintf(directory->path,
                      sizeof(directory->path),
                      "/tmp/nixbench-runtime-%" PRIuMAX "-XXXXXX",
                      (uintmax_t)owner);
    if (length < 0 || (size_t)length >= sizeof(directory->path)) {
        errno = ENAMETOOLONG;
        return false;
    }
    previous_umask = umask((mode_t)077);
    if (mkdtemp(directory->path) == NULL) {
        const int saved_error = errno;

        (void)umask(previous_umask);
        errno = saved_error;
        return false;
    }
    (void)umask(previous_umask);
    directory->created = true;

    basename = strrchr(directory->path, '/');
    if (basename == NULL || basename[1] == '\0' ||
        snprintf(directory->basename,
                 sizeof(directory->basename),
                 "%s",
                 basename + 1) < 0 ||
        strlen(basename + 1) >= sizeof(directory->basename)) {
        errno = ENAMETOOLONG;
        goto fail;
    }
    directory->parent_fd = open_directory(runtime_parent_path);
    if (directory->parent_fd < 0) {
        goto fail;
    }
    directory->directory_fd = open_directory(directory->path);
    if (directory->directory_fd < 0) {
        goto fail;
    }
    if (!set_cloexec(directory->parent_fd) ||
        !set_cloexec(directory->directory_fd) ||
        fstat(directory->directory_fd, &directory->identity) != 0) {
        goto fail;
    }
    if (!directory_identity_is_valid(&directory->identity, owner)) {
        errno = EPERM;
        goto fail;
    }
    if (fstatat(directory->parent_fd,
                directory->basename,
                &named,
                AT_SYMLINK_NOFOLLOW) != 0) {
        goto fail;
    }
    if (!directory_identity_is_valid(&named, owner) ||
        !same_identity(&named, &directory->identity)) {
        errno = EPERM;
        goto fail;
    }
    return true;

fail:
    {
        const int saved_error = errno != 0 ? errno : EIO;

        directory_close(directory);
        (void)rmdir(directory->path);
        directory->created = false;
        errno = saved_error;
        return false;
    }
}

static int duplicate_cloexec(int descriptor)
{
    int duplicate;

#if defined(F_DUPFD_CLOEXEC)
    duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (duplicate >= 0 || errno != EINVAL) {
        return duplicate;
    }
#endif
    duplicate = dup(descriptor);
    if (duplicate >= 0 && !set_cloexec(duplicate)) {
        const int saved_error = errno;

        (void)close(duplicate);
        errno = saved_error;
        return -1;
    }
    return duplicate;
}

static bool remove_direct_entries(
    const struct runtime_sentinel_directory *directory)
{
    int scan_fd = duplicate_cloexec(directory->directory_fd);
    DIR *stream;
    struct dirent *entry;
    bool success = true;

    if (scan_fd < 0) {
        return false;
    }
    stream = fdopendir(scan_fd);
    if (stream == NULL) {
        const int saved_error = errno;

        (void)close(scan_fd);
        errno = saved_error;
        return false;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        struct stat status;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (fstatat(directory->directory_fd,
                    entry->d_name,
                    &status,
                    AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno == ENOENT) {
                errno = 0;
                continue;
            }
            success = false;
            break;
        }
        if (S_ISDIR(status.st_mode)) {
            errno = ENOTEMPTY;
            success = false;
            break;
        }
        if (unlinkat(directory->directory_fd, entry->d_name, 0) != 0 &&
            errno != ENOENT) {
            success = false;
            break;
        }
        errno = 0;
    }
    if (entry == NULL && errno != 0) {
        success = false;
    }
    if (closedir(stream) != 0 && success) {
        success = false;
    }
    return success;
}

static bool directory_cleanup(struct runtime_sentinel_directory *directory)
{
    struct stat open_identity;
    struct stat remaining;

    if (!directory->created || directory->parent_fd < 0 ||
        directory->directory_fd < 0 ||
        fstat(directory->directory_fd, &open_identity) != 0 ||
        !directory_identity_is_valid(&open_identity,
                                     directory->identity.st_uid) ||
        !same_identity(&open_identity, &directory->identity) ||
        !named_directory_matches(directory) ||
        !remove_direct_entries(directory) ||
        !named_directory_matches(directory) ||
        unlinkat(directory->parent_fd,
                 directory->basename,
                 AT_REMOVEDIR) != 0) {
        return false;
    }
    errno = 0;
    if (fstatat(directory->parent_fd,
                directory->basename,
                &remaining,
                AT_SYMLINK_NOFOLLOW) == 0 ||
        errno != ENOENT) {
        if (errno == 0) {
            errno = EEXIST;
        }
        return false;
    }
    directory->created = false;
    return true;
}

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return UINT64_MAX;
    }
    if ((uint64_t)now.tv_sec > UINT64_MAX / UINT64_C(1000)) {
        return UINT64_MAX;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static bool wait_until_ready(int descriptor,
                             short events,
                             uint64_t deadline)
{
    for (;;) {
        struct pollfd poll_descriptor;
        const uint64_t now = monotonic_milliseconds();
        uint64_t remaining;
        int timeout;
        int result;

        if (now == UINT64_MAX) {
            return false;
        }
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return false;
        }
        remaining = deadline - now;
        timeout = remaining > (uint64_t)INT_MAX
                      ? INT_MAX
                      : (int)remaining;
        memset(&poll_descriptor, 0, sizeof(poll_descriptor));
        poll_descriptor.fd = descriptor;
        poll_descriptor.events = events;
        result = poll(&poll_descriptor, 1, timeout);
        if (result > 0) {
            if ((poll_descriptor.revents & POLLNVAL) != 0) {
                errno = EBADF;
                return false;
            }
            return true;
        }
        if (result == 0) {
            errno = ETIMEDOUT;
            return false;
        }
        if (errno != EINTR) {
            return false;
        }
    }
}

static bool send_all_until(int descriptor,
                           const void *data,
                           size_t size,
                           uint64_t deadline)
{
    const unsigned char *bytes = data;

    while (size != 0U) {
        ssize_t count;

        if (!wait_until_ready(descriptor, POLLOUT, deadline)) {
            return false;
        }
        count = write_without_sigpipe(descriptor, bytes, size);
        if (count > 0) {
            bytes += (size_t)count;
            size -= (size_t)count;
        } else if (count < 0 &&
                   (errno == EINTR || errno == EAGAIN ||
                    errno == EWOULDBLOCK)) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool receive_all_until(int descriptor,
                              void *data,
                              size_t size,
                              uint64_t deadline)
{
    unsigned char *bytes = data;

    while (size != 0U) {
        ssize_t count;

        if (!wait_until_ready(descriptor, POLLIN, deadline)) {
            return false;
        }
        count = recv(descriptor, bytes, size, 0);
        if (count > 0) {
            bytes += (size_t)count;
            size -= (size_t)count;
        } else if (count == 0) {
            errno = EPIPE;
            return false;
        } else if (errno == EINTR || errno == EAGAIN ||
                   errno == EWOULDBLOCK) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool begin_controller_operation(int descriptor,
                                       uint32_t timeout_ms,
                                       int *saved_flags,
                                       uint64_t *deadline)
{
    const uint64_t now = monotonic_milliseconds();

    if (descriptor < 0 || timeout_ms == 0 || saved_flags == NULL ||
        deadline == NULL) {
        errno = EINVAL;
        return false;
    }
    if (now == UINT64_MAX) {
        return false;
    }
    *saved_flags = fcntl(descriptor, F_GETFL);
    if (*saved_flags < 0 ||
        fcntl(descriptor, F_SETFL, *saved_flags | O_NONBLOCK) != 0) {
        return false;
    }
    if (!configure_no_sigpipe(descriptor)) {
        const int saved_error = errno;

        (void)fcntl(descriptor, F_SETFL, *saved_flags);
        errno = saved_error;
        return false;
    }
    *deadline = timeout_ms > UINT64_MAX - now
                    ? UINT64_MAX
                    : now + (uint64_t)timeout_ms;
    return true;
}

static bool finish_controller_operation(int descriptor,
                                        int saved_flags,
                                        bool success,
                                        int operation_error)
{
    if (fcntl(descriptor, F_SETFL, saved_flags) != 0) {
        return false;
    }
    if (!success) {
        errno = operation_error != 0 ? operation_error : EIO;
    }
    return success;
}

int nb_session_runtime_sentinel_run(int controller_fd)
{
    struct runtime_sentinel_directory directory;
    struct runtime_sentinel_message message;
    enum sentinel_read_result read_result;
    bool explicit_request = false;
    bool cleaned;
    bool response_sent = true;

    directory_init(&directory);
    if (controller_fd < 0 || geteuid() == (uid_t)0 ||
        getuid() != geteuid() || getgid() != getegid() ||
        !configure_no_sigpipe(controller_fd) ||
        !directory_create(&directory)) {
        directory_close(&directory);
        return 1;
    }

    initialize_message(&message, NB_RUNTIME_SENTINEL_WIRE_READY);
    (void)snprintf(message.path, sizeof(message.path), "%s", directory.path);
    if (!send_all(controller_fd, &message, sizeof(message))) {
        (void)directory_cleanup(&directory);
        directory_close(&directory);
        return 1;
    }

    memset(&message, 0, sizeof(message));
    read_result = receive_all(controller_fd, &message, sizeof(message));
    if (read_result == SENTINEL_READ_COMPLETE &&
        empty_message_is_valid(&message, NB_RUNTIME_SENTINEL_WIRE_CLEAN)) {
        explicit_request = true;
    } else if (read_result != SENTINEL_READ_EOF) {
        cleaned = directory_cleanup(&directory);
        if (read_result == SENTINEL_READ_COMPLETE) {
            initialize_message(&message, NB_RUNTIME_SENTINEL_WIRE_FAILED);
            (void)send_all(controller_fd, &message, sizeof(message));
        }
        directory_close(&directory);
        (void)cleaned;
        return 1;
    }

    cleaned = directory_cleanup(&directory);
    if (explicit_request) {
        initialize_message(
            &message,
            cleaned ? NB_RUNTIME_SENTINEL_WIRE_CLEANED
                    : NB_RUNTIME_SENTINEL_WIRE_FAILED);
        response_sent = send_all(controller_fd, &message, sizeof(message));
    }
    directory_close(&directory);
    return cleaned && response_sent ? 0 : 1;
}

bool nb_session_runtime_sentinel_wait_ready(
    int controller_fd,
    uint32_t timeout_ms,
    char path[NB_SESSION_RUNTIME_SENTINEL_PATH_CAPACITY],
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY])
{
    struct runtime_sentinel_message message;
    uint64_t deadline;
    int saved_flags = 0;
    int operation_error;
    bool success;

    if (path != NULL) {
        path[0] = '\0';
    }
    if (error != NULL) {
        error[0] = '\0';
    }
    if (path == NULL) {
        errno = EINVAL;
        set_error(error,
                  "could not start the runtime-sentinel ready exchange: %s",
                  strerror(errno));
        return false;
    }
    if (!begin_controller_operation(controller_fd,
                                    timeout_ms,
                                    &saved_flags,
                                    &deadline)) {
        operation_error = errno != 0 ? errno : EINVAL;
        set_error(error,
                  "could not start the runtime-sentinel ready exchange: %s",
                  strerror(operation_error));
        errno = operation_error;
        return false;
    }
    memset(&message, 0, sizeof(message));
    success = receive_all_until(controller_fd,
                                &message,
                                sizeof(message),
                                deadline) &&
              message_header_is_valid(&message,
                                      NB_RUNTIME_SENTINEL_WIRE_READY) &&
              ready_path_is_valid(message.path);
    operation_error = success ? 0 : errno != 0 ? errno : EPROTO;
    success = finish_controller_operation(controller_fd,
                                          saved_flags,
                                          success,
                                          operation_error);
    if (!success) {
        operation_error = errno != 0 ? errno : operation_error;
        set_error(error,
                  "runtime sentinel did not become ready: %s",
                  strerror(operation_error));
        errno = operation_error;
        return false;
    }
    (void)memcpy(path, message.path, sizeof(message.path));
    return true;
}

bool nb_session_runtime_sentinel_request_cleanup(
    int controller_fd,
    uint32_t timeout_ms,
    char error[NB_SESSION_RUNTIME_SENTINEL_ERROR_CAPACITY])
{
    struct runtime_sentinel_message command;
    struct runtime_sentinel_message response;
    uint64_t deadline;
    int saved_flags = 0;
    int operation_error;
    bool success;

    if (error != NULL) {
        error[0] = '\0';
    }
    if (!begin_controller_operation(controller_fd,
                                    timeout_ms,
                                    &saved_flags,
                                    &deadline)) {
        operation_error = errno != 0 ? errno : EINVAL;
        set_error(error,
                  "could not start the runtime-sentinel cleanup exchange: %s",
                  strerror(operation_error));
        errno = operation_error;
        return false;
    }
    initialize_message(&command, NB_RUNTIME_SENTINEL_WIRE_CLEAN);
    memset(&response, 0, sizeof(response));
    success = send_all_until(controller_fd,
                             &command,
                             sizeof(command),
                             deadline) &&
              receive_all_until(controller_fd,
                                &response,
                                sizeof(response),
                                deadline) &&
              empty_message_is_valid(&response,
                                     NB_RUNTIME_SENTINEL_WIRE_CLEANED);
    operation_error = success ? 0 : errno != 0 ? errno : EPROTO;
    success = finish_controller_operation(controller_fd,
                                          saved_flags,
                                          success,
                                          operation_error);
    if (!success) {
        operation_error = errno != 0 ? errno : operation_error;
        if (empty_message_is_valid(&response,
                                   NB_RUNTIME_SENTINEL_WIRE_FAILED)) {
            set_error(error,
                      "runtime sentinel could not remove its directory");
        } else {
            set_error(error,
                      "runtime sentinel cleanup exchange failed: %s",
                      strerror(operation_error));
        }
        errno = operation_error;
        return false;
    }
    return true;
}
