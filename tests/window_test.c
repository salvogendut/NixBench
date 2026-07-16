#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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
    CHECK(!window.active);
    return window;
}

static void test_geometry_and_hit_testing(void)
{
    struct nb_window window = make_window();
    const struct nb_rect title = nb_window_title_rect(&window);
    const struct nb_rect content = nb_window_content_rect(&window);
    const struct nb_rect footer = nb_window_footer_rect(&window);
    const struct nb_rect close = nb_window_close_rect(&window);
    const struct nb_rect resize = nb_window_resize_rect(&window);

    CHECK(title.x == 103);
    CHECK(title.y == 83);
    CHECK(title.width == 314);
    CHECK(title.height == 24);
    CHECK(content.x == 103);
    CHECK(content.y == 107);
    CHECK(content.width == 314);
    CHECK(content.height == 170);
    CHECK(footer.x == 103);
    CHECK(footer.y == 277);
    CHECK(footer.width == 314);
    CHECK(footer.height == 20);
    CHECK(close.x == 107);
    CHECK(close.y == 87);
    CHECK(close.width == 16);
    CHECK(close.height == 16);
    CHECK(resize.x == 397);
    CHECK(resize.y == 277);
    CHECK(resize.width == 20);
    CHECK(resize.height == 20);

    CHECK(nb_window_hit_test(&window, 110, 90) == NB_WINDOW_HIT_CLOSE);
    CHECK(nb_window_hit_test(&window, 122, 102) == NB_WINDOW_HIT_CLOSE);
    CHECK(nb_window_hit_test(&window, 123, 102) == NB_WINDOW_HIT_TITLE);
    CHECK(nb_window_hit_test(&window, 122, 103) == NB_WINDOW_HIT_TITLE);
    CHECK(nb_window_hit_test(&window, 150, 90) == NB_WINDOW_HIT_TITLE);
    CHECK(nb_window_hit_test(&window, 150, 120) == NB_WINDOW_HIT_CONTENT);
    CHECK(nb_window_hit_test(&window, 397, 277) == NB_WINDOW_HIT_RESIZE);
    CHECK(nb_window_hit_test(&window, 416, 296) == NB_WINDOW_HIT_RESIZE);
    CHECK(nb_window_hit_test(&window, 396, 277) == NB_WINDOW_HIT_FRAME);
    CHECK(nb_window_hit_test(&window, 397, 276) == NB_WINDOW_HIT_CONTENT);
    CHECK(nb_window_hit_test(&window, 417, 296) == NB_WINDOW_HIT_FRAME);
    CHECK(nb_window_hit_test(&window, 416, 297) == NB_WINDOW_HIT_FRAME);
    CHECK(nb_window_hit_test(&window, 100, 80) == NB_WINDOW_HIT_FRAME);
    CHECK(nb_window_hit_test(&window, 419, 299) == NB_WINDOW_HIT_FRAME);
    CHECK(nb_window_hit_test(&window, 420, 299) == NB_WINDOW_HIT_NONE);
    CHECK(nb_window_hit_test(&window, 419, 300) == NB_WINDOW_HIT_NONE);

    window.visible = false;
    CHECK(nb_window_hit_test(&window, 150, 90) == NB_WINDOW_HIT_NONE);
}

static void test_title_ownership(void)
{
    struct nb_window window;
    struct nb_rect frame = {0, 0, 200, 120};
    char title[] = "Mutable title";

    nb_window_init(&window, title, frame);
    title[0] = 'X';
    CHECK(strcmp(window.title, "Mutable title") == 0);

    nb_window_init(&window, NULL, frame);
    CHECK(window.title[0] == '\0');

    frame.width = 1;
    frame.height = 1;
    nb_window_init(&window, "Small", frame);
    CHECK(window.frame.width == NB_WINDOW_MIN_WIDTH);
    CHECK(window.frame.height == NB_WINDOW_MIN_HEIGHT);
}

static void test_dragging(void)
{
    struct nb_window window = make_window();
    const struct nb_rect bounds = {0, 0, 800, 600};

    CHECK(nb_window_pointer_down(&window, 150, 90) == NB_WINDOW_HIT_TITLE);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_DRAG);
    CHECK(window.pointer_offset_x == 50);
    CHECK(window.pointer_offset_y == 10);
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

static void test_maximize_toggle(void)
{
    struct nb_window window = make_window();
    const struct nb_rect bounds = {0, 0, 800, 600};
    const struct nb_rect title = nb_window_title_rect(&window);
    const struct nb_rect maximize = nb_window_maximize_rect(&window);
    const int maximize_x = maximize.x + (maximize.width / 2);
    const int maximize_y = maximize.y + (maximize.height / 2);

    CHECK(title.width == 314);
    CHECK(maximize.x == 397);
    CHECK(maximize.y == 87);
    CHECK(nb_window_hit_test(&window, maximize_x, maximize_y) ==
          NB_WINDOW_HIT_MAXIMIZE);
    CHECK(nb_window_pointer_down(&window, maximize_x, maximize_y) ==
          NB_WINDOW_HIT_MAXIMIZE);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_MAXIMIZE);
    CHECK(window.maximize_pressed);
    CHECK(nb_window_pointer_up(&window, maximize_x, maximize_y) ==
          NB_WINDOW_ACTION_MAXIMIZE_TOGGLED);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!window.maximize_pressed);
    CHECK(!window.maximized);

    CHECK(nb_window_toggle_maximized(&window, bounds));
    CHECK(window.maximized);
    CHECK(window.frame.x == 0);
    CHECK(window.frame.y == 0);
    CHECK(window.frame.width == 800);
    CHECK(window.frame.height == 600);
    CHECK(nb_window_hit_test(&window, 790, 590) == NB_WINDOW_HIT_FRAME);
    {
        const struct nb_rect maximized_maximize = nb_window_maximize_rect(&window);
        const int restored_maximize_x =
            maximized_maximize.x + (maximized_maximize.width / 2);
        const int restored_maximize_y =
            maximized_maximize.y + (maximized_maximize.height / 2);

        CHECK(nb_window_hit_test(&window,
                                 restored_maximize_x,
                                 restored_maximize_y) ==
              NB_WINDOW_HIT_MAXIMIZE);
    }
    CHECK(nb_window_pointer_down(&window, 150, 10) == NB_WINDOW_HIT_TITLE);
    CHECK(!window.maximized);
    CHECK(window.frame.x == 100);
    CHECK(window.frame.y == 80);
    CHECK(window.frame.width == 320);
    CHECK(window.frame.height == 220);
}

