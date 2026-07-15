#include "host_sdl.h"

#include <SDL3/SDL.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_backend.h"

enum {
    NB_HOST_SDL_EVENT_CAPACITY = 64,
    NB_HOST_SDL_ERROR_CAPACITY = 256,
    NB_HOST_SDL_DEFAULT_WIDTH = 1024,
    NB_HOST_SDL_DEFAULT_HEIGHT = 640,
    NB_HOST_SDL_DEFAULT_MINIMUM_WIDTH = 640,
    NB_HOST_SDL_DEFAULT_MINIMUM_HEIGHT = 400
};

struct nb_host_sdl_context {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_WindowID window_id;
    SDL_PixelFormat texture_format;
    int texture_width;
    int texture_height;
    struct nb_host_output output;
    struct nb_host_event events[NB_HOST_SDL_EVENT_CAPACITY];
    size_t event_head;
    size_t event_count;
    enum nb_host_state state;
    int system_error;
    char error[NB_HOST_SDL_ERROR_CAPACITY];
    bool failure_event_pending;
    bool video_initialized;
};

enum translated_event_status {
    TRANSLATED_EVENT_ERROR = -1,
    TRANSLATED_EVENT_IGNORED,
    TRANSLATED_EVENT_READY
};

static char creation_error[NB_HOST_SDL_ERROR_CAPACITY];

static const struct nb_host_backend_operations sdl_operations;

static void copy_error(char destination[NB_HOST_SDL_ERROR_CAPACITY],
                       const char *operation,
                       const char *detail)
{
    if (detail == NULL || detail[0] == '\0') {
        (void)snprintf(destination,
                       NB_HOST_SDL_ERROR_CAPACITY,
                       "%s",
                       operation);
    } else {
        (void)snprintf(destination,
                       NB_HOST_SDL_ERROR_CAPACITY,
                       "%s: %s",
                       operation,
                       detail);
    }
}

static void remember_sdl_error(struct nb_host_sdl_context *context,
                               const char *operation)
{
    copy_error(context->error, operation, SDL_GetError());
    context->system_error = 0;
}

static void fail_sdl_host(struct nb_host_sdl_context *context,
                          const char *operation)
{
    if (context->state == NB_HOST_STATE_FAILED) {
        return;
    }
    remember_sdl_error(context, operation);
    context->state = NB_HOST_STATE_FAILED;
    context->failure_event_pending = true;
}

static void mark_sdl_host_failed(struct nb_host_sdl_context *context)
{
    context->state = NB_HOST_STATE_FAILED;
    context->failure_event_pending = true;
}

static bool queue_event(struct nb_host_sdl_context *context,
                        const struct nb_host_event *event)
{
    size_t tail;

    if (context->event_count >= NB_HOST_SDL_EVENT_CAPACITY) {
        return false;
    }
    tail = (context->event_head + context->event_count) %
           NB_HOST_SDL_EVENT_CAPACITY;
    context->events[tail] = *event;
    ++context->event_count;
    return true;
}

static struct nb_host_event *reserve_event(
    struct nb_host_sdl_context *context)
{
    size_t tail;

    if (context->event_count >= NB_HOST_SDL_EVENT_CAPACITY) {
        return NULL;
    }
    tail = (context->event_head + context->event_count) %
           NB_HOST_SDL_EVENT_CAPACITY;
    ++context->event_count;
    return &context->events[tail];
}

static void cancel_event_reservation(struct nb_host_sdl_context *context)
{
    if (context->event_count > 0) {
        --context->event_count;
    }
}

static enum nb_host_event_status pop_internal_event(
    struct nb_host_sdl_context *context,
    struct nb_host_event *event)
{
    if (context->failure_event_pending) {
        memset(event, 0, sizeof(*event));
        event->type = NB_HOST_EVENT_FAILED;
        event->milliseconds = SDL_GetTicks();
        event->data.failed.system_error = context->system_error;
        context->failure_event_pending = false;
        return NB_HOST_EVENT_STATUS_AVAILABLE;
    }
    if (context->event_count == 0) {
        memset(event, 0, sizeof(*event));
        return context->state == NB_HOST_STATE_FAILED
                   ? NB_HOST_EVENT_STATUS_ERROR
                   : NB_HOST_EVENT_STATUS_EMPTY;
    }
    *event = context->events[context->event_head];
    context->event_head = (context->event_head + 1) %
                          NB_HOST_SDL_EVENT_CAPACITY;
    --context->event_count;
    return NB_HOST_EVENT_STATUS_AVAILABLE;
}

