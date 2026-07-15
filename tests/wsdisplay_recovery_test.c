#define _POSIX_C_SOURCE 200809L

#include "wsdisplay_recovery.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

struct fixture {
    char directory[64];
    char path[96];
    struct nb_wsdisplay_recovery_options options;
};

static bool fixture_create(struct fixture *fixture)
{
    char template_path[] = "/tmp/nixbench-recovery-XXXXXX";
    char *directory;
    int length;

    memset(fixture, 0, sizeof(*fixture));
    directory = mkdtemp(template_path);
    if (directory == NULL) {
        perror("mkdtemp");
        ++failures;
        return false;
    }
    length = snprintf(fixture->directory,
                      sizeof(fixture->directory),
                      "%s",
                      directory);
    if (length < 0 || (size_t)length >= sizeof(fixture->directory)) {
        ++failures;
        (void)rmdir(directory);
        return false;
    }
    length = snprintf(fixture->path,
                      sizeof(fixture->path),
                      "%s/state",
                      fixture->directory);
    if (length < 0 || (size_t)length >= sizeof(fixture->path)) {
        ++failures;
        (void)rmdir(fixture->directory);
        return false;
    }
    fixture->options.record_path = fixture->path;
    fixture->options.status_device_path = "/dev/ttyEstat";
    fixture->options.screen_device_prefix = "/dev/ttyE";
    fixture->options.record_owner = geteuid();
    return true;
}

static void fixture_destroy(struct fixture *fixture)
{
    (void)unlink(fixture->path);
    (void)rmdir(fixture->directory);
}

static struct nb_wsdisplay_console_state valid_state(void)
{
    struct nb_wsdisplay_console_state state;

    memset(&state, 0, sizeof(state));
    state.active_screen = 3;
    state.display_mode = 7;
    state.video = 1;
    state.vt_mode.mode = 9;
    state.video_available = true;
    (void)snprintf(state.status_device,
                   sizeof(state.status_device),
                   "/dev/ttyEstat");
    (void)snprintf(state.screen_device,
                   sizeof(state.screen_device),
                   "/dev/ttyE3");
    return state;
}

static void test_round_trip_and_exclusive_create(void)
{
    struct fixture fixture;
    struct nb_wsdisplay_console_state source = valid_state();
    struct nb_wsdisplay_console_state loaded;
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY];

    if (!fixture_create(&fixture)) {
        return;
    }
    CHECK(nb_wsdisplay_recovery_store(&fixture.options, &source, error));
    CHECK(error[0] == '\0');
    CHECK(!nb_wsdisplay_recovery_store(&fixture.options, &source, error));
    CHECK(nb_wsdisplay_recovery_load(&fixture.options, &loaded, error));
    CHECK(memcmp(&loaded, &source, sizeof(source)) == 0);
    CHECK(nb_wsdisplay_recovery_remove(&fixture.options, error));
    CHECK(access(fixture.path, F_OK) != 0 && errno == ENOENT);
    CHECK(nb_wsdisplay_recovery_remove(&fixture.options, error));
    fixture_destroy(&fixture);
}

static void test_state_path_validation(void)
{
    struct fixture fixture;
    struct nb_wsdisplay_console_state state = valid_state();
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY];

    if (!fixture_create(&fixture)) {
        return;
    }
    state.active_screen = -1;
    CHECK(!nb_wsdisplay_recovery_store(&fixture.options, &state, error));
    state = valid_state();
    (void)snprintf(state.screen_device,
                   sizeof(state.screen_device),
                   "/dev/ttyE4");
    CHECK(!nb_wsdisplay_recovery_store(&fixture.options, &state, error));
    state = valid_state();
    (void)snprintf(state.status_device,
                   sizeof(state.status_device),
                   "/tmp/not-the-status-device");
    CHECK(!nb_wsdisplay_recovery_store(&fixture.options, &state, error));
    fixture.options.record_path = "relative";
    state = valid_state();
    CHECK(!nb_wsdisplay_recovery_store(&fixture.options, &state, error));
    fixture_destroy(&fixture);
}

static void test_metadata_and_content_rejections(void)
{
    struct fixture fixture;
    struct nb_wsdisplay_console_state source = valid_state();
    struct nb_wsdisplay_console_state loaded;
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY];
    int descriptor;
    unsigned char byte = 0;

    if (!fixture_create(&fixture)) {
        return;
    }
    CHECK(nb_wsdisplay_recovery_store(&fixture.options, &source, error));
    fixture.options.record_owner = geteuid() == (uid_t)0
                                       ? (uid_t)1
                                       : (uid_t)0;
    CHECK(!nb_wsdisplay_recovery_load(&fixture.options, &loaded, error));
    fixture.options.record_owner = geteuid();
    CHECK(chmod(fixture.path, 0644) == 0);
    CHECK(!nb_wsdisplay_recovery_load(&fixture.options, &loaded, error));
    CHECK(chmod(fixture.path, 0600) == 0);

    descriptor = open(fixture.path, O_WRONLY | O_TRUNC);
    CHECK(descriptor >= 0);
    if (descriptor >= 0) {
        CHECK(write(descriptor, &byte, sizeof(byte)) ==
              (ssize_t)sizeof(byte));
        CHECK(close(descriptor) == 0);
    }
    CHECK(!nb_wsdisplay_recovery_load(&fixture.options, &loaded, error));
    fixture_destroy(&fixture);
}

static void test_symlink_rejection(void)
{
    struct fixture fixture;
    struct nb_wsdisplay_console_state state = valid_state();
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY];
    char target[96];
    int descriptor;

    if (!fixture_create(&fixture)) {
        return;
    }
    (void)snprintf(target, sizeof(target), "%s/target", fixture.directory);
    descriptor = open(target, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(descriptor >= 0);
    if (descriptor >= 0) {
        CHECK(close(descriptor) == 0);
    }
    CHECK(symlink(target, fixture.path) == 0);
    CHECK(!nb_wsdisplay_recovery_store(&fixture.options, &state, error));
    CHECK(!nb_wsdisplay_recovery_load(&fixture.options, &state, error));
    CHECK(unlink(fixture.path) == 0);
    CHECK(unlink(target) == 0);
    fixture_destroy(&fixture);
}

static void test_invalid_arguments(void)
{
    struct nb_wsdisplay_console_state state = valid_state();
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY];

    CHECK(!nb_wsdisplay_recovery_store(NULL, &state, error));
    CHECK(!nb_wsdisplay_recovery_load(NULL, &state, error));
    CHECK(!nb_wsdisplay_recovery_remove(NULL, error));
}

int main(void)
{
    test_round_trip_and_exclusive_create();
    test_state_path_validation();
    test_metadata_and_content_rejections();
    test_symlink_rejection();
    test_invalid_arguments();

    if (failures != 0) {
        fprintf(stderr, "%d wsdisplay recovery test(s) failed\n", failures);
        return 1;
    }
    puts("wsdisplay recovery tests passed");
    return 0;
}
