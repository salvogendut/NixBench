#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_browser.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static bool touch_file(const char *path)
{
    const int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    return descriptor >= 0 && close(descriptor) == 0;
}

int main(void)
{
    char temporary[] = "/tmp/nixbench-filesystem-XXXXXX";
    char path[NB_FILESYSTEM_PATH_CAPACITY];
    char parent[NB_FILESYSTEM_PATH_CAPACITY];
    char error[256] = {0};
    struct nb_filesystem_directory directory;
    struct nb_file_browser browser;
    const struct nb_filesystem_entry *entry;

    CHECK(mkdtemp(temporary) != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/Pictures", temporary) > 0);
    CHECK(mkdir(path, 0700) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/alpha.png", temporary) > 0);
    CHECK(touch_file(path));
    CHECK(snprintf(path, sizeof(path), "%s/BETA.PNG", temporary) > 0);
    CHECK(touch_file(path));
    CHECK(snprintf(path, sizeof(path), "%s/notes.txt", temporary) > 0);
    CHECK(touch_file(path));
    CHECK(snprintf(path, sizeof(path), "%s/.hidden.png", temporary) > 0);
    CHECK(touch_file(path));

    nb_filesystem_directory_init(&directory);
    CHECK(nb_filesystem_directory_load(&directory,
                                       temporary,
                                       error,
                                       sizeof(error)));
    CHECK(directory.count == 5);
    CHECK(directory.entries[0].kind == NB_FILESYSTEM_ENTRY_DIRECTORY);
    CHECK(strcmp(directory.entries[0].name, "Pictures") == 0);
    CHECK(nb_filesystem_parent_path(directory.path,
                                    parent,
                                    sizeof(parent)));
    CHECK(strcmp(parent, "/tmp") == 0);
    CHECK(nb_filesystem_name_has_extension("wallpaper.PNG", ".png"));
    CHECK(!nb_filesystem_name_has_extension("png", ".png"));
    CHECK(!nb_filesystem_directory_load(&directory,
                                        "relative",
                                        error,
                                        sizeof(error)));
    CHECK(strcmp(directory.path, temporary) == 0);
    nb_filesystem_directory_destroy(&directory);

    nb_file_browser_init(&browser, ".png");
    CHECK(nb_file_browser_open(&browser,
                               temporary,
                               error,
                               sizeof(error)));
    CHECK(nb_file_browser_visible_count(&browser) == 3);
    entry = nb_file_browser_visible_entry(&browser, 0, NULL);
    CHECK(entry != NULL &&
          entry->kind == NB_FILESYSTEM_ENTRY_DIRECTORY);
    CHECK(nb_file_browser_select(&browser, 0));
    CHECK(nb_file_browser_enter_selected(&browser, error, sizeof(error)));
    CHECK(strstr(browser.directory.path, "/Pictures") != NULL);
    CHECK(nb_file_browser_parent(&browser, error, sizeof(error)));
    CHECK(nb_file_browser_select(&browser, 1));
    CHECK(nb_file_browser_selected_path(&browser, path, sizeof(path)));
    CHECK(strstr(path, ".png") != NULL || strstr(path, ".PNG") != NULL);
    browser.show_hidden = true;
    CHECK(nb_file_browser_visible_count(&browser) == 4);
    nb_file_browser_scroll(&browser, 100, 2);
    CHECK(browser.first_visible == 2);
    nb_file_browser_scroll(&browser, -100, 2);
    CHECK(browser.first_visible == 0);
    nb_file_browser_destroy(&browser);

    CHECK(snprintf(path, sizeof(path), "%s/.hidden.png", temporary) > 0);
    CHECK(unlink(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/notes.txt", temporary) > 0);
    CHECK(unlink(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/BETA.PNG", temporary) > 0);
    CHECK(unlink(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/alpha.png", temporary) > 0);
    CHECK(unlink(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/Pictures", temporary) > 0);
    CHECK(rmdir(path) == 0);
    CHECK(rmdir(temporary) == 0);

    if (failures != 0) {
        fprintf(stderr, "filesystem tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("filesystem tests: ok");
    return 0;
}
