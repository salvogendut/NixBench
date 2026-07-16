#ifndef NIXBENCH_HOST_WSDISPLAY_H
#define NIXBENCH_HOST_WSDISPLAY_H

#include "host.h"
#include "host_fd_wait.h"

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

/*
 * Request a switch to a one-based USL VT while this wsdisplay host is active.
 * A successful request is completed asynchronously through the normal
 * CONSOLE_RELEASE_REQUESTED event and acknowledgement path. The fixed range
 * matches the privileged Ctrl+Alt+F1 through F12 bindings; a VT may still be
 * unavailable when it has not been configured by wscons.
 */
enum nb_host_result nb_host_wsdisplay_request_vt_switch(
    struct nb_host *host,
    int vt_number);

/*
 * Backend-specific integration seam for the standalone event loop. The
 * descriptors are borrowed for this call and used only as wake sources; the
 * caller remains responsible for reading them. Queued wsdisplay events are
 * checked before blocking and take priority when lifecycle and external
 * readiness occur together.
 */
enum nb_host_event_status
nb_host_wsdisplay_wait_event_with_descriptors(
    struct nb_host *host,
    const int *external_descriptors,
    size_t external_descriptor_count,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event,
    struct nb_host_fd_wait_result *wait_result);

/* Borrowed creation error text; invalidated by the next create attempt. */
const char *nb_host_wsdisplay_creation_error(void);
int nb_host_wsdisplay_creation_system_error(void);

#endif
