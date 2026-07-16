#ifndef NIXBENCH_SESSION_CONSOLE_SHORTCUTS_H
#define NIXBENCH_SESSION_CONSOLE_SHORTCUTS_H

#include <stdbool.h>
#include <stdint.h>

#include "host.h"

enum nb_session_console_shortcut_type {
    NB_SESSION_CONSOLE_SHORTCUT_NONE,
    NB_SESSION_CONSOLE_SHORTCUT_EMERGENCY_SHUTDOWN,
    NB_SESSION_CONSOLE_SHORTCUT_SWITCH_VT
};

struct nb_session_console_shortcut {
    enum nb_session_console_shortcut_type type;
    /* One-based USL VT number; zero for non-switch actions. */
    int vt_number;
    /* The worker must not forward a consumed event to the desktop core. */
    bool consumed;
};

/*
 * Tracks physical console chords in the privileged input path. Physical XKB
 * key names keep these bindings independent of client focus, keyboard layout,
 * text translation, and the desktop core's responsiveness.
 */
struct nb_session_console_shortcuts {
    bool left_control;
    bool right_control;
    bool left_alt;
    bool right_alt;
    bool backspace;
    bool consumed_backspace;
    uint16_t function_keys;
    uint16_t consumed_function_keys;
};

void nb_session_console_shortcuts_reset(
    struct nb_session_console_shortcuts *shortcuts);

/*
 * Ctrl+Alt+Backspace requests emergency shutdown. Ctrl+Alt+F1 through F12
 * request the corresponding one-based VT. Each press acts at most once.
 */
struct nb_session_console_shortcut nb_session_console_shortcuts_apply(
    struct nb_session_console_shortcuts *shortcuts,
    const struct nb_host_event *event);

#endif
