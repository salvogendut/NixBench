#ifndef NIXBENCH_NIXCLOCK_UI_H
#define NIXBENCH_NIXCLOCK_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * NixClock deliberately receives local civil time from its host.  Keeping the
 * clock source and timezone conversion outside this module makes its model and
 * renderer deterministic and usable by both hosted and standalone frontends.
 */
struct nb_nixclock_local_time {
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    unsigned int millisecond;
};

/* Dial-space coordinates: the origin is the centre, +x is right, +y is down. */
struct nb_nixclock_point {
    double x;
    double y;
};

struct nb_nixclock_hand_geometry {
    struct nb_nixclock_point hour_tip;
    struct nb_nixclock_point minute_tip;
    struct nb_nixclock_point second_tip;
    bool second_visible;
};

struct nb_nixclock_palette {
    uint32_t desktop;
    uint32_t rim_shadow;
    uint32_t rim_highlight;
    uint32_t face;
    uint32_t minor_tick;
    uint32_t hour_tick;
    uint32_t hour_hand;
    uint32_t minute_hand;
    uint32_t second_hand;
    uint32_t centre_pin;
};

struct nb_nixclock_ui {
    int width;
    int height;
    struct nb_nixclock_local_time local_time;
    bool show_seconds;
};

bool nb_nixclock_local_time_is_valid(
    const struct nb_nixclock_local_time *local_time);

/*
 * Returns normalized hand tips.  The hour and minute hands include all
 * smaller supplied time units, so neither jumps to a coarse clock division.
 */
bool nb_nixclock_hand_geometry(
    const struct nb_nixclock_local_time *local_time,
    bool show_seconds,
    struct nb_nixclock_hand_geometry *geometry);

/*
 * Returns the positive delay to the next wall-clock second when seconds are
 * shown, or the next wall-clock minute otherwise.
 */
bool nb_nixclock_next_update_delay_ms(
    const struct nb_nixclock_local_time *local_time,
    bool show_seconds,
    uint32_t *delay_ms);

const struct nb_nixclock_palette *nb_nixclock_ui_palette(void);

void nb_nixclock_ui_init(struct nb_nixclock_ui *ui, int width, int height);
void nb_nixclock_ui_resize(struct nb_nixclock_ui *ui, int width, int height);
bool nb_nixclock_ui_set_local_time(
    struct nb_nixclock_ui *ui,
    const struct nb_nixclock_local_time *local_time);

/* Returns true only when the visible option changed. */
bool nb_nixclock_ui_set_show_seconds(struct nb_nixclock_ui *ui,
                                     bool show_seconds);

/* Pixels are opaque XRGB8888; stride is measured in uint32_t pixels. */
bool nb_nixclock_ui_render(const struct nb_nixclock_ui *ui,
                           uint32_t *pixels,
                           size_t stride_pixels);

#endif
