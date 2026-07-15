#include "wsdisplay_console_session_internal.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum {
    FAKE_STATUS_FD = 10,
    FAKE_SCREEN_FD = 20,
    FAKE_EMULATION_MODE = 7,
    FAKE_AUTOMATIC_VT_MODE = 0
};

enum fake_action {
    FAKE_OPEN_STATUS,
    FAKE_OPEN_SCREEN_READ,
    FAKE_OPEN_SCREEN_WRITE,
    FAKE_CLOSE_STATUS,
    FAKE_CLOSE_SCREEN,
    FAKE_INSPECT_STATUS,
    FAKE_INSPECT_SCREEN,
    FAKE_GET_ACTIVE_SCREEN,
    FAKE_GET_DISPLAY_MODE,
    FAKE_SET_DISPLAY_MODE,
    FAKE_GET_VIDEO,
    FAKE_SET_VIDEO,
    FAKE_GET_VT_MODE,
    FAKE_SET_VT_MODE,
    FAKE_GET_ACTIVE_VT,
    FAKE_ACTIVATE_VT,
    FAKE_SLEEP,
    FAKE_ACTION_COUNT
};

struct fake_console {
    unsigned int action_calls[FAKE_ACTION_COUNT];
    enum fake_action failure_action;
    unsigned int failure_occurrence;
    int failure_error;
    char log[1024];
    size_t log_length;

    int active_screen;
    int active_vt;
    unsigned int display_mode;
    unsigned int video;
    struct nb_wsdisplay_console_vt_mode vt_mode;

    unsigned int write_busy_failures;
    unsigned int activation_polls_remaining;
    int activation_target;
    int last_activated_vt;
    uint64_t milliseconds;
    uint64_t slept_milliseconds;
    bool activation_pending;
    bool never_activate;
    bool status_character;
    bool screen_character;
    bool status_open;
    bool screen_open;
    bool ignore_display_set;
    bool ignore_video_set;
    bool ignore_vt_set;
};

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                  \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static const char *const status_path = "/dev/status";
static const char *const screen_prefix = "/dev/screen";
static const char *const screen_path = "/dev/screen0";

static char action_code(enum fake_action action)
{
    static const char codes[FAKE_ACTION_COUNT] = {
        'O', 'R', 'W', 'o', 'w', 'I', 'i', 'A', 'M',
        'm', 'V', 'v', 'T', 't', 'X', 'x', 's'
    };

    return codes[action];
}

static void record_action(struct fake_console *fake,
                          enum fake_action action)
{
    ++fake->action_calls[action];
    if (fake->log_length + 1U < sizeof(fake->log)) {
        fake->log[fake->log_length++] = action_code(action);
        fake->log[fake->log_length] = '\0';
    }
}

static int action_result(struct fake_console *fake,
                         enum fake_action action)
{
    record_action(fake, action);
    if (fake->failure_action == action &&
        fake->failure_occurrence == fake->action_calls[action]) {
        return -fake->failure_error;
    }
    return 0;
}

static enum fake_action inspect_action(int descriptor)
{
    return descriptor == FAKE_STATUS_FD
               ? FAKE_INSPECT_STATUS
               : FAKE_INSPECT_SCREEN;
}

static int fake_open_device(void *opaque,
                            const char *path,
                            bool writable)
{
    struct fake_console *fake = opaque;
    enum fake_action action;
    int result;

    if (strcmp(path, status_path) == 0) {
        action = FAKE_OPEN_STATUS;
        CHECK(!writable);
    } else {
        CHECK(strcmp(path, screen_path) == 0);
        action = writable ? FAKE_OPEN_SCREEN_WRITE
                          : FAKE_OPEN_SCREEN_READ;
    }
    result = action_result(fake, action);
    if (result != 0) {
        return result;
    }
    if (action == FAKE_OPEN_SCREEN_WRITE &&
        fake->write_busy_failures != 0U) {
        --fake->write_busy_failures;
        return -EBUSY;
    }
    if (action == FAKE_OPEN_STATUS) {
        CHECK(!fake->status_open);
        fake->status_open = true;
        return FAKE_STATUS_FD;
    }
    CHECK(!fake->screen_open);
    fake->screen_open = true;
    return FAKE_SCREEN_FD;
}