static void test_resizing(void)
{
    struct nb_window window = make_window();
    const struct nb_rect bounds = {0, 0, 800, 600};
    const struct nb_rect offset_bounds = {20, 30, 500, 400};
    const struct nb_rect empty_bounds = {0, 0, 0, 0};
    struct nb_rect resize = nb_window_resize_rect(&window);
    int resize_x = resize.x + (resize.width / 2);
    int resize_y = resize.y + (resize.height / 2);

    CHECK(nb_window_pointer_down(&window, resize_x, resize_y) ==
          NB_WINDOW_HIT_RESIZE);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_RESIZE);
    CHECK(window.pointer_offset_x == 13);
    CHECK(window.pointer_offset_y == 13);
    CHECK(!nb_window_pointer_move(&window, resize_x, resize_y, bounds));
    CHECK(window.frame.width == 320);
    CHECK(window.frame.height == 220);

    CHECK(nb_window_pointer_move(&window, 500, 400, bounds));
    CHECK(window.frame.x == 100);
    CHECK(window.frame.y == 80);
    CHECK(window.frame.width == 413);
    CHECK(window.frame.height == 333);

    CHECK(nb_window_pointer_move(&window, -100, -100, bounds));
    CHECK(window.frame.width == NB_WINDOW_MIN_WIDTH);
    CHECK(window.frame.height == NB_WINDOW_MIN_HEIGHT);

    CHECK(nb_window_pointer_move(&window, -100, 1000, bounds));
    CHECK(window.frame.width == NB_WINDOW_MIN_WIDTH);
    CHECK(window.frame.height == 520);

    CHECK(nb_window_pointer_move(&window, 1000, 1000, bounds));
    CHECK(window.frame.width == 700);
    CHECK(window.frame.height == 520);
    CHECK(!nb_window_pointer_move(&window, 1000, 1000, bounds));

    CHECK(nb_window_pointer_move(&window, 1000, 1000, offset_bounds));
    CHECK(window.frame.width == 420);
    CHECK(window.frame.height == 350);
    CHECK(!nb_window_pointer_move(&window, 200, 200, empty_bounds));
    CHECK(window.frame.width == 420);
    CHECK(window.frame.height == 350);

    CHECK(nb_window_pointer_up(&window, 200, 200) == NB_WINDOW_ACTION_RESIZED);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!nb_window_pointer_move(&window, 300, 300, bounds));

    nb_window_init(&window, "Small desktop", (struct nb_rect){0, 0, 320, 220});
    resize = nb_window_resize_rect(&window);
    resize_x = resize.x + (resize.width / 2);
    resize_y = resize.y + (resize.height / 2);
    CHECK(nb_window_pointer_down(&window, resize_x, resize_y) ==
          NB_WINDOW_HIT_RESIZE);
    CHECK(nb_window_pointer_move(&window,
                                 500,
                                 500,
                                 (struct nb_rect){0, 0, 120, 70}));
    CHECK(window.frame.width == 120);
    CHECK(window.frame.height == 70);

    CHECK(nb_window_pointer_move(&window,
                                 500,
                                 500,
                                 (struct nb_rect){0, 0, 120, 40}));
    CHECK(window.frame.width == 120);
    CHECK(window.frame.height == 40);
    CHECK(nb_window_title_rect(&window).y +
              nb_window_title_rect(&window).height ==
          nb_window_footer_rect(&window).y);
    CHECK(nb_window_content_rect(&window).height == 0);
    CHECK(nb_window_footer_rect(&window).height == 10);
    CHECK(nb_window_resize_rect(&window).width == 10);
    CHECK(nb_window_hit_test(&window, 106, 27) == NB_WINDOW_HIT_FRAME);
    CHECK(nb_window_hit_test(&window, 107, 27) == NB_WINDOW_HIT_RESIZE);

    nb_window_pointer_cancel(&window);
    CHECK(window.pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!nb_window_pointer_move(&window, 300, 300, bounds));

    nb_window_init(&window, "Outside bounds", (struct nb_rect){100, 80, 320, 220});
    resize = nb_window_resize_rect(&window);
    CHECK(nb_window_pointer_down(&window,
                                 resize.x + (resize.width / 2),
                                 resize.y + (resize.height / 2)) ==
          NB_WINDOW_HIT_RESIZE);
    CHECK(!nb_window_pointer_move(&window,
                                  500,
                                  500,
                                  (struct nb_rect){0, 0, 100, 80}));
    CHECK(window.frame.width == 320);
    CHECK(window.frame.height == 220);
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
    test_title_ownership();
    test_dragging();
    test_clamping();
    test_maximize_toggle();
    test_resizing();
    test_close_tracking();
    test_cancel_and_repeated_down();

    if (failures != 0) {
        fprintf(stderr, "%d window model check(s) failed\n", failures);
        return 1;
    }

    puts("window model checks passed");
    return 0;
}