static int refresh_millihertz(SDL_Window *window)
{
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    const SDL_DisplayMode *mode;
    int refresh = 0;

    if (display == 0) {
        (void)SDL_ClearError();
        return 0;
    }
    mode = SDL_GetCurrentDisplayMode(display);
    if (mode == NULL) {
        (void)SDL_ClearError();
        return 0;
    }

    if (mode->refresh_rate_numerator > 0 &&
        mode->refresh_rate_denominator > 0) {
        const int64_t numerator =
            (int64_t)mode->refresh_rate_numerator * INT64_C(1000);
        const int64_t rounded =
            numerator + ((int64_t)mode->refresh_rate_denominator / 2);
        const int64_t value =
            rounded / (int64_t)mode->refresh_rate_denominator;

        if (value > 0 && value <= INT_MAX) {
            refresh = (int)value;
        }
    } else if (mode->refresh_rate > 0.0f) {
        const double value = (double)mode->refresh_rate * 1000.0;

        if (value <= (double)INT_MAX) {
            refresh = (int)(value + 0.5);
        }
    }
    return refresh;
}

static bool read_output(struct nb_host_sdl_context *context,
                        struct nb_host_output *output)
{
    int width;
    int height;

    if (!SDL_GetCurrentRenderOutputSize(context->renderer,
                                        &width,
                                        &height) ||
        width <= 0 || height <= 0) {
        return false;
    }
    output->logical_width = width;
    output->logical_height = height;
    output->pixel_width = width;
    output->pixel_height = height;
    output->refresh_millihertz = refresh_millihertz(context->window);
    return true;
}

static bool outputs_equal(const struct nb_host_output *left,
                          const struct nb_host_output *right)
{
    return left->logical_width == right->logical_width &&
           left->logical_height == right->logical_height &&
           left->pixel_width == right->pixel_width &&
           left->pixel_height == right->pixel_height &&
           left->refresh_millihertz == right->refresh_millihertz;
}

static bool update_output(struct nb_host_sdl_context *context,
                          uint64_t milliseconds,
                          struct nb_host_event *event,
                          bool *changed)
{
    struct nb_host_output output;

    *changed = false;
    if (!read_output(context, &output)) {
        fail_sdl_host(context, "Could not query SDL render output");
        return false;
    }
    if (outputs_equal(&context->output, &output)) {
        return true;
    }
    context->output = output;
    memset(event, 0, sizeof(*event));
    event->type = NB_HOST_EVENT_OUTPUT_CHANGED;
    event->milliseconds = milliseconds;
    event->data.output = output;
    *changed = true;
    return true;
}

static int logical_coordinate(float coordinate)
{
    int truncated;

    if (coordinate >= (float)INT_MAX) {
        return INT_MAX;
    }
    if (coordinate <= (float)INT_MIN) {
        return INT_MIN;
    }
    truncated = (int)coordinate;
    return coordinate < (float)truncated ? truncated - 1 : truncated;
}

static bool pointer_button_for_sdl(
    Uint8 sdl_button,
    enum nb_host_pointer_button *host_button)
{
    if (sdl_button == SDL_BUTTON_LEFT) {
        *host_button = NB_HOST_POINTER_BUTTON_LEFT;
    } else if (sdl_button == SDL_BUTTON_MIDDLE) {
        *host_button = NB_HOST_POINTER_BUTTON_MIDDLE;
    } else if (sdl_button == SDL_BUTTON_RIGHT) {
        *host_button = NB_HOST_POINTER_BUTTON_RIGHT;
    } else if (sdl_button == SDL_BUTTON_X1) {
        *host_button = NB_HOST_POINTER_BUTTON_SIDE;
    } else if (sdl_button == SDL_BUTTON_X2) {
        *host_button = NB_HOST_POINTER_BUTTON_EXTRA;
    } else {
        return false;
    }
    return true;
}