static int fake_close_device(void *opaque, int descriptor)
{
    struct fake_console *fake = opaque;
    enum fake_action action;

    if (descriptor == FAKE_STATUS_FD) {
        action = FAKE_CLOSE_STATUS;
        CHECK(fake->status_open);
        fake->status_open = false;
    } else {
        action = FAKE_CLOSE_SCREEN;
        CHECK(descriptor == FAKE_SCREEN_FD);
        CHECK(fake->screen_open);
        fake->screen_open = false;
    }
    return action_result(fake, action);
}

static int fake_inspect_character_device(void *opaque,
                                         int descriptor,
                                         bool *is_character)
{
    struct fake_console *fake = opaque;
    const enum fake_action action = inspect_action(descriptor);
    const int result = action_result(fake, action);

    if (result == 0) {
        *is_character = descriptor == FAKE_STATUS_FD
                            ? fake->status_character
                            : fake->screen_character;
    }
    return result;
}

static int fake_get_active_screen(void *opaque,
                                  int descriptor,
                                  int *active_screen)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_GET_ACTIVE_SCREEN);

    CHECK(descriptor == FAKE_STATUS_FD);
    if (result != 0) {
        return result;
    }
    if (fake->activation_pending && !fake->never_activate) {
        if (fake->activation_polls_remaining > 0U) {
            --fake->activation_polls_remaining;
        }
        if (fake->activation_polls_remaining == 0U) {
            fake->active_screen = fake->activation_target;
            fake->activation_pending = false;
        }
    }
    *active_screen = fake->active_screen;
    return 0;
}

static int fake_get_display_mode(void *opaque,
                                 int descriptor,
                                 unsigned int *mode)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_GET_DISPLAY_MODE);

    CHECK(descriptor == FAKE_SCREEN_FD);
    if (result == 0) {
        *mode = fake->display_mode;
    }
    return result;
}

static int fake_set_display_mode(void *opaque,
                                 int descriptor,
                                 unsigned int mode)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_SET_DISPLAY_MODE);

    CHECK(descriptor == FAKE_SCREEN_FD);
    if (result == 0 && !fake->ignore_display_set) {
        fake->display_mode = mode;
    }
    return result;
}

static int fake_get_video(void *opaque,
                          int descriptor,
                          unsigned int *video)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_GET_VIDEO);

    CHECK(descriptor == FAKE_SCREEN_FD);
    if (result == 0) {
        *video = fake->video;
    }
    return result;
}

static int fake_set_video(void *opaque,
                          int descriptor,
                          unsigned int video)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_SET_VIDEO);

    CHECK(descriptor == FAKE_SCREEN_FD);
    if (result == 0 && !fake->ignore_video_set) {
        fake->video = video;
    }
    return result;
}

static int fake_get_vt_mode(
    void *opaque,
    int descriptor,
    struct nb_wsdisplay_console_vt_mode *mode)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_GET_VT_MODE);

    CHECK(descriptor == FAKE_SCREEN_FD);
    if (result == 0) {
        *mode = fake->vt_mode;
    }
    return result;
}

static int fake_set_vt_mode(
    void *opaque,
    int descriptor,
    const struct nb_wsdisplay_console_vt_mode *mode)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_SET_VT_MODE);

    CHECK(descriptor == FAKE_SCREEN_FD);
    if (result == 0 && !fake->ignore_vt_set) {
        fake->vt_mode = *mode;
    }
    return result;
}

static int fake_get_active_vt(void *opaque,
                              int descriptor,
                              int *active_vt)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_GET_ACTIVE_VT);

    CHECK(descriptor == FAKE_SCREEN_FD);
    if (result == 0) {
        *active_vt = fake->active_vt;
    }
    return result;
}

static int fake_activate_vt(void *opaque,
                            int descriptor,
                            int vt_number)
{
    struct fake_console *fake = opaque;
    const int result = action_result(fake, FAKE_ACTIVATE_VT);

    CHECK(descriptor == FAKE_SCREEN_FD);
    fake->last_activated_vt = vt_number;
    if (result == 0) {
        fake->activation_target = vt_number - 1;
        fake->activation_pending = true;
    }
    return result;
}

static uint64_t fake_monotonic_milliseconds(void *opaque)
{
    const struct fake_console *fake = opaque;

    return fake->milliseconds;
}

