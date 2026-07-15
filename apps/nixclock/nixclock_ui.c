#include "nixclock_ui.h"

#include <math.h>
#include <stdint.h>

static const double nb_nixclock_pi = 3.14159265358979323846;

static const struct nb_nixclock_palette nb_nixclock_palette = {
    UINT32_C(0xff315861), /* desktop */
    UINT32_C(0xff172f36), /* rim shadow */
    UINT32_C(0xfff0dda2), /* rim highlight */
    UINT32_C(0xffc9c5a7), /* face */
    UINT32_C(0xff68706d), /* minor tick */
    UINT32_C(0xff243a43), /* hour tick */
    UINT32_C(0xff203842), /* hour hand */
    UINT32_C(0xff813f3b), /* minute hand */
    UINT32_C(0xffe69b35), /* second hand */
    UINT32_C(0xfff4e8b9)  /* centre pin */
};

static int minimum(int left, int right)
{
    return left < right ? left : right;
}

static int maximum(int left, int right)
{
    return left > right ? left : right;
}

bool nb_nixclock_local_time_is_valid(
    const struct nb_nixclock_local_time *local_time)
{
    return local_time != NULL &&
           local_time->hour < 24U &&
           local_time->minute < 60U &&
           local_time->second < 60U &&
           local_time->millisecond < 1000U;
}

static struct nb_nixclock_point hand_tip(double divisions,
                                         double position,
                                         double length)
{
    const double angle = (position / divisions) * 2.0 * nb_nixclock_pi;
    const struct nb_nixclock_point point = {
        sin(angle) * length,
        -cos(angle) * length
    };

    return point;
}

bool nb_nixclock_hand_geometry(
    const struct nb_nixclock_local_time *local_time,
    bool show_seconds,
    struct nb_nixclock_hand_geometry *geometry)
{
    double precise_second;
    double precise_minute;
    double precise_hour;

    if (!nb_nixclock_local_time_is_valid(local_time) || geometry == NULL) {
        return false;
    }

    precise_second = (double)local_time->second +
                     ((double)local_time->millisecond / 1000.0);
    precise_minute = (double)local_time->minute + precise_second / 60.0;
    precise_hour = (double)(local_time->hour % 12U) + precise_minute / 60.0;

    geometry->hour_tip = hand_tip(12.0, precise_hour, 0.48);
    geometry->minute_tip = hand_tip(60.0, precise_minute, 0.70);
    geometry->second_visible = show_seconds;
    if (show_seconds) {
        geometry->second_tip = hand_tip(60.0, precise_second, 0.78);
    } else {
        geometry->second_tip = (struct nb_nixclock_point){0.0, 0.0};
    }
    return true;
}

bool nb_nixclock_next_update_delay_ms(
    const struct nb_nixclock_local_time *local_time,
    bool show_seconds,
    uint32_t *delay_ms)
{
    uint32_t delay;

    if (!nb_nixclock_local_time_is_valid(local_time) || delay_ms == NULL) {
        return false;
    }

    delay = UINT32_C(1000) - (uint32_t)local_time->millisecond;
    if (!show_seconds) {
        delay += (UINT32_C(59) - (uint32_t)local_time->second) *
                 UINT32_C(1000);
    }
    *delay_ms = delay;
    return true;
}

const struct nb_nixclock_palette *nb_nixclock_ui_palette(void)
{
    return &nb_nixclock_palette;
}

void nb_nixclock_ui_init(struct nb_nixclock_ui *ui, int width, int height)
{
    if (ui == NULL) {
        return;
    }
    ui->width = maximum(0, width);
    ui->height = maximum(0, height);
    ui->local_time = (struct nb_nixclock_local_time){0U, 0U, 0U, 0U};
    ui->show_seconds = false;
}

void nb_nixclock_ui_resize(struct nb_nixclock_ui *ui, int width, int height)
{
    if (ui == NULL) {
        return;
    }
    ui->width = maximum(0, width);
    ui->height = maximum(0, height);
}