static const char *xkb_key_name_for_sdl(SDL_Scancode scancode)
{
    static const char *const key_names[SDL_SCANCODE_COUNT] = {
        [SDL_SCANCODE_A] = "AC01",
        [SDL_SCANCODE_B] = "AB05",
        [SDL_SCANCODE_C] = "AB03",
        [SDL_SCANCODE_D] = "AC03",
        [SDL_SCANCODE_E] = "AD03",
        [SDL_SCANCODE_F] = "AC04",
        [SDL_SCANCODE_G] = "AC05",
        [SDL_SCANCODE_H] = "AC06",
        [SDL_SCANCODE_I] = "AD08",
        [SDL_SCANCODE_J] = "AC07",
        [SDL_SCANCODE_K] = "AC08",
        [SDL_SCANCODE_L] = "AC09",
        [SDL_SCANCODE_M] = "AB07",
        [SDL_SCANCODE_N] = "AB06",
        [SDL_SCANCODE_O] = "AD09",
        [SDL_SCANCODE_P] = "AD10",
        [SDL_SCANCODE_Q] = "AD01",
        [SDL_SCANCODE_R] = "AD04",
        [SDL_SCANCODE_S] = "AC02",
        [SDL_SCANCODE_T] = "AD05",
        [SDL_SCANCODE_U] = "AD07",
        [SDL_SCANCODE_V] = "AB04",
        [SDL_SCANCODE_W] = "AD02",
        [SDL_SCANCODE_X] = "AB02",
        [SDL_SCANCODE_Y] = "AD06",
        [SDL_SCANCODE_Z] = "AB01",
        [SDL_SCANCODE_1] = "AE01",
        [SDL_SCANCODE_2] = "AE02",
        [SDL_SCANCODE_3] = "AE03",
        [SDL_SCANCODE_4] = "AE04",
        [SDL_SCANCODE_5] = "AE05",
        [SDL_SCANCODE_6] = "AE06",
        [SDL_SCANCODE_7] = "AE07",
        [SDL_SCANCODE_8] = "AE08",
        [SDL_SCANCODE_9] = "AE09",
        [SDL_SCANCODE_0] = "AE10",
        [SDL_SCANCODE_RETURN] = "RTRN",
        [SDL_SCANCODE_ESCAPE] = "ESC",
        [SDL_SCANCODE_BACKSPACE] = "BKSP",
        [SDL_SCANCODE_TAB] = "TAB",
        [SDL_SCANCODE_SPACE] = "SPCE",
        [SDL_SCANCODE_MINUS] = "AE11",
        [SDL_SCANCODE_EQUALS] = "AE12",
        [SDL_SCANCODE_LEFTBRACKET] = "AD11",
        [SDL_SCANCODE_RIGHTBRACKET] = "AD12",
        [SDL_SCANCODE_BACKSLASH] = "BKSL",
        [SDL_SCANCODE_NONUSHASH] = "BKSL",
        [SDL_SCANCODE_SEMICOLON] = "AC10",
        [SDL_SCANCODE_APOSTROPHE] = "AC11",
        [SDL_SCANCODE_GRAVE] = "TLDE",
        [SDL_SCANCODE_COMMA] = "AB08",
        [SDL_SCANCODE_PERIOD] = "AB09",
        [SDL_SCANCODE_SLASH] = "AB10",
        [SDL_SCANCODE_CAPSLOCK] = "CAPS",
        [SDL_SCANCODE_F1] = "FK01",
        [SDL_SCANCODE_F2] = "FK02",
        [SDL_SCANCODE_F3] = "FK03",
        [SDL_SCANCODE_F4] = "FK04",
        [SDL_SCANCODE_F5] = "FK05",
        [SDL_SCANCODE_F6] = "FK06",
        [SDL_SCANCODE_F7] = "FK07",
        [SDL_SCANCODE_F8] = "FK08",
        [SDL_SCANCODE_F9] = "FK09",
        [SDL_SCANCODE_F10] = "FK10",
        [SDL_SCANCODE_F11] = "FK11",
        [SDL_SCANCODE_F12] = "FK12",
        [SDL_SCANCODE_PRINTSCREEN] = "PRSC",
        [SDL_SCANCODE_SCROLLLOCK] = "SCLK",
        [SDL_SCANCODE_PAUSE] = "PAUS",
        [SDL_SCANCODE_INSERT] = "INS",
        [SDL_SCANCODE_HOME] = "HOME",
        [SDL_SCANCODE_PAGEUP] = "PGUP",
        [SDL_SCANCODE_DELETE] = "DELE",
        [SDL_SCANCODE_END] = "END",
        [SDL_SCANCODE_PAGEDOWN] = "PGDN",
        [SDL_SCANCODE_RIGHT] = "RGHT",
        [SDL_SCANCODE_LEFT] = "LEFT",
        [SDL_SCANCODE_DOWN] = "DOWN",
        [SDL_SCANCODE_UP] = "UP",
        [SDL_SCANCODE_NUMLOCKCLEAR] = "NMLK",
        [SDL_SCANCODE_KP_DIVIDE] = "KPDV",
        [SDL_SCANCODE_KP_MULTIPLY] = "KPMU",
        [SDL_SCANCODE_KP_MINUS] = "KPSU",
        [SDL_SCANCODE_KP_PLUS] = "KPAD",
        [SDL_SCANCODE_KP_ENTER] = "KPEN",
        [SDL_SCANCODE_KP_1] = "KP1",
        [SDL_SCANCODE_KP_2] = "KP2",
        [SDL_SCANCODE_KP_3] = "KP3",
        [SDL_SCANCODE_KP_4] = "KP4",
        [SDL_SCANCODE_KP_5] = "KP5",
        [SDL_SCANCODE_KP_6] = "KP6",
        [SDL_SCANCODE_KP_7] = "KP7",
        [SDL_SCANCODE_KP_8] = "KP8",
        [SDL_SCANCODE_KP_9] = "KP9",
        [SDL_SCANCODE_KP_0] = "KP0",
        [SDL_SCANCODE_KP_PERIOD] = "KPDL",
        [SDL_SCANCODE_NONUSBACKSLASH] = "LSGT",
        [SDL_SCANCODE_APPLICATION] = "MENU",
        [SDL_SCANCODE_POWER] = "POWR",
        [SDL_SCANCODE_KP_EQUALS] = "KPEQ",
        [SDL_SCANCODE_F13] = "FK13",
        [SDL_SCANCODE_F14] = "FK14",
        [SDL_SCANCODE_F15] = "FK15",
        [SDL_SCANCODE_F16] = "FK16",
        [SDL_SCANCODE_F17] = "FK17",
        [SDL_SCANCODE_F18] = "FK18",
        [SDL_SCANCODE_F19] = "FK19",
        [SDL_SCANCODE_F20] = "FK20",
        [SDL_SCANCODE_F21] = "FK21",
        [SDL_SCANCODE_F22] = "FK22",
        [SDL_SCANCODE_F23] = "FK23",
        [SDL_SCANCODE_F24] = "FK24",
        [SDL_SCANCODE_LCTRL] = "LCTL",
        [SDL_SCANCODE_LSHIFT] = "LFSH",
        [SDL_SCANCODE_LALT] = "LALT",
        [SDL_SCANCODE_LGUI] = "LWIN",
        [SDL_SCANCODE_RCTRL] = "RCTL",
        [SDL_SCANCODE_RSHIFT] = "RTSH",
        [SDL_SCANCODE_RALT] = "RALT",
        [SDL_SCANCODE_RGUI] = "RWIN",
        [SDL_SCANCODE_MODE] = "RALT"
    };

    if (scancode <= SDL_SCANCODE_UNKNOWN ||
        scancode >= SDL_SCANCODE_COUNT) {
        return NULL;
    }
    return key_names[scancode];
}

