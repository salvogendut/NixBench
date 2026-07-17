#define _POSIX_C_SOURCE 200809L

#include "screenshot.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "png_writer.h"

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static void screenshot_user(char user[64])
{
    const char *source = getenv("USER");
    const struct passwd *password = NULL;
    size_t destination = 0;

    if (source == NULL || source[0] == '\0') {
        password = getpwuid(geteuid());
        source = password != NULL ? password->pw_name : "user";
    }
    while (*source != '\0' && destination + 1 < 64) {
        const unsigned char character = (unsigned char)*source++;

        if (isalnum(character) || character == '-' || character == '_') {
            user[destination++] = (char)character;
        }
    }
    if (destination == 0) {
        (void)memcpy(user, "user", sizeof("user"));
    } else {
        user[destination] = '\0';
    }
}

static bool reserve_path(const char *home,
                         const char *user,
                         const char *timestamp,
                         char *path,
                         size_t capacity)
{
    unsigned int suffix;

    for (suffix = 0; suffix < 1000; ++suffix) {
        const int characters = suffix == 0
                                   ? snprintf(path,
                                              capacity,
                                              "%s/nixbench-%s-%ld-%s.png",
                                              home,
                                              user,
                                              (long)getpid(),
                                              timestamp)
                                   : snprintf(path,
                                              capacity,
                                              "%s/nixbench-%s-%ld-%s-%u.png",
                                              home,
                                              user,
                                              (long)getpid(),
                                              timestamp,
                                              suffix + 1);
        int descriptor;

        if (characters < 0 || (size_t)characters >= capacity) {
            errno = ENAMETOOLONG;
            return false;
        }
        descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor >= 0) {
            if (close(descriptor) == 0) {
                return true;
            }
            (void)unlink(path);
            return false;
        }
        if (errno != EEXIST) {
            return false;
        }
    }
    errno = EEXIST;
    return false;
}

bool nb_screenshot_save_home(const struct nb_host_frame *frame,
                             char *saved_path,
                             size_t saved_path_capacity,
                             char *error,
                             size_t error_capacity)
{
    const char *home = getenv("HOME");
    const time_t now = time(NULL);
    struct tm local_time;
    char timestamp[32];
    char user[64];

    if (saved_path != NULL && saved_path_capacity > 0) {
        saved_path[0] = '\0';
    }
    if (error != NULL && error_capacity > 0) {
        error[0] = '\0';
    }
    if (frame == NULL || !nb_host_frame_is_valid(frame) ||
        frame->format != NB_HOST_PIXEL_FORMAT_XRGB8888 ||
        saved_path == NULL || saved_path_capacity == 0) {
        set_error(error, error_capacity, "invalid screenshot frame");
        return false;
    }
    if (home == NULL || home[0] != '/') {
        set_error(error, error_capacity, "HOME is not an absolute path");
        return false;
    }
    if (now == (time_t)-1 || localtime_r(&now, &local_time) == NULL ||
        strftime(timestamp,
                 sizeof(timestamp),
                 "%Y%m%d-%H%M%S",
                 &local_time) == 0) {
        set_error(error, error_capacity, "could not determine local time");
        return false;
    }
    screenshot_user(user);
    if (!reserve_path(home,
                      user,
                      timestamp,
                      saved_path,
                      saved_path_capacity)) {
        if (errno == ENAMETOOLONG) {
            set_error(error, error_capacity, "screenshot path is too long");
        } else if (error != NULL && error_capacity > 0) {
            (void)snprintf(error,
                           error_capacity,
                           "could not reserve screenshot path: %s",
                           strerror(errno));
        }
        saved_path[0] = '\0';
        return false;
    }
    if (!nb_png_write_xrgb8888(saved_path,
                               frame->pixels,
                               frame->width,
                               frame->height,
                               frame->stride,
                               error,
                               error_capacity)) {
        (void)unlink(saved_path);
        saved_path[0] = '\0';
        return false;
    }
    return true;
}
