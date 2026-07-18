#define _XOPEN_SOURCE 700

#include "filesystem.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static void set_error(char *error,
                      size_t error_capacity,
                      const char *format,
                      ...)
{
    va_list arguments;

    if (error == NULL || error_capacity == 0) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error, error_capacity, format, arguments);
    va_end(arguments);
}

void nb_filesystem_directory_init(struct nb_filesystem_directory *directory)
{
    if (directory != NULL) {
        (void)memset(directory, 0, sizeof(*directory));
    }
}

void nb_filesystem_directory_destroy(struct nb_filesystem_directory *directory)
{
    if (directory == NULL) {
        return;
    }
    free(directory->entries);
    nb_filesystem_directory_init(directory);
}

static int compare_entries(const void *first_pointer,
                           const void *second_pointer)
{
    const struct nb_filesystem_entry *first = first_pointer;
    const struct nb_filesystem_entry *second = second_pointer;
    int comparison;

    if (first->kind == NB_FILESYSTEM_ENTRY_DIRECTORY &&
        second->kind != NB_FILESYSTEM_ENTRY_DIRECTORY) {
        return -1;
    }
    if (second->kind == NB_FILESYSTEM_ENTRY_DIRECTORY &&
        first->kind != NB_FILESYSTEM_ENTRY_DIRECTORY) {
        return 1;
    }
    comparison = strcasecmp(first->name, second->name);
    return comparison != 0 ? comparison : strcmp(first->name, second->name);
}

static bool append_entry(struct nb_filesystem_directory *directory,
                         const struct nb_filesystem_entry *entry,
                         size_t *capacity)
{
    struct nb_filesystem_entry *grown;
    size_t next_capacity;

    if (directory->count >= NB_FILESYSTEM_ENTRY_LIMIT) {
        directory->truncated = true;
        return true;
    }
    if (directory->count == *capacity) {
        next_capacity = *capacity == 0 ? 32 : *capacity * 2;
        if (next_capacity > NB_FILESYSTEM_ENTRY_LIMIT) {
            next_capacity = NB_FILESYSTEM_ENTRY_LIMIT;
        }
        grown = realloc(directory->entries,
                        next_capacity * sizeof(*directory->entries));
        if (grown == NULL) {
            return false;
        }
        directory->entries = grown;
        *capacity = next_capacity;
    }
    directory->entries[directory->count++] = *entry;
    return true;
}

static bool joined_path(const char *directory,
                        const char *name,
                        char *path,
                        size_t path_capacity)
{
    int length;

    length = strcmp(directory, "/") == 0
                 ? snprintf(path, path_capacity, "/%s", name)
                 : snprintf(path, path_capacity, "%s/%s", directory, name);
    return length >= 0 && (size_t)length < path_capacity;
}

bool nb_filesystem_directory_load(struct nb_filesystem_directory *directory,
                                  const char *path,
                                  char *error,
                                  size_t error_capacity)
{
    struct nb_filesystem_directory loaded;
    char *resolved = NULL;
    DIR *stream = NULL;
    struct dirent *entry;
    size_t capacity = 0;
    bool ok = false;

    if (directory == NULL || path == NULL || path[0] != '/') {
        set_error(error, error_capacity, "directory path must be absolute");
        return false;
    }
    nb_filesystem_directory_init(&loaded);
    resolved = realpath(path, NULL);
    if (resolved == NULL) {
        set_error(error,
                  error_capacity,
                  "could not resolve '%s': %s",
                  path,
                  strerror(errno));
        goto cleanup;
    }
    if (strlen(resolved) >= sizeof(loaded.path)) {
        set_error(error, error_capacity, "resolved directory path is too long");
        goto cleanup;
    }
    (void)memcpy(loaded.path, resolved, strlen(resolved) + 1);
    stream = opendir(resolved);
    if (stream == NULL) {
        set_error(error,
                  error_capacity,
                  "could not open '%s': %s",
                  resolved,
                  strerror(errno));
        goto cleanup;
    }
    errno = 0;
    while ((entry = readdir(stream)) != NULL) {
        struct nb_filesystem_entry item;
        struct stat status;
        char item_path[NB_FILESYSTEM_PATH_CAPACITY];
        const size_t name_length = strlen(entry->d_name);

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (name_length >= sizeof(item.name) ||
            !joined_path(loaded.path,
                         entry->d_name,
                         item_path,
                         sizeof(item_path))) {
            loaded.truncated = true;
            continue;
        }
        (void)memset(&item, 0, sizeof(item));
        (void)memcpy(item.name, entry->d_name, name_length + 1);
        item.hidden = entry->d_name[0] == '.';
        if (stat(item_path, &status) == 0) {
            if (S_ISDIR(status.st_mode)) {
                item.kind = NB_FILESYSTEM_ENTRY_DIRECTORY;
            } else if (S_ISREG(status.st_mode)) {
                item.kind = NB_FILESYSTEM_ENTRY_REGULAR;
                item.size = status.st_size > 0 ? (uint64_t)status.st_size : 0;
            } else {
                item.kind = NB_FILESYSTEM_ENTRY_OTHER;
            }
        } else {
            item.kind = NB_FILESYSTEM_ENTRY_OTHER;
        }
        if (!append_entry(&loaded, &item, &capacity)) {
            set_error(error, error_capacity, "out of memory listing directory");
            goto cleanup;
        }
        errno = 0;
    }
    if (errno != 0) {
        set_error(error,
                  error_capacity,
                  "could not read '%s': %s",
                  resolved,
                  strerror(errno));
        goto cleanup;
    }
    qsort(loaded.entries,
          loaded.count,
          sizeof(*loaded.entries),
          compare_entries);
    nb_filesystem_directory_destroy(directory);
    *directory = loaded;
    nb_filesystem_directory_init(&loaded);
    ok = true;

cleanup:
    if (stream != NULL) {
        (void)closedir(stream);
    }
    free(resolved);
    nb_filesystem_directory_destroy(&loaded);
    return ok;
}

bool nb_filesystem_entry_path(const struct nb_filesystem_directory *directory,
                              size_t index,
                              char *path,
                              size_t path_capacity)
{
    return directory != NULL && directory->path[0] == '/' &&
           index < directory->count && path != NULL && path_capacity > 0 &&
           joined_path(directory->path,
                       directory->entries[index].name,
                       path,
                       path_capacity);
}

bool nb_filesystem_parent_path(const char *path,
                               char *parent,
                               size_t parent_capacity)
{
    size_t length;
    char *slash;

    if (path == NULL || path[0] != '/' || parent == NULL ||
        parent_capacity == 0) {
        return false;
    }
    length = strlen(path);
    while (length > 1 && path[length - 1] == '/') {
        --length;
    }
    if (length >= parent_capacity) {
        return false;
    }
    (void)memcpy(parent, path, length);
    parent[length] = '\0';
    slash = strrchr(parent, '/');
    if (slash == parent) {
        parent[1] = '\0';
    } else if (slash != NULL) {
        *slash = '\0';
    } else {
        return false;
    }
    return true;
}

bool nb_filesystem_name_has_extension(const char *name,
                                      const char *extension)
{
    size_t name_length;
    size_t extension_length;

    if (name == NULL || extension == NULL || extension[0] != '.') {
        return false;
    }
    name_length = strlen(name);
    extension_length = strlen(extension);
    return name_length > extension_length &&
           strcasecmp(name + name_length - extension_length, extension) == 0;
}
