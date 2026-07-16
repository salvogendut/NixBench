#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "session_emergency_chord.h"

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

static bool apply_key(struct nb_session_emergency_chord *chord,
                      const char *name,
                      bool pressed,
                      bool repeat)
{
    const struct nb_host_event event = key_event(name, pressed, repeat);

    return nb_session_emergency_chord_apply(chord, &event);
}

static void test_left_modifiers_trigger_once(void)
{
    struct nb_session_emergency_chord chord;

    nb_session_emergency_chord_reset(&chord);
    CHECK(!apply_key(&chord, "LCTL", true, false));
    CHECK(!apply_key(&chord, "LALT", true, false));
    CHECK(apply_key(&chord, "BKSP", true, false));
    CHECK(!apply_key(&chord, "BKSP", true, true));
    CHECK(!apply_key(&chord, "BKSP", false, false));
    CHECK(apply_key(&chord, "BKSP", true, false));
}

static void test_right_and_mixed_modifiers(void)
{
    struct nb_session_emergency_chord chord;

    nb_session_emergency_chord_reset(&chord);
    CHECK(!apply_key(&chord, "RCTL", true, false));
    CHECK(!apply_key(&chord, "RALT", true, false));
    CHECK(apply_key(&chord, "BKSP", true, false));

    nb_session_emergency_chord_reset(&chord);
    CHECK(!apply_key(&chord, "LCTL", true, false));
    CHECK(!apply_key(&chord, "RALT", true, false));
    CHECK(apply_key(&chord, "BKSP", true, false));
}

static void test_partial_and_wrong_order_do_not_trigger(void)
{
    struct nb_session_emergency_chord chord;

    nb_session_emergency_chord_reset(&chord);
    CHECK(!apply_key(&chord, "LCTL", true, false));
    CHECK(!apply_key(&chord, "BKSP", true, false));
    CHECK(!apply_key(&chord, "BKSP", false, false));
    CHECK(!apply_key(&chord, "LCTL", false, false));
    CHECK(!apply_key(&chord, "LALT", true, false));
    CHECK(!apply_key(&chord, "BKSP", true, false));

    nb_session_emergency_chord_reset(&chord);
    CHECK(!apply_key(&chord, "BKSP", true, false));
    CHECK(!apply_key(&chord, "LCTL", true, false));
    CHECK(!apply_key(&chord, "LALT", true, false));
    CHECK(!apply_key(&chord, "BKSP", true, true));
    CHECK(!apply_key(&chord, "BKSP", false, false));
    CHECK(apply_key(&chord, "BKSP", true, false));
}

static void test_release_and_reset_clear_state(void)
{
    struct nb_session_emergency_chord chord;

    nb_session_emergency_chord_reset(&chord);
    CHECK(!apply_key(&chord, "LCTL", true, false));
    CHECK(!apply_key(&chord, "LALT", true, false));
    CHECK(!apply_key(&chord, "LCTL", false, false));
    CHECK(!apply_key(&chord, "BKSP", true, false));

    nb_session_emergency_chord_reset(&chord);
    CHECK(!apply_key(&chord, "BKSP", true, false));
}

static void test_unrelated_and_invalid_events(void)
{
    struct nb_session_emergency_chord chord;
    struct nb_host_event event;

    nb_session_emergency_chord_reset(&chord);
    CHECK(!apply_key(&chord, "LCTL", true, false));
    CHECK(!apply_key(&chord, "LALT", true, false));
    CHECK(!apply_key(&chord, "AC01", true, false));

    memset(&event, 0, sizeof(event));
    event.type = NB_HOST_EVENT_POINTER_MOTION;
    CHECK(!nb_session_emergency_chord_apply(&chord, &event));
    CHECK(!nb_session_emergency_chord_apply(NULL, &event));
    CHECK(!nb_session_emergency_chord_apply(&chord, NULL));
}

int main(void)
{
    test_left_modifiers_trigger_once();
    test_right_and_mixed_modifiers();
    test_partial_and_wrong_order_do_not_trigger();
    test_release_and_reset_clear_state();
    test_unrelated_and_invalid_events();

    if (failures != 0) {
        fprintf(stderr,
                "session emergency chord tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("session emergency chord tests: ok");
    return 0;
}
