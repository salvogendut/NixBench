#ifndef NIXBENCH_WSDISPLAY_RECOVERY_H
#define NIXBENCH_WSDISPLAY_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "wsdisplay_console_session.h"

enum {
    NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY = 256
};

struct nb_wsdisplay_recovery_options {
    const char *record_path;
    const char *status_device_path;
    const char *screen_device_prefix;
    uid_t record_owner;
};

/*
 * The record is deliberately private to one build/version. Its header and
 * exact size reject stale or foreign data before any saved device path is
 * considered. Device paths must also match the launcher's fixed namespace.
 */
bool nb_wsdisplay_recovery_store(
    const struct nb_wsdisplay_recovery_options *options,
    const struct nb_wsdisplay_console_state *state,
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY]);

bool nb_wsdisplay_recovery_load(
    const struct nb_wsdisplay_recovery_options *options,
    struct nb_wsdisplay_console_state *state,
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY]);

bool nb_wsdisplay_recovery_remove(
    const struct nb_wsdisplay_recovery_options *options,
    char error[NB_WSDISPLAY_RECOVERY_ERROR_CAPACITY]);

#endif
