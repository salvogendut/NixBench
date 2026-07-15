#include "wsdisplay_smoke.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char default_status_device[] = "/dev/ttyEstat";
static const char default_screen_prefix[] = "/dev/ttyE";

static void set_error(char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY],
                      const char *message)
{
    if (error != NULL) {
        (void)snprintf(error,
                       NB_WSDISPLAY_SMOKE_ERROR_CAPACITY,
                       "%s",
                       message);
    }
}

static bool option_value(int argc,
                         char *argv[],
                         int *index,
                         const char **value,
                         char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY])
{
    if (*index + 1 >= argc || argv[*index + 1][0] == '\0') {
        set_error(error, "Option requires a non-empty value");
        return false;
    }
    ++*index;
    *value = argv[*index];
    return true;
}

static bool parse_duration(
    const char *text,
    uint32_t *duration,
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY])
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || text[0] == '\0' || text[0] == '-' ||
        text[0] == '+') {
        set_error(error, "Duration must be an unsigned decimal integer");
        return false;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        set_error(error, "Invalid duration");
        return false;
    }
    if (value < NB_WSDISPLAY_SMOKE_MIN_DURATION_MS ||
        value > NB_WSDISPLAY_SMOKE_MAX_DURATION_MS) {
        set_error(error, "Duration is outside the 250..30000 ms safety range");
        return false;
    }
    *duration = (uint32_t)value;
    return true;
}

static bool parse_pointer_sensitivity(
    const char *text,
    uint32_t *sensitivity_percent,
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY])
{
    char *end = NULL;
    const char *character;
    unsigned long value;

    if (text == NULL || text[0] == '\0' || text[0] == '-' ||
        text[0] == '+') {
        set_error(error,
                  "Pointer sensitivity must be an unsigned decimal integer");
        return false;
    }
    for (character = text; *character != '\0'; ++character) {
        if (*character < '0' || *character > '9') {
            set_error(error,
                      "Pointer sensitivity must be an unsigned decimal integer");
            return false;
        }
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        set_error(error, "Invalid pointer sensitivity");
        return false;
    }
    if (value < NB_WSDISPLAY_SMOKE_MIN_POINTER_SENSITIVITY_PERCENT ||
        value > NB_WSDISPLAY_SMOKE_MAX_POINTER_SENSITIVITY_PERCENT) {
        set_error(error, "Pointer sensitivity is outside the 25..400 percent range");
        return false;
    }
    *sensitivity_percent = (uint32_t)value;
    return true;
}

static bool parse_pointer_profile(
    const char *text,
    enum nb_wscons_pointer_profile *profile,
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY])
{
    if (strcmp(text, "flat") == 0) {
        *profile = NB_WSCONS_POINTER_PROFILE_FLAT;
        return true;
    }
    if (strcmp(text, "adaptive") == 0) {
        *profile = NB_WSCONS_POINTER_PROFILE_ADAPTIVE;
        return true;
    }
    set_error(error, "Pointer profile must be flat or adaptive");
    return false;
}

void nb_wsdisplay_smoke_options_init(
    struct nb_wsdisplay_smoke_options *options)
{
    if (options == NULL) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->action = NB_WSDISPLAY_SMOKE_ACTION_RUN;
    options->status_device_path = default_status_device;
    options->screen_device_prefix = default_screen_prefix;
    options->duration_ms = NB_WSDISPLAY_SMOKE_DEFAULT_DURATION_MS;
    options->wscons_pointer_profile = NB_WSCONS_POINTER_PROFILE_FLAT;
    options->wscons_pointer_sensitivity_percent =
        NB_WSDISPLAY_SMOKE_DEFAULT_POINTER_SENSITIVITY_PERCENT;
}

static bool select_action(
    struct nb_wsdisplay_smoke_options *options,
    enum nb_wsdisplay_smoke_action action,
    bool *action_selected,
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY])
{
    if (*action_selected) {
        set_error(error, "Only one of --help, --preflight-only, or --recover may be used");
        return false;
    }
    options->action = action;
    *action_selected = true;
    return true;
}

