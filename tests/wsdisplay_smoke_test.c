#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "wsdisplay_smoke.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static bool parse(int argc,
                  char *argv[],
                  struct nb_wsdisplay_smoke_options *options,
                  char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY])
{
    memset(error, 0, NB_WSDISPLAY_SMOKE_ERROR_CAPACITY);
    return nb_wsdisplay_smoke_parse_options(argc, argv, options, error);
}

static void test_parser_safe_actions(void)
{
    struct nb_wsdisplay_smoke_options options;
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY];
    char *help[] = {"smoke", "--help"};
    char *preflight[] = {"smoke", "--preflight-only"};
    char *recover[] = {"smoke", "--recover"};
    char *missing_ack[] = {"smoke"};

    CHECK(parse(2, help, &options, error));
    CHECK(options.action == NB_WSDISPLAY_SMOKE_ACTION_HELP);
    CHECK(parse(2, preflight, &options, error));
    CHECK(options.action == NB_WSDISPLAY_SMOKE_ACTION_PREFLIGHT);
    CHECK(parse(2, recover, &options, error));
    CHECK(options.action == NB_WSDISPLAY_SMOKE_ACTION_RECOVER);
    CHECK(!parse(1, missing_ack, &options, error));
    CHECK(error[0] != '\0');
}

static void test_parser_takeover(void)
{
    struct nb_wsdisplay_smoke_options options;
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY];
    char *diagnostic[] = {
        "smoke",
        "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog"
    };
    char *run[] = {
        "smoke",
        "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog",
        "--desktop-preview",
        "--duration-ms",
        "1250",
        "--status-device",
        "/dev/customstat",
        "--screen-prefix",
        "/dev/custom"
    };
    char *interactive[] = {
        "smoke",
        "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog",
        "--interactive-preview"
    };
    char *runtime[] = {
        "smoke",
        "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog",
        "--runtime-preview"
    };

    CHECK(parse((int)(sizeof(diagnostic) / sizeof(diagnostic[0])),
                diagnostic,
                &options,
                error));
    CHECK(options.content == NB_WSDISPLAY_SMOKE_CONTENT_DIAGNOSTIC);
    CHECK(options.wscons_pointer_sensitivity_percent ==
          NB_WSDISPLAY_SMOKE_DEFAULT_POINTER_SENSITIVITY_PERCENT);
    CHECK(!options.wscons_input_stats);
    CHECK(parse((int)(sizeof(run) / sizeof(run[0])),
                run,
                &options,
                error));
    CHECK(options.action == NB_WSDISPLAY_SMOKE_ACTION_RUN);
    CHECK(strcmp(options.program_path, "smoke") == 0);
    CHECK(options.acknowledge_console_takeover);
    CHECK(options.acknowledge_no_crash_watchdog);
    CHECK(options.content == NB_WSDISPLAY_SMOKE_CONTENT_DESKTOP_PREVIEW);
    CHECK(options.duration_ms == 1250);
    CHECK(strcmp(options.status_device_path, "/dev/customstat") == 0);
    CHECK(strcmp(options.screen_device_prefix, "/dev/custom") == 0);
    CHECK(parse((int)(sizeof(interactive) / sizeof(interactive[0])),
                interactive,
                &options,
                error));
    CHECK(options.content == NB_WSDISPLAY_SMOKE_CONTENT_INTERACTIVE_PREVIEW);
    CHECK(parse((int)(sizeof(runtime) / sizeof(runtime[0])),
                runtime,
                &options,
                error));
    CHECK(options.content == NB_WSDISPLAY_SMOKE_CONTENT_RUNTIME_PREVIEW);
}

