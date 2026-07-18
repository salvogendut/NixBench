#ifndef NIXBENCH_FILESYSTEM_H
#define NIXBENCH_FILESYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_FILESYSTEM_PATH_CAPACITY = 1024,
    NB_FILESYSTEM_NAME_CAPACITY = 256,
    NB_FILESYSTEM_ENTRY_LIMIT = 4096
};

enum nb_filesystem_entry_kind {
    NB_FILESYSTEM_ENTRY_DIRECTORY = 0,
    NB_FILESYSTEM_ENTRY_REGULAR,
    NB_FILESYSTEM_ENTRY_OTHER
};

struct nb_filesystem_entry {
    char name[NB_FILESYSTEM_NAME_CAPACITY];
    enum nb_filesystem_entry_kind kind;
    uint64_t size;
    bool hidden;
};

struct nb_filesystem_directory {
    char path[NB_FILESYSTEM_PATH_CAPACITY];
    struct nb_filesystem_entry *entries;
    size_t count;
    bool truncated;
};

void nb_filesystem_directory_init(struct nb_filesystem_directory *directory);
void nb_filesystem_directory_destroy(struct nb_filesystem_directory *directory);

/*
 * Resolve and enumerate an absolute directory. A failed load leaves the
 * previously loaded directory intact. Entries are sorted directories first,
 * then case-insensitively by name. Dot and dot-dot are never returned.
 */
bool nb_filesystem_directory_load(struct nb_filesystem_directory *directory,
                                  const char *path,
                                  char *error,
                                  size_t error_capacity);

bool nb_filesystem_entry_path(const struct nb_filesystem_directory *directory,
                              size_t index,
                              char *path,
                              size_t path_capacity);
bool nb_filesystem_parent_path(const char *path,
                               char *parent,
                               size_t parent_capacity);
bool nb_filesystem_name_has_extension(const char *name,
                                      const char *extension);

#endif
