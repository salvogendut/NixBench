#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef NIXBENCH_HTML_APP_VERSION
#define NIXBENCH_HTML_APP_VERSION "development"
#endif

#ifndef NIXBENCH_HTML_APP_SOURCE_DIRECTORY
#define NIXBENCH_HTML_APP_SOURCE_DIRECTORY "apps/html"
#endif

#ifndef NIXBENCH_HTML_APP_INSTALL_DIRECTORY
#define NIXBENCH_HTML_APP_INSTALL_DIRECTORY "/usr/local/share/nixbench/apps"
#endif

enum {
    HTML_APP_PATH_CAPACITY = PATH_MAX,
    HTML_APP_DEFAULT_WIDTH = 420,
    HTML_APP_DEFAULT_HEIGHT = 420
};

struct html_app_spec {
    const char *id;
    const char *title;
    const char *entrypoint;
    int width;
    int height;
};

struct html_app_options {
    const struct html_app_spec *app;
    bool check_only;
    bool show_seconds;
};

static const struct html_app_spec html_apps[] = {
    {
        "nixclock",
        "NixClock",
        "index.html",
        HTML_APP_DEFAULT_WIDTH,
        HTML_APP_DEFAULT_HEIGHT
    }
};

static void print_usage(const char *program)
{
    printf("Usage: %s [OPTION]\n", program);
    puts("Run a bundled NixBench HTML application.\n");
    puts("  --app APP         select the HTML application (default: nixclock)");
    puts("  --show-seconds    initially show NixClock's red seconds hand");
    puts("  --check           validate the selected application without a display");
    puts("  --help            show this help");
    puts("  --version         show the HTML application host version");
}

static const struct html_app_spec *find_app(const char *id)
{
    size_t index;

    for (index = 0; index < sizeof(html_apps) / sizeof(html_apps[0]); ++index) {
        if (strcmp(html_apps[index].id, id) == 0) {
            return &html_apps[index];
        }
    }
    return NULL;
}

static int parse_options(int argc,
                         char *argv[],
                         struct html_app_options *options)
{
    int index;

    options->app = &html_apps[0];
    options->check_only = false;
    options->show_seconds = false;
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--app") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "%s: --app requires a value\n", argv[0]);
                return -1;
            }
            options->app = find_app(argv[index]);
            if (options->app == NULL) {
                fprintf(stderr,
                        "%s: unknown HTML application: %s\n",
                        argv[0],
                        argv[index]);
                return -1;
            }
        } else if (strcmp(argv[index], "--show-seconds") == 0) {
            options->show_seconds = true;
        } else if (strcmp(argv[index], "--check") == 0) {
            options->check_only = true;
        } else if (strcmp(argv[index], "--help") == 0) {
            print_usage(argv[0]);
            return 1;
        } else if (strcmp(argv[index], "--version") == 0) {
            printf("NixBench HTML application host %s\n",
                   NIXBENCH_HTML_APP_VERSION);
            return 1;
        } else {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[index]);
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return -1;
        }
    }
    return 0;
}

static bool entrypoint_at(const char *root,
                          const struct html_app_spec *app,
                          char path[HTML_APP_PATH_CAPACITY])
{
    const int length = snprintf(path,
                                HTML_APP_PATH_CAPACITY,
                                "%s/%s/%s",
                                root,
                                app->id,
                                app->entrypoint);

    return length >= 0 && length < HTML_APP_PATH_CAPACITY &&
           access(path, R_OK) == 0;
}

static bool resolve_entrypoint(const struct html_app_spec *app,
                               char path[HTML_APP_PATH_CAPACITY])
{
    const char *override = getenv("NIXBENCH_HTML_APP_DIRECTORY");

    if (override != NULL && override[0] != '\0') {
        return entrypoint_at(override, app, path);
    }
    if (entrypoint_at(NIXBENCH_HTML_APP_INSTALL_DIRECTORY, app, path)) {
        return true;
    }
    return entrypoint_at(NIXBENCH_HTML_APP_SOURCE_DIRECTORY, app, path);
}

static void configure_web_view(WebKitWebView *web_view)
{
    WebKitSettings *settings = webkit_web_view_get_settings(web_view);
    const GdkRGBA transparent = {0.0, 0.0, 0.0, 0.0};

    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_html5_local_storage(settings, FALSE);
    webkit_settings_set_enable_page_cache(settings, FALSE);
    webkit_settings_set_enable_media_stream(settings, FALSE);
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, FALSE);
    webkit_web_view_set_background_color(web_view, &transparent);
}

