#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "screenshot.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static void test_screenshot_file(void)
{
    static const unsigned char png_signature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
    };
    char directory[] = "/tmp/nixbench-screenshot-XXXXXX";
    uint32_t pixels[4] = {
        UINT32_C(0x00ff0000), UINT32_C(0x0000ff00),
        UINT32_C(0x000000ff), UINT32_C(0x00ffffff)
    };
    const struct nb_host_frame frame = {
        pixels,
        2,
        2,
        2 * sizeof(uint32_t),
        NB_HOST_PIXEL_FORMAT_XRGB8888,
        1,
        0,
        0,
        0,
        0,
        NULL,
        0
    };
    char first[1024];
    char second[1024];
    char error[256];
    unsigned char header[24];
    char pid[32];
    FILE *file;

    CHECK(mkdtemp(directory) != NULL);
    CHECK(setenv("HOME", directory, 1) == 0);
    CHECK(setenv("USER", "test-user", 1) == 0);
    CHECK(nb_screenshot_save_home(&frame,
                                  first,
                                  sizeof(first),
                                  error,
                                  sizeof(error)));
    CHECK(strstr(first, "/nixbench-test-user-") != NULL);
    (void)snprintf(pid, sizeof(pid), "-%ld-", (long)getpid());
    CHECK(strstr(first, pid) != NULL);
    CHECK(strlen(first) > 4 && strcmp(first + strlen(first) - 4, ".png") == 0);

    file = fopen(first, "rb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fread(header, 1, sizeof(header), file) == sizeof(header));
        CHECK(fclose(file) == 0);
        CHECK(memcmp(header, png_signature, sizeof(png_signature)) == 0);
        CHECK(memcmp(header + 12, "IHDR", 4) == 0);
        CHECK(header[19] == 2);
        CHECK(header[23] == 2);
    }

    CHECK(nb_screenshot_save_home(&frame,
                                  second,
                                  sizeof(second),
                                  error,
                                  sizeof(error)));
    CHECK(strcmp(first, second) != 0);
    CHECK(unlink(first) == 0);
    CHECK(unlink(second) == 0);
    CHECK(rmdir(directory) == 0);
}

static void test_invalid_requests(void)
{
    char path[64];
    char error[64];

    CHECK(!nb_screenshot_save_home(NULL,
                                   path,
                                   sizeof(path),
                                   error,
                                   sizeof(error)));
    CHECK(path[0] == '\0');
    CHECK(error[0] != '\0');
}

int main(void)
{
    test_screenshot_file();
    test_invalid_requests();

    if (failures != 0) {
        fprintf(stderr, "screenshot tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("screenshot tests passed");
    return 0;
}