static bool event_window_is_ours(const struct nb_host_sdl_context *context,
                                 SDL_WindowID window_id)
{
    return window_id == context->window_id;
}

static enum translated_event_status translate_window_event(
    struct nb_host_sdl_context *context,
    const SDL_Event *source,
    struct nb_host_event *event)
{
    const uint64_t milliseconds = SDL_NS_TO_MS(source->window.timestamp);

    if (!event_window_is_ours(context, source->window.windowID)) {
        return TRANSLATED_EVENT_IGNORED;
    }
    if (source->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
        source->type == SDL_EVENT_WINDOW_DESTROYED) {
        memset(event, 0, sizeof(*event));
        event->type = NB_HOST_EVENT_QUIT;
        event->milliseconds = milliseconds;
        return TRANSLATED_EVENT_READY;
    }
    if (source->type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
        source->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        memset(event, 0, sizeof(*event));
        event->type = NB_HOST_EVENT_FOCUS_CHANGED;
        event->milliseconds = milliseconds;
        event->data.focus.focused =
            source->type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        return TRANSLATED_EVENT_READY;
    }
    if (source->type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
        memset(event, 0, sizeof(*event));
        event->type = NB_HOST_EVENT_POINTER_LEAVE;
        event->milliseconds = milliseconds;
        return TRANSLATED_EVENT_READY;
    }
    if (source->type == SDL_EVENT_WINDOW_RESIZED ||
        source->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        source->type == SDL_EVENT_WINDOW_MAXIMIZED ||
        source->type == SDL_EVENT_WINDOW_RESTORED ||
        source->type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
        source->type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
        source->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
        source->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        return TRANSLATED_EVENT_IGNORED;
    }
    return TRANSLATED_EVENT_IGNORED;
}