bool nb_nixclock_ui_set_local_time(
    struct nb_nixclock_ui *ui,
    const struct nb_nixclock_local_time *local_time)
{
    if (ui == NULL || !nb_nixclock_local_time_is_valid(local_time)) {
        return false;
    }
    ui->local_time = *local_time;
    return true;
}

bool nb_nixclock_ui_set_show_seconds(struct nb_nixclock_ui *ui,
                                     bool show_seconds)
{
    if (ui == NULL || ui->show_seconds == show_seconds) {
        return false;
    }
    ui->show_seconds = show_seconds;
    return true;
}

static void put_pixel(uint32_t *pixels,
                      int width,
                      int height,
                      size_t stride,
                      int x,
                      int y,
                      uint32_t color)
{
    if (x >= 0 && y >= 0 && x < width && y < height) {
        pixels[(size_t)y * stride + (size_t)x] = color;
    }
}

static void fill(uint32_t *pixels,
                 int width,
                 int height,
                 size_t stride,
                 uint32_t color)
{
    int y;

    for (y = 0; y < height; ++y) {
        int x;

        for (x = 0; x < width; ++x) {
            pixels[(size_t)y * stride + (size_t)x] = color;
        }
    }
}

static void draw_disk(uint32_t *pixels,
                      int width,
                      int height,
                      size_t stride,
                      int centre_x,
                      int centre_y,
                      int radius,
                      uint32_t color)
{
    const int safe_radius = maximum(0, radius);
    const int x_begin = maximum(0, centre_x - safe_radius);
    const int y_begin = maximum(0, centre_y - safe_radius);
    const int x_end = minimum(width - 1, centre_x + safe_radius);
    const int y_end = minimum(height - 1, centre_y + safe_radius);
    const int64_t radius_squared = (int64_t)safe_radius * safe_radius;
    int y;

    for (y = y_begin; y <= y_end; ++y) {
        int x;

        for (x = x_begin; x <= x_end; ++x) {
            const int64_t dx = (int64_t)x - centre_x;
            const int64_t dy = (int64_t)y - centre_y;

            if (dx * dx + dy * dy <= radius_squared) {
                put_pixel(pixels, width, height, stride, x, y, color);
            }
        }
    }
}

