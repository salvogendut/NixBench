#include "wsdisplay_smoke.h"
#include "wsdisplay_smoke_runner.h"

#include <stdio.h>

static void print_usage(const char *program_name)
{
    printf("Usage:\n"
           "  %s --preflight-only [PATH OPTIONS]\n"
           "  %s --recover\n"
           "  %s --acknowledge-console-takeover "
           "--acknowledge-no-crash-watchdog [RUN OPTIONS]\n\n",
           program_name,
           program_name,
           program_name);
    puts("A supervised, duration-bounded NetBSD wsdisplay presentation test.\n");
    puts("Actions:");
    puts("  --preflight-only                   inspect state without takeover");
    puts("  --recover                          restore the root-owned saved state");
    puts("  --help                             show this help without device access");
    puts("\nRequired together for a takeover run:");
    puts("  --acknowledge-console-takeover");
    puts("  --acknowledge-no-crash-watchdog   parent failure needs --recover");
    puts("\nRun options:");
    puts("  --desktop-preview                  render NixBench shell content");
    puts("  --interactive-preview              add wscons cursor, drag, and Escape");
    puts("  --duration-ms N                    250..30000 (default 3000)");
    puts("  The default content is the framebuffer diagnostic pattern.");
    puts("\nPath options:");
    puts("  --status-device PATH               default /dev/ttyEstat");
    puts("  --screen-prefix PATH               default /dev/ttyE");
}

int main(int argc, char *argv[])
{
    struct nb_wsdisplay_smoke_options options;
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY];

    if (!nb_wsdisplay_smoke_parse_options(argc,
                                          argv,
                                          &options,
                                          error)) {
        fprintf(stderr, "%s: %s\n", argv[0], error);
        fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
        return 2;
    }
    if (options.action == NB_WSDISPLAY_SMOKE_ACTION_HELP) {
        print_usage(argv[0]);
        return 0;
    }
    return nb_wsdisplay_smoke_run(&options);
}
