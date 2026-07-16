#ifndef NIXBENCH_DESKTOP_RUNTIME_H
#define NIXBENCH_DESKTOP_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "desktop.h"
#include "host.h"

/*
 * The desktop runtime owns shell, application, compositor-service, and
 * software-composition state. Display hosts, input providers, frame pacing,
 * and console lifecycle acknowledgement remain the caller's responsibility.
 */
struct nb_desktop_runtime;

enum nb_desktop_launch_request {
    NB_DESKTOP_LAUNCH_NONE = 0,
    NB_DESKTOP_LAUNCH_NIXCLOCK,
    NB_DESKTOP_LAUNCH_SAKURA,
    NB_DESKTOP_LAUNCH_MIDORI
};

struct nb_desktop_runtime_options {
    bool enable_wayland;
    bool publish_wayland_socket;
    bool software_pointer;
    bool enable_application_launcher;
};

struct nb_desktop_runtime_update {
    bool redraw;
    bool quit_requested;
    enum nb_desktop_launch_request launch_request;
};

/* Defaults to a host-independent runtime with optional services disabled. */
void nb_desktop_runtime_options_init(
    struct nb_desktop_runtime_options *options);

/*
 * The initial output is required so creation never exposes a runtime without
 * a renderable canvas. The returned object's address remains stable for the
 * lifetime of its in-process application host.
 */
struct nb_desktop_runtime *nb_desktop_runtime_create(
    const struct nb_desktop_runtime_options *options,
    const struct nb_host_output *initial_output);
void nb_desktop_runtime_destroy(struct nb_desktop_runtime *runtime);

bool nb_desktop_runtime_set_output(
    struct nb_desktop_runtime *runtime,
    const struct nb_host_output *output);
bool nb_desktop_runtime_get_output(
    const struct nb_desktop_runtime *runtime,
    struct nb_host_output *output);

/* Seed an optional software pointer without synthesizing an input event. */
bool nb_desktop_runtime_set_pointer(
    struct nb_desktop_runtime *runtime,
    int x,
    int y,
    bool visible);

/*
 * Accept normalized pointer-motion, pointer-button, or key events only.
 * Each successful call replaces update with the consequences of that event.
 */
bool nb_desktop_runtime_handle_input(
    struct nb_desktop_runtime *runtime,
    const struct nb_host_event *event,
    struct nb_desktop_runtime_update *update);

/* Translate host focus changes without performing any host operation. */
bool nb_desktop_runtime_set_focus(
    struct nb_desktop_runtime *runtime,
    bool focused,
    uint64_t milliseconds,
    struct nb_desktop_runtime_update *update);

/*
 * Cancel pointer interaction on an uncaptured leave. actual_capture is the
 * frontend's successfully applied capture state, not merely runtime intent.
 */
bool nb_desktop_runtime_pointer_leave(
    struct nb_desktop_runtime *runtime,
    bool actual_capture,
    uint64_t milliseconds,
    struct nb_desktop_runtime_update *update);

/* Synthesize releases and clear shell/client focus before suspension. */
void nb_desktop_runtime_cancel_input(
    struct nb_desktop_runtime *runtime,
    uint64_t milliseconds);

/* Dispatch optional compositor services without blocking. */
bool nb_desktop_runtime_dispatch(
    struct nb_desktop_runtime *runtime,
    struct nb_desktop_runtime_update *update);

/*
 * Render a complete canonical frame. Pixels are borrowed from the runtime
 * until its next render, output change, or destruction.
 */
bool nb_desktop_runtime_render(
    struct nb_desktop_runtime *runtime,
    const char *clock_text,
    uint64_t serial,
    struct nb_host_frame *frame);

/* Notify optional compositor services only after an accepted frame completes. */
void nb_desktop_runtime_frame_presented(
    struct nb_desktop_runtime *runtime,
    uint64_t milliseconds);

bool nb_desktop_runtime_wants_pointer_capture(
    const struct nb_desktop_runtime *runtime);
bool nb_desktop_runtime_quit_requested(
    const struct nb_desktop_runtime *runtime);

/* Borrowed display name, or NULL when no Wayland socket is published. */
const char *nb_desktop_runtime_wayland_display_name(
    const struct nb_desktop_runtime *runtime);

/* Read-only state seams for deterministic runtime integration tests. */
size_t nb_desktop_runtime_window_count(
    const struct nb_desktop_runtime *runtime);
bool nb_desktop_runtime_active_window_frame(
    const struct nb_desktop_runtime *runtime,
    nb_window_id *window,
    struct nb_rect *frame);

#endif