static void test_parser_wscons_tuning(void)
{
    struct nb_wsdisplay_smoke_options options;
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY];
    char *interactive[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--wscons-pointer-sensitivity-percent", "150",
        "--wscons-input-stats"
    };
    char *minimum[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--runtime-preview",
        "--wscons-pointer-sensitivity-percent", "25"
    };
    char *maximum[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--runtime-preview",
        "--wscons-pointer-sensitivity-percent", "400"
    };
    char *low[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--wscons-pointer-sensitivity-percent", "24"
    };
    char *high[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--wscons-pointer-sensitivity-percent", "401"
    };
    char *plus[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--wscons-pointer-sensitivity-percent", "+100"
    };
    char *minus[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--wscons-pointer-sensitivity-percent", "-100"
    };
    char *junk[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--wscons-pointer-sensitivity-percent", "100x"
    };
    char *leading_space[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--wscons-pointer-sensitivity-percent", " 100"
    };
    char *missing[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--wscons-pointer-sensitivity-percent"
    };
    char *duplicate_sensitivity[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--runtime-preview",
        "--wscons-pointer-sensitivity-percent", "100",
        "--wscons-pointer-sensitivity-percent", "200"
    };
    char *duplicate_stats[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--runtime-preview",
        "--wscons-input-stats", "--wscons-input-stats"
    };
    char *diagnostic_sensitivity[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog",
        "--wscons-pointer-sensitivity-percent", "100"
    };
    char *diagnostic_stats[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--wscons-input-stats"
    };
    char *desktop_stats[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--desktop-preview",
        "--wscons-input-stats"
    };
    char *preflight_sensitivity[] = {
        "smoke", "--preflight-only",
        "--wscons-pointer-sensitivity-percent", "100"
    };
    char *recover_stats[] = {
        "smoke", "--recover", "--wscons-input-stats"
    };
    char *help_stats[] = {
        "smoke", "--help", "--wscons-input-stats"
    };

    CHECK(parse(7, interactive, &options, error));
    CHECK(options.wscons_pointer_sensitivity_percent == 150);
    CHECK(options.wscons_input_stats);
    CHECK(parse(6, minimum, &options, error));
    CHECK(options.wscons_pointer_sensitivity_percent ==
          NB_WSDISPLAY_SMOKE_MIN_POINTER_SENSITIVITY_PERCENT);
    CHECK(!options.wscons_input_stats);
    CHECK(parse(6, maximum, &options, error));
    CHECK(options.wscons_pointer_sensitivity_percent ==
          NB_WSDISPLAY_SMOKE_MAX_POINTER_SENSITIVITY_PERCENT);

    CHECK(!parse(6, low, &options, error));
    CHECK(!parse(6, high, &options, error));
    CHECK(!parse(6, plus, &options, error));
    CHECK(!parse(6, minus, &options, error));
    CHECK(!parse(6, junk, &options, error));
    CHECK(!parse(6, leading_space, &options, error));
    CHECK(!parse(5, missing, &options, error));
    CHECK(!parse(8, duplicate_sensitivity, &options, error));
    CHECK(!parse(6, duplicate_stats, &options, error));
    CHECK(!parse(5, diagnostic_sensitivity, &options, error));
    CHECK(!parse(4, diagnostic_stats, &options, error));
    CHECK(!parse(5, desktop_stats, &options, error));
    CHECK(!parse(4, preflight_sensitivity, &options, error));
    CHECK(!parse(3, recover_stats, &options, error));
    CHECK(!parse(3, help_stats, &options, error));
}

static void test_parser_rejections(void)
{
    struct nb_wsdisplay_smoke_options options;
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY];
    char *low[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--duration-ms", "249"
    };
    char *high[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--duration-ms", "30001"
    };
    char *maximum[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--duration-ms", "30000"
    };
    char *signed_value[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--duration-ms", "+3000"
    };
    char *duplicate[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog"
    };
    char *conflict[] = {
        "smoke", "--preflight-only", "--recover"
    };
    char *unsafe_preflight[] = {
        "smoke", "--preflight-only", "--duration-ms", "1000"
    };
    char *unknown[] = {"smoke", "--definitely-unknown"};
    char *recover_path[] = {
        "smoke", "--recover", "--status-device", "/dev/ignored"
    };
    char *relative_status[] = {
        "smoke", "--preflight-only", "--status-device", "ttyEstat"
    };
    char *relative_prefix[] = {
        "smoke", "--preflight-only", "--screen-prefix", "ttyE"
    };
    char *unsafe_preview[] = {
        "smoke", "--preflight-only", "--desktop-preview"
    };
    char *duplicate_preview[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--desktop-preview",
        "--desktop-preview"
    };
    char *conflicting_preview[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--desktop-preview",
        "--interactive-preview"
    };
    char *unsafe_interactive[] = {
        "smoke", "--preflight-only", "--interactive-preview"
    };
    char *duplicate_runtime[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--runtime-preview",
        "--runtime-preview"
    };
    char *conflicting_runtime[] = {
        "smoke", "--acknowledge-console-takeover",
        "--acknowledge-no-crash-watchdog", "--interactive-preview",
        "--runtime-preview"
    };
    char *unsafe_runtime[] = {
        "smoke", "--preflight-only", "--runtime-preview"
    };

    CHECK(!parse(5, low, &options, error));
    CHECK(!parse(5, high, &options, error));
    CHECK(parse(5, maximum, &options, error));
    CHECK(options.duration_ms == NB_WSDISPLAY_SMOKE_MAX_DURATION_MS);
    CHECK(!parse(5, signed_value, &options, error));
    CHECK(!parse(4, duplicate, &options, error));
    CHECK(!parse(3, conflict, &options, error));
    CHECK(!parse(4, unsafe_preflight, &options, error));
    CHECK(!parse(2, unknown, &options, error));
    CHECK(!parse(4, recover_path, &options, error));
    CHECK(!parse(4, relative_status, &options, error));
    CHECK(!parse(4, relative_prefix, &options, error));
    CHECK(!parse(3, unsafe_preview, &options, error));
    CHECK(!parse(5, duplicate_preview, &options, error));
    CHECK(!parse(5, conflicting_preview, &options, error));
    CHECK(!parse(3, unsafe_interactive, &options, error));
    CHECK(!parse(5, duplicate_runtime, &options, error));
    CHECK(!parse(5, conflicting_runtime, &options, error));
    CHECK(!parse(3, unsafe_runtime, &options, error));
}