static enum translated_event_status translate_pointer_event(
    struct nb_host_sdl_context *context,
    const SDL_Event *source,
    struct nb_host_event *event)
{
    SDL_Event converted = *source;

    if ((source->type == SDL_EVENT_MOUSE_MOTION &&
         !event_window_is_ours(context, source->motion.windowID)) ||
        ((source->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
          source->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
         !event_window_is_ours(context, source->button.windowID))) {
        return TRANSLATED_EVENT_IGNORED;
    }
    if (!SDL_ConvertEventToRenderCoordinates(context->renderer, &converted)) {
        fail_sdl_host(context, "Could not convert SDL pointer coordinates");
        return TRANSLATED_EVENT_ERROR;
    }

    memset(event, 0, sizeof(*event));
    if (source->type == SDL_EVENT_MOUSE_MOTION) {
        event->type = NB_HOST_EVENT_POINTER_MOTION;
        event->milliseconds = SDL_NS_TO_MS(converted.motion.timestamp);
        event->data.pointer_motion.x =
            logical_coordinate(converted.motion.x);
        event->data.pointer_motion.y =
            logical_coordinate(converted.motion.y);
        return TRANSLATED_EVENT_READY;
    }

    if (!pointer_button_for_sdl(converted.button.button,
                                &event->data.pointer_button.button)) {
        return TRANSLATED_EVENT_IGNORED;
    }
    event->type = NB_HOST_EVENT_POINTER_BUTTON;
    event->milliseconds = SDL_NS_TO_MS(converted.button.timestamp);
    event->data.pointer_button.x = logical_coordinate(converted.button.x);
    event->data.pointer_button.y = logical_coordinate(converted.button.y);
    event->data.pointer_button.pressed = converted.button.down;
    return TRANSLATED_EVENT_READY;
}

static enum translated_event_status translate_key_event(
    const struct nb_host_sdl_context *context,
    const SDL_Event *source,
    struct nb_host_event *event)
{
    const char *key_name;

    if (!event_window_is_ours(context, source->key.windowID)) {
        return TRANSLATED_EVENT_IGNORED;
    }
    key_name = xkb_key_name_for_sdl(source->key.scancode);
    if (key_name == NULL) {
        return TRANSLATED_EVENT_IGNORED;
    }
    memset(event, 0, sizeof(*event));
    event->type = NB_HOST_EVENT_KEY;
    event->milliseconds = SDL_NS_TO_MS(source->key.timestamp);
    (void)snprintf(event->data.key.xkb_key_name,
                   sizeof(event->data.key.xkb_key_name),
                   "%s",
                   key_name);
    event->data.key.pressed = source->key.down;
    event->data.key.repeat = source->key.repeat;
    return TRANSLATED_EVENT_READY;
}

static enum translated_event_status translate_sdl_event(
    struct nb_host_sdl_context *context,
    const SDL_Event *source,
    struct nb_host_event *event)
{
    if (source->type == SDL_EVENT_QUIT) {
        memset(event, 0, sizeof(*event));
        event->type = NB_HOST_EVENT_QUIT;
        event->milliseconds = SDL_NS_TO_MS(source->common.timestamp);
        return TRANSLATED_EVENT_READY;
    }
    if (source->type >= SDL_EVENT_WINDOW_FIRST &&
        source->type <= SDL_EVENT_WINDOW_LAST) {
        return translate_window_event(context, source, event);
    }
    if (source->type == SDL_EVENT_MOUSE_MOTION ||
        source->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        source->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        return translate_pointer_event(context, source, event);
    }
    if (source->type == SDL_EVENT_KEY_DOWN ||
        source->type == SDL_EVENT_KEY_UP) {
        return translate_key_event(context, source, event);
    }
    return TRANSLATED_EVENT_IGNORED;
}

static bool sdl_get_output(const void *opaque, struct nb_host_output *output)
{
    const struct nb_host_sdl_context *context = opaque;

    *output = context->output;
    return context->state != NB_HOST_STATE_FAILED;
}

static enum nb_host_state sdl_get_state(const void *opaque)
{
    const struct nb_host_sdl_context *context = opaque;

