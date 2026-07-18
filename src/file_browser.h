#ifndef NIXBENCH_FILE_BROWSER_H
#define NIXBENCH_FILE_BROWSER_H

#include <stdbool.h>
#include <stddef.h>

#include "filesystem.h"

struct nb_file_browser {
    struct nb_filesystem_directory directory;
    char extension[16];
    size_t selected;
    size_t first_visible;
    bool show_hidden;
};

void nb_file_browser_init(struct nb_file_browser *browser,
                          const char *extension);
void nb_file_browser_destroy(struct nb_file_browser *browser);
bool nb_file_browser_open(struct nb_file_browser *browser,
                          const char *path,
                          char *error,
                          size_t error_capacity);

size_t nb_file_browser_visible_count(const struct nb_file_browser *browser);
const struct nb_filesystem_entry *nb_file_browser_visible_entry(
    const struct nb_file_browser *browser,
    size_t visible_index,
    size_t *directory_index);
bool nb_file_browser_select(struct nb_file_browser *browser,
                            size_t visible_index);
const struct nb_filesystem_entry *nb_file_browser_selected(
    const struct nb_file_browser *browser,
    size_t *directory_index);
bool nb_file_browser_selected_path(const struct nb_file_browser *browser,
                                   char *path,
                                   size_t path_capacity);
bool nb_file_browser_enter_selected(struct nb_file_browser *browser,
                                    char *error,
                                    size_t error_capacity);
bool nb_file_browser_parent(struct nb_file_browser *browser,
                            char *error,
                            size_t error_capacity);
void nb_file_browser_scroll(struct nb_file_browser *browser,
                            int rows,
                            size_t page_rows);

#endif
