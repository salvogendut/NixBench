#include "wsdisplay_session.h"

#include <stdio.h>

static void print_usage(const char *program)
{
    printf("Usage:\n"
           "  %s --help\n"
           "  sudo %s --preflight\n"
           "  sudo %s --recover\n"
           "  sudo %s --acknowledge-console-takeover [--core ABSOLUTE]\n"
           "       [--require-supervisor-sigterm]\n\n",
           program,
           program,
           program,
           program);
    puts("Starts a privilege-separated standalone NixBench session on the "
         "active NetBSD wsdisplay console.");
    puts("\nActions:");
    puts("  --preflight                    inspect the fixed console without takeover");
    puts("  --recover                      restore /var/run/nixbench-wsdisplay-session.state");
    puts("  --acknowledge-console-takeover start the supervised session");
    puts("  --require-supervisor-sigterm   require caught SIGTERM and verified recovery");
    puts("  --help                         show this help without device access");
    puts("\nThe default core is the nixbench-session-core sibling of this executable.");
}

int main(int argc, char *argv[])
{
    struct nb_wsdisplay_session_options options;
    char error[NB_WSDISPLAY_SESSION_ERROR_CAPACITY];

    if (!nb_wsdisplay_session_parse_options(argc, argv, &options, error)) {
        fprintf(stderr, "%s: %s\n", argv[0], error);
        fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
        return 2;
    }
    if (options.action == NB_WSDISPLAY_SESSION_ACTION_HELP) {
        print_usage(argv[0]);
        return 0;
    }
    return nb_wsdisplay_session_run(&options);
}
