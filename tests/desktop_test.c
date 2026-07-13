#include <stdbool.h>
#include <stdio.h>

#include "desktop.h"

static int failures;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #expression);                         \
            ++failures;                                                       \
        }                                                                     \
    } while (false)

struct fixture {
    struct nb_desktop desktop;
    nb_window_id a;
    nb_window_id b;
};

static struct fixture make_fixture(void)
{
    struct fixture fixture;
    const struct nb_rect frame_a = {40, 40, 300, 220};
    const struct nb_rect frame_b = {140, 90, 300, 220};

    nb_desktop_init(&fixture.desktop);
    fixture.a = nb_desktop_open_window(&fixture.desktop, "A", frame_a);
    fixture.b = nb_desktop_open_window(&fixture.desktop, "B", frame_b);
    return fixture;
}

static void check_invariants(const struct nb_desktop *desktop)
{
    const nb_window_id active = nb_desktop_active_window_id(desktop);
    size_t active_count = 0;
    size_t interaction_count = 0;
    size_t first;

    for (first = 0; first < nb_desktop_window_count(desktop); ++first) {
        const nb_window_id first_id =
            nb_desktop_window_id_at(desktop, first);
        const struct nb_window *window =
            nb_desktop_find_window(desktop, first_id);
        size_t second;

        CHECK(first_id != NB_WINDOW_ID_NONE);
        CHECK(window != NULL);
        if (window != NULL && window->active) {
            ++active_count;
            CHECK(first_id == active);
        }
        if (window != NULL &&
            window->pointer_mode != NB_WINDOW_POINTER_IDLE) {
            ++interaction_count;
            CHECK(first_id == desktop->pointer_window);
        }

        for (second = first + 1;
             second < nb_desktop_window_count(desktop);
             ++second) {
            CHECK(first_id != nb_desktop_window_id_at(desktop, second));
        }
    }

    if (active == NB_WINDOW_ID_NONE) {
        CHECK(active_count == 0);
    } else {
        CHECK(active_count == 1);
        CHECK(nb_desktop_find_window(desktop, active) != NULL);
    }

    if (nb_desktop_has_pointer_interaction(desktop)) {
        const struct nb_window *pointer_window =
            nb_desktop_find_window(desktop, desktop->pointer_window);

        CHECK(pointer_window != NULL);
        if (pointer_window != NULL) {
            CHECK(pointer_window->pointer_mode != NB_WINDOW_POINTER_IDLE);
        }
        CHECK(interaction_count == 1);
    } else {
        CHECK(interaction_count == 0);
    }
}

static void test_stacking_and_focus(void)
{
    struct fixture fixture = make_fixture();
    struct nb_desktop_action action;
    const struct nb_window *window_a;
    const struct nb_window *window_b;

    CHECK(fixture.a != NB_WINDOW_ID_NONE);
    CHECK(fixture.b != NB_WINDOW_ID_NONE);
    CHECK(fixture.a != fixture.b);
    CHECK(nb_desktop_window_count(&fixture.desktop) == 2);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 0) == fixture.a);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.b);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 2) ==
          NB_WINDOW_ID_NONE);
    CHECK(nb_desktop_window_at(&fixture.desktop, 2) == NULL);
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.b);

    window_a = nb_desktop_find_window(&fixture.desktop, fixture.a);
    window_b = nb_desktop_find_window(&fixture.desktop, fixture.b);
    CHECK(window_a != NULL && !window_a->active);
    CHECK(window_b != NULL && window_b->active);

    CHECK(nb_desktop_pointer_down(&fixture.desktop, 200, 150) ==
          NB_WINDOW_HIT_CONTENT);
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.b);
    action = nb_desktop_pointer_up(&fixture.desktop, 200, 150);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);
    CHECK(action.window == NB_WINDOW_ID_NONE);

    CHECK(nb_desktop_pointer_down(&fixture.desktop, 80, 150) ==
          NB_WINDOW_HIT_CONTENT);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 0) == fixture.b);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.a);
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.a);
    action = nb_desktop_pointer_up(&fixture.desktop, 80, 150);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);
    CHECK(nb_desktop_pointer_down(&fixture.desktop, 200, 150) ==
          NB_WINDOW_HIT_CONTENT);
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.a);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.a);
    action = nb_desktop_pointer_up(&fixture.desktop, 200, 150);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);
    CHECK(nb_desktop_pointer_down(&fixture.desktop, 400, 150) ==
          NB_WINDOW_HIT_CONTENT);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 0) == fixture.a);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.b);
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.b);
    action = nb_desktop_pointer_up(&fixture.desktop, 400, 150);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);

    CHECK(nb_desktop_pointer_down(&fixture.desktop, 790, 590) ==
          NB_WINDOW_HIT_NONE);
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == NB_WINDOW_ID_NONE);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 0) == fixture.a);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.b);
    check_invariants(&fixture.desktop);
}

