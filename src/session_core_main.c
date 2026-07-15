#include "session_core.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_descriptor(const char *text, int *descriptor)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 ||
        value > INT_MAX) {
        return false;
    }
    *descriptor = (int)value;
    return true;
}

int main(int argc, char *argv[])
{
    int descriptor;

    if (argc != 5 || strcmp(argv[1], "--ipc-fd") != 0 ||
        strcmp(argv[3], "--launch") != 0 ||
        !parse_descriptor(argv[2], &descriptor) ||
        descriptor != NB_SESSION_CORE_PROTOCOL_DESCRIPTOR ||
        argv[4][0] != '/') {
        fprintf(stderr, "%s: this is an internal NixBench session process\n",
                argv[0]);
        return 2;
    }
    return nb_session_core_run(descriptor, argv[4]);
}