static void test_vt_number_translation(void)
{
    int vt_number = -1;

    CHECK(nb_wsdisplay_screen_index_to_vt_number(0, &vt_number));
    CHECK(vt_number == 1);
    CHECK(nb_wsdisplay_screen_index_to_vt_number(1, &vt_number));
    CHECK(vt_number == 2);
    CHECK(nb_wsdisplay_screen_index_to_vt_number(255, &vt_number));
    CHECK(vt_number == 256);
    CHECK(!nb_wsdisplay_screen_index_to_vt_number(-1, &vt_number));
    CHECK(!nb_wsdisplay_screen_index_to_vt_number(INT_MAX, &vt_number));
    CHECK(!nb_wsdisplay_screen_index_to_vt_number(0, NULL));
}

static void test_pattern(void)
{
    struct nb_wsdisplay_smoke_image image;
    struct nb_host_frame frame;
    uint32_t colors[32];
    size_t color_count = 0;
    int y;

    memset(&image, 0xa5, sizeof(image));
    CHECK(!nb_wsdisplay_smoke_image_create(NULL, 80, 60));
    CHECK(!nb_wsdisplay_smoke_image_create(&image, 0, 60));
    CHECK(!nb_wsdisplay_smoke_image_create(&image, 80, -1));
    CHECK(nb_wsdisplay_smoke_image_create(&image, 80, 60));
    CHECK(image.pixels != NULL);
    CHECK(image.stride == 80 * sizeof(uint32_t));
    CHECK(image.pixels[0] == UINT32_C(0x00f0f0f0));
    CHECK(image.pixels[79] == UINT32_C(0x00f0f0f0));
    CHECK(image.pixels[(size_t)59 * 80] == UINT32_C(0x00f0f0f0));

    for (y = 0; y < image.height; ++y) {
        const uint32_t *row = (const uint32_t *)
            ((const unsigned char *)image.pixels +
             (size_t)y * image.stride);
        int x;

        for (x = 0; x < image.width; ++x) {
            size_t index;

            for (index = 0; index < color_count; ++index) {
                if (colors[index] == row[x]) {
                    break;
                }
            }
            if (index == color_count &&
                color_count < sizeof(colors) / sizeof(colors[0])) {
                colors[color_count++] = row[x];
            }
        }
    }
    CHECK(color_count >= 10);

    memset(&frame, 0, sizeof(frame));
    CHECK(!nb_wsdisplay_smoke_image_frame(&image, 0, &frame));
    CHECK(nb_wsdisplay_smoke_image_frame(&image, 7, &frame));
    CHECK(frame.pixels == image.pixels);
    CHECK(frame.width == 80);
    CHECK(frame.height == 60);
    CHECK(frame.serial == 7);
    CHECK(frame.format == NB_HOST_PIXEL_FORMAT_XRGB8888);

    nb_wsdisplay_smoke_image_destroy(&image);
    CHECK(image.pixels == NULL);
    nb_wsdisplay_smoke_image_destroy(&image);
}

int main(void)
{
    test_parser_safe_actions();
    test_parser_takeover();
    test_parser_wscons_tuning();
    test_parser_rejections();
    test_vt_number_translation();
    test_pattern();

    if (failures != 0) {
        fprintf(stderr, "wsdisplay smoke tests: %d failure(s)\n", failures);
        return 1;
    }
    puts("wsdisplay smoke tests: ok");
    return 0;
}
