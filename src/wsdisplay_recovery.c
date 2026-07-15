#if defined(__NetBSD__)
#define _NETBSD_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L

#include "wsdisplay_recovery.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    NB_WSDISPLAY_RECOVERY_VERSION = 1,
    NB_WSDISPLAY_RECOVERY_ACTIVE_SCREEN_MAX = 255
};

static const uint32_t recovery_magic = UINT32_C(0x4e425253);

struct nb_wsdisplay_recovery_record {
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t reserved;
    struct nb_wsdisplay_console_state state;
};

static void set_error(char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY],
                      const char *format,
                      ...)
{
    va_list arguments;

    if (error == NULL) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error,
                    NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY,
                    format,
                    arguments);
    va_end(arguments);
}

static bool text_is_absolute_and_terminated(const char *text,
                                            size_t capacity)
{
    return text != NULL && text[0] == '/' &&
           memchr(text, '\0', capacity) != NULL;
}

static bool options_are_valid(
    const struct nb_wsdisplay_recovery_options *options)
{
    return options != NULL && options->record_path != NULL &&
           options->record_path[0] == '/' &&
           options->status_device_path != NULL &&
           options->status_device_path[0] == '/' &&
           options->screen_device_prefix != NULL &&
           options->screen_device_prefix[0] == '/';
}

static bool saved_state_is_valid(
    const struct nb_wsdisplay_recovery_options *options,
    const struct nb_wsdisplay_console_state *state)
{
    char expected_screen[NB_WSDISPLAY_CONSOLE_PATH_CAPACITY];
    int length;

    if (state == NULL || state->active_screen < 0 ||
        state->active_screen > NB_WSDISPLAY_RECOVERY_ACTIVE_SCREEN_MAX ||
        !text_is_absolute_and_terminated(state->status_device,
                                         sizeof(state->status_device)) ||
        !text_is_absolute_and_terminated(state->screen_device,
                                         sizeof(state->screen_device)) ||
        strcmp(state->status_device, options->status_device_path) != 0) {
        return false;
    }
    length = snprintf(expected_screen,
                      sizeof(expected_screen),
                      "%s%d",
                      options->screen_device_prefix,
                      state->active_screen);
    return length >= 0 && (size_t)length < sizeof(expected_screen) &&
           strcmp(state->screen_device, expected_screen) == 0;
}

static bool write_all(int descriptor, const void *data, size_t size)
{
    const unsigned char *bytes = data;

    while (size != 0U) {
        const ssize_t written = write(descriptor, bytes, size);

        if (written > 0) {
            bytes += (size_t)written;
            size -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool read_all(int descriptor, void *data, size_t size)
{
    unsigned char *bytes = data;

    while (size != 0U) {
        const ssize_t count = read(descriptor, bytes, size);

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

static bool record_file_metadata_is_valid(const struct stat *status,
                                          uid_t owner)
{
    return status != NULL && S_ISREG(status->st_mode) &&
           status->st_uid == owner && status->st_nlink == 1 &&
           (status->st_mode & (S_IRWXG | S_IRWXO)) == 0 &&
           (status->st_mode & S_IRWXU) == (S_IRUSR | S_IWUSR);
}

static bool record_file_is_valid(const struct stat *status,
                                 uid_t owner)
{
    return record_file_metadata_is_valid(status, owner) &&
           status->st_size ==
               (off_t)sizeof(struct nb_wsdisplay_recovery_record);
}

bool nb_wsdisplay_recovery_store(
    const struct nb_wsdisplay_recovery_options *options,
    const struct nb_wsdisplay_console_state *state,
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY])
{
    struct nb_wsdisplay_recovery_record record;
    struct stat status;
    int flags = O_WRONLY | O_CREAT | O_EXCL;
    int descriptor;
    int saved_error = 0;
    bool success;

    if (error != NULL) {
        error[0] = '\0';
    }
    if (!options_are_valid(options) ||
        !saved_state_is_valid(options, state)) {
        set_error(error, "invalid wsdisplay recovery state");
        errno = EINVAL;
        return false;
    }
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    descriptor = open(options->record_path,
                      flags,
                      S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        set_error(error,
                  "could not create %s: %s",
                  options->record_path,
                  strerror(errno));
        return false;
    }

    memset(&record, 0, sizeof(record));
    record.magic = recovery_magic;
    record.version = NB_WSDISPLAY_RECOVERY_VERSION;
    record.record_size = (uint32_t)sizeof(record);
    record.state = *state;
    success = fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 &&
              fstat(descriptor, &status) == 0 &&
              record_file_metadata_is_valid(&status,
                                            options->record_owner) &&
              write_all(descriptor, &record, sizeof(record)) &&
              fsync(descriptor) == 0 &&
              fstat(descriptor, &status) == 0 &&
              record_file_is_valid(&status, options->record_owner);
    if (!success) {
        saved_error = errno != 0 ? errno : EPERM;
    }
    if (close(descriptor) != 0 && success) {
        success = false;
        saved_error = errno;
    }
    if (!success) {
        (void)unlink(options->record_path);
        errno = saved_error != 0 ? saved_error : EIO;
        set_error(error,
                  "could not persist %s: %s",
                  options->record_path,
                  strerror(errno));
    }
    return success;
}

bool nb_wsdisplay_recovery_load(
    const struct nb_wsdisplay_recovery_options *options,
    struct nb_wsdisplay_console_state *state,
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY])
{
    struct nb_wsdisplay_recovery_record record;
    struct stat status;
    int flags = O_RDONLY;
    int descriptor;
    int saved_error = 0;
    bool success;

    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
    if (error != NULL) {
        error[0] = '\0';
    }
    if (!options_are_valid(options) || state == NULL) {
        set_error(error, "invalid wsdisplay recovery arguments");
        errno = EINVAL;
        return false;
    }
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    descriptor = open(options->record_path, flags);
    if (descriptor < 0) {
        set_error(error,
                  "could not open %s: %s",
                  options->record_path,
                  strerror(errno));
        return false;
    }
    memset(&record, 0, sizeof(record));
    success = fstat(descriptor, &status) == 0 &&
              record_file_is_valid(&status, options->record_owner) &&
              read_all(descriptor, &record, sizeof(record)) &&
              record.magic == recovery_magic &&
              record.version == NB_WSDISPLAY_RECOVERY_VERSION &&
              record.record_size == (uint32_t)sizeof(record) &&
              record.reserved == 0 &&
              saved_state_is_valid(options, &record.state);
    if (!success) {
        saved_error = errno != 0 ? errno : EINVAL;
    }
    if (close(descriptor) != 0 && success) {
        success = false;
        saved_error = errno;
    }
    if (!success) {
        memset(state, 0, sizeof(*state));
        errno = saved_error != 0 ? saved_error : EIO;
        set_error(error,
                  "invalid recovery record %s: %s",
                  options->record_path,
                  strerror(errno));
        return false;
    }
    *state = record.state;
    return true;
}

bool nb_wsdisplay_recovery_remove(
    const struct nb_wsdisplay_recovery_options *options,
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY])
{
    if (error != NULL) {
        error[0] = '\0';
    }
    if (!options_are_valid(options)) {
        set_error(error, "invalid wsdisplay recovery arguments");
        errno = EINVAL;
        return false;
    }
    if (unlink(options->record_path) == 0 || errno == ENOENT) {
        return true;
    }
    set_error(error,
              "could not remove %s: %s",
              options->record_path,
              strerror(errno));
    return false;
}
