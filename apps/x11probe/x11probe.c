#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NIXBENCH_X11_PROBE_VERSION
#define NIXBENCH_X11_PROBE_VERSION "development"
#endif

enum {
    NIXBENCH_X11_PROBE_DEFAULT_WIDTH = 720,
    NIXBENCH_X11_PROBE_DEFAULT_HEIGHT = 360,
    NIXBENCH_X11_PROBE_MARGIN = 24,
    NIXBENCH_X11_PROBE_LINE_GAP = 8,
    NIXBENCH_X11_PROBE_STATUS_CAPACITY = 96
};

struct x11probe_options {
    bool exit_after_first_paint;
    const char *title;
    const char *message;
};

struct x11probe_app {
    Display *display;
    int screen;
    Window window;
    GC gc;
    XFontStruct *font;
    Atom wm_delete_window;
    const char *title;
    const char *message;
    int width;
    int height;
    bool running;
    bool dirty;
    bool painted;
    bool exit_after_first_paint;
    unsigned long key_count;
    char last_key[NIXBENCH_X11_PROBE_STATUS_CAPACITY];
};

static void print_usage(const char *program)
{
    printf("Usage: %s [OPTION]\n", program);
    puts("Open a native X11 window through Xwayland on a NixBench desktop.\n");
    puts("  --title TEXT               set the window title");
    puts("  --message TEXT             set the probe message");
    puts("  --exit-after-first-paint   quit after the first redraw");
    puts("  --help                    show this help");
    puts("  --version                 show the probe version");
}

static bool parse_options(int argc,
                          char *argv[],
                          struct x11probe_options *options,
                          int *exit_status)
{
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--exit-after-first-paint") == 0) {
            options->exit_after_first_paint = true;
        } else if (strcmp(argv[index], "--title") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "%s: missing argument for --title\n",
                        argv[0]);
                *exit_status = 2;
                return false;
            }
            options->title = argv[++index];
        } else if (strcmp(argv[index], "--message") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "%s: missing argument for --message\n",
                        argv[0]);
                *exit_status = 2;
                return false;
            }
            options->message = argv[++index];
        } else if (strcmp(argv[index], "--help") == 0) {
            print_usage(argv[0]);
            return false;
        } else if (strcmp(argv[index], "--version") == 0) {
            printf("NixBench X11 probe %s\n", NIXBENCH_X11_PROBE_VERSION);
            return false;
        } else {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[index]);
            fprintf(stderr, "Try '%s --help' for more information.\n",
                    argv[0]);
            *exit_status = 2;
            return false;
        }
    }
    return true;
}

static void fail_app(struct x11probe_app *app, const char *message)
{
    fprintf(stderr, "nixbench-x11-probe: %s\n", message);
    app->running = false;
}

static void remember_key(struct x11probe_app *app,
                         KeySym keysym,
                         const char *text)
{
    const char *label = NULL;

    if (keysym == XK_Escape) {
        label = "Escape";
    } else if (keysym == XK_Return) {
        label = "Return";
    } else if (keysym == XK_BackSpace) {
        label = "Backspace";
    } else if (keysym == XK_Left) {
        label = "Left";
    } else if (keysym == XK_Right) {
        label = "Right";
    } else if (keysym == XK_Up) {
        label = "Up";
    } else if (keysym == XK_Down) {
        label = "Down";
    } else if (text != NULL && text[0] != '\0' &&
               text[1] == '\0' &&
               isprint((unsigned char)text[0])) {
        app->last_key[0] = text[0];
        app->last_key[1] = '\0';
        return;
    } else if (keysym != NoSymbol) {
        label = XKeysymToString(keysym);
    }

    if (label == NULL || label[0] == '\0') {
        label = "unknown";
    }
    (void)snprintf(app->last_key,
                   sizeof(app->last_key),
                   "%s",
                   label);
}

static void draw_text_line(Display *display,
                           Window window,
                           GC gc,
                           XFontStruct *font,
                           int x,
                           int *y,
                           const char *text)
{
    const int line_height =
        font != NULL ? font->ascent + font->descent + NIXBENCH_X11_PROBE_LINE_GAP
                     : 24;

    if (font != NULL) {
        XDrawString(display, window, gc, x, *y + font->ascent, text,
                    (int)strlen(text));
    } else {
        XDrawString(display, window, gc, x, *y + 16, text,
                    (int)strlen(text));
    }
    *y += line_height;
}

static void redraw(struct x11probe_app *app)
{
    const char *instructions =
        "Press keys to update the status line. Press Escape or q to quit.";
    char status[NIXBENCH_X11_PROBE_STATUS_CAPACITY * 2U];
    int y = NIXBENCH_X11_PROBE_MARGIN;

    XSetForeground(app->display, app->gc, WhitePixel(app->display,
                                                     app->screen));
    XFillRectangle(app->display,
                   app->window,
                   app->gc,
                   0,
                   0,
                   (unsigned int)app->width,
                   (unsigned int)app->height);
    XSetForeground(app->display,
                   app->gc,
                   BlackPixel(app->display, app->screen));

    draw_text_line(app->display,
                   app->window,
                   app->gc,
                   app->font,
                   NIXBENCH_X11_PROBE_MARGIN,
                   &y,
                   app->title);
    draw_text_line(app->display,
                   app->window,
                   app->gc,
                   app->font,
                   NIXBENCH_X11_PROBE_MARGIN,
                   &y,
                   app->message);
    draw_text_line(app->display,
                   app->window,
                   app->gc,
                   app->font,
                   NIXBENCH_X11_PROBE_MARGIN,
                   &y,
                   instructions);
    (void)snprintf(status,
                   sizeof(status),
                   "Last key: %s   Key presses: %lu",
                   app->last_key[0] != '\0' ? app->last_key : "(none)",
                   app->key_count);
    draw_text_line(app->display,
                   app->window,
                   app->gc,
                   app->font,
                   NIXBENCH_X11_PROBE_MARGIN,
                   &y,
                   status);
    draw_text_line(app->display,
                   app->window,
                   app->gc,
                   app->font,
                   NIXBENCH_X11_PROBE_MARGIN,
                   &y,
                   "Window manager close buttons are also accepted.");
    XFlush(app->display);
    app->dirty = false;
    app->painted = true;
    if (app->exit_after_first_paint) {
        app->running = false;
    }
}