static void test_drag_capture_routing(void)
{
    struct fixture fixture = make_fixture();
    const struct nb_rect bounds = {0, 0, 800, 600};
    const struct nb_window *window_a =
        nb_desktop_find_window(&fixture.desktop, fixture.a);
    const struct nb_window *window_b =
        nb_desktop_find_window(&fixture.desktop, fixture.b);
    struct nb_desktop_action action;

    CHECK(window_a != NULL);
    CHECK(window_b != NULL);
    CHECK(nb_desktop_pointer_down(&fixture.desktop, 100, 50) ==
          NB_WINDOW_HIT_TITLE);
    CHECK(nb_desktop_has_pointer_interaction(&fixture.desktop));
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.a);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.a);

    CHECK(nb_desktop_pointer_down(&fixture.desktop, 400, 150) ==
          NB_WINDOW_HIT_NONE);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.a);
    CHECK(nb_desktop_pointer_move(&fixture.desktop, 160, 100, bounds));
    CHECK(window_a->frame.x == 100);
    CHECK(window_a->frame.y == 90);
    CHECK(window_b->frame.x == 140);
    CHECK(window_b->frame.y == 90);
    CHECK(window_b->pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!window_b->close_pressed);

    action = nb_desktop_pointer_up(&fixture.desktop, 160, 100);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);
    CHECK(!nb_desktop_has_pointer_interaction(&fixture.desktop));
    CHECK(!nb_desktop_pointer_move(&fixture.desktop, 300, 300, bounds));
    CHECK(window_a->frame.x == 100);
    CHECK(window_a->frame.y == 90);
    check_invariants(&fixture.desktop);
}

static void test_close_request_and_lifecycle(void)
{
    struct fixture fixture = make_fixture();
    const struct nb_rect bounds = {0, 0, 800, 600};
    const struct nb_window *window_a =
        nb_desktop_find_window(&fixture.desktop, fixture.a);
    const struct nb_window *window_b =
        nb_desktop_find_window(&fixture.desktop, fixture.b);
    struct nb_rect close_a;
    struct nb_rect close_b;
    struct nb_desktop_action action;
    int a_x;
    int a_y;
    int b_x;
    int b_y;

    CHECK(window_a != NULL);
    CHECK(window_b != NULL);
    close_a = nb_window_close_rect(window_a);
    close_b = nb_window_close_rect(window_b);
    a_x = close_a.x + (close_a.width / 2);
    a_y = close_a.y + (close_a.height / 2);
    b_x = close_b.x + (close_b.width / 2);
    b_y = close_b.y + (close_b.height / 2);

    CHECK(nb_desktop_pointer_down(&fixture.desktop, a_x, a_y) ==
          NB_WINDOW_HIT_CLOSE);
    CHECK(nb_desktop_pointer_move(&fixture.desktop, b_x, b_y, bounds));
    CHECK(!window_a->close_pressed);
    CHECK(window_b->pointer_mode == NB_WINDOW_POINTER_IDLE);
    action = nb_desktop_pointer_up(&fixture.desktop, b_x, b_y);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);
    CHECK(nb_desktop_window_count(&fixture.desktop) == 2);

    CHECK(nb_desktop_pointer_down(&fixture.desktop, a_x, a_y) ==
          NB_WINDOW_HIT_CLOSE);
    action = nb_desktop_pointer_up(&fixture.desktop, a_x, a_y);
    CHECK(action.type == NB_WINDOW_ACTION_CLOSE_REQUESTED);
    CHECK(action.window == fixture.a);
    CHECK(nb_desktop_window_count(&fixture.desktop) == 2);
    CHECK(nb_desktop_find_window(&fixture.desktop, fixture.a) != NULL);

    CHECK(nb_desktop_destroy_window(&fixture.desktop, action.window));
    CHECK(nb_desktop_window_count(&fixture.desktop) == 1);
    CHECK(nb_desktop_find_window(&fixture.desktop, fixture.a) == NULL);
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.b);
    CHECK(!nb_desktop_destroy_window(&fixture.desktop, fixture.a));
    check_invariants(&fixture.desktop);
}

