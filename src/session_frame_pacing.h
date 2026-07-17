#ifndef NIXBENCH_SESSION_FRAME_PACING_H
#define NIXBENCH_SESSION_FRAME_PACING_H

#include <stdbool.h>
#include <stdint.h>

enum {
    NB_SESSION_FRAME_FALLBACK_INTERVAL_MS = 17,
    NB_SESSION_FRAME_MAX_INTERVAL_MS = 1000
};

struct nb_session_frame_pacing {
    uint64_t deadline_milliseconds;
    uint32_t interval_milliseconds;
};

void nb_session_frame_pacing_init(struct nb_session_frame_pacing *pacing);
void nb_session_frame_pacing_configure(struct nb_session_frame_pacing *pacing,
                                       int refresh_millihertz);
bool nb_session_frame_pacing_ready(
    const struct nb_session_frame_pacing *pacing,
    uint64_t now_milliseconds);
void nb_session_frame_pacing_presented(struct nb_session_frame_pacing *pacing,
                                       uint64_t now_milliseconds);
uint32_t nb_session_frame_pacing_wait_timeout(
    const struct nb_session_frame_pacing *pacing,
    uint64_t now_milliseconds,
    bool redraw_pending,
    bool presentation_pending,
    uint32_t maximum_timeout_milliseconds);

#endif
