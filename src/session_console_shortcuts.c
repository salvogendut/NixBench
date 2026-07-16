#include "session_console_shortcuts.h"

#include <stddef.h>
#include <string.h>

static struct nb_session_console_shortcut no_shortcut(void)
{
    const struct nb_session_console_shortcut shortcut = {
        NB_SESSION_CONSOLE_SHORTCUT_NONE,
        0,
        false
    };

    return shortcut;
}

static bool key_is(const struct nb_host_event *event, const char *name)
{
    return strncmp(event->data.key.xkb_key_name,
                   name,
                   NB_HOST_XKB_KEY_NAME_CAPACITY) == 0;
}

static int function_key_number(const struct nb_host_event *event)
{
    static const char *const names[] = {
        "FK01", "FK02", "FK03", "FK04", "FK05", "FK06",
        "FK07", "FK08", "FK09", "FK10", "FK11", "FK12"
    };
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (key_is(event, names[index])) {
            return (int)index + 1;
        }
    }
    return 0;
}

void nb_session_console_shortcuts_reset(
    struct nb_session_console_shortcuts *shortcuts)
{
    if (shortcuts != NULL) {
        memset(shortcuts, 0, sizeof(*shortcuts));
    }
}

struct nb_session_console_shortcut nb_session_console_shortcuts_apply(
    struct nb_session_console_shortcuts *shortcuts,
    const struct nb_host_event *event)
{
    struct nb_session_console_shortcut shortcut = no_shortcut();
    bool *modifier = NULL;
    bool control;
    bool alt;
    int function_number;
    uint16_t function_mask;

    if (shortcuts == NULL || event == NULL ||
        event->type != NB_HOST_EVENT_KEY) {
        return shortcut;
    }

    if (key_is(event, "LCTL")) {
        modifier = &shortcuts->left_control;
    } else if (key_is(event, "RCTL")) {
        modifier = &shortcuts->right_control;
    } else if (key_is(event, "LALT")) {
        modifier = &shortcuts->left_alt;
    } else if (key_is(event, "RALT")) {
        modifier = &shortcuts->right_alt;
    }
    if (modifier != NULL) {
        *modifier = event->data.key.pressed;
        return shortcut;
    }

    control = shortcuts->left_control || shortcuts->right_control;
    alt = shortcuts->left_alt || shortcuts->right_alt;
    if (key_is(event, "BKSP")) {
        if (!event->data.key.pressed) {
            shortcut.consumed = shortcuts->consumed_backspace;
            shortcuts->backspace = false;
            shortcuts->consumed_backspace = false;
            return shortcut;
        }
        if (shortcuts->backspace || event->data.key.repeat) {
            shortcuts->backspace = true;
            shortcut.consumed = shortcuts->consumed_backspace;
            return shortcut;
        }
        shortcuts->backspace = true;
        if (control && alt) {
            shortcut.type =
                NB_SESSION_CONSOLE_SHORTCUT_EMERGENCY_SHUTDOWN;
            shortcut.consumed = true;
            shortcuts->consumed_backspace = true;
        }
        return shortcut;
    }

    function_number = function_key_number(event);
    if (function_number == 0) {
        return shortcut;
    }
    function_mask = (uint16_t)(UINT16_C(1) << (function_number - 1));
    if (!event->data.key.pressed) {
        shortcut.consumed =
            (shortcuts->consumed_function_keys & function_mask) != 0;
        shortcuts->function_keys &= (uint16_t)~function_mask;
        shortcuts->consumed_function_keys &= (uint16_t)~function_mask;
        return shortcut;
    }
    if ((shortcuts->function_keys & function_mask) != 0 ||
        event->data.key.repeat) {
        shortcut.consumed =
            (shortcuts->consumed_function_keys & function_mask) != 0;
        shortcuts->function_keys |= function_mask;
        return shortcut;
    }

    shortcuts->function_keys |= function_mask;
    if (control && alt) {
        shortcuts->consumed_function_keys |= function_mask;
        shortcut.type = NB_SESSION_CONSOLE_SHORTCUT_SWITCH_VT;
        shortcut.vt_number = function_number;
        shortcut.consumed = true;
    }
    return shortcut;
}