static void fake_sleep_milliseconds(void *opaque,
                                    unsigned int milliseconds)
{
    struct fake_console *fake = opaque;

    record_action(fake, FAKE_SLEEP);
    fake->milliseconds += milliseconds;
    fake->slept_milliseconds += milliseconds;
}

static const struct nb_wsdisplay_console_operations fake_operations = {
    .emulation_display_mode = FAKE_EMULATION_MODE,
    .automatic_vt_mode = FAKE_AUTOMATIC_VT_MODE,
    .open_device = fake_open_device,
    .close_device = fake_close_device,
    .inspect_character_device = fake_inspect_character_device,
    .get_active_screen = fake_get_active_screen,
    .get_display_mode = fake_get_display_mode,
    .set_display_mode = fake_set_display_mode,
    .get_video = fake_get_video,
    .set_video = fake_set_video,
    .get_vt_mode = fake_get_vt_mode,
    .set_vt_mode = fake_set_vt_mode,
    .get_active_vt = fake_get_active_vt,
    .activate_vt = fake_activate_vt,
    .monotonic_milliseconds = fake_monotonic_milliseconds,
    .sleep_milliseconds = fake_sleep_milliseconds
};

static void initialize_fake(struct fake_console *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->failure_action = FAKE_ACTION_COUNT;
    fake->failure_occurrence = 1;
    fake->failure_error = EIO;
    fake->active_screen = 0;
    fake->active_vt = 1;
    fake->display_mode = FAKE_EMULATION_MODE;
    fake->video = 1;
    fake->vt_mode.mode = FAKE_AUTOMATIC_VT_MODE;
    fake->vt_mode.waitv = 2;
    fake->vt_mode.relsig = 10;
    fake->vt_mode.acqsig = 12;
    fake->vt_mode.frsig = 14;
    fake->status_character = true;
    fake->screen_character = true;
}

static struct nb_wsdisplay_console_capture_options capture_options(void)
{
    const struct nb_wsdisplay_console_capture_options options = {
        .status_device_path = status_path,
        .screen_device_prefix = screen_prefix
    };

    return options;
}

static struct nb_wsdisplay_console_state saved_state(void)
{
    struct nb_wsdisplay_console_state state;

    memset(&state, 0, sizeof(state));
    state.active_screen = 0;
    state.display_mode = FAKE_EMULATION_MODE;
    state.video = 1;
    state.video_available = true;
    state.vt_mode.mode = FAKE_AUTOMATIC_VT_MODE;
    state.vt_mode.waitv = 2;
    state.vt_mode.relsig = 10;
    state.vt_mode.acqsig = 12;
    state.vt_mode.frsig = 14;
    (void)snprintf(state.status_device,
                   sizeof(state.status_device),
                   "%s",
                   status_path);
    (void)snprintf(state.screen_device,
                   sizeof(state.screen_device),
                   "%s",
                   screen_path);
    return state;
}

static bool capture(struct fake_console *fake,
                    struct nb_wsdisplay_console_state *state)
{
    const struct nb_wsdisplay_console_capture_options options =
        capture_options();

    return nb_wsdisplay_console_capture_with_operations(
        &options, state, &fake_operations, fake, NULL);
}

static bool restore(struct fake_console *fake,
                    const struct nb_wsdisplay_console_state *state)
{
    return nb_wsdisplay_console_restore_with_operations(
        state, &fake_operations, fake, NULL);
}

static void check_closed(const struct fake_console *fake)
{
    CHECK(!fake->status_open);
    CHECK(!fake->screen_open);
}

static void test_capture_success_and_unsupported_video(void)
{
    struct nb_wsdisplay_console_state state;
    struct fake_console fake;

    initialize_fake(&fake);
    CHECK(capture(&fake, &state));
    CHECK(strcmp(fake.log, "OIARiMTXVwo") == 0);
    CHECK(state.active_screen == 0);
    CHECK(state.display_mode == FAKE_EMULATION_MODE);
    CHECK(state.video_available);
    CHECK(state.video == 1U);
    CHECK(state.vt_mode.waitv == 2);
    CHECK(strcmp(state.status_device, status_path) == 0);
    CHECK(strcmp(state.screen_device, screen_path) == 0);
    check_closed(&fake);

    initialize_fake(&fake);
    fake.failure_action = FAKE_GET_VIDEO;
    fake.failure_error = ENOTTY;
    CHECK(capture(&fake, &state));
    CHECK(!state.video_available);
    check_closed(&fake);
}

