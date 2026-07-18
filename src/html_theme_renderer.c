#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include "theme_atlas.h"
#include "theme_bundle.h"

#ifndef NIXBENCH_HTML_THEME_RENDERER_VERSION
#define NIXBENCH_HTML_THEME_RENDERER_VERSION "unknown"
#endif

enum component_kind {
    COMPONENT_WINDOW,
    COMPONENT_DESKTOP,
    COMPONENT_MENUBAR
};

struct renderer_options {
    const char *theme_directory;
    const char *snapshot_path;
    enum component_kind component;
    int width;
    int height;
};

struct renderer_state {
    WebKitWebView *web_view;
    const char *snapshot_path;
    bool snapshot_started;
    int exit_status;
};

static void print_usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "Usage: %s --theme DIRECTORY [OPTIONS]\n"
            "\n"
            "Render one standard-HTML NixBench theme component through "
            "WebKitGTK.\n"
            "\n"
            "Options:\n"
            "  --component window|desktop|menubar\n"
            "                              component to render (default: window)\n"
            "  --size WIDTHxHEIGHT         viewport size (default: 640x96)\n"
            "  --snapshot FILE.png         save one transparent PNG and exit\n"
            "  --help                      show this help\n"
            "  --version                   show the renderer version\n",
            program);
}

static bool parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed <= 0 ||
        parsed > NB_THEME_ATLAS_MAX_DIMENSION) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_size(const char *text, int *width, int *height)
{
    const char *separator = strchr(text, 'x');
    char width_text[32];
    size_t width_length;

    if (separator == NULL || separator == text || separator[1] == '\0') {
        return false;
    }
    width_length = (size_t)(separator - text);
    if (width_length >= sizeof(width_text)) {
        return false;
    }
    memcpy(width_text, text, width_length);
    width_text[width_length] = '\0';
    return parse_positive_int(width_text, width) &&
           parse_positive_int(separator + 1, height);
}

static int parse_options(int argc,
                         char **argv,
                         struct renderer_options *options)
{
    int index;

    *options = (struct renderer_options){NULL, NULL, COMPONENT_WINDOW, 640, 96};
    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 1;
        }
        if (strcmp(argv[index], "--version") == 0) {
            printf("NixBench HTML theme renderer %s\n",
                   NIXBENCH_HTML_THEME_RENDERER_VERSION);
            return 1;
        }
        if (strcmp(argv[index], "--theme") == 0 && index + 1 < argc) {
            options->theme_directory = argv[++index];
            continue;
        }
        if (strcmp(argv[index], "--snapshot") == 0 && index + 1 < argc) {
            options->snapshot_path = argv[++index];
            continue;
        }
        if (strcmp(argv[index], "--size") == 0 && index + 1 < argc) {
            if (!parse_size(argv[++index], &options->width, &options->height)) {
                fprintf(stderr, "Invalid renderer size: %s\n", argv[index]);
                return -1;
            }
            continue;
        }
        if (strcmp(argv[index], "--component") == 0 && index + 1 < argc) {
            const char *component = argv[++index];

            if (strcmp(component, "window") == 0) {
                options->component = COMPONENT_WINDOW;
            } else if (strcmp(component, "desktop") == 0) {
                options->component = COMPONENT_DESKTOP;
            } else if (strcmp(component, "menubar") == 0) {
                options->component = COMPONENT_MENUBAR;
            } else {
                fprintf(stderr, "Unknown theme component: %s\n", component);
                return -1;
            }
            continue;
        }
        fprintf(stderr, "Unknown or incomplete option: %s\n", argv[index]);
        print_usage(stderr, argv[0]);
        return -1;
    }
    if (options->theme_directory == NULL) {
        fputs("The --theme DIRECTORY option is required\n", stderr);
        return -1;
    }
    return 0;
}

