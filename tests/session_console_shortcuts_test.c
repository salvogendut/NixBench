#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "session_console_shortcuts.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static struct nb_host_event key_event(const char *name,
                                      bool pressed,
                                      bool repeat)
{
    struct nb_host_event event;

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_KEY;
    event.data.key.pressed = pressed;
    event.data.key.repeat = repeat;
    (void)snprintf(event.data.key.xkb_key_name,
                   sizeof(event.data.key.xkb_key_name),
                   "%s",
                   name);
    return event;
}

static struct nb_session_console_shortcut apply_key(
    struct nb_session_console_shortcuts *shortcuts,
    const char *name,
    bool pressed,
    bool repeat)
{
    const struct nb_host_event event = key_event(name, pressed, repeat);

    return nb_session_console_shortcuts_apply(shortcuts, &event);
}

static void check_none(struct nb_session_console_shortcut shortcut,
                       bool consumed)
{
    CHECK(shortcut.type == NB_SESSION_CONSOLE_SHORTCUT_NONE);
    CHECK(shortcut.vt_number == 0);
    CHECK(shortcut.consumed == consumed);
}

static void test_emergency_shutdown(void)
{
    struct nb_session_console_shortcuts shortcuts;
    struct nb_session_console_shortcut shortcut;

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "LCTL", true, false), false);
    check_none(apply_key(&shortcuts, "LALT", true, false), false);
    shortcut = apply_key(&shortcuts, "BKSP", true, false);
    CHECK(shortcut.type ==
          NB_SESSION_CONSOLE_SHORTCUT_EMERGENCY_SHUTDOWN);
    CHECK(shortcut.vt_number == 0);
    CHECK(shortcut.consumed);
    check_none(apply_key(&shortcuts, "BKSP", true, true), true);
    check_none(apply_key(&shortcuts, "BKSP", false, false), true);

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "RCTL", true, false), false);
    check_none(apply_key(&shortcuts, "RALT", true, false), false);
    shortcut = apply_key(&shortcuts, "BKSP", true, false);
    CHECK(shortcut.type ==
          NB_SESSION_CONSOLE_SHORTCUT_EMERGENCY_SHUTDOWN);
    CHECK(shortcut.consumed);
}

static void test_all_vt_shortcuts(void)
{
    static const char *const keys[] = {
        "FK01", "FK02", "FK03", "FK04", "FK05", "FK06",
        "FK07", "FK08", "FK09", "FK10", "FK11", "FK12"
    };
    struct nb_session_console_shortcuts shortcuts;
    size_t index;

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "LCTL", true, false), false);
    check_none(apply_key(&shortcuts, "RALT", true, false), false);
    for (index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        struct nb_session_console_shortcut shortcut =
            apply_key(&shortcuts, keys[index], true, false);

        CHECK(shortcut.type == NB_SESSION_CONSOLE_SHORTCUT_SWITCH_VT);
        CHECK(shortcut.vt_number == (int)index + 1);
        CHECK(shortcut.consumed);
        check_none(apply_key(&shortcuts, keys[index], true, true), true);
        check_none(apply_key(&shortcuts, keys[index], false, false), true);
    }
}

static void test_plain_function_keys_are_forwarded(void)
{
    struct nb_session_console_shortcuts shortcuts;

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "FK02", true, false), false);
    check_none(apply_key(&shortcuts, "FK02", true, true), false);
    check_none(apply_key(&shortcuts, "FK02", false, false), false);
}

static void test_partial_and_wrong_order_do_not_trigger(void)
{
    struct nb_session_console_shortcuts shortcuts;

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "LCTL", true, false), false);
    check_none(apply_key(&shortcuts, "FK02", true, false), false);
    check_none(apply_key(&shortcuts, "FK02", false, false), false);
    check_none(apply_key(&shortcuts, "LCTL", false, false), false);

    check_none(apply_key(&shortcuts, "LALT", true, false), false);
    check_none(apply_key(&shortcuts, "FK02", true, false), false);
    check_none(apply_key(&shortcuts, "FK02", false, false), false);

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "FK02", true, false), false);
    check_none(apply_key(&shortcuts, "LCTL", true, false), false);
    check_none(apply_key(&shortcuts, "LALT", true, false), false);
    check_none(apply_key(&shortcuts, "FK02", true, true), false);
    check_none(apply_key(&shortcuts, "FK02", false, false), false);
    CHECK(apply_key(&shortcuts, "FK02", true, false).type ==
          NB_SESSION_CONSOLE_SHORTCUT_SWITCH_VT);
}

static void test_modifier_release_and_reset_clear_state(void)
{
    struct nb_session_console_shortcuts shortcuts;

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "LCTL", true, false), false);
    check_none(apply_key(&shortcuts, "LALT", true, false), false);
    check_none(apply_key(&shortcuts, "LCTL", false, false), false);
    check_none(apply_key(&shortcuts, "FK03", true, false), false);

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "FK03", true, false), false);
}

static void test_unrelated_and_invalid_events(void)
{
    struct nb_session_console_shortcuts shortcuts;
    struct nb_host_event event;

    nb_session_console_shortcuts_reset(&shortcuts);
    check_none(apply_key(&shortcuts, "LCTL", true, false), false);
    check_none(apply_key(&shortcuts, "LALT", true, false), false);
    check_none(apply_key(&shortcuts, "AC01", true, false), false);

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_MOTION;
    check_none(nb_session_console_shortcuts_apply(&shortcuts, &event),
               false);
    check_none(nb_session_console_shortcuts_apply(NULL, &event), false);
    check_none(nb_session_console_shortcuts_apply(&shortcuts, NULL), false);
}

int main(void)
{
    test_emergency_shutdown();
    test_all_vt_shortcuts();
    test_plain_function_keys_are_forwarded();
    test_partial_and_wrong_order_do_not_trigger();
    test_modifier_release_and_reset_clear_state();
    test_unrelated_and_invalid_events();

    if (failures != 0) {
        fprintf(stderr,
                "session console shortcut tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("session console shortcut tests: ok");
    return 0;
}