    return context->state;
}

static uint64_t sdl_monotonic_milliseconds(const void *opaque)
{
    (void)opaque;
    return SDL_GetTicks();
}

static enum nb_host_event_status sdl_poll_event(
    void *opaque,
    struct nb_host_event *event)
{
    struct nb_host_sdl_context *context = opaque;
    enum nb_host_event_status internal;
    SDL_Event source;

    internal = pop_internal_event(context, event);
    if (internal != NB_HOST_EVENT_STATUS_EMPTY) {
        return internal;
    }
    while (SDL_PollEvent(&source)) {
        const enum translated_event_status translated =
            translate_sdl_event(context, &source, event);

        if (translated == TRANSLATED_EVENT_READY) {
            return NB_HOST_EVENT_STATUS_AVAILABLE;
        }
        if (translated == TRANSLATED_EVENT_ERROR) {
            return pop_internal_event(context, event);
        }
    }
    memset(event, 0, sizeof(*event));
    return NB_HOST_EVENT_STATUS_EMPTY;
}

static Sint32 wait_chunk(uint64_t milliseconds)
{
    return milliseconds > (uint64_t)INT32_MAX
               ? INT32_MAX
               : (Sint32)milliseconds;
}

static enum nb_host_event_status sdl_wait_event(
    void *opaque,
    uint32_t timeout_milliseconds,
    struct nb_host_event *event)
{
    struct nb_host_sdl_context *context = opaque;
    enum nb_host_event_status internal;
    const uint64_t started = SDL_GetTicks();
    const uint64_t deadline = started + timeout_milliseconds;

    internal = pop_internal_event(context, event);
    if (internal != NB_HOST_EVENT_STATUS_EMPTY) {
        return internal;
    }

    for (;;) {
        const uint64_t now = SDL_GetTicks();
        uint64_t remaining = now < deadline ? deadline - now : 0;
        SDL_Event source;
        bool received;

        if (timeout_milliseconds == 0) {
            received = SDL_PollEvent(&source);
        } else {
            received = SDL_WaitEventTimeout(&source,
                                            wait_chunk(remaining));
        }
        if (!received) {
            if (timeout_milliseconds != 0 && remaining > INT32_MAX) {
                continue;
            }
            memset(event, 0, sizeof(*event));
            return NB_HOST_EVENT_STATUS_EMPTY;
        }

        {
            const enum translated_event_status translated =
                translate_sdl_event(context, &source, event);

            if (translated == TRANSLATED_EVENT_READY) {
                return NB_HOST_EVENT_STATUS_AVAILABLE;
            }
            if (translated == TRANSLATED_EVENT_ERROR) {
                return pop_internal_event(context, event);
            }
        }

        {
            const uint64_t after_event = SDL_GetTicks();

            remaining = after_event < deadline
                            ? deadline - after_event
                            : 0;
        }
        if (remaining == 0) {
            return sdl_poll_event(context, event);
        }
    }
}

static bool sdl_set_pointer_capture(void *opaque, bool captured)
{
    struct nb_host_sdl_context *context = opaque;

    if (context->state != NB_HOST_STATE_ACTIVE) {
        return false;
    }
    if (!SDL_CaptureMouse(captured)) {
        remember_sdl_error(context, "Could not change SDL pointer capture");
        return false;
    }
    return true;
}

static SDL_PixelFormat sdl_pixel_format(enum nb_host_pixel_format format)
{
    return format == NB_HOST_PIXEL_FORMAT_XRGB8888
               ? SDL_PIXELFORMAT_XRGB8888
               : SDL_PIXELFORMAT_ARGB8888;
}

static bool ensure_texture(struct nb_host_sdl_context *context,
                           const struct nb_host_frame *frame)
{
    const SDL_PixelFormat format = sdl_pixel_format(frame->format);
    SDL_Texture *texture;

    if (context->texture != NULL &&
        context->texture_width == frame->width &&
        context->texture_height == frame->height &&
        context->texture_format == format) {
        return true;
    }
    texture = SDL_CreateTexture(context->renderer,
                                format,
                                SDL_TEXTUREACCESS_STREAMING,
                                frame->width,
                                frame->height);
    if (texture == NULL) {
        fail_sdl_host(context, "Could not create SDL presentation texture");
        return false;
    }
    if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) ||
        !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) {
        remember_sdl_error(context,
                           "Could not configure SDL presentation texture");
        SDL_DestroyTexture(texture);
        mark_sdl_host_failed(context);
        return false;
    }
    if (context->texture != NULL) {
        SDL_DestroyTexture(context->texture);
    }
    context->texture = texture;
    context->texture_width = frame->width;
    context->texture_height = frame->height;
    context->texture_format = format;
    return true;
}