static void draw_line(uint32_t *pixels,
                      int width,
                      int height,
                      size_t stride,
                      int x0,
                      int y0,
                      int x1,
                      int y1,
                      int brush_radius,
                      uint32_t color)
{
    const int step_x = x0 < x1 ? 1 : -1;
    const int step_y = y0 < y1 ? 1 : -1;
    const int64_t dx = x0 < x1 ? (int64_t)x1 - x0 : (int64_t)x0 - x1;
    const int64_t dy = y0 < y1 ? (int64_t)y0 - y1 : (int64_t)y1 - y0;
    int64_t error = dx + dy;

    for (;;) {
        int64_t doubled_error;

        draw_disk(pixels, width, height, stride,
                  x0, y0, brush_radius, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        doubled_error = 2 * error;
        if (doubled_error >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (doubled_error <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

static int dial_coordinate(int centre, double radius, double component)
{
    return centre + (int)lround(radius * component);
}

static void draw_ticks(uint32_t *pixels,
                       int width,
                       int height,
                       size_t stride,
                       int centre_x,
                       int centre_y,
                       int face_radius)
{
    const double outer_radius = (double)face_radius * 0.90;
    unsigned int tick;

    for (tick = 0U; tick < 60U; ++tick) {
        const bool is_hour = tick % 5U == 0U;
        const double angle = ((double)tick / 60.0) *
                             2.0 * nb_nixclock_pi;
        const double unit_x = sin(angle);
        const double unit_y = -cos(angle);
        const double inner_radius = (double)face_radius *
                                    (is_hour ? 0.68 : 0.80);
        const int x0 = dial_coordinate(centre_x, inner_radius, unit_x);
        const int y0 = dial_coordinate(centre_y, inner_radius, unit_y);
        const int x1 = dial_coordinate(centre_x, outer_radius, unit_x);
        const int y1 = dial_coordinate(centre_y, outer_radius, unit_y);
        const int brush_radius = is_hour && face_radius >= 42 ? 1 : 0;

        draw_line(pixels, width, height, stride,
                  x0, y0, x1, y1, brush_radius,
                  is_hour ? nb_nixclock_palette.hour_tick
                          : nb_nixclock_palette.minor_tick);
    }
}

static void draw_hand(uint32_t *pixels,
                      int width,
                      int height,
                      size_t stride,
                      int centre_x,
                      int centre_y,
                      int face_radius,
                      struct nb_nixclock_point tip,
                      int brush_radius,
                      uint32_t color)
{
    const int tip_x = dial_coordinate(centre_x, (double)face_radius, tip.x);
    const int tip_y = dial_coordinate(centre_y, (double)face_radius, tip.y);

    draw_line(pixels, width, height, stride,
              centre_x, centre_y, tip_x, tip_y, brush_radius, color);
}

bool nb_nixclock_ui_render(const struct nb_nixclock_ui *ui,
                           uint32_t *pixels,
                           size_t stride_pixels)
{
    struct nb_nixclock_hand_geometry hands;
    int centre_x;
    int centre_y;
    int dial_radius;
    int face_radius;
    int hour_brush;
    int minute_brush;

    if (ui == NULL || pixels == NULL || ui->width <= 0 || ui->height <= 0 ||
        stride_pixels < (size_t)ui->width ||
        !nb_nixclock_hand_geometry(&ui->local_time,
                                   ui->show_seconds,
                                   &hands)) {
        return false;
    }

    fill(pixels, ui->width, ui->height, stride_pixels,
         nb_nixclock_palette.desktop);

    centre_x = ui->width / 2;
    centre_y = ui->height / 2;
    dial_radius = (int)((double)minimum(ui->width, ui->height) * 0.46);
    dial_radius = maximum(1, dial_radius);
    draw_disk(pixels, ui->width, ui->height, stride_pixels,
              centre_x, centre_y, dial_radius,
              nb_nixclock_palette.rim_shadow);
    draw_disk(pixels, ui->width, ui->height, stride_pixels,
              centre_x, centre_y, maximum(0, dial_radius - 1),
              nb_nixclock_palette.rim_highlight);
    face_radius = maximum(0, dial_radius - 3);
    draw_disk(pixels, ui->width, ui->height, stride_pixels,
              centre_x, centre_y, face_radius,
              nb_nixclock_palette.face);

    if (face_radius > 0) {
        draw_ticks(pixels, ui->width, ui->height, stride_pixels,
                   centre_x, centre_y, face_radius);
    }

    hour_brush = maximum(1, face_radius / 34);
    minute_brush = maximum(1, face_radius / 52);
    draw_hand(pixels, ui->width, ui->height, stride_pixels,
              centre_x, centre_y, face_radius, hands.hour_tip,
              hour_brush, nb_nixclock_palette.hour_hand);
    draw_hand(pixels, ui->width, ui->height, stride_pixels,
              centre_x, centre_y, face_radius, hands.minute_tip,
              minute_brush, nb_nixclock_palette.minute_hand);
    if (hands.second_visible) {
        draw_hand(pixels, ui->width, ui->height, stride_pixels,
                  centre_x, centre_y, face_radius, hands.second_tip,
                  0, nb_nixclock_palette.second_hand);
    }
    draw_disk(pixels, ui->width, ui->height, stride_pixels,
              centre_x, centre_y, maximum(1, face_radius / 40),
              nb_nixclock_palette.centre_pin);
    return true;
}