static void window_realized(GtkWidget *widget, gpointer data)
{
    const struct html_app_spec *app = data;
    GdkWindow *window = gtk_widget_get_window(widget);

    if (window != NULL && GDK_IS_WAYLAND_WINDOW(window)) {
        char application_id[128];
        const int length = snprintf(application_id,
                                    sizeof(application_id),
                                    "org.nixbench.%s",
                                    app->id);

        if (length >= 0 && (size_t)length < sizeof(application_id)) {
            gdk_wayland_window_set_application_id(window, application_id);
        }
    }
}

static gboolean suppress_context_menu(WebKitWebView *web_view,
                                      WebKitContextMenu *menu,
                                      GdkEvent *event,
                                      WebKitHitTestResult *result,
                                      gpointer data)
{
    (void)web_view;
    (void)menu;
    (void)event;
    (void)result;
    (void)data;
    return TRUE;
}

static gboolean load_failed(WebKitWebView *web_view,
                            WebKitLoadEvent event,
                            gchar *uri,
                            GError *error,
                            gpointer data)
{
    (void)web_view;
    (void)event;
    (void)data;
    fprintf(stderr,
            "Could not load NixBench HTML application %s: %s\n",
            uri != NULL ? uri : "document",
            error != NULL ? error->message : "unknown error");
    return FALSE;
}

int main(int argc, char *argv[])
{
    struct html_app_options options;
    char entrypoint[HTML_APP_PATH_CAPACITY];
    WebKitWebContext *context;
    WebKitWebView *web_view;
    GtkWidget *window;
    gchar *uri;
    gchar *uri_with_query = NULL;
    GError *error = NULL;
    int gtk_argc = 1;
    char *gtk_arguments[] = {argv[0], NULL};
    char **gtk_argv = gtk_arguments;
    int parse_result = parse_options(argc, argv, &options);

    if (parse_result != 0) {
        return parse_result > 0 ? 0 : 2;
    }
    if (!resolve_entrypoint(options.app, entrypoint)) {
        fprintf(stderr,
                "Could not find the %s HTML application bundle: %s\n",
                options.app->title,
                strerror(errno));
        return 1;
    }
    if (options.check_only) {
        printf("%s HTML application: %s\n", options.app->title, entrypoint);
        return 0;
    }
    if (setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0) != 0) {
        fprintf(stderr,
                "Could not select software WebKit composition: %s\n",
                strerror(errno));
        return 1;
    }
    if (!gtk_init_check(&gtk_argc, &gtk_argv)) {
        fputs("Could not initialize the GTK display for HTML application\n",
              stderr);
        return 1;
    }

    uri = g_filename_to_uri(entrypoint, NULL, &error);
    if (uri == NULL) {
        fprintf(stderr,
                "Could not create the HTML application URI: %s\n",
                error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        return 1;
    }
    if (options.show_seconds) {
        uri_with_query = g_strconcat(uri, "?seconds=1", NULL);
    }

    g_set_application_name(options.app->title);
    context = webkit_web_context_new_ephemeral();
    web_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                                            "web-context",
                                            context,
                                            NULL));
    configure_web_view(web_view);
    g_signal_connect(web_view,
                     "context-menu",
                     G_CALLBACK(suppress_context_menu),
                     NULL);
    g_signal_connect(web_view, "load-failed", G_CALLBACK(load_failed), NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), options.app->title);
    gtk_window_set_default_size(GTK_WINDOW(window),
                                options.app->width,
                                options.app->height);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_widget_set_app_paintable(window, TRUE);
    {
        GdkVisual *visual = gdk_screen_get_rgba_visual(
            gtk_widget_get_screen(window));

        if (visual != NULL) {
            gtk_widget_set_visual(window, visual);
        }
    }
    g_signal_connect(window, "realize", G_CALLBACK(window_realized),
                     (gpointer)options.app);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(web_view));
    webkit_web_view_load_uri(web_view,
                             uri_with_query != NULL ? uri_with_query : uri);
    gtk_widget_show_all(window);
    gtk_main();

    g_free(uri_with_query);
    g_free(uri);
    g_object_unref(context);
    return 0;
}