static enum nb_host_result sdl_present(
    void *opaque,
    const struct nb_host_frame *frame)
{
    struct nb_host_sdl_context *context = opaque;
    struct nb_host_event output_event;
    struct nb_host_event *complete;
    bool output_changed;

    if (context->state != NB_HOST_STATE_ACTIVE) {
        return NB_HOST_RESULT_ERROR;
    }
    if (context->event_count >= NB_HOST_SDL_EVENT_CAPACITY) {
        return NB_HOST_RESULT_WOULD_BLOCK;
    }
    if (!update_output(context,
                       SDL_GetTicks(),
                       &output_event,
                       &output_changed)) {
        return NB_HOST_RESULT_ERROR;
    }
    if (output_changed) {
        if (!queue_event(context, &output_event)) {
            return NB_HOST_RESULT_WOULD_BLOCK;
        }
        return NB_HOST_RESULT_WOULD_BLOCK;
    }
    if (frame->width != context->output.pixel_width ||
        frame->height != context->output.pixel_height) {
        copy_error(context->error,
                   "SDL frame dimensions do not match the output",
                   NULL);
        context->system_error = 0;
        return NB_HOST_RESULT_INVALID_ARGUMENT;
    }
    if (frame->stride > (size_t)INT_MAX) {
        copy_error(context->error,
                   "SDL cannot accept a frame stride above INT_MAX",
                   NULL);
        context->system_error = 0;
        return NB_HOST_RESULT_UNSUPPORTED;
    }
    if (!ensure_texture(context, frame)) {
        return NB_HOST_RESULT_ERROR;
    }
    complete = reserve_event(context);
    if (complete == NULL) {
        return NB_HOST_RESULT_WOULD_BLOCK;
    }
    memset(complete, 0, sizeof(*complete));
    complete->type = NB_HOST_EVENT_FRAME_COMPLETE;
    complete->data.frame_complete.frame_serial = frame->serial;
    if (!SDL_UpdateTexture(context->texture,
                           NULL,
                           frame->pixels,
                           (int)frame->stride) ||
        !SDL_SetRenderDrawColor(context->renderer,
                                0,
                                0,
                                0,
                                SDL_ALPHA_OPAQUE) ||
        !SDL_RenderClear(context->renderer) ||
        !SDL_RenderTexture(context->renderer,
                           context->texture,
                           NULL,
                           NULL) ||
        !SDL_RenderPresent(context->renderer)) {
        cancel_event_reservation(context);
        fail_sdl_host(context, "Could not present SDL frame");
        return NB_HOST_RESULT_ERROR;
    }
    complete->milliseconds = SDL_GetTicks();
    return NB_HOST_RESULT_OK;
}

static enum nb_host_result sdl_complete_console_release(void *opaque)
{
    (void)opaque;
    return NB_HOST_RESULT_UNSUPPORTED;
}

static enum nb_host_result sdl_complete_console_acquire(void *opaque)
{
    (void)opaque;
    return NB_HOST_RESULT_UNSUPPORTED;
}

static bool sdl_get_last_error(const void *opaque,
                               int *system_error,
                               char *message,
                               size_t message_size)
{
    const struct nb_host_sdl_context *context = opaque;

    *system_error = context->system_error;
    if (context->error[0] == '\0') {
        message[0] = '\0';
        return false;
    }
    (void)snprintf(message, message_size, "%s", context->error);
    return true;
}

