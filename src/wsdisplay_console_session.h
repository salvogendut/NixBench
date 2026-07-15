#ifndef NIXBENCH_WSDISPLAY_CONSOLE_SESSION_H
#define NIXBENCH_WSDISPLAY_CONSOLE_SESSION_H

#include <stdbool.h>

enum {
    NB_WSDISPLAY_CONSOLE_PATH_CAPACITY = 256
};

/*
 * Portable representation of the console properties guarded by the
 * wsdisplay smoke supervisor.  The smoke harness keeps ownership of its
 * versioned on-disk record and translates that native record to and from this
 * representation, so extracting this policy does not change the recovery
 * file ABI.
 */
struct nb_wsdisplay_console_vt_mode {
    int mode;
    int waitv;
    int relsig;
    int acqsig;
    int frsig;
};

struct nb_wsdisplay_console_state {
    int active_screen;
    unsigned int display_mode;
    unsigned int video;
    struct nb_wsdisplay_console_vt_mode vt_mode;
    char status_device[NB_WSDISPLAY_CONSOLE_PATH_CAPACITY];
    char screen_device[NB_WSDISPLAY_CONSOLE_PATH_CAPACITY];
    bool video_available;
};

struct nb_wsdisplay_console_capture_options {
    const char *status_device_path;
    const char *screen_device_prefix;
};

/* Real NetBSD entry points used by the privileged smoke supervisor. */
bool nb_wsdisplay_console_capture(
    const struct nb_wsdisplay_console_capture_options *options,
    struct nb_wsdisplay_console_state *state);

bool nb_wsdisplay_console_restore_and_verify(
    const struct nb_wsdisplay_console_state *state);

#endif