static bool initialize_app(struct x11probe_app *app,
                           const struct x11probe_options *options)
{
    XSetWindowAttributes attributes;
    long event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
    const unsigned int border_width = 1U;
    const unsigned long background = WhitePixel(app->display, app->screen);
    const unsigned long border = BlackPixel(app->display, app->screen);
    Atom wm_delete;

    app->screen = DefaultScreen(app->display);
    app->width = NIXBENCH_X11_PROBE_DEFAULT_WIDTH;
    app->height = NIXBENCH_X11_PROBE_DEFAULT_HEIGHT;
    app->running = true;
    app->dirty = true;
    app->painted = false;
    app->exit_after_first_paint = options->exit_after_first_paint;
    app->title = options->title;
    app->message = options->message;
    app->key_count = 0;
    app->last_key[0] = '\0';

    attributes.background_pixel = background;
    attributes.border_pixel = border;
    app->window = XCreateWindow(app->display,
                                RootWindow(app->display, app->screen),
                                80,
                                72,
                                (unsigned int)app->width,
                                (unsigned int)app->height,
                                border_width,
                                CopyFromParent,
                                InputOutput,
                                CopyFromParent,
                                CWBackPixel | CWBorderPixel,
                                &attributes);
    if (app->window == 0) {
        fail_app(app, "could not create the X11 window");
        return false;
    }

    app->gc = XCreateGC(app->display, app->window, 0, NULL);
    if (app->gc == 0) {
        fail_app(app, "could not create the X11 graphics context");
        return false;
    }
    app->font = XLoadQueryFont(app->display, "fixed");
    if (app->font != NULL) {
        XSetFont(app->display, app->gc, app->font->fid);
    }

    XSelectInput(app->display, app->window, event_mask);
    XStoreName(app->display, app->window, options->title);
    XSetIconName(app->display, app->window, options->title);
    XMapWindow(app->display, app->window);

    wm_delete = XInternAtom(app->display, "WM_DELETE_WINDOW", False);
    app->wm_delete_window = wm_delete;
    if (wm_delete != None) {
        XSetWMProtocols(app->display, app->window, &wm_delete, 1);
    }

    XFlush(app->display);
    return true;
}

static void destroy_app(struct x11probe_app *app)
{
    if (app->font != NULL) {
        XFreeFont(app->display, app->font);
    }
    if (app->gc != 0) {
        XFreeGC(app->display, app->gc);
    }
    if (app->window != 0) {
        XDestroyWindow(app->display, app->window);
    }
    if (app->display != NULL) {
        XCloseDisplay(app->display);
    }
    memset(app, 0, sizeof(*app));
}

static void handle_key(struct x11probe_app *app, XKeyEvent *event)
{
    char text[32];
    KeySym keysym = NoSymbol;
    int count;

    text[0] = '\0';
    count = XLookupString(event, text, sizeof(text) - 1, &keysym, NULL);
    if (count >= 0 && count < (int)sizeof(text)) {
        text[count] = '\0';
    }
    if (keysym == XK_Escape || keysym == XK_q || keysym == XK_Q) {
        app->running = false;
    } else if (keysym == XK_Return) {
        app->dirty = true;
    } else if (keysym == XK_BackSpace) {
        app->dirty = true;
    } else if (keysym == XK_Left ||
               keysym == XK_Right ||
               keysym == XK_Up ||
               keysym == XK_Down) {
        app->dirty = true;
    } else if (count > 0) {
        app->dirty = true;
    }

    remember_key(app, keysym, text);
    ++app->key_count;
}

static bool run_app(struct x11probe_app *app)
{
    while (app->running) {
        XEvent event;

        XNextEvent(app->display, &event);
        if (event.type == Expose) {
            if (event.xexpose.count == 0) {
                app->dirty = true;
            }
        } else if (event.type == ConfigureNotify) {
            app->width = event.xconfigure.width;
            app->height = event.xconfigure.height;
            app->dirty = true;
        } else if (event.type == KeyPress) {
            handle_key(app, &event.xkey);
        } else if (event.type == ClientMessage) {
            if ((Atom)event.xclient.data.l[0] == app->wm_delete_window) {
                app->running = false;
            }
        }

        if (app->dirty) {
            redraw(app);
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    struct x11probe_options options = {
        false,
        "NixBench Xwayland probe",
        "This is a native X11 client running through Xwayland."
    };
    struct x11probe_app app;
    int exit_status = 0;

    if (!parse_options(argc, argv, &options, &exit_status)) {
        return exit_status;
    }

    memset(&app, 0, sizeof(app));
    app.display = XOpenDisplay(NULL);
    if (app.display == NULL) {
        fprintf(stderr, "nixbench-x11-probe: could not connect to DISPLAY\n");
        return 1;
    }
    if (!initialize_app(&app, &options) || !run_app(&app)) {
        exit_status = 1;
    }
    destroy_app(&app);
    return exit_status;
}