static void sdl_destroy(void *opaque)
{
    struct nb_host_sdl_context *context = opaque;

    (void)SDL_CaptureMouse(false);
    if (context->texture != NULL) {
        SDL_DestroyTexture(context->texture);
    }
    SDL_DestroyRenderer(context->renderer);
    SDL_DestroyWindow(context->window);
    if (context->video_initialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    free(context);
}

static const struct nb_host_backend_operations sdl_operations = {
    .get_output = sdl_get_output,
    .get_state = sdl_get_state,
    .monotonic_milliseconds = sdl_monotonic_milliseconds,
    .poll_event = sdl_poll_event,
    .wait_event = sdl_wait_event,
    .set_pointer_capture = sdl_set_pointer_capture,
    .present = sdl_present,
    .complete_console_release = sdl_complete_console_release,
    .complete_console_acquire = sdl_complete_console_acquire,
    .get_last_error = sdl_get_last_error,
    .destroy = sdl_destroy
};

void nb_host_sdl_options_init(struct nb_host_sdl_options *options)
{
    if (options == NULL) {
        return;
    }
    options->title = "NixBench Desktop";
    options->window_width = NB_HOST_SDL_DEFAULT_WIDTH;
    options->window_height = NB_HOST_SDL_DEFAULT_HEIGHT;
    options->minimum_width = NB_HOST_SDL_DEFAULT_MINIMUM_WIDTH;
    options->minimum_height = NB_HOST_SDL_DEFAULT_MINIMUM_HEIGHT;
    options->fullscreen = false;
    options->resizable = true;
    options->high_pixel_density = true;
}

static bool options_are_valid(const struct nb_host_sdl_options *options)
{
    return options != NULL && options->title != NULL &&
           options->title[0] != '\0' && options->window_width > 0 &&
           options->window_height > 0 && options->minimum_width >= 0 &&
           options->minimum_height >= 0 &&
           ((options->minimum_width == 0 && options->minimum_height == 0) ||
            (options->minimum_width > 0 && options->minimum_height > 0));
}

static void destroy_failed_context(struct nb_host_sdl_context *context)
{
    if (context == NULL) {
        return;
    }
    if (context->texture != NULL) {
        SDL_DestroyTexture(context->texture);
    }
    SDL_DestroyRenderer(context->renderer);
    SDL_DestroyWindow(context->window);
    if (context->video_initialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    free(context);
}

struct nb_host *nb_host_sdl_create(
    const struct nb_host_sdl_options *options)
{
    struct nb_host_sdl_context *context;
    struct nb_host_event focus = {0};
    struct nb_host *host;
    SDL_WindowFlags flags = 0;

    creation_error[0] = '\0';
    if (!options_are_valid(options)) {
        copy_error(creation_error, "Invalid SDL host options", NULL);
        return NULL;
    }
    context = calloc(1, sizeof(*context));
    if (context == NULL) {
        copy_error(creation_error, "Could not allocate SDL host", NULL);
        return NULL;
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        copy_error(creation_error,
                   "Could not initialize SDL video",
                   SDL_GetError());
        destroy_failed_context(context);
        return NULL;
    }
    context->video_initialized = true;

    if (options->fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (options->resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (options->high_pixel_density) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (!SDL_CreateWindowAndRenderer(options->title,
                                     options->window_width,
                                     options->window_height,
                                     flags,
                                     &context->window,
                                     &context->renderer)) {
        copy_error(creation_error,
                   "Could not create SDL window and renderer",
                   SDL_GetError());
        destroy_failed_context(context);
        return NULL;
    }
    if (!SDL_SetRenderLogicalPresentation(context->renderer,
                                          0,
                                          0,
                                          SDL_LOGICAL_PRESENTATION_DISABLED)) {
        copy_error(creation_error,
                   "Could not disable SDL logical presentation",
                   SDL_GetError());
        destroy_failed_context(context);
        return NULL;
    }
    if (options->minimum_width > 0 &&
        !SDL_SetWindowMinimumSize(context->window,
                                  options->minimum_width,
                                  options->minimum_height)) {
        copy_error(creation_error,
                   "Could not set SDL minimum window size",
                   SDL_GetError());
        destroy_failed_context(context);
        return NULL;
    }
    context->window_id = SDL_GetWindowID(context->window);
    if (context->window_id == 0 || !read_output(context, &context->output)) {
        copy_error(creation_error,
                   "Could not query initial SDL output",
                   SDL_GetError());
        destroy_failed_context(context);
        return NULL;
    }
    context->state = NB_HOST_STATE_ACTIVE;

    focus.type = NB_HOST_EVENT_FOCUS_CHANGED;
    focus.milliseconds = SDL_GetTicks();
    focus.data.focus.focused =
        (SDL_GetWindowFlags(context->window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (!queue_event(context, &focus)) {
        copy_error(creation_error,
                   "Could not queue initial SDL focus state",
                   NULL);
        destroy_failed_context(context);
        return NULL;
    }

    host = nb_host_backend_create(&sdl_operations, context);
    if (host == NULL) {
        copy_error(creation_error,
                   "Could not allocate SDL host facade",
                   NULL);
        destroy_failed_context(context);
    }
    return host;
}

const char *nb_host_sdl_creation_error(void)
{
    return creation_error;
}