bool nb_wsdisplay_smoke_parse_options(
    int argc,
    char *argv[],
    struct nb_wsdisplay_smoke_options *options,
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY])
{
    bool action_selected = false;
    bool duration_selected = false;
    bool pointer_profile_selected = false;
    bool pointer_sensitivity_selected = false;
    bool content_selected = false;
    bool status_selected = false;
    bool prefix_selected = false;
    int index;

    if (options == NULL || argc < 1 || argv == NULL) {
        set_error(error, "Invalid command-line parser arguments");
        return false;
    }
    nb_wsdisplay_smoke_options_init(options);
    if (argv[0] == NULL || argv[0][0] == '\0') {
        set_error(error, "Program path is empty");
        return false;
    }
    options->program_path = argv[0];
    if (error != NULL) {
        error[0] = '\0';
    }

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0) {
            if (!select_action(options,
                               NB_WSDISPLAY_SMOKE_ACTION_HELP,
                               &action_selected,
                               error)) {
                return false;
            }
        } else if (strcmp(argv[index], "--preflight-only") == 0) {
            if (!select_action(options,
                               NB_WSDISPLAY_SMOKE_ACTION_PREFLIGHT,
                               &action_selected,
                               error)) {
                return false;
            }
        } else if (strcmp(argv[index], "--recover") == 0) {
            if (!select_action(options,
                               NB_WSDISPLAY_SMOKE_ACTION_RECOVER,
                               &action_selected,
                               error)) {
                return false;
            }
        } else if (strcmp(argv[index],
                          "--acknowledge-console-takeover") == 0) {
            if (options->acknowledge_console_takeover) {
                set_error(error, "Duplicate console-takeover acknowledgement");
                return false;
            }
            options->acknowledge_console_takeover = true;
        } else if (strcmp(argv[index],
                          "--acknowledge-no-crash-watchdog") == 0) {
            if (options->acknowledge_no_crash_watchdog) {
                set_error(error, "Duplicate crash-watchdog acknowledgement");
                return false;
            }
            options->acknowledge_no_crash_watchdog = true;
        } else if (strcmp(argv[index], "--desktop-preview") == 0) {
            if (content_selected) {
                set_error(error, "Duplicate presentation-content option");
                return false;
            }
            options->content = NB_WSDISPLAY_SMOKE_CONTENT_DESKTOP_PREVIEW;
            content_selected = true;
        } else if (strcmp(argv[index], "--interactive-preview") == 0) {
            if (content_selected) {
                set_error(error, "Duplicate presentation-content option");
                return false;
            }
            options->content = NB_WSDISPLAY_SMOKE_CONTENT_INTERACTIVE_PREVIEW;
            content_selected = true;
        } else if (strcmp(argv[index], "--runtime-preview") == 0) {
            if (content_selected) {
                set_error(error, "Duplicate presentation-content option");
                return false;
            }
            options->content = NB_WSDISPLAY_SMOKE_CONTENT_RUNTIME_PREVIEW;
            content_selected = true;
        } else if (strcmp(argv[index], "--duration-ms") == 0) {
            const char *value;

            if (duration_selected ||
                !option_value(argc, argv, &index, &value, error) ||
                !parse_duration(value, &options->duration_ms, error)) {
                if (duration_selected) {
                    set_error(error, "Duplicate --duration-ms option");
                }
                return false;
            }
            duration_selected = true;
        } else if (strcmp(argv[index],
                          "--wscons-pointer-profile") == 0) {
            const char *value;

            if (pointer_profile_selected) {
                set_error(error,
                          "Duplicate --wscons-pointer-profile option");
                return false;
            }
            if (!option_value(argc, argv, &index, &value, error) ||
                !parse_pointer_profile(value,
                                       &options->wscons_pointer_profile,
                                       error)) {
                return false;
            }
            pointer_profile_selected = true;
        } else if (strcmp(argv[index],
                          "--wscons-pointer-sensitivity-percent") == 0) {
            const char *value;

            if (pointer_sensitivity_selected) {
                set_error(error,
                          "Duplicate --wscons-pointer-sensitivity-percent option");
                return false;
            }
            if (!option_value(argc, argv, &index, &value, error) ||
                !parse_pointer_sensitivity(
                    value,
                    &options->wscons_pointer_sensitivity_percent,
                    error)) {
                return false;
            }
            pointer_sensitivity_selected = true;
        } else if (strcmp(argv[index], "--wscons-input-stats") == 0) {
            if (options->wscons_input_stats) {
                set_error(error, "Duplicate --wscons-input-stats option");
                return false;
            }
            options->wscons_input_stats = true;
        } else if (strcmp(argv[index], "--require-vt-cycle") == 0) {
            if (options->require_vt_cycle) {
                set_error(error, "Duplicate --require-vt-cycle option");
                return false;
            }
            options->require_vt_cycle = true;
        } else if (strcmp(argv[index], "--status-device") == 0) {
            if (status_selected ||
                !option_value(argc,
                              argv,
                              &index,
                              &options->status_device_path,
                              error)) {
                if (status_selected) {
                    set_error(error, "Duplicate --status-device option");
                }
                return false;
            }
            status_selected = true;
        } else if (strcmp(argv[index], "--screen-prefix") == 0) {
            if (prefix_selected ||
                !option_value(argc,
                              argv,
                              &index,
                              &options->screen_device_prefix,
                              error)) {
                if (prefix_selected) {
                    set_error(error, "Duplicate --screen-prefix option");
                }
                return false;
            }
            prefix_selected = true;
        } else {
            if (error != NULL) {
                (void)snprintf(error,
                               NB_WSDISPLAY_SMOKE_ERROR_CAPACITY,
                               "Unknown option: %s",
                               argv[index]);
            }
            return false;
        }
    }

    if (options->action == NB_WSDISPLAY_SMOKE_ACTION_RUN &&
        (!options->acknowledge_console_takeover ||
         !options->acknowledge_no_crash_watchdog)) {
        set_error(error,
                  "Refusing takeover without both explicit acknowledgements");
        return false;
    }
    if (options->wscons_pointer_profile ==
            NB_WSCONS_POINTER_PROFILE_ADAPTIVE &&
        pointer_sensitivity_selected) {
        set_error(error,
                  "--wscons-pointer-profile adaptive cannot be combined with "
                  "--wscons-pointer-sensitivity-percent");
        return false;
    }
    if (options->action != NB_WSDISPLAY_SMOKE_ACTION_RUN &&
        (options->acknowledge_console_takeover ||
         options->acknowledge_no_crash_watchdog || duration_selected ||
         content_selected || pointer_profile_selected ||
         pointer_sensitivity_selected ||
         options->wscons_input_stats || options->require_vt_cycle)) {
        set_error(error,
                  "Run acknowledgements, content, duration, and wscons options apply only to takeover");
        return false;
    }
    if (options->action == NB_WSDISPLAY_SMOKE_ACTION_RUN &&
        (pointer_profile_selected || pointer_sensitivity_selected ||
         options->wscons_input_stats || options->require_vt_cycle) &&
        options->content != NB_WSDISPLAY_SMOKE_CONTENT_INTERACTIVE_PREVIEW &&
        options->content != NB_WSDISPLAY_SMOKE_CONTENT_RUNTIME_PREVIEW) {
        set_error(error,
                  "wscons and VT-cycle options require "
                  "--interactive-preview or --runtime-preview");
        return false;
    }
    if ((options->action == NB_WSDISPLAY_SMOKE_ACTION_HELP ||
         options->action == NB_WSDISPLAY_SMOKE_ACTION_RECOVER) &&
        (status_selected || prefix_selected)) {
        set_error(error,
                  "Path options apply only to takeover and preflight runs");
        return false;
    }
    if (options->status_device_path[0] != '/' ||
        options->screen_device_prefix[0] != '/') {
        set_error(error, "Device paths must be absolute for safe recovery");
        return false;
    }
    return true;
}

