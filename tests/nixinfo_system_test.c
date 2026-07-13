#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nixinfo_system.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static bool contains_ascii_whitespace_run(const char *text)
{
    size_t index;

    for (index = 0; text[index] != '\0'; ++index) {
        const char character = text[index];

        if (character == '\t' || character == '\n' || character == '\r' ||
            character == '\f' || character == '\v') {
            return true;
        }
        if (character == ' ' && text[index + 1] == ' ') {
            return true;
        }
    }
    return text[0] == ' ' || (index > 0 && text[index - 1] == ' ');
}

static void test_sanitize_text(void)
{
    char text[32];
    char short_text[6];
    char in_place[] = "  one\t two \n";

    nb_nixinfo_sanitize_text(text,
                             sizeof(text),
                             " \tNetBSD\n  10.1\r\nGENERIC  ");
    CHECK(strcmp(text, "NetBSD 10.1 GENERIC") == 0);

    nb_nixinfo_sanitize_text(short_text, sizeof(short_text), "abc def");
    CHECK(strcmp(short_text, "abc d") == 0);

    nb_nixinfo_sanitize_text(in_place, sizeof(in_place), in_place);
    CHECK(strcmp(in_place, "one two") == 0);

    nb_nixinfo_sanitize_text(text, sizeof(text), NULL);
    CHECK(text[0] == '\0');
    nb_nixinfo_sanitize_text(NULL, 0, "ignored");
}

static void test_format_bytes(void)
{
    char text[32];
    char too_small[4] = "old";

    CHECK(nb_nixinfo_format_bytes(text, sizeof(text), UINT64_C(0)));
    CHECK(strcmp(text, "0 B") == 0);
    CHECK(nb_nixinfo_format_bytes(text, sizeof(text), UINT64_C(1023)));
    CHECK(strcmp(text, "1023 B") == 0);
    CHECK(nb_nixinfo_format_bytes(text, sizeof(text), UINT64_C(1024)));
    CHECK(strcmp(text, "1.0 KiB") == 0);
    CHECK(nb_nixinfo_format_bytes(text, sizeof(text), UINT64_C(1536)));
    CHECK(strcmp(text, "1.5 KiB") == 0);
    CHECK(nb_nixinfo_format_bytes(text,
                                  sizeof(text),
                                  UINT64_C(1048575)));
    CHECK(strcmp(text, "1.0 MiB") == 0);
    CHECK(nb_nixinfo_format_bytes(text, sizeof(text), UINT64_MAX));
    CHECK(strcmp(text, "16.0 EiB") == 0);

    CHECK(!nb_nixinfo_format_bytes(too_small,
                                   sizeof(too_small),
                                   UINT64_C(1024)));
    CHECK(too_small[0] == '\0');
    CHECK(!nb_nixinfo_format_bytes(NULL, 0, UINT64_C(1)));
}

static void test_format_duration(void)
{
    char text[64];
    char too_small[5] = "old";

    CHECK(nb_nixinfo_format_duration(text, sizeof(text), UINT64_C(0)));
    CHECK(strcmp(text, "0s") == 0);
    CHECK(nb_nixinfo_format_duration(text, sizeof(text), UINT64_C(59)));
    CHECK(strcmp(text, "59s") == 0);
    CHECK(nb_nixinfo_format_duration(text, sizeof(text), UINT64_C(754)));
    CHECK(strcmp(text, "12m 34s") == 0);
    CHECK(nb_nixinfo_format_duration(text, sizeof(text), UINT64_C(7384)));
    CHECK(strcmp(text, "2h 03m") == 0);
    CHECK(nb_nixinfo_format_duration(text, sizeof(text), UINT64_C(266580)));
    CHECK(strcmp(text, "3d 02h 03m") == 0);
    CHECK(nb_nixinfo_format_duration(text, sizeof(text), UINT64_MAX));
    CHECK(strcmp(text, "213503982334601d 07h 00m") == 0);

    CHECK(!nb_nixinfo_format_duration(too_small,
                                      sizeof(too_small),
                                      UINT64_C(754)));
    CHECK(too_small[0] == '\0');
    CHECK(!nb_nixinfo_format_duration(NULL, 0, UINT64_C(1)));
}

static void check_available_text(uint32_t available,
                                 uint32_t flag,
                                 const char *text)
{
    if ((available & flag) != 0) {
        CHECK(text[0] != '\0');
        CHECK(!contains_ascii_whitespace_run(text));
    } else {
        CHECK(text[0] == '\0');
    }
}

static void test_native_collection_smoke(void)
{
    struct nb_nixinfo_system_snapshot snapshot;

    memset(&snapshot, 0xa5, sizeof(snapshot));
    nb_nixinfo_system_collect(&snapshot);

    check_available_text(snapshot.available,
                         NB_NIXINFO_SYSTEM_HAS_HOSTNAME,
                         snapshot.hostname);
    check_available_text(snapshot.available,
                         NB_NIXINFO_SYSTEM_HAS_SYSTEM_NAME,
                         snapshot.system_name);
    check_available_text(snapshot.available,
                         NB_NIXINFO_SYSTEM_HAS_RELEASE,
                         snapshot.release);
    check_available_text(snapshot.available,
                         NB_NIXINFO_SYSTEM_HAS_VERSION,
                         snapshot.version);
    check_available_text(snapshot.available,
                         NB_NIXINFO_SYSTEM_HAS_ARCHITECTURE,
                         snapshot.architecture);
    check_available_text(snapshot.available,
                         NB_NIXINFO_SYSTEM_HAS_CPU_MODEL,
                         snapshot.cpu_model);

    CHECK((snapshot.available & NB_NIXINFO_SYSTEM_HAS_SYSTEM_NAME) != 0);
    CHECK((snapshot.available & NB_NIXINFO_SYSTEM_HAS_RELEASE) != 0);
    CHECK((snapshot.available & NB_NIXINFO_SYSTEM_HAS_ARCHITECTURE) != 0);

    if ((snapshot.available & NB_NIXINFO_SYSTEM_HAS_CPU_COUNT) != 0) {
        CHECK(snapshot.online_cpu_count > 0);
    }
    if ((snapshot.available & NB_NIXINFO_SYSTEM_HAS_PHYSICAL_MEMORY) != 0) {
        CHECK(snapshot.physical_memory_bytes > 0);
    }
    if ((snapshot.available & NB_NIXINFO_SYSTEM_HAS_LOAD_AVERAGES) != 0) {
        size_t index;

        for (index = 0; index < NB_NIXINFO_LOAD_AVERAGE_COUNT; ++index) {
            CHECK(snapshot.load_averages[index] >= 0.0);
        }
    }
    if ((snapshot.available & NB_NIXINFO_SYSTEM_HAS_ROOT_FILESYSTEM) != 0) {
        CHECK(snapshot.root_available_bytes <= snapshot.root_total_bytes);
    }
}

int main(void)
{
    test_sanitize_text();
    test_format_bytes();
    test_format_duration();
    test_native_collection_smoke();

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    puts("nixinfo system tests passed");
    return 0;
}