static void test_resize_capture_routing(void)
{
    struct fixture fixture = make_fixture();
    const struct nb_rect bounds = {0, 0, 800, 600};
    const struct nb_window *window_a =
        nb_desktop_find_window(&fixture.desktop, fixture.a);
    const struct nb_window *window_b =
        nb_desktop_find_window(&fixture.desktop, fixture.b);
    struct nb_desktop_action action;
    struct nb_rect resize_a;
    struct nb_rect close_b;
    int resize_x;
    int resize_y;
    int close_x;
    int close_y;

    CHECK(window_a != NULL);
    CHECK(window_b != NULL);

    CHECK(nb_desktop_pointer_down(&fixture.desktop, 80, 150) ==
          NB_WINDOW_HIT_CONTENT);
    action = nb_desktop_pointer_up(&fixture.desktop, 80, 150);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.a);

    resize_a = nb_window_resize_rect(window_a);
    close_b = nb_window_close_rect(window_b);
    resize_x = resize_a.x + (resize_a.width / 2);
    resize_y = resize_a.y + (resize_a.height / 2);
    close_x = close_b.x + (close_b.width / 2);
    close_y = close_b.y + (close_b.height / 2);

    CHECK(nb_desktop_pointer_down(&fixture.desktop, resize_x, resize_y) ==
          NB_WINDOW_HIT_RESIZE);
    CHECK(nb_desktop_has_pointer_interaction(&fixture.desktop));
    CHECK(window_a->pointer_mode == NB_WINDOW_POINTER_RESIZE);
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.a);
    CHECK(nb_desktop_pointer_down(&fixture.desktop, close_x, close_y) ==
          NB_WINDOW_HIT_NONE);

    CHECK(nb_desktop_pointer_move(&fixture.desktop,
                                  close_x,
                                  close_y,
                                  bounds));
    CHECK(window_a->frame.width == NB_WINDOW_MIN_WIDTH);
    CHECK(window_a->frame.height == NB_WINDOW_MIN_HEIGHT);
    CHECK(window_b->frame.width == 300);
    CHECK(window_b->frame.height == 220);
    CHECK(window_b->pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!window_b->close_pressed);

    CHECK(nb_desktop_pointer_move(&fixture.desktop, 600, 500, bounds));
    CHECK(window_a->frame.x == 40);
    CHECK(window_a->frame.y == 40);
    CHECK(window_a->frame.width == 573);
    CHECK(window_a->frame.height == 473);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 0) == fixture.b);
    CHECK(nb_desktop_window_id_at(&fixture.desktop, 1) == fixture.a);

    action = nb_desktop_pointer_up(&fixture.desktop, 600, 500);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);
    CHECK(action.window == NB_WINDOW_ID_NONE);
    CHECK(!nb_desktop_has_pointer_interaction(&fixture.desktop));
    CHECK(window_a->pointer_mode == NB_WINDOW_POINTER_IDLE);
    check_invariants(&fixture.desktop);
}

