#ifndef NIXBENCH_SESSION_EMERGENCY_CHORD_H
#define NIXBENCH_SESSION_EMERGENCY_CHORD_H

#include <stdbool.h>

#include "host.h"

/*
 * Tracks the physical Ctrl+Alt+Backspace chord in the privileged input path.
 * The detector deliberately uses XKB physical key names so it is independent
 * of the desktop core, client focus, keyboard layout, and text translation.
 */
struct nb_session_emergency_chord {
    bool left_control;
    bool right_control;
    bool left_alt;
    bool right_alt;
    bool backspace;
};

void nb_session_emergency_chord_reset(
    struct nb_session_emergency_chord *chord);

/* Returns true once for each newly pressed Ctrl+Alt+Backspace chord. */
bool nb_session_emergency_chord_apply(
    struct nb_session_emergency_chord *chord,
    const struct nb_host_event *event);

#endif
