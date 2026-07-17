#include "session_frame_pacing.h"

#include <limits.h>
#include <stddef.h>

enum {
    NB_MILLIHERTZ_MILLISECONDS_NUMERATOR = 1000000
};

static uint64_t add_milliseconds(uint64_t start, uint32_t duration)
{
    return duration > UINT64_MAX - start ? UINT64_MAX : start + duration;
}

void nb_session_frame_pacing_init(struct nb_session_frame_pacing *pacing)
{
    if (pacing == NULL) {
        return;
    }
    pacing->deadline_milliseconds = 0;
    pacing->interval_milliseconds =
        NB_SESSION_FRAME_FALLBACK_INTERVAL_MS;
}

void nb_session_frame_pacing_configure(struct nb_session_frame_pacing *pacing,
                                       int refresh_millihertz)
{
    uint64_t interval = NB_SESSION_FRAME_FALLBACK_INTERVAL_MS;

    if (pacing == NULL) {
        return;
    }
    if (refresh_millihertz > 0) {
        interval = ((uint64_t)NB_MILLIHERTZ_MILLISECONDS_NUMERATOR +
                    (uint64_t)refresh_millihertz - UINT64_C(1)) /
                   (uint64_t)refresh_millihertz;
        if (interval < 1U) {
            interval = 1U;
        } else if (interval > NB_SESSION_FRAME_MAX_INTERVAL_MS) {
            interval = NB_SESSION_FRAME_MAX_INTERVAL_MS;
        }
    }
    pacing->interval_milliseconds = (uint32_t)interval;
    pacing->deadline_milliseconds = 0;
}

bool nb_session_frame_pacing_ready(
    const struct nb_session_frame_pacing *pacing,
    uint64_t now_milliseconds)
{
    return pacing != NULL &&
           (pacing->deadline_milliseconds == 0 ||
            now_milliseconds >= pacing->deadline_milliseconds);
}

void nb_session_frame_pacing_presented(struct nb_session_frame_pacing *pacing,
                                       uint64_t now_milliseconds)
{
    if (pacing == NULL) {
        return;
    }
    pacing->deadline_milliseconds =
        add_milliseconds(now_milliseconds, pacing->interval_milliseconds);
}

uint32_t nb_session_frame_pacing_wait_timeout(
    const struct nb_session_frame_pacing *pacing,
    uint64_t now_milliseconds,
    bool redraw_pending,
    bool presentation_pending,
    uint32_t maximum_timeout_milliseconds)
{
    uint64_t remaining;

    if (pacing == NULL || !redraw_pending || presentation_pending ||
        pacing->deadline_milliseconds == 0) {
        return maximum_timeout_milliseconds;
    }
    if (now_milliseconds >= pacing->deadline_milliseconds) {
        return 0;
    }
    remaining = pacing->deadline_milliseconds - now_milliseconds;
    if (remaining > maximum_timeout_milliseconds) {
        return maximum_timeout_milliseconds;
    }
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}
