#include "session_emergency_chord.h"

#include <string.h>

static bool key_is(const struct nb_host_event *event, const char *name)
{
    return strncmp(event->data.key.xkb_key_name,
                   name,
                   NB_HOST_XKB_KEY_NAME_CAPACITY) == 0;
}

void nb_session_emergency_chord_reset(
    struct nb_session_emergency_chord *chord)
{
    if (chord != NULL) {
        memset(chord, 0, sizeof(*chord));
    }
}

bool nb_session_emergency_chord_apply(
    struct nb_session_emergency_chord *chord,
    const struct nb_host_event *event)
{
    bool *modifier = NULL;
    bool control;
    bool alt;

    if (chord == NULL || event == NULL ||
        event->type != NB_HOST_EVENT_KEY) {
        return false;
    }

    if (key_is(event, "LCTL")) {
        modifier = &chord->left_control;
    } else if (key_is(event, "RCTL")) {
        modifier = &chord->right_control;
    } else if (key_is(event, "LALT")) {
        modifier = &chord->left_alt;
    } else if (key_is(event, "RALT")) {
        modifier = &chord->right_alt;
    }
    if (modifier != NULL) {
        *modifier = event->data.key.pressed;
        return false;
    }

    if (!key_is(event, "BKSP")) {
        return false;
    }
    if (!event->data.key.pressed) {
        chord->backspace = false;
        return false;
    }
    if (chord->backspace || event->data.key.repeat) {
        chord->backspace = true;
        return false;
    }

    chord->backspace = true;
    control = chord->left_control || chord->right_control;
    alt = chord->left_alt || chord->right_alt;
    return control && alt;
}
