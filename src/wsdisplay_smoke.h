#ifndef NIXBENCH_WSDISPLAY_SMOKE_H
#define NIXBENCH_WSDISPLAY_SMOKE_H

#include "host.h"
#include "wscons_input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    NB_WSDISPLAY_SMOKE_MIN_DURATION_MS = 250,
    NB_WSDISPLAY_SMOKE_DEFAULT_DURATION_MS = 3000,
    NB_WSDISPLAY_SMOKE_MAX_DURATION_MS = 30000,
    NB_WSDISPLAY_SMOKE_MIN_POINTER_SENSITIVITY_PERCENT = 25,
    NB_WSDISPLAY_SMOKE_DEFAULT_POINTER_SENSITIVITY_PERCENT = 100,
    NB_WSDISPLAY_SMOKE_MAX_POINTER_SENSITIVITY_PERCENT = 400,
    NB_WSDISPLAY_SMOKE_ERROR_CAPACITY = 256
};

enum nb_wsdisplay_smoke_action {
    NB_WSDISPLAY_SMOKE_ACTION_RUN,
    NB_WSDISPLAY_SMOKE_ACTION_HELP,
    NB_WSDISPLAY_SMOKE_ACTION_PREFLIGHT,
    NB_WSDISPLAY_SMOKE_ACTION_RECOVER
};

enum nb_wsdisplay_smoke_content {
    NB_WSDISPLAY_SMOKE_CONTENT_DIAGNOSTIC,
    NB_WSDISPLAY_SMOKE_CONTENT_DESKTOP_PREVIEW,
    NB_WSDISPLAY_SMOKE_CONTENT_INTERACTIVE_PREVIEW,
    NB_WSDISPLAY_SMOKE_CONTENT_RUNTIME_PREVIEW
};

struct nb_wsdisplay_smoke_options {
    enum nb_wsdisplay_smoke_action action;
    const char *program_path;
    const char *status_device_path;
    const char *screen_device_prefix;
    enum nb_wsdisplay_smoke_content content;
    uint32_t duration_ms;
    enum nb_wscons_pointer_profile wscons_pointer_profile;
    uint32_t wscons_pointer_sensitivity_percent;
    bool acknowledge_console_takeover;
    bool acknowledge_no_crash_watchdog;
    bool until_exit;
    bool wscons_input_stats;
    bool require_vt_cycle;
};

struct nb_wsdisplay_smoke_vt_cycle_observation {
    uint64_t release_requests;
    uint64_t release_completions;
    uint64_t acquire_requests;
    uint64_t acquire_completions;
    uint64_t input_suspends;
    uint64_t input_resumes;
    uint64_t release_timing_samples;
    uint64_t suspended_timing_samples;
    uint64_t acquire_timing_samples;
    uint64_t timing_regressions;
    bool post_acquire_frame_completed;
};

struct nb_wsdisplay_smoke_image {
    uint32_t *pixels;
    int width;
    int height;
    size_t stride;
};

void nb_wsdisplay_smoke_options_init(
    struct nb_wsdisplay_smoke_options *options);

bool nb_wsdisplay_smoke_parse_options(
    int argc,
    char *argv[],
    struct nb_wsdisplay_smoke_options *options,
    char error[NB_WSDISPLAY_SMOKE_ERROR_CAPACITY]);

/* NetBSD's native active-screen ioctl is zero-based, while the USL VT
 * compatibility ioctls use one-based VT numbers. */
bool nb_wsdisplay_screen_index_to_vt_number(
    int screen_index,
    int *vt_number);

bool nb_wsdisplay_smoke_vt_cycle_complete(
    const struct nb_wsdisplay_smoke_vt_cycle_observation *observation);

bool nb_wsdisplay_smoke_image_create(
    struct nb_wsdisplay_smoke_image *image,
    int width,
    int height);
void nb_wsdisplay_smoke_image_destroy(
    struct nb_wsdisplay_smoke_image *image);
bool nb_wsdisplay_smoke_image_frame(
    const struct nb_wsdisplay_smoke_image *image,
    uint64_t serial,
    struct nb_host_frame *frame);

#endif