bool nb_wsdisplay_screen_index_to_vt_number(
    int screen_index,
    int *vt_number)
{
    if (screen_index < 0 || screen_index == INT_MAX || vt_number == NULL) {
        return false;
    }
    *vt_number = screen_index + 1;
    return true;
}

bool nb_wsdisplay_smoke_vt_cycle_complete(
    const struct nb_wsdisplay_smoke_vt_cycle_observation *observation)
{
    if (observation == NULL) {
        return false;
    }
    return observation->post_acquire_frame_completed &&
           observation->release_completions != 0 &&
           observation->release_requests ==
               observation->release_completions &&
           observation->acquire_requests ==
               observation->acquire_completions &&
           observation->release_completions ==
               observation->acquire_completions &&
           observation->input_suspends ==
               observation->release_completions &&
           observation->input_resumes ==
               observation->acquire_completions &&
           observation->timing_regressions == 0 &&
           observation->release_timing_samples ==
               observation->release_completions &&
           observation->suspended_timing_samples ==
               observation->acquire_completions &&
           observation->acquire_timing_samples ==
               observation->acquire_completions;
}

static void fill_rect(struct nb_wsdisplay_smoke_image *image,
                      int x,
                      int y,
                      int width,
                      int height,
                      uint32_t color)
{
    int right;
    int bottom;
    int row;

    if (width <= 0 || height <= 0 || x >= image->width ||
        y >= image->height) {
        return;
    }
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    right = width > image->width - x ? image->width : x + width;
    bottom = height > image->height - y ? image->height : y + height;
    for (row = y; row < bottom; ++row) {
        uint32_t *pixels = (uint32_t *)
            ((unsigned char *)image->pixels + (size_t)row * image->stride);
        int column;

        for (column = x; column < right; ++column) {
            pixels[column] = color;
        }
    }
}