static void test_capture_validation_and_race_rejections(void)
{
    struct nb_wsdisplay_console_capture_options options = capture_options();
    struct nb_wsdisplay_console_state state;
    struct fake_console fake;
    char long_prefix[NB_WSDISPLAY_CONSOLE_PATH_CAPACITY];
    char long_status[NB_WSDISPLAY_CONSOLE_PATH_CAPACITY + 1U];

    initialize_fake(&fake);
    fake.active_screen = -1;
    CHECK(!capture(&fake, &state));
    CHECK(fake.action_calls[FAKE_OPEN_SCREEN_READ] == 0U);
    check_closed(&fake);

    initialize_fake(&fake);
    fake.active_screen = 256;
    CHECK(!capture(&fake, &state));
    CHECK(fake.action_calls[FAKE_OPEN_SCREEN_READ] == 0U);
    check_closed(&fake);

    initialize_fake(&fake);
    fake.display_mode = FAKE_EMULATION_MODE + 1U;
    CHECK(!capture(&fake, &state));
    check_closed(&fake);

    initialize_fake(&fake);
    fake.vt_mode.mode = FAKE_AUTOMATIC_VT_MODE + 1;
    CHECK(!capture(&fake, &state));
    check_closed(&fake);

    initialize_fake(&fake);
    fake.active_vt = 2;
    CHECK(!capture(&fake, &state));
    check_closed(&fake);

    memset(long_prefix, 'a', sizeof(long_prefix));
    long_prefix[0] = '/';
    long_prefix[sizeof(long_prefix) - 1U] = '\0';
    options.screen_device_prefix = long_prefix;
    initialize_fake(&fake);
    CHECK(!nb_wsdisplay_console_capture_with_operations(
        &options, &state, &fake_operations, &fake, NULL));
    CHECK(fake.action_calls[FAKE_OPEN_SCREEN_READ] == 0U);
    check_closed(&fake);

    memset(long_status, 'a', sizeof(long_status));
    long_status[0] = '/';
    long_status[sizeof(long_status) - 1U] = '\0';
    options = capture_options();
    options.status_device_path = long_status;
    initialize_fake(&fake);
    CHECK(!nb_wsdisplay_console_capture_with_operations(
        &options, &state, &fake_operations, &fake, NULL));
    CHECK(fake.log[0] == '\0');
    check_closed(&fake);
}

static void test_capture_failure_cleanup(void)
{
    static const enum fake_action actions[] = {
        FAKE_OPEN_STATUS,
        FAKE_INSPECT_STATUS,
        FAKE_GET_ACTIVE_SCREEN,
        FAKE_OPEN_SCREEN_READ,
        FAKE_INSPECT_SCREEN,
        FAKE_GET_DISPLAY_MODE,
        FAKE_GET_VT_MODE,
        FAKE_GET_ACTIVE_VT,
        FAKE_GET_VIDEO
    };
    struct nb_wsdisplay_console_state state;
    struct fake_console fake;
    size_t index;

    for (index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index) {
        initialize_fake(&fake);
        fake.failure_action = actions[index];
        CHECK(!capture(&fake, &state));
        check_closed(&fake);
    }

    initialize_fake(&fake);
    fake.status_character = false;
    CHECK(!capture(&fake, &state));
    check_closed(&fake);

    initialize_fake(&fake);
    fake.screen_character = false;
    CHECK(!capture(&fake, &state));
    check_closed(&fake);
}

static void test_capture_defensive_arguments(void)
{
    struct nb_wsdisplay_console_capture_options options = capture_options();
    struct nb_wsdisplay_console_operations operations = fake_operations;
    struct nb_wsdisplay_console_state state;
    struct fake_console fake;

    initialize_fake(&fake);
    CHECK(!nb_wsdisplay_console_capture_with_operations(
        NULL, &state, &operations, &fake, NULL));
    CHECK(!nb_wsdisplay_console_capture_with_operations(
        &options, NULL, &operations, &fake, NULL));
    options.status_device_path = NULL;
    CHECK(!nb_wsdisplay_console_capture_with_operations(
        &options, &state, &operations, &fake, NULL));
    options = capture_options();
    operations.get_video = NULL;
    CHECK(!nb_wsdisplay_console_capture_with_operations(
        &options, &state, &operations, &fake, NULL));
    CHECK(fake.log[0] == '\0');
}

