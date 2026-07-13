#include <stdbool.h>
#include <stdio.h>

#include "window.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

static struct nb_window make_window(void)
{
    struct nb_window window;
    const struct nb_rect frame = {100, 80, 320, 220};

    nb_window_init(&window, "Test window", frame);
    return window;
}

static void test_geometry_and_hit_testing(void)
{
    struct nb_window window = make_window();
    const struct nb_rect title = nb_window_title_rect(&window);
    const struct nb_rect content = nb_window_content_rect(&window);
    const struct nb_rect close = nb_window_close_rect(&window);

    CHECK(title.x == 103);
    CHECK(title.y == 83);
    CHECK(title.width == 314);
    CHECK(title.height == 24);
    CHECK(content.x == 103);
    CHECK(content.y == 107);
    CHECK(content.width == 314);
    CHECK(content.height == 190);
    CHECK(close.x == 107);
    CHECK(close.y == 87);
    CHECK(close.width == 16);
    CHECK(close.height == 16);

    CHECK(nb_window_hit_test(&window, 110, 90) == NB_WINDOW_HIT_CLOSE);
    CHECK(nb_window_hit_test(&window, 122, 102) == NB_WINDOW_HIT_CLOSE);
    CHECK(nb_window_hit_test(&window, 123, 102) == NB_WINDOW_HIT_TITLE);
    CHECK(nb_window_hit_test(&window, 122, 103) == NB_WINDOW_HIT_TITLE);
    CHECK(nb_window_hit_test(&window, 150, 90) == NB_WINDOW_HIT_TITLE);
    CHECK(nb_window_hit_test(&window, 150, 120) == NB_WINDOW_HIT_CONTENT);
    CHECK(nb_window_hit_test(&window, 100, 80) == NB_WINDOW_HIT_FRAME);
    CHECK(nb_window_hit_test(&window, 419, 299) == NB_WINDOW_HIT_FRAME);
    CHECK(nb_window_hit_test(&window, 420, 299) == NB_WINDOW_HIT_NONE);
    CHECK(nb_window_hit_test(&window, 419, 300) == NB_WINDOW_HIT_NONE);

    window.visible = false;
    CHECK(nb_window_hit_test(&window, 150, 90) == NB_WINDOW_HIT_NONE);
}

static void test_dragging(void)
{
    struct nb_window window = make_window();
    const struct nb_rect bounds = {0, 0, 800, 600};

    CHECK(nb_window_pointer_down(&window, 150, 90) == NB_WINDOW_HIT_TITLE);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_DRAG);
    CHECK(window.grab_x == 50);
    CHECK(window.grab_y == 10);
    CHECK(nb_window_pointer_move(&window, 300, 210, bounds));
    CHECK(window.frame.x == 250);
    CHECK(window.frame.y == 200);

    CHECK(nb_window_pointer_move(&window, -50, -20, bounds));
    CHECK(window.frame.x == 0);
    CHECK(window.frame.y == 0);

    CHECK(nb_window_pointer_move(&window, 900, 700, bounds));
    CHECK(window.frame.x == 480);
    CHECK(window.frame.y == 380);

    CHECK(nb_window_pointer_up(&window, 900, 700) == NB_WINDOW_ACTION_NONE);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!nb_window_pointer_move(&window, 200, 200, bounds));
    CHECK(window.frame.x == 480);
    CHECK(window.frame.y == 380);
}

static void test_clamping(void)
{
    struct nb_window window = make_window();
    const struct nb_rect offset_bounds = {20, 30, 500, 400};
    const struct nb_rect small_bounds = {5, 7, 100, 90};

    window.frame.x = 1000;
    window.frame.y = 1000;
    CHECK(nb_window_clamp_to(&window, offset_bounds));
    CHECK(window.frame.x == 200);
    CHECK(window.frame.y == 210);

    CHECK(nb_window_clamp_to(&window, small_bounds));
    CHECK(window.frame.x == 5);
    CHECK(window.frame.y == 7);
    CHECK(!nb_window_clamp_to(&window, small_bounds));
}

static void test_close_tracking(void)
{
    struct nb_window window = make_window();
    const struct nb_rect bounds = {0, 0, 800, 600};

    CHECK(nb_window_pointer_down(&window, 110, 90) == NB_WINDOW_HIT_CLOSE);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_CLOSE);
    CHECK(window.close_pressed);
    CHECK(nb_window_pointer_move(&window, 150, 90, bounds));
    CHECK(!window.close_pressed);
    CHECK(nb_window_pointer_move(&window, 110, 90, bounds));
    CHECK(window.close_pressed);
    CHECK(nb_window_pointer_up(&window, 110, 90) ==
          NB_WINDOW_ACTION_CLOSE_REQUESTED);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!window.close_pressed);

    CHECK(nb_window_pointer_down(&window, 110, 90) == NB_WINDOW_HIT_CLOSE);
    CHECK(nb_window_pointer_up(&window, 150, 90) == NB_WINDOW_ACTION_NONE);

    CHECK(nb_window_pointer_down(&window, 150, 90) == NB_WINDOW_HIT_TITLE);
    CHECK(nb_window_pointer_up(&window, 110, 90) == NB_WINDOW_ACTION_NONE);

    CHECK(nb_window_pointer_up(&window, 110, 90) == NB_WINDOW_ACTION_NONE);
    CHECK(nb_window_pointer_down(&window, 150, 120) == NB_WINDOW_HIT_CONTENT);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_IDLE);
}

static void test_cancel_and_repeated_down(void)
{
    struct nb_window window = make_window();
    const struct nb_rect bounds = {0, 0, 800, 600};

    CHECK(nb_window_pointer_down(&window, 150, 90) == NB_WINDOW_HIT_TITLE);
    CHECK(nb_window_pointer_down(&window, 110, 90) == NB_WINDOW_HIT_NONE);
    nb_window_pointer_cancel(&window);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!nb_window_pointer_move(&window, 300, 300, bounds));

    CHECK(nb_window_pointer_down(&window, 110, 90) == NB_WINDOW_HIT_CLOSE);
    window.visible = false;
    CHECK(nb_window_pointer_up(&window, 110, 90) == NB_WINDOW_ACTION_NONE);
}

int main(void)
{
    test_geometry_and_hit_testing();
    test_dragging();
    test_clamping();
    test_close_tracking();
    test_cancel_and_repeated_down();

    if (failures != 0) {
        fprintf(stderr, "%d window model check(s) failed\n", failures);
        return 1;
    }

    puts("window model checks passed");
    return 0;
}