static void test_active_fallback(void)
{
    struct fixture fixture = make_fixture();
    const struct nb_rect frame_c = {500, 50, 200, 160};
    const nb_window_id c =
        nb_desktop_open_window(&fixture.desktop, "C", frame_c);

    CHECK(nb_desktop_active_window_id(&fixture.desktop) == c);
    CHECK(nb_desktop_destroy_window(&fixture.desktop, c));
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.b);
    CHECK(nb_desktop_destroy_window(&fixture.desktop, fixture.a));
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.b);
    CHECK(nb_desktop_destroy_window(&fixture.desktop, fixture.b));
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == NB_WINDOW_ID_NONE);
    CHECK(nb_desktop_window_count(&fixture.desktop) == 0);
    check_invariants(&fixture.desktop);
}

static void test_clamp_all(void)
{
    struct nb_desktop desktop;
    const struct nb_rect bounds = {0, 0, 800, 600};
    nb_window_id a;
    nb_window_id b;
    nb_window_id c;
    const struct nb_window *window;

    nb_desktop_init(&desktop);
    a = nb_desktop_open_window(&desktop, "A", (struct nb_rect){-50, -60, 300, 220});
    b = nb_desktop_open_window(&desktop, "B", (struct nb_rect){700, 500, 300, 220});
    c = nb_desktop_open_window(&desktop, "C", (struct nb_rect){10, 20, 900, 700});

    CHECK(nb_desktop_clamp_windows(&desktop, bounds));
    window = nb_desktop_find_window(&desktop, a);
    CHECK(window != NULL && window->frame.x == 0 && window->frame.y == 0);
    window = nb_desktop_find_window(&desktop, b);
    CHECK(window != NULL && window->frame.x == 500 && window->frame.y == 380);
    window = nb_desktop_find_window(&desktop, c);
    CHECK(window != NULL && window->frame.x == 0 && window->frame.y == 0);
    CHECK(!nb_desktop_clamp_windows(&desktop, bounds));
    CHECK(nb_desktop_window_id_at(&desktop, 0) == a);
    CHECK(nb_desktop_window_id_at(&desktop, 1) == b);
    CHECK(nb_desktop_window_id_at(&desktop, 2) == c);
    check_invariants(&desktop);
}

static void test_capacity_and_stale_ids(void)
{
    struct nb_desktop desktop;
    nb_window_id ids[NB_DESKTOP_MAX_WINDOWS];
    const struct nb_window *addresses[NB_DESKTOP_MAX_WINDOWS];
    size_t index;
    size_t other;
    nb_window_id replacement;
    nb_window_id active_before_overflow;
    nb_window_id back_before_overflow;
    nb_window_id front_before_overflow;

    nb_desktop_init(&desktop);
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        ids[index] = nb_desktop_open_window(
            &desktop,
            "Capacity window",
            (struct nb_rect){(int)(index * 10), (int)(index * 8), 160, 100});
        addresses[index] = nb_desktop_find_window(&desktop, ids[index]);
        CHECK(ids[index] != NB_WINDOW_ID_NONE);
        CHECK(addresses[index] != NULL);
        for (other = 0; other < index; ++other) {
            CHECK(ids[index] != ids[other]);
        }
    }

    CHECK(nb_desktop_window_count(&desktop) == NB_DESKTOP_MAX_WINDOWS);
    active_before_overflow = nb_desktop_active_window_id(&desktop);
    back_before_overflow = nb_desktop_window_id_at(&desktop, 0);
    front_before_overflow = nb_desktop_window_id_at(
        &desktop, NB_DESKTOP_MAX_WINDOWS - 1);
    CHECK(nb_desktop_open_window(&desktop,
                                 "Overflow",
                                 (struct nb_rect){0, 0, 100, 100}) ==
          NB_WINDOW_ID_NONE);
    CHECK(nb_desktop_window_count(&desktop) == NB_DESKTOP_MAX_WINDOWS);
    CHECK(nb_desktop_active_window_id(&desktop) == active_before_overflow);
    CHECK(nb_desktop_window_id_at(&desktop, 0) == back_before_overflow);
    CHECK(nb_desktop_window_id_at(&desktop,
                                  NB_DESKTOP_MAX_WINDOWS - 1) ==
          front_before_overflow);
    CHECK(nb_desktop_raise_window(&desktop, ids[0]));
    CHECK(nb_desktop_window_id_at(&desktop,
                                  NB_DESKTOP_MAX_WINDOWS - 1) == ids[0]);
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        CHECK(nb_desktop_find_window(&desktop, ids[index]) ==
              addresses[index]);
    }

    CHECK(nb_desktop_destroy_window(&desktop, ids[5]));
    CHECK(nb_desktop_find_window(&desktop, ids[5]) == NULL);
    replacement = nb_desktop_open_window(
        &desktop, "Replacement", (struct nb_rect){20, 20, 180, 120});
    CHECK(replacement != NB_WINDOW_ID_NONE);
    CHECK(replacement != ids[5]);
    CHECK(nb_desktop_find_window(&desktop, ids[5]) == NULL);
    CHECK(nb_desktop_window_count(&desktop) == NB_DESKTOP_MAX_WINDOWS);
    for (index = 0; index < NB_DESKTOP_MAX_WINDOWS; ++index) {
        if (index != 5) {
            CHECK(nb_desktop_find_window(&desktop, ids[index]) ==
                  addresses[index]);
        }
    }
    check_invariants(&desktop);
}