static void test_restore_success_and_video_skip(void)
{
    struct nb_wsdisplay_console_state state = saved_state();
    struct fake_console fake;

    initialize_fake(&fake);
    CHECK(restore(&fake, &state));
    CHECK(strcmp(fake.log, "WimvtOIAMVTAow") == 0);
    check_closed(&fake);

    state.video_available = false;
    initialize_fake(&fake);
    CHECK(restore(&fake, &state));
    CHECK(fake.action_calls[FAKE_SET_VIDEO] == 0U);
    CHECK(fake.action_calls[FAKE_GET_VIDEO] == 0U);
    check_closed(&fake);
}

static void test_restore_reactivates_saved_vt(void)
{
    const struct nb_wsdisplay_console_state state = saved_state();
    struct fake_console fake;

    initialize_fake(&fake);
    fake.active_screen = 1;
    fake.activation_polls_remaining = 2;
    CHECK(restore(&fake, &state));
    CHECK(fake.last_activated_vt == 1);
    CHECK(fake.action_calls[FAKE_ACTIVATE_VT] == 1U);
    CHECK(fake.slept_milliseconds == 40U);
    CHECK(fake.active_screen == 0);
    check_closed(&fake);
}

static void test_restore_open_retry_boundaries(void)
{
    const struct nb_wsdisplay_console_state state = saved_state();
    struct fake_console fake;

    initialize_fake(&fake);
    fake.write_busy_failures = 19;
    CHECK(restore(&fake, &state));
    CHECK(fake.action_calls[FAKE_OPEN_SCREEN_WRITE] == 20U);
    CHECK(fake.slept_milliseconds == 950U);
    check_closed(&fake);

    initialize_fake(&fake);
    fake.write_busy_failures = 20;
    CHECK(!restore(&fake, &state));
    CHECK(fake.action_calls[FAKE_OPEN_SCREEN_WRITE] == 20U);
    CHECK(fake.slept_milliseconds == 1000U);
    check_closed(&fake);

    initialize_fake(&fake);
    fake.failure_action = FAKE_OPEN_SCREEN_WRITE;
    fake.failure_error = EACCES;
    CHECK(!restore(&fake, &state));
    CHECK(fake.action_calls[FAKE_OPEN_SCREEN_WRITE] == 1U);
    CHECK(fake.slept_milliseconds == 0U);
    check_closed(&fake);
}

static void test_restore_mutation_failures_continue(void)
{
    static const enum fake_action actions[] = {
        FAKE_SET_DISPLAY_MODE,
        FAKE_SET_VIDEO,
        FAKE_SET_VT_MODE
    };
    const struct nb_wsdisplay_console_state state = saved_state();
    struct fake_console fake;
    size_t index;

    for (index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index) {
        initialize_fake(&fake);
        fake.failure_action = actions[index];
        CHECK(!restore(&fake, &state));
        CHECK(fake.action_calls[FAKE_SET_VT_MODE] != 0U);
        CHECK(fake.action_calls[FAKE_GET_ACTIVE_SCREEN] != 0U);
        CHECK(fake.action_calls[FAKE_GET_DISPLAY_MODE] != 0U);
        check_closed(&fake);
    }

    initialize_fake(&fake);
    fake.failure_action = FAKE_SET_DISPLAY_MODE;
    fake.failure_error = EINTR;
    CHECK(restore(&fake, &state));
    CHECK(fake.action_calls[FAKE_SET_DISPLAY_MODE] == 2U);
    check_closed(&fake);
}

