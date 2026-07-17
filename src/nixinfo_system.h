#ifndef NIXBENCH_NIXINFO_SYSTEM_H
#define NIXBENCH_NIXINFO_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_NIXINFO_SYSTEM_TEXT_CAPACITY = 256,
    NB_NIXINFO_LOAD_AVERAGE_COUNT = 3,
    NB_NIXINFO_CPU_STATE_CAPACITY = 8
};

enum nb_nixinfo_usage_availability {
    NB_NIXINFO_USAGE_HAS_CPU = 1u << 0,
    NB_NIXINFO_USAGE_HAS_MEMORY = 1u << 1
};

struct nb_nixinfo_usage_sample {
    uint32_t available;
    unsigned int cpu_percent;
    unsigned int memory_percent;
};

struct nb_nixinfo_usage_sampler {
    uint64_t previous_cpu[NB_NIXINFO_CPU_STATE_CAPACITY];
    size_t cpu_state_count;
    bool previous_cpu_valid;
};

enum nb_nixinfo_system_availability {
    NB_NIXINFO_SYSTEM_HAS_HOSTNAME = 1u << 0,
    NB_NIXINFO_SYSTEM_HAS_SYSTEM_NAME = 1u << 1,
    NB_NIXINFO_SYSTEM_HAS_RELEASE = 1u << 2,
    NB_NIXINFO_SYSTEM_HAS_VERSION = 1u << 3,
    NB_NIXINFO_SYSTEM_HAS_ARCHITECTURE = 1u << 4,
    NB_NIXINFO_SYSTEM_HAS_CPU_MODEL = 1u << 5,
    NB_NIXINFO_SYSTEM_HAS_CPU_COUNT = 1u << 6,
    NB_NIXINFO_SYSTEM_HAS_PHYSICAL_MEMORY = 1u << 7,
    NB_NIXINFO_SYSTEM_HAS_UPTIME = 1u << 8,
    NB_NIXINFO_SYSTEM_HAS_LOAD_AVERAGES = 1u << 9,
    NB_NIXINFO_SYSTEM_HAS_ROOT_FILESYSTEM = 1u << 10
};

struct nb_nixinfo_system_snapshot {
    uint32_t available;
    char hostname[NB_NIXINFO_SYSTEM_TEXT_CAPACITY];
    char system_name[NB_NIXINFO_SYSTEM_TEXT_CAPACITY];
    char release[NB_NIXINFO_SYSTEM_TEXT_CAPACITY];
    char version[NB_NIXINFO_SYSTEM_TEXT_CAPACITY];
    char architecture[NB_NIXINFO_SYSTEM_TEXT_CAPACITY];
    char cpu_model[NB_NIXINFO_SYSTEM_TEXT_CAPACITY];
    unsigned int online_cpu_count;
    uint64_t physical_memory_bytes;
    uint64_t uptime_seconds;
    double load_averages[NB_NIXINFO_LOAD_AVERAGE_COUNT];
    uint64_t root_total_bytes;
    uint64_t root_available_bytes;
};

/*
 * Collection is best-effort. The output is always initialized, and each
 * successful field is identified by its corresponding availability flag.
 */
void nb_nixinfo_system_collect(struct nb_nixinfo_system_snapshot *snapshot);

void nb_nixinfo_usage_sampler_init(
    struct nb_nixinfo_usage_sampler *sampler);
bool nb_nixinfo_system_sample_usage(
    struct nb_nixinfo_usage_sampler *sampler,
    struct nb_nixinfo_usage_sample *sample);

/* Deterministic calculation seams used by the native sampler and tests. */
bool nb_nixinfo_cpu_usage_percent(const uint64_t *previous,
                                  const uint64_t *current,
                                  size_t state_count,
                                  size_t idle_state,
                                  unsigned int *percent);
bool nb_nixinfo_memory_usage_percent(uint64_t total_pages,
                                     uint64_t free_pages,
                                     unsigned int *percent);

/* Collapse ASCII whitespace, trim it at both ends, and always NUL-terminate. */
void nb_nixinfo_sanitize_text(char *destination,
                              size_t capacity,
                              const char *source);

/* The formatting functions return false and an empty string if space is low. */
bool nb_nixinfo_format_bytes(char *destination,
                             size_t capacity,
                             uint64_t bytes);
bool nb_nixinfo_format_duration(char *destination,
                                size_t capacity,
                                uint64_t seconds);

#endif