static void test_cancel(void)
{
    struct fixture fixture = make_fixture();
    const struct nb_rect bounds = {0, 0, 800, 600};
    const struct nb_window *window_a =
        nb_desktop_find_window(&fixture.desktop, fixture.a);
    struct nb_desktop_action action;
    struct nb_rect close;
    int close_x;
    int close_y;

    CHECK(nb_desktop_pointer_down(&fixture.desktop, 100, 50) ==
          NB_WINDOW_HIT_TITLE);
    nb_desktop_pointer_cancel(&fixture.desktop);
    CHECK(!nb_desktop_has_pointer_interaction(&fixture.desktop));
    CHECK(window_a != NULL &&
          window_a->pointer_mode == NB_WINDOW_POINTER_IDLE);
    CHECK(!nb_desktop_pointer_move(&fixture.desktop, 300, 300, bounds));
    action = nb_desktop_pointer_up(&fixture.desktop, 300, 300);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);

    close = nb_window_close_rect(window_a);
    close_x = close.x + (close.width / 2);
    close_y = close.y + (close.height / 2);
    CHECK(nb_desktop_pointer_down(&fixture.desktop, close_x, close_y) ==
          NB_WINDOW_HIT_CLOSE);
    CHECK(window_a->close_pressed);
    nb_desktop_pointer_cancel(&fixture.desktop);
    CHECK(!window_a->close_pressed);
    CHECK(window_a->pointer_mode == NB_WINDOW_POINTER_IDLE);
    check_invariants(&fixture.desktop);
}

static void test_close_captured_window(void)
{
    struct fixture fixture = make_fixture();
    struct nb_desktop_action action;

    CHECK(nb_desktop_pointer_down(&fixture.desktop, 100, 50) ==
          NB_WINDOW_HIT_TITLE);
    CHECK(nb_desktop_has_pointer_interaction(&fixture.desktop));
    CHECK(nb_desktop_destroy_window(&fixture.desktop, fixture.a));
    CHECK(!nb_desktop_has_pointer_interaction(&fixture.desktop));
    CHECK(nb_desktop_active_window_id(&fixture.desktop) == fixture.b);
    action = nb_desktop_pointer_up(&fixture.desktop, 200, 200);
    CHECK(action.type == NB_WINDOW_ACTION_NONE);
    CHECK(action.window == NB_WINDOW_ID_NONE);
    CHECK(!nb_desktop_raise_window(&fixture.desktop, fixture.a));
    check_invariants(&fixture.desktop);
}

int main(void)
{
    test_stacking_and_focus();
    test_drag_capture_routing();
    test_close_request_and_lifecycle();
    test_resize_capture_routing();
    test_active_fallback();
    test_clamp_all();
    test_capacity_and_stale_ids();
    test_cancel();
    test_close_captured_window();

    if (failures != 0) {
        fprintf(stderr, "%d desktop model check(s) failed\n", failures);
        return 1;
    }

    puts("desktop model checks passed");
    return 0;
}