static void test_restore_query_and_value_failures(void)
{
    static const enum fake_action actions[] = {
        FAKE_GET_DISPLAY_MODE,
        FAKE_GET_VIDEO,
        FAKE_GET_VT_MODE
    };
    const struct nb_wsdisplay_console_state state = saved_state();
    struct fake_console fake;
    size_t index;

    for (index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index) {
        initialize_fake(&fake);
        fake.failure_action = actions[index];
        CHECK(!restore(&fake, &state));
        check_closed(&fake);
    }

    initialize_fake(&fake);
    fake.failure_action = FAKE_GET_ACTIVE_SCREEN;
    fake.failure_occurrence = 2;
    CHECK(!restore(&fake, &state));
    check_closed(&fake);

    initialize_fake(&fake);
    fake.ignore_display_set = true;
    fake.display_mode = FAKE_EMULATION_MODE + 1U;
    CHECK(!restore(&fake, &state));
    check_closed(&fake);

    initialize_fake(&fake);
    fake.ignore_video_set = true;
    fake.video = 0;
    CHECK(!restore(&fake, &state));
    check_closed(&fake);

    initialize_fake(&fake);
    fake.ignore_vt_set = true;
    fake.vt_mode.waitv = 99;
    CHECK(!restore(&fake, &state));
    check_closed(&fake);
}

static void test_restore_activation_failures(void)
{
    const struct nb_wsdisplay_console_state state = saved_state();
    struct fake_console fake;

    initialize_fake(&fake);
    fake.active_screen = 1;
    fake.failure_action = FAKE_ACTIVATE_VT;
    CHECK(!restore(&fake, &state));
    check_closed(&fake);

    initialize_fake(&fake);
    fake.active_screen = 1;
    fake.never_activate = true;
    CHECK(!restore(&fake, &state));
    CHECK(fake.action_calls[FAKE_ACTIVATE_VT] == 1U);
    CHECK(fake.slept_milliseconds == 1000U);
    CHECK(fake.action_calls[FAKE_GET_ACTIVE_SCREEN] == 52U);
    check_closed(&fake);

    initialize_fake(&fake);
    fake.active_screen = 1;
    fake.activation_polls_remaining = 2;
    fake.failure_action = FAKE_GET_ACTIVE_SCREEN;
    fake.failure_occurrence = 2;
    CHECK(!restore(&fake, &state));
    check_closed(&fake);
}

static void test_restore_character_and_state_validation(void)
{
    struct nb_wsdisplay_console_state state = saved_state();
    struct nb_wsdisplay_console_operations operations = fake_operations;
    struct fake_console fake;

    initialize_fake(&fake);
    fake.screen_character = false;
    CHECK(!restore(&fake, &state));
    CHECK(fake.action_calls[FAKE_SET_DISPLAY_MODE] == 0U);
    check_closed(&fake);

    initialize_fake(&fake);
    fake.status_character = false;
    CHECK(!restore(&fake, &state));
    CHECK(fake.action_calls[FAKE_GET_DISPLAY_MODE] == 1U);
    check_closed(&fake);

    initialize_fake(&fake);
    state.active_screen = -1;
    CHECK(!restore(&fake, &state));
    CHECK(fake.log[0] == '\0');

    state = saved_state();
    state.status_device[0] = 'x';
    CHECK(!restore(&fake, &state));
    CHECK(fake.log[0] == '\0');

    state = saved_state();
    memset(state.screen_device, 'x', sizeof(state.screen_device));
    state.screen_device[0] = '/';
    CHECK(!restore(&fake, &state));
    CHECK(fake.log[0] == '\0');

    state = saved_state();
    operations.activate_vt = NULL;
    CHECK(!nb_wsdisplay_console_restore_with_operations(
        &state, &operations, &fake, NULL));
    CHECK(!nb_wsdisplay_console_restore_with_operations(
        NULL, &fake_operations, &fake, NULL));
    CHECK(fake.log[0] == '\0');
}

int main(void)
{
    test_capture_success_and_unsupported_video();
    test_capture_validation_and_race_rejections();
    test_capture_failure_cleanup();
    test_capture_defensive_arguments();
    test_restore_success_and_video_skip();
    test_restore_reactivates_saved_vt();
    test_restore_open_retry_boundaries();
    test_restore_mutation_failures_continue();
    test_restore_query_and_value_failures();
    test_restore_activation_failures();
    test_restore_character_and_state_validation();

    if (failures != 0) {
        fprintf(stderr,
                "wsdisplay console-session tests: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("wsdisplay console-session tests: ok");
    return 0;
}