static void draw_pattern(struct nb_wsdisplay_smoke_image *image)
{
    static const uint32_t bars[] = {
        UINT32_C(0x00ffffff), UINT32_C(0x00ffff00),
        UINT32_C(0x0000ffff), UINT32_C(0x0000ff00),
        UINT32_C(0x00ff00ff), UINT32_C(0x00ff0000),
        UINT32_C(0x000000ff), UINT32_C(0x00202020)
    };
    const int border = image->width < 64 || image->height < 64 ? 1 : 4;
    const int bar_top = border;
    const int bar_height = image->height / 7 > 12
                               ? image->height / 7
                               : 12;
    const int marker_width = image->width / 18 > 4
                                 ? image->width / 18
                                 : 4;
    const int marker_height = image->height / 5 > 4
                                  ? image->height / 5
                                  : 4;
    size_t index;

    fill_rect(image, 0, 0, image->width, image->height,
              UINT32_C(0x0027475f));
    for (index = 0; index < sizeof(bars) / sizeof(bars[0]); ++index) {
        const int left = (int)((int64_t)image->width * (int64_t)index / 8);
        const int right = (int)((int64_t)image->width *
                                (int64_t)(index + 1) / 8);

        fill_rect(image,
                  left,
                  bar_top,
                  right - left,
                  bar_height,
                  bars[index]);
    }
    fill_rect(image,
              border,
              image->height / 2 - marker_height / 2,
              marker_width,
              marker_height,
              UINT32_C(0x00ff3030));
    fill_rect(image,
              image->width - border - marker_width,
              image->height / 2 - marker_height / 2,
              marker_width,
              marker_height,
              UINT32_C(0x0030ff30));
    fill_rect(image,
              image->width / 2 - marker_width / 2,
              image->height - border - marker_height,
              marker_width,
              marker_height,
              UINT32_C(0x003060ff));

    {
        const int tile = image->width < image->height
                             ? image->width / 24
                             : image->height / 24;
        const int safe_tile = tile > 2 ? tile : 2;
        const int area_width = safe_tile * 8;
        const int area_height = safe_tile * 6;
        const int origin_x = (image->width - area_width) / 2;
        const int origin_y = (image->height - area_height) / 2;
        int row;

        for (row = 0; row < 6; ++row) {
            int column;

            for (column = 0; column < 8; ++column) {
                fill_rect(image,
                          origin_x + column * safe_tile,
                          origin_y + row * safe_tile,
                          safe_tile,
                          safe_tile,
                          ((row + column) & 1) != 0
                              ? UINT32_C(0x00d8e8e8)
                              : UINT32_C(0x00304050));
            }
        }
    }

    fill_rect(image, 0, 0, image->width, border, UINT32_C(0x00f0f0f0));
    fill_rect(image, 0, image->height - border, image->width, border,
              UINT32_C(0x00f0f0f0));
    fill_rect(image, 0, 0, border, image->height, UINT32_C(0x00f0f0f0));
    fill_rect(image, image->width - border, 0, border, image->height,
              UINT32_C(0x00f0f0f0));
}

bool nb_wsdisplay_smoke_image_create(
    struct nb_wsdisplay_smoke_image *image,
    int width,
    int height)
{
    size_t stride;
    size_t size;

    if (image == NULL || width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / sizeof(uint32_t)) {
        return false;
    }
    memset(image, 0, sizeof(*image));
    stride = (size_t)width * sizeof(uint32_t);
    if ((size_t)height > SIZE_MAX / stride) {
        return false;
    }
    size = (size_t)height * stride;
    image->pixels = malloc(size);
    if (image->pixels == NULL) {
        return false;
    }
    image->width = width;
    image->height = height;
    image->stride = stride;
    draw_pattern(image);
    return true;
}

void nb_wsdisplay_smoke_image_destroy(
    struct nb_wsdisplay_smoke_image *image)
{
    if (image == NULL) {
        return;
    }
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}

bool nb_wsdisplay_smoke_image_frame(
    const struct nb_wsdisplay_smoke_image *image,
    uint64_t serial,
    struct nb_host_frame *frame)
{
    if (image == NULL || image->pixels == NULL || frame == NULL ||
        serial == 0) {
        return false;
    }
    frame->pixels = image->pixels;
    frame->width = image->width;
    frame->height = image->height;
    frame->stride = image->stride;
    frame->format = NB_HOST_PIXEL_FORMAT_XRGB8888;
    frame->serial = serial;
    return nb_host_frame_is_valid(frame);
}
