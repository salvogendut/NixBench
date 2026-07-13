#include "nixinfo_system.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>

#if defined(__NetBSD__)
#include <float.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <time.h>
#endif

static bool is_ascii_whitespace(char character)
{
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\r' || character == '\f' || character == '\v';
}

void nb_nixinfo_sanitize_text(char *destination,
                              size_t capacity,
                              const char *source)
{
    size_t source_index = 0;
    size_t destination_index = 0;
    bool pending_space = false;

    if (destination == NULL || capacity == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    while (source[source_index] != '\0') {
        const char character = source[source_index++];

        if (is_ascii_whitespace(character)) {
            pending_space = destination_index > 0;
            continue;
        }

        if (pending_space) {
            /* Preserve room for the pending space, this byte, and the NUL. */
            if (destination_index + 2 >= capacity) {
                break;
            }
            destination[destination_index++] = ' ';
            pending_space = false;
        } else if (destination_index + 1 >= capacity) {
            break;
        }
        destination[destination_index++] = character;
    }

    destination[destination_index] = '\0';
}

static void copy_uname_field(char *destination,
                             const char *source,
                             uint32_t flag,
                             struct nb_nixinfo_system_snapshot *snapshot)
{
    nb_nixinfo_sanitize_text(destination,
                             NB_NIXINFO_SYSTEM_TEXT_CAPACITY,
                             source);
    if (destination[0] != '\0') {
        snapshot->available |= flag;
    }
}

static void collect_uname(struct nb_nixinfo_system_snapshot *snapshot)
{
    struct utsname identity;

    if (uname(&identity) != 0) {
        return;
    }

    copy_uname_field(snapshot->hostname,
                     identity.nodename,
                     NB_NIXINFO_SYSTEM_HAS_HOSTNAME,
                     snapshot);
    copy_uname_field(snapshot->system_name,
                     identity.sysname,
                     NB_NIXINFO_SYSTEM_HAS_SYSTEM_NAME,
                     snapshot);
    copy_uname_field(snapshot->release,
                     identity.release,
                     NB_NIXINFO_SYSTEM_HAS_RELEASE,
                     snapshot);
    copy_uname_field(snapshot->version,
                     identity.version,
                     NB_NIXINFO_SYSTEM_HAS_VERSION,
                     snapshot);
    copy_uname_field(snapshot->architecture,
                     identity.machine,
                     NB_NIXINFO_SYSTEM_HAS_ARCHITECTURE,
                     snapshot);
}

static bool multiply_u64(uint64_t left, uint64_t right, uint64_t *product)
{
    if (left != 0 && right > UINT64_MAX / left) {
        return false;
    }
    *product = left * right;
    return true;
}

static void collect_root_filesystem(
    struct nb_nixinfo_system_snapshot *snapshot)
{
    struct statvfs filesystem;
    uint64_t total;
    uint64_t available;
    uint64_t fragment_size;
    uint64_t total_blocks;
    uint64_t available_blocks;

    if (statvfs("/", &filesystem) != 0 || filesystem.f_frsize == 0 ||
        filesystem.f_bavail > filesystem.f_blocks) {
        return;
    }

    fragment_size = (uint64_t)filesystem.f_frsize;
    total_blocks = (uint64_t)filesystem.f_blocks;
    available_blocks = (uint64_t)filesystem.f_bavail;
    if (!multiply_u64(total_blocks, fragment_size, &total) ||
        !multiply_u64(available_blocks, fragment_size, &available)) {
        return;
    }

    snapshot->root_total_bytes = total;
    snapshot->root_available_bytes = available;
    snapshot->available |= NB_NIXINFO_SYSTEM_HAS_ROOT_FILESYSTEM;
}

#if defined(__NetBSD__)
static bool netbsd_sysctl_value(const char *name,
                                void *value,
                                size_t expected_size)
{
    size_t actual_size = expected_size;

    return sysctlbyname(name, value, &actual_size, NULL, 0) == 0 &&
           actual_size == expected_size;
}

static void collect_netbsd_cpu_model(
    struct nb_nixinfo_system_snapshot *snapshot)
{
    char model[NB_NIXINFO_SYSTEM_TEXT_CAPACITY];
    size_t model_size = sizeof(model);

    if (sysctlbyname("hw.model", model, &model_size, NULL, 0) != 0 ||
        model_size == 0) {
        return;
    }
    model[sizeof(model) - 1] = '\0';
    nb_nixinfo_sanitize_text(snapshot->cpu_model,
                             sizeof(snapshot->cpu_model),
                             model);
    if (snapshot->cpu_model[0] != '\0') {
        snapshot->available |= NB_NIXINFO_SYSTEM_HAS_CPU_MODEL;
    }
}

static void collect_netbsd_optional_fields(
    struct nb_nixinfo_system_snapshot *snapshot)
{
    int cpu_count = 0;
    uint64_t physical_memory = 0;
    struct timespec boot_time;
    const time_t current_time = time(NULL);
    double load_averages[NB_NIXINFO_LOAD_AVERAGE_COUNT];
    size_t index;
    bool loads_are_valid = true;

    collect_netbsd_cpu_model(snapshot);

    if (netbsd_sysctl_value("hw.ncpuonline",
                            &cpu_count,
                            sizeof(cpu_count)) &&
        cpu_count > 0) {
        snapshot->online_cpu_count = (unsigned int)cpu_count;
        snapshot->available |= NB_NIXINFO_SYSTEM_HAS_CPU_COUNT;
    }

    if (netbsd_sysctl_value("hw.physmem64",
                            &physical_memory,
                            sizeof(physical_memory)) &&
        physical_memory > 0) {
        snapshot->physical_memory_bytes = physical_memory;
        snapshot->available |= NB_NIXINFO_SYSTEM_HAS_PHYSICAL_MEMORY;
    }

    if (current_time != (time_t)-1 &&
        netbsd_sysctl_value("kern.boottime",
                            &boot_time,
                            sizeof(boot_time)) &&
        boot_time.tv_sec >= 0 && current_time >= boot_time.tv_sec) {
        snapshot->uptime_seconds =
            (uint64_t)(current_time - boot_time.tv_sec);
        snapshot->available |= NB_NIXINFO_SYSTEM_HAS_UPTIME;
    }

    if (getloadavg(load_averages, NB_NIXINFO_LOAD_AVERAGE_COUNT) !=
        NB_NIXINFO_LOAD_AVERAGE_COUNT) {
        return;
    }
    for (index = 0; index < NB_NIXINFO_LOAD_AVERAGE_COUNT; ++index) {
        if (load_averages[index] != load_averages[index] ||
            load_averages[index] < 0.0 ||
            load_averages[index] > DBL_MAX) {
            loads_are_valid = false;
        }
    }
    if (loads_are_valid) {
        memcpy(snapshot->load_averages,
               load_averages,
               sizeof(snapshot->load_averages));
        snapshot->available |= NB_NIXINFO_SYSTEM_HAS_LOAD_AVERAGES;
    }
}
#endif

void nb_nixinfo_system_collect(struct nb_nixinfo_system_snapshot *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    collect_uname(snapshot);
    collect_root_filesystem(snapshot);
#if defined(__NetBSD__)
    collect_netbsd_optional_fields(snapshot);
#endif
}

static bool format_succeeded(char *destination,
                             size_t capacity,
                             int character_count)
{
    if (character_count < 0 || (size_t)character_count >= capacity) {
        destination[0] = '\0';
        return false;
    }
    return true;
}

bool nb_nixinfo_format_bytes(char *destination,
                             size_t capacity,
                             uint64_t bytes)
{
    static const uint64_t divisors[] = {
        UINT64_C(1),
        UINT64_C(1024),
        UINT64_C(1048576),
        UINT64_C(1073741824),
        UINT64_C(1099511627776),
        UINT64_C(1125899906842624),
        UINT64_C(1152921504606846976)
    };
    static const char *const units[] = {
        "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"
    };
    const size_t unit_count = sizeof(divisors) / sizeof(divisors[0]);
    size_t unit = 0;
    uint64_t whole;
    uint64_t tenths;
    int character_count;

    if (destination == NULL || capacity == 0) {
        return false;
    }
    destination[0] = '\0';

    while (unit + 1 < unit_count && bytes >= divisors[unit + 1]) {
        ++unit;
    }

    if (unit == 0) {
        character_count = snprintf(destination,
                                   capacity,
                                   "%" PRIu64 " B",
                                   bytes);
        return format_succeeded(destination, capacity, character_count);
    }

    whole = bytes / divisors[unit];
    tenths = ((bytes % divisors[unit]) * UINT64_C(10) +
              (divisors[unit] / UINT64_C(2))) /
             divisors[unit];
    if (tenths == 10) {
        ++whole;
        tenths = 0;
        if (whole == 1024 && unit + 1 < unit_count) {
            ++unit;
            whole = 1;
        }
    }

    character_count = snprintf(destination,
                               capacity,
                               "%" PRIu64 ".%" PRIu64 " %s",
                               whole,
                               tenths,
                               units[unit]);
    return format_succeeded(destination, capacity, character_count);
}

bool nb_nixinfo_format_duration(char *destination,
                                size_t capacity,
                                uint64_t seconds)
{
    const uint64_t seconds_per_minute = UINT64_C(60);
    const uint64_t seconds_per_hour = UINT64_C(3600);
    const uint64_t seconds_per_day = UINT64_C(86400);
    const uint64_t days = seconds / seconds_per_day;
    const uint64_t hours = (seconds % seconds_per_day) / seconds_per_hour;
    const uint64_t minutes = (seconds % seconds_per_hour) /
                             seconds_per_minute;
    const uint64_t remaining_seconds = seconds % seconds_per_minute;
    int character_count;

    if (destination == NULL || capacity == 0) {
        return false;
    }
    destination[0] = '\0';

    if (days > 0) {
        character_count = snprintf(destination,
                                   capacity,
                                   "%" PRIu64 "d %02" PRIu64
                                   "h %02" PRIu64 "m",
                                   days,
                                   hours,
                                   minutes);
    } else if (hours > 0) {
        character_count = snprintf(destination,
                                   capacity,
                                   "%" PRIu64 "h %02" PRIu64 "m",
                                   hours,
                                   minutes);
    } else if (minutes > 0) {
        character_count = snprintf(destination,
                                   capacity,
                                   "%" PRIu64 "m %02" PRIu64 "s",
                                   minutes,
                                   remaining_seconds);
    } else {
        character_count = snprintf(destination,
                                   capacity,
                                   "%" PRIu64 "s",
                                   remaining_seconds);
    }

    return format_succeeded(destination, capacity, character_count);
}
