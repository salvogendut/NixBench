#ifndef NIXBENCH_HOST_WSDISPLAY_H
#define NIXBENCH_HOST_WSDISPLAY_H

#include "host.h"

struct nb_host_wsdisplay_options {
    const char *device_path;
    /* Optional one-based USL VT number which must still be active immediately
     * after VT_PROCESS is installed. Zero disables this takeover guard. */
    int expected_active_vt;
};

void nb_host_wsdisplay_options_init(
    struct nb_host_wsdisplay_options *options);

/*
 * Experimental NetBSD output-only adapter. It takes exclusive ownership of
 * one wsdisplay screen, reserves SIGUSR1/SIGUSR2 for VT release/acquire,
 * temporarily translates SIGINT/SIGTERM/SIGHUP/SIGQUIT into a host quit event,
 * and converts software frames into the framebuffer's native RGB layout. No
 * wscons input is opened yet. The caller must keep polling the host for both VT
 * and termination events, and the process must remain single-threaded from
 * creation through destruction. Only one instance may exist in a process.
 * Graceful shutdown restores the console, but there is not yet an external
 * watchdog to recover it after SIGKILL or a fatal process crash.
 *
 * Other platforms provide a portable unsupported stub returning NULL.
 */
struct nb_host *nb_host_wsdisplay_create(
    const struct nb_host_wsdisplay_options *options);

/* Borrowed creation error text; invalidated by the next create attempt. */
const char *nb_host_wsdisplay_creation_error(void);
int nb_host_wsdisplay_creation_system_error(void);

#endif
