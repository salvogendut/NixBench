#ifndef NIXBENCH_WSDISPLAY_CONSOLE_SESSION_INTERNAL_H
#define NIXBENCH_WSDISPLAY_CONSOLE_SESSION_INTERNAL_H

#include "wsdisplay_console_session.h"

#include <stdint.h>
#include <stdio.h>

/*
 * Device-free seam for the capture and restoration policy.  Operations return
 * zero on success and a negative errno value on failure.  open_device()
 * returns a non-negative borrowed test/OS descriptor or a negative errno.
 *
 * These callbacks deliberately describe the narrow console operations rather
 * than accepting arbitrary ioctl request numbers.  They are private to the
 * implementation and unit tests; the privileged command has no runtime hook
 * for replacing them.
 */
struct nb_wsdisplay_console_operations {
    unsigned int emulation_display_mode;
    int automatic_vt_mode;

    int (*open_device)(void *opaque, const char *path, bool writable);
    int (*close_device)(void *opaque, int descriptor);
    int (*inspect_character_device)(void *opaque,
                                    int descriptor,
                                    bool *is_character);
    int (*get_active_screen)(void *opaque,
                             int descriptor,
                             int *active_screen);
    int (*get_display_mode)(void *opaque,
                            int descriptor,
                            unsigned int *mode);
    int (*set_display_mode)(void *opaque,
                            int descriptor,
                            unsigned int mode);
    int (*get_video)(void *opaque,
                     int descriptor,
                     unsigned int *video);
    int (*set_video)(void *opaque,
                     int descriptor,
                     unsigned int video);
    int (*get_vt_mode)(void *opaque,
                       int descriptor,
                       struct nb_wsdisplay_console_vt_mode *mode);
    int (*set_vt_mode)(void *opaque,
                       int descriptor,
                       const struct nb_wsdisplay_console_vt_mode *mode);
    int (*get_active_vt)(void *opaque,
                         int descriptor,
                         int *active_vt);
    int (*activate_vt)(void *opaque,
                       int descriptor,
                       int vt_number);
    uint64_t (*monotonic_milliseconds)(void *opaque);
    void (*sleep_milliseconds)(void *opaque, unsigned int milliseconds);
};

bool nb_wsdisplay_console_capture_with_operations(
    const struct nb_wsdisplay_console_capture_options *options,
    struct nb_wsdisplay_console_state *state,
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    FILE *error_stream);

bool nb_wsdisplay_console_restore_with_operations(
    const struct nb_wsdisplay_console_state *state,
    const struct nb_wsdisplay_console_operations *operations,
    void *opaque,
    FILE *error_stream);

#endif