static bool read_text_file(const char *path,
                           gchar **contents,
                           gsize *length)
{
    GError *error = NULL;

    if (!g_file_get_contents(path, contents, length, &error)) {
        fprintf(stderr, "Could not read %s: %s\n", path,
                error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }
    return true;
}

static const char *component_path(const struct nb_theme_bundle *bundle,
                                  enum component_kind component)
{
    switch (component) {
    case COMPONENT_WINDOW:
        return bundle->window_path;
    case COMPONENT_DESKTOP:
        return bundle->desktop_path;
    case COMPONENT_MENUBAR:
        return bundle->menubar_path;
    }
    return NULL;
}

static gchar *build_document(const struct nb_theme_bundle *bundle,
                             enum component_kind component)
{
    static const char prefix[] =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta http-equiv=\"Content-Security-Policy\" content=\""
        "default-src 'none'; script-src 'none'; connect-src 'none'; "
        "style-src 'unsafe-inline'; img-src data:; font-src data:; "
        "media-src 'none'; object-src 'none'; frame-src 'none'; "
        "form-action 'none'; base-uri 'none'\">"
        "<style>html,body{width:100%;height:100%;margin:0;overflow:hidden;"
        "background:transparent}</style><style>";
    static const char middle[] = "</style></head><body>";
    static const char suffix[] = "</body></html>";
    gchar *stylesheet = NULL;
    gchar *fragment = NULL;
    gsize stylesheet_length = 0;
    gsize fragment_length = 0;
    GString *document;

    if (!read_text_file(bundle->stylesheet_path,
                        &stylesheet,
                        &stylesheet_length) ||
        !read_text_file(component_path(bundle, component),
                        &fragment,
                        &fragment_length)) {
        g_free(stylesheet);
        g_free(fragment);
        return NULL;
    }
    document = g_string_sized_new(sizeof(prefix) + stylesheet_length +
                                  sizeof(middle) + fragment_length +
                                  sizeof(suffix));
    g_string_append(document, prefix);
    g_string_append_len(document, stylesheet, (gssize)stylesheet_length);
    g_string_append(document, middle);
    g_string_append_len(document, fragment, (gssize)fragment_length);
    g_string_append(document, suffix);
    g_free(stylesheet);
    g_free(fragment);
    return g_string_free(document, FALSE);
}

static void snapshot_finished(GObject *source,
                              GAsyncResult *result,
                              gpointer user_data)
{
    struct renderer_state *state = user_data;
    GError *error = NULL;
    cairo_surface_t *surface = webkit_web_view_get_snapshot_finish(
        WEBKIT_WEB_VIEW(source), result, &error);

    if (surface == NULL) {
        fprintf(stderr, "HTML theme snapshot failed: %s\n",
                error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        state->exit_status = 1;
    } else {
        const cairo_status_t status = cairo_surface_write_to_png(
            surface, state->snapshot_path);

        if (status != CAIRO_STATUS_SUCCESS) {
            fprintf(stderr, "Could not save HTML theme snapshot: %s\n",
                    cairo_status_to_string(status));
            state->exit_status = 1;
        } else {
            printf("Saved HTML theme snapshot: %s\n", state->snapshot_path);
        }
        cairo_surface_destroy(surface);
    }
    gtk_main_quit();
}

static gboolean begin_snapshot(gpointer user_data)
{
    struct renderer_state *state = user_data;

    webkit_web_view_get_snapshot(
        state->web_view,
        WEBKIT_SNAPSHOT_REGION_VISIBLE,
        WEBKIT_SNAPSHOT_OPTIONS_TRANSPARENT_BACKGROUND,
        NULL,
        snapshot_finished,
        state);
    return G_SOURCE_REMOVE;
}

static void load_changed(WebKitWebView *web_view,
                         WebKitLoadEvent event,
                         gpointer user_data)
{
    struct renderer_state *state = user_data;

    (void)web_view;
    if (event == WEBKIT_LOAD_FINISHED && state->snapshot_path != NULL &&
        !state->snapshot_started) {
        state->snapshot_started = true;
        (void)g_timeout_add(50, begin_snapshot, state);
    }
}

static gboolean load_failed(WebKitWebView *web_view,
                            WebKitLoadEvent event,
                            const gchar *failing_uri,
                            GError *error,
                            gpointer user_data)
{
    struct renderer_state *state = user_data;

    (void)web_view;
    (void)event;
    fprintf(stderr, "HTML theme load failed for %s: %s\n",
            failing_uri != NULL ? failing_uri : "document",
            error != NULL ? error->message : "unknown error");
    state->exit_status = 1;
    gtk_main_quit();
    return TRUE;
}

static gboolean decide_policy(WebKitWebView *web_view,
                              WebKitPolicyDecision *decision,
                              WebKitPolicyDecisionType type,
                              gpointer user_data)
{
    WebKitURIRequest *request;
    WebKitNavigationAction *navigation;
    const char *uri;

    (void)web_view;
    (void)user_data;
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        return FALSE;
    }
    navigation = webkit_navigation_policy_decision_get_navigation_action(
        WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    request = navigation != NULL
                  ? webkit_navigation_action_get_request(navigation)
                  : NULL;
    uri = request != NULL ? webkit_uri_request_get_uri(request) : NULL;
    if (uri != NULL && strcmp(uri, "about:blank") == 0) {
        return FALSE;
    }
    webkit_policy_decision_ignore(decision);
    return TRUE;
}

static void configure_settings(WebKitWebView *web_view)
{
    WebKitSettings *settings = webkit_web_view_get_settings(web_view);

    webkit_settings_set_enable_javascript(settings, FALSE);
    webkit_settings_set_enable_offline_web_application_cache(settings, FALSE);
    webkit_settings_set_enable_html5_local_storage(settings, FALSE);
    webkit_settings_set_enable_page_cache(settings, FALSE);
    webkit_settings_set_enable_media_stream(settings, FALSE);
    webkit_settings_set_enable_dns_prefetching(settings, FALSE);
    webkit_settings_set_allow_file_access_from_file_urls(settings, FALSE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, FALSE);
}

int main(int argc, char **argv)
{
    struct renderer_options options;
    struct nb_theme_bundle bundle;
    struct renderer_state state = {0};
    WebKitWebContext *context;
    GtkWidget *window;
    GtkWidget *web_view_widget;
    WebKitWebView *web_view;
    GdkRGBA transparent = {0.0, 0.0, 0.0, 0.0};
    gchar *document;
    char error[256];
    int parse_result = parse_options(argc, argv, &options);
    int gtk_argc = 1;
    char *gtk_arguments[] = {argv[0], NULL};
    char **gtk_argv = gtk_arguments;

    if (parse_result != 0) {
        return parse_result > 0 ? 0 : 2;
    }
    if (!nb_theme_bundle_load(options.theme_directory,
                              &bundle,
                              error,
                              sizeof(error))) {
        fprintf(stderr, "Could not load HTML theme: %s\n", error);
        return 1;
    }
    document = build_document(&bundle, options.component);
    if (document == NULL) {
        return 1;
    }
    if (!gtk_init_check(&gtk_argc, &gtk_argv)) {
        fputs("Could not initialize GTK display for HTML theme renderer\n",
              stderr);
        g_free(document);
        return 1;
    }

    context = webkit_web_context_new_ephemeral();
    web_view_widget = webkit_web_view_new_with_context(context);
    web_view = WEBKIT_WEB_VIEW(web_view_widget);
    configure_settings(web_view);
    webkit_web_view_set_background_color(web_view, &transparent);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "NixBench HTML Theme Preview");
    gtk_window_set_default_size(GTK_WINDOW(window), options.width, options.height);
    gtk_container_add(GTK_CONTAINER(window), web_view_widget);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    state.web_view = web_view;
    state.snapshot_path = options.snapshot_path;
    g_signal_connect(web_view, "load-changed", G_CALLBACK(load_changed), &state);
    g_signal_connect(web_view, "load-failed", G_CALLBACK(load_failed), &state);
    g_signal_connect(web_view, "decide-policy", G_CALLBACK(decide_policy), NULL);

    gtk_widget_show_all(window);
    webkit_web_view_load_html(web_view, document, NULL);
    g_free(document);
    gtk_main();
    g_object_unref(context);
    return state.exit_status;
}
