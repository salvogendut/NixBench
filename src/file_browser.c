#include "file_browser.h"

#include <stdint.h>
#include <string.h>

static bool entry_visible(const struct nb_file_browser *browser,
                          const struct nb_filesystem_entry *entry)
{
    if (!browser->show_hidden && entry->hidden) {
        return false;
    }
    return entry->kind == NB_FILESYSTEM_ENTRY_DIRECTORY ||
           (entry->kind == NB_FILESYSTEM_ENTRY_REGULAR &&
            (browser->extension[0] == '\0' ||
             nb_filesystem_name_has_extension(entry->name,
                                              browser->extension)));
}

void nb_file_browser_init(struct nb_file_browser *browser,
                          const char *extension)
{
    if (browser == NULL) {
        return;
    }
    (void)memset(browser, 0, sizeof(*browser));
    nb_filesystem_directory_init(&browser->directory);
    browser->selected = SIZE_MAX;
    if (extension != NULL && strlen(extension) < sizeof(browser->extension)) {
        (void)memcpy(browser->extension, extension, strlen(extension) + 1);
    }
}

void nb_file_browser_destroy(struct nb_file_browser *browser)
{
    if (browser == NULL) {
        return;
    }
    nb_filesystem_directory_destroy(&browser->directory);
    (void)memset(browser, 0, sizeof(*browser));
    browser->selected = SIZE_MAX;
}

bool nb_file_browser_open(struct nb_file_browser *browser,
                          const char *path,
                          char *error,
                          size_t error_capacity)
{
    if (browser == NULL ||
        !nb_filesystem_directory_load(&browser->directory,
                                      path,
                                      error,
                                      error_capacity)) {
        return false;
    }
    browser->selected = SIZE_MAX;
    browser->first_visible = 0;
    return true;
}

size_t nb_file_browser_visible_count(const struct nb_file_browser *browser)
{
    size_t index;
    size_t count = 0;

    if (browser == NULL) {
        return 0;
    }
    for (index = 0; index < browser->directory.count; ++index) {
        if (entry_visible(browser, &browser->directory.entries[index])) {
            ++count;
        }
    }
    return count;
}

const struct nb_filesystem_entry *nb_file_browser_visible_entry(
    const struct nb_file_browser *browser,
    size_t visible_index,
    size_t *directory_index)
{
    size_t index;
    size_t visible = 0;

    if (browser == NULL) {
        return NULL;
    }
    for (index = 0; index < browser->directory.count; ++index) {
        if (!entry_visible(browser, &browser->directory.entries[index])) {
            continue;
        }
        if (visible++ == visible_index) {
            if (directory_index != NULL) {
                *directory_index = index;
            }
            return &browser->directory.entries[index];
        }
    }
    return NULL;
}

bool nb_file_browser_select(struct nb_file_browser *browser,
                            size_t visible_index)
{
    if (browser == NULL ||
        nb_file_browser_visible_entry(browser, visible_index, NULL) == NULL) {
        return false;
    }
    browser->selected = visible_index;
    return true;
}

const struct nb_filesystem_entry *nb_file_browser_selected(
    const struct nb_file_browser *browser,
    size_t *directory_index)
{
    return browser == NULL || browser->selected == SIZE_MAX
               ? NULL
               : nb_file_browser_visible_entry(browser,
                                               browser->selected,
                                               directory_index);
}

bool nb_file_browser_selected_path(const struct nb_file_browser *browser,
                                   char *path,
                                   size_t path_capacity)
{
    size_t directory_index;

    return nb_file_browser_selected(browser, &directory_index) != NULL &&
           nb_filesystem_entry_path(&browser->directory,
                                    directory_index,
                                    path,
                                    path_capacity);
}

bool nb_file_browser_enter_selected(struct nb_file_browser *browser,
                                    char *error,
                                    size_t error_capacity)
{
    const struct nb_filesystem_entry *entry;
    char path[NB_FILESYSTEM_PATH_CAPACITY];

    entry = nb_file_browser_selected(browser, NULL);
    return entry != NULL && entry->kind == NB_FILESYSTEM_ENTRY_DIRECTORY &&
           nb_file_browser_selected_path(browser, path, sizeof(path)) &&
           nb_file_browser_open(browser, path, error, error_capacity);
}

bool nb_file_browser_parent(struct nb_file_browser *browser,
                            char *error,
                            size_t error_capacity)
{
    char parent[NB_FILESYSTEM_PATH_CAPACITY];

    return browser != NULL &&
           nb_filesystem_parent_path(browser->directory.path,
                                     parent,
                                     sizeof(parent)) &&
           nb_file_browser_open(browser, parent, error, error_capacity);
}

void nb_file_browser_scroll(struct nb_file_browser *browser,
                            int rows,
                            size_t page_rows)
{
    const size_t count = nb_file_browser_visible_count(browser);
    const size_t maximum = count > page_rows ? count - page_rows : 0;
    size_t next;

    if (browser == NULL || rows == 0) {
        return;
    }
    if (rows < 0) {
        const size_t magnitude = (size_t)(-(int64_t)rows);
        next = magnitude > browser->first_visible
                   ? 0
                   : browser->first_visible - magnitude;
    } else {
        const size_t magnitude = (size_t)rows;
        next = magnitude > maximum -
                             (browser->first_visible > maximum
                                  ? maximum
                                  : browser->first_visible)
                   ? maximum
                   : browser->first_visible + magnitude;
    }
    browser->first_visible = next > maximum ? maximum : next;
}
