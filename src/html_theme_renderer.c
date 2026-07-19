#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cairo.h>
#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>
#include <wayland-client.h>
#include <webkit2/webkit2.h>

#include "nixbench-html-theme-v1-client-protocol.h"
#include "theme_atlas.h"
#include "theme_bundle.h"
#include "window.h"

#ifndef NIXBENCH_HTML_THEME_RENDERER_VERSION
#define NIXBENCH_HTML_THEME_RENDERER_VERSION "unknown"
#endif

enum component_kind {
    COMPONENT_WINDOW,
    COMPONENT_DESKTOP,
    COMPONENT_MENUBAR
};

enum {
    LIVE_ATLAS_PREFERRED_ROW_WIDTH = 2048,
    DESKTOP_MINIMIZED_WINDOW_CAPACITY = 4,
    THEME_ASSET_MAXIMUM_BYTES = 4 * 1024 * 1024
};

static const char theme_asset_scheme[] = "nixbench-theme";
static const char fantasy_window_frame_uri[] =
    "nixbench-theme:///unicorn-window-frame.png";
static const char fantasy_compact_window_frame_uri[] =
    "nixbench-theme:///gnome-compact-window-frame.png";
static const char fantasy_dock_uri[] =
    "nixbench-theme:///enchanted-dock.png";
static const char fantasy_wallpaper_uri[] =
    "nixbench-theme:///woods.png";

struct renderer_window {
    uint64_t id;
    uint32_t state;
    int width;
    int height;
    int atlas_x;
    int atlas_y;
    char title[NB_WINDOW_TITLE_CAPACITY];
};

struct renderer_minimized_window {
    uint64_t id;
    char title[NB_WINDOW_TITLE_CAPACITY];
};

struct renderer_options {
    const char *theme_directory;
    const char *snapshot_path;
    const char *atlas_token;
    enum component_kind component;
    int width;
    int height;
};

struct renderer_state {
    const struct nb_theme_bundle *bundle;
    GtkWindow *window;
    WebKitWebView *web_view;
    const char *snapshot_path;
    struct wl_display *display;
    struct wl_registry *registry;
    struct nixbench_html_theme_manager_v1 *theme_manager;
    struct nixbench_html_theme_atlas_v1 *theme_atlas;
    uint32_t maximum_width;
    uint32_t maximum_height;
    uint32_t state_serial;
    uint64_t render_generation;
    int render_width;
    int render_height;
    int output_width;
    int output_height;
    int dock_x;
    int dock_y;
    int dock_width;
    int dock_height;
    char clock_text[16];
    struct renderer_window windows[NB_THEME_ATLAS_MAX_TILES];
    size_t window_count;
    struct renderer_minimized_window
        minimized_windows[DESKTOP_MINIMIZED_WINDOW_CAPACITY];
    size_t minimized_window_count;
    gchar *sakura_icon_uri;
    gchar *midori_icon_uri;
    gchar *thunar_icon_uri;
    gchar *fantasy_window_frame_uri;
    gchar *fantasy_compact_window_frame_uri;
    gchar *fantasy_dock_uri;
    gchar *fantasy_wallpaper_uri;
    GBytes *fantasy_window_frame_bytes;
    GBytes *fantasy_compact_window_frame_bytes;
    GBytes *fantasy_dock_bytes;
    GBytes *fantasy_wallpaper_bytes;
    guint render_source;
    guint publish_source;
    unsigned int publish_attempts;
    bool live_atlas;
    bool state_theme_valid;
    bool state_shell_available;
    bool state_window_available;
    bool gtk_window_configured;
    bool layout_in_flight;
    bool snapshot_started;
    int exit_status;
};

static bool path_belongs_to_directory(const char *directory,
                                      const char *path)
{
    const size_t length = directory != NULL ? strlen(directory) : 0;

    return length != 0 && path != NULL &&
           strncmp(directory, path, length) == 0 && path[length] == '/';
}

static GBytes *theme_asset_bytes(const struct nb_theme_bundle *bundle,
                                 const char *relative_path)
{
    char combined[NB_THEME_PATH_CAPACITY];
    char canonical[NB_THEME_PATH_CAPACITY];
    gchar *contents = NULL;
    gsize length = 0;
    const int result =
        bundle != NULL && relative_path != NULL
            ? snprintf(combined,
                       sizeof(combined),
                       "%s/%s",
                       bundle->directory,
                       relative_path)
            : -1;

    if (result < 0 || (size_t)result >= sizeof(combined) ||
        realpath(combined, canonical) == NULL ||
        !path_belongs_to_directory(bundle->directory, canonical) ||
        !g_file_get_contents(canonical, &contents, &length, NULL) ||
        length == 0 || length > THEME_ASSET_MAXIMUM_BYTES) {
        g_free(contents);
        return NULL;
    }
    return g_bytes_new_take(contents, length);
}

static void theme_asset_request(WebKitURISchemeRequest *request,
                                gpointer user_data)
{
    struct renderer_state *state = user_data;
    const char *path = webkit_uri_scheme_request_get_path(request);
    GBytes *bytes = NULL;
    GInputStream *stream;
    GError *error;
    gsize length;

    if (path != NULL && strcmp(path, "/unicorn-window-frame.png") == 0) {
        bytes = state->fantasy_window_frame_bytes;
    } else if (path != NULL &&
               strcmp(path, "/gnome-compact-window-frame.png") == 0) {
        bytes = state->fantasy_compact_window_frame_bytes;
    } else if (path != NULL && strcmp(path, "/enchanted-dock.png") == 0) {
        bytes = state->fantasy_dock_bytes;
    } else if (path != NULL && strcmp(path, "/woods.png") == 0) {
        bytes = state->fantasy_wallpaper_bytes;
    }
    if (bytes == NULL) {
        error = g_error_new(G_IO_ERROR,
                            G_IO_ERROR_NOT_FOUND,
                            "Unknown NixBench theme asset: %s",
                            path != NULL ? path : "(missing path)");
        webkit_uri_scheme_request_finish_error(request, error);
        g_error_free(error);
        return;
    }
    length = g_bytes_get_size(bytes);
    stream = g_memory_input_stream_new_from_bytes(bytes);
    webkit_uri_scheme_request_finish(request,
                                     stream,
                                     (gint64)length,
                                     "image/png");
    g_object_unref(stream);
}

static gchar *gtk_icon_data_uri(const char *icon_name)
{
    GtkIconTheme *theme;
    GtkIconInfo *info;
    const char *filename;
    const char *mime_type = "image/png";
    gchar *contents = NULL;
    gchar *encoded;
    gchar *uri;
    gsize length = 0;

    if (icon_name == NULL || icon_name[0] == '\0') {
        return NULL;
    }
    theme = gtk_icon_theme_get_default();
    info = theme != NULL
               ? gtk_icon_theme_lookup_icon(theme, icon_name, 32, 0)
               : NULL;
    filename = info != NULL ? gtk_icon_info_get_filename(info) : NULL;
    if (filename == NULL ||
        !g_file_get_contents(filename, &contents, &length, NULL)) {
        if (info != NULL) {
            g_object_unref(info);
        }
        return NULL;
    }
    if (g_str_has_suffix(filename, ".svg") ||
        g_str_has_suffix(filename, ".svgz")) {
        mime_type = "image/svg+xml";
    } else if (g_str_has_suffix(filename, ".xpm")) {
        mime_type = "image/x-xpixmap";
    }
    encoded = g_base64_encode((const guchar *)contents, length);
    uri = g_strdup_printf("data:%s;base64,%s", mime_type, encoded);
    g_free(encoded);
    g_free(contents);
    g_object_unref(info);
    return uri;
}

static void append_launcher_icon_style(GString *document,
                                       const char *selector,
                                       const char *uri)
{
    if (document == NULL || selector == NULL || uri == NULL) {
        return;
    }
    g_string_append_printf(
        document,
        "%s .launcher-image{background-image:url(\"%s\");}"
        "%s .launcher-fallback{visibility:hidden;}",
        selector,
        uri,
        selector);
}

static void append_background_image_style(GString *document,
                                          const char *selector,
                                          const char *property,
                                          const char *uri)
{
    if (document == NULL || selector == NULL || property == NULL ||
        uri == NULL) {
        return;
    }
    g_string_append_printf(document,
                           "%s{%s:url(\"%s\");}",
                           selector,
                           property,
                           uri);
}

static bool renders_desktop(const struct renderer_state *state)
{
    return state != NULL && state->bundle != NULL &&
           (strcmp(state->bundle->id, "cde") == 0 ||
            strcmp(state->bundle->id, "fantasy") == 0);
}

static bool has_render_state(const struct renderer_state *state)
{
    return state != NULL &&
           (state->state_window_available ||
            (renders_desktop(state) && state->state_shell_available));
}

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
            "  --atlas-token TOKEN         attach as the private live atlas\n"
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

    *options = (struct renderer_options){
        NULL, NULL, NULL, COMPONENT_WINDOW, 640, 96
    };
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
        if (strcmp(argv[index], "--atlas-token") == 0 && index + 1 < argc) {
            options->atlas_token = argv[++index];
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
    if (options->atlas_token != NULL && options->atlas_token[0] == '\0') {
        fputs("The --atlas-token value may not be empty\n", stderr);
        return -1;
    }
    if (options->atlas_token != NULL && options->snapshot_path != NULL) {
        fputs("Live atlas and PNG snapshot modes are mutually exclusive\n",
              stderr);
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

static gchar *set_fragment_title(const gchar *fragment,
                                 gsize fragment_length,
                                 const char *title)
{
    static const char marker[] = "data-nixbench-title";
    const gchar *attribute;
    const gchar *text_start;
    const gchar *text_end;
    gchar *escaped;
    GString *updated;

    if (title == NULL || title[0] == '\0') {
        return g_strndup(fragment, fragment_length);
    }
    attribute = g_strstr_len(fragment, (gssize)fragment_length, marker);
    text_start = attribute != NULL ? strchr(attribute, '>') : NULL;
    text_end = text_start != NULL ? strchr(text_start + 1, '<') : NULL;
    if (text_start == NULL || text_end == NULL ||
        (gsize)(text_end - fragment) > fragment_length) {
        return g_strndup(fragment, fragment_length);
    }
    escaped = g_markup_escape_text(title, -1);
    updated = g_string_sized_new(fragment_length + strlen(escaped) + 1);
    g_string_append_len(updated,
                        fragment,
                        (gssize)(text_start + 1 - fragment));
    g_string_append(updated, escaped);
    g_string_append_len(updated,
                        text_end,
                        (gssize)(fragment_length -
                                 (gsize)(text_end - fragment)));
    g_free(escaped);
    return g_string_free(updated, FALSE);
}

static gchar *replace_fragment_text(gchar *fragment,
                                    const char *marker,
                                    const char *text)
{
    const gchar *attribute;
    const gchar *text_start;
    const gchar *text_end;
    gchar *escaped;
    GString *updated;

    if (fragment == NULL || marker == NULL || text == NULL) {
        return fragment;
    }
    attribute = g_strstr_len(fragment, -1, marker);
    text_start = attribute != NULL ? strchr(attribute, '>') : NULL;
    text_end = text_start != NULL ? strchr(text_start + 1, '<') : NULL;
    if (text_start == NULL || text_end == NULL) {
        return fragment;
    }
    escaped = g_markup_escape_text(text, -1);
    updated = g_string_sized_new(strlen(fragment) + strlen(escaped) + 1);
    g_string_append_len(updated,
                        fragment,
                        (gssize)(text_start + 1 - fragment));
    g_string_append(updated, escaped);
    g_string_append(updated, text_end);
    g_free(escaped);
    g_free(fragment);
    return g_string_free(updated, FALSE);
}

static gchar *replace_fragment_markup(gchar *fragment,
                                      const char *marker,
                                      const char *markup)
{
    const gchar *attribute;
    const gchar *content_start;
    const gchar *content_end;
    GString *updated;

    if (fragment == NULL || marker == NULL || markup == NULL) {
        return fragment;
    }
    attribute = g_strstr_len(fragment, -1, marker);
    content_start = attribute != NULL ? strchr(attribute, '>') : NULL;
    content_end = content_start != NULL ? strchr(content_start + 1, '<')
                                        : NULL;
    if (content_start == NULL || content_end == NULL) {
        return fragment;
    }
    updated = g_string_sized_new(strlen(fragment) + strlen(markup) + 1);
    g_string_append_len(updated,
                        fragment,
                        (gssize)(content_start + 1 - fragment));
    g_string_append(updated, markup);
    g_string_append(updated, content_end);
    g_free(fragment);
    return g_string_free(updated, FALSE);
}

static gchar *build_minimized_window_markup(
    const struct renderer_state *state)
{
    GString *markup = g_string_new(NULL);
    size_t index;

    if (state->minimized_window_count == 0) {
        g_string_append(markup,
                        "<span class=\"minimized-empty\">"
                        "No minimized windows</span>");
        return g_string_free(markup, FALSE);
    }
    for (index = 0; index < state->minimized_window_count; ++index) {
        const struct renderer_minimized_window *window =
            &state->minimized_windows[index];
        gchar *escaped = g_markup_escape_text(window->title, -1);

        g_string_append_printf(
            markup,
            "<button class=\"minimized-window\" "
            "data-nixbench-minimized-window=\"%" G_GUINT64_FORMAT "\" "
            "title=\"%s\">%s</button>",
            (guint64)window->id,
            escaped,
            escaped);
        g_free(escaped);
    }
    return g_string_free(markup, FALSE);
}

static void current_calendar_text(char month[4],
                                  char day[3],
                                  char weekday[4])
{
    static const char *const months[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    static const char *const weekdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
    };
    const time_t now = time(NULL);
    struct tm local_time;

    if (now == (time_t)-1 || localtime_r(&now, &local_time) == NULL ||
        local_time.tm_mon < 0 || local_time.tm_mon > 11 ||
        local_time.tm_mday < 1 || local_time.tm_mday > 31 ||
        local_time.tm_wday < 0 || local_time.tm_wday > 6) {
        (void)snprintf(month, 4, "---");
        (void)snprintf(day, 3, "--");
        (void)snprintf(weekday, 4, "---");
        return;
    }
    (void)snprintf(month, 4, "%s", months[local_time.tm_mon]);
    (void)snprintf(day, 3, "%d", local_time.tm_mday);
    (void)snprintf(weekday, 4, "%s", weekdays[local_time.tm_wday]);
}

static gchar *build_document(const struct nb_theme_bundle *bundle,
                             enum component_kind component,
                             const char *title,
                             uint32_t window_state)
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
    gchar *updated_fragment = NULL;
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
    updated_fragment = set_fragment_title(fragment, fragment_length, title);
    if (updated_fragment == NULL) {
        g_free(stylesheet);
        g_free(fragment);
        return NULL;
    }
    document = g_string_sized_new(sizeof(prefix) + stylesheet_length +
                                  sizeof(middle) + fragment_length +
                                  sizeof(suffix));
    g_string_append(document, prefix);
    if (component == COMPONENT_WINDOW) {
        g_string_append_printf(
            document,
            ":root{--nixbench-frame-border:%dpx;"
            "--nixbench-title-height:%dpx;"
            "--nixbench-footer-height:%dpx;"
            "--nixbench-gadget-margin:%dpx;"
            "--nixbench-control-size:%dpx;"
            "--nixbench-menu-height:%dpx}",
            NB_WINDOW_BORDER_WIDTH,
            NB_WINDOW_TITLE_HEIGHT,
            NB_WINDOW_FOOTER_HEIGHT,
            NB_WINDOW_GADGET_MARGIN,
            NB_WINDOW_CLOSE_SIZE,
            NB_WINDOW_MENU_HEIGHT);
    } else if (component == COMPONENT_DESKTOP) {
        g_string_append(
            document,
            ":root{--nixbench-dock-x:24px;--nixbench-dock-y:24px;"
            "--nixbench-dock-width:760px;--nixbench-dock-height:88px;"
            "--nixbench-hour-angle:0deg;--nixbench-minute-angle:0deg}");
    }
    g_string_append_len(document, stylesheet, (gssize)stylesheet_length);
    if (component == COMPONENT_WINDOW &&
        (window_state &
         NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_SHOW_MINIMIZE) == 0) {
        g_string_append(
            document,
            "[data-nixbench-action=minimize]{display:none!important}");
    }
    if (component == COMPONENT_WINDOW &&
        (window_state &
         NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_SHOW_MAXIMIZE) == 0) {
        g_string_append(
            document,
            "[data-nixbench-action=maximize]{display:none!important}");
    }
    g_string_append(document, middle);
    g_string_append(document, updated_fragment);
    g_string_append(document, suffix);
    g_free(stylesheet);
    g_free(fragment);
    g_free(updated_fragment);
    return g_string_free(document, FALSE);
}

static gchar *build_live_atlas_document(
    const struct renderer_state *state)
{
    static const char prefix[] =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta http-equiv=\"Content-Security-Policy\" content=\""
        "default-src 'none'; script-src 'none'; connect-src 'none'; "
        "style-src 'unsafe-inline'; img-src data: nixbench-theme:; "
        "font-src data:; "
        "media-src 'none'; object-src 'none'; frame-src 'none'; "
        "form-action 'none'; base-uri 'none'\">"
        "<style>html,body{width:100%;height:100%;margin:0;overflow:hidden;"
        "background:transparent}"
        ".nixbench-atlas-tile{position:absolute;overflow:hidden}"
        ".nixbench-atlas-tile>[data-nixbench-window]{"
        "width:100%!important;height:100%!important}"
        ".nixbench-hide-minimize "
        "[data-nixbench-action=minimize]{display:none!important}"
        ".nixbench-hide-maximize "
        "[data-nixbench-action=maximize]{display:none!important}"
        "</style><style>";
    static const char middle[] = "</style></head><body>";
    static const char suffix[] = "</body></html>";
    gchar *stylesheet = NULL;
    gchar *fragment = NULL;
    gchar *desktop_fragment = NULL;
    gsize stylesheet_length = 0;
    gsize fragment_length = 0;
    gsize desktop_fragment_length = 0;
    GString *document;
    size_t index;
    int hour = 0;
    int minute = 0;
    char calendar_month[4];
    char calendar_day[3];
    char calendar_weekday[4];
    const bool desktop = renders_desktop(state) &&
                         state->state_shell_available;

    if (state == NULL || (!desktop && state->window_count == 0) ||
        !read_text_file(state->bundle->stylesheet_path,
                        &stylesheet,
                        &stylesheet_length)) {
        g_free(stylesheet);
        return NULL;
    }
    if (state->window_count != 0 &&
        !read_text_file(state->bundle->window_path,
                        &fragment,
                        &fragment_length)) {
        g_free(stylesheet);
        return NULL;
    }
    if (desktop &&
        !read_text_file(state->bundle->desktop_path,
                        &desktop_fragment,
                        &desktop_fragment_length)) {
        g_free(stylesheet);
        g_free(fragment);
        return NULL;
    }
    if (sscanf(state->clock_text, "%d:%d", &hour, &minute) != 2 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        hour = 0;
        minute = 0;
    }
    current_calendar_text(calendar_month,
                          calendar_day,
                          calendar_weekday);
    document = g_string_sized_new(sizeof(prefix) + stylesheet_length +
                                  sizeof(middle) +
                                  desktop_fragment_length +
                                  state->window_count *
                                      (fragment_length + 256) +
                                  sizeof(suffix));
    g_string_append(document, prefix);
    g_string_append_printf(
        document,
        ":root{--nixbench-frame-border:%dpx;"
        "--nixbench-title-height:%dpx;"
        "--nixbench-footer-height:%dpx;"
        "--nixbench-gadget-margin:%dpx;"
        "--nixbench-control-size:%dpx;"
        "--nixbench-menu-height:%dpx;"
        "--nixbench-dock-x:%dpx;"
        "--nixbench-dock-y:%dpx;"
        "--nixbench-dock-width:%dpx;"
        "--nixbench-dock-height:%dpx;"
        "--nixbench-hour-angle:%ddeg;"
        "--nixbench-minute-angle:%ddeg}",
        NB_WINDOW_BORDER_WIDTH,
        NB_WINDOW_TITLE_HEIGHT,
        NB_WINDOW_FOOTER_HEIGHT,
        NB_WINDOW_GADGET_MARGIN,
        NB_WINDOW_CLOSE_SIZE,
        NB_WINDOW_MENU_HEIGHT,
        state->dock_x,
        state->dock_y,
        state->dock_width,
        state->dock_height,
        (hour % 12) * 30 + minute / 2,
        minute * 6);
    if (desktop) {
        append_launcher_icon_style(document,
                                   ".terminal-icon",
                                   state->sakura_icon_uri);
        append_launcher_icon_style(document,
                                   ".midori-icon",
                                   state->midori_icon_uri);
        append_launcher_icon_style(document,
                                   ".thunar-icon",
                                   state->thunar_icon_uri);
        append_background_image_style(document,
                                      ".fantasy-window",
                                      "--fantasy-window-frame-image",
                                      state->fantasy_window_frame_uri);
        append_background_image_style(
            document,
            ".fantasy-compact-tile .fantasy-window",
            "--fantasy-window-frame-image",
            state->fantasy_compact_window_frame_uri);
        append_background_image_style(document,
                                      ".fantasy-dock",
                                      "--fantasy-dock-image",
                                      state->fantasy_dock_uri);
        append_background_image_style(document,
                                      ".fantasy-desktop",
                                      "--fantasy-wallpaper-image",
                                      state->fantasy_wallpaper_uri);
    }
    g_string_append_len(document, stylesheet, (gssize)stylesheet_length);
    g_string_append(document, middle);
    if (desktop) {
        gchar *clock_fragment = set_fragment_title(
            desktop_fragment,
            desktop_fragment_length,
            NULL);
        gchar *minimized_markup;
        if (clock_fragment == NULL) {
            g_string_free(document, TRUE);
            g_free(stylesheet);
            g_free(fragment);
            g_free(desktop_fragment);
            return NULL;
        }
        clock_fragment = replace_fragment_text(
            clock_fragment, "data-nixbench-clock", state->clock_text);
        clock_fragment = replace_fragment_text(
            clock_fragment, "data-nixbench-date-month", calendar_month);
        clock_fragment = replace_fragment_text(
            clock_fragment, "data-nixbench-date-day", calendar_day);
        clock_fragment = replace_fragment_text(
            clock_fragment,
            "data-nixbench-date-weekday",
            calendar_weekday);
        minimized_markup = build_minimized_window_markup(state);
        clock_fragment = replace_fragment_markup(
            clock_fragment,
            "data-nixbench-minimized-windows",
            minimized_markup);
        g_free(minimized_markup);
        g_string_append_printf(
            document,
            "<section class=\"nixbench-atlas-tile\" "
            "data-nixbench-desktop-tile "
            "style=\"left:0;top:0;width:%dpx;height:%dpx\">",
            state->output_width,
            state->output_height);
        g_string_append(document, clock_fragment);
        g_string_append(document, "</section>");
        g_free(clock_fragment);
    }
    for (index = 0; index < state->window_count; ++index) {
        const struct renderer_window *window = &state->windows[index];
        const bool show_minimize =
            (window->state &
             NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_SHOW_MINIMIZE) != 0;
        const bool show_maximize =
            (window->state &
             NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_SHOW_MAXIMIZE) != 0;
        const bool active =
            (window->state &
             NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_ACTIVE) != 0;
        const bool compact = window->width <= 400 || window->height <= 300;
        gchar *updated_fragment = set_fragment_title(
            fragment, fragment_length, window->title);

        if (updated_fragment == NULL) {
            g_string_free(document, TRUE);
            g_free(stylesheet);
            g_free(fragment);
            return NULL;
        }
        g_string_append_printf(
            document,
            "<section class=\"nixbench-atlas-tile%s%s%s\" "
            "data-nixbench-window-id=\"%" G_GUINT64_FORMAT "\" "
            "data-nixbench-active=\"%s\" "
            "style=\"left:%dpx;top:%dpx;width:%dpx;height:%dpx\">",
            show_minimize ? "" : " nixbench-hide-minimize",
            show_maximize ? "" : " nixbench-hide-maximize",
            compact ? " fantasy-compact-tile" : "",
            (guint64)window->id,
            active ? "true" : "false",
            window->atlas_x,
            window->atlas_y,
            window->width,
            window->height);
        g_string_append(document, updated_fragment);
        g_string_append(document, "</section>");
        g_free(updated_fragment);
    }
    g_string_append(document, suffix);
    g_free(stylesheet);
    g_free(fragment);
    g_free(desktop_fragment);
    return g_string_free(document, FALSE);
}

static bool pack_live_windows(struct renderer_state *state)
{
    const int maximum_width = (int)state->maximum_width;
    const int maximum_height = (int)state->maximum_height;
    int row_limit = LIVE_ATLAS_PREFERRED_ROW_WIDTH;
    const bool desktop = renders_desktop(state) &&
                         state->state_shell_available;
    int atlas_width = desktop ? state->output_width : 0;
    int atlas_height = desktop ? state->output_height : 0;
    int x = 0;
    int y = desktop ? state->output_height : 0;
    int row_height = 0;
    size_t input_index;
    size_t packed_count = 0;

    if (maximum_width <= 0 || maximum_height <= 0) {
        return false;
    }
    if (row_limit > maximum_width) {
        row_limit = maximum_width;
    }
    for (input_index = 0;
         input_index < state->window_count;
         ++input_index) {
        if (state->windows[input_index].width > row_limit &&
            state->windows[input_index].width <= maximum_width) {
            row_limit = state->windows[input_index].width;
        }
    }
    for (input_index = 0;
         input_index < state->window_count;
         ++input_index) {
        struct renderer_window window = state->windows[input_index];

        if (window.width <= 0 || window.height <= 0 ||
            window.width > maximum_width || window.height > maximum_height) {
            continue;
        }
        if (x > 0 && window.width > row_limit - x) {
            x = 0;
            y += row_height;
            row_height = 0;
        }
        if (window.height > maximum_height - y) {
            continue;
        }
        window.atlas_x = x;
        window.atlas_y = y;
        state->windows[packed_count++] = window;
        x += window.width;
        if (window.height > row_height) {
            row_height = window.height;
        }
        if (x > atlas_width) {
            atlas_width = x;
        }
        if (y + row_height > atlas_height) {
            atlas_height = y + row_height;
        }
    }
    state->window_count = packed_count;
    state->render_width = atlas_width;
    state->render_height = atlas_height;
    state->state_window_available = packed_count != 0;
    return has_render_state(state);
}

static void renderer_registry_global(void *data,
                                     struct wl_registry *registry,
                                     uint32_t name,
                                     const char *interface,
                                     uint32_t version)
{
    struct renderer_state *state = data;

    if (state->theme_manager == NULL &&
        strcmp(interface,
               nixbench_html_theme_manager_v1_interface.name) == 0) {
        state->theme_manager = wl_registry_bind(
            registry,
            name,
            &nixbench_html_theme_manager_v1_interface,
            version < 1 ? version : 1);
    }
}

static void renderer_registry_global_remove(void *data,
                                            struct wl_registry *registry,
                                            uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static bool ensure_theme_manager(struct renderer_state *state)
{
    static const struct wl_registry_listener listener = {
        .global = renderer_registry_global,
        .global_remove = renderer_registry_global_remove
    };
    GdkDisplay *gdk_display = gdk_display_get_default();

    if (gdk_display == NULL || !GDK_IS_WAYLAND_DISPLAY(gdk_display)) {
        fputs("Live HTML themes require the NixBench Wayland display\n",
              stderr);
        return false;
    }
    state->display = gdk_wayland_display_get_wl_display(gdk_display);
    if (state->display == NULL) {
        fputs("Could not obtain the renderer's Wayland display\n", stderr);
        return false;
    }
    state->registry = wl_display_get_registry(state->display);
    if (state->registry == NULL ||
        wl_registry_add_listener(state->registry, &listener, state) != 0 ||
        wl_display_roundtrip(state->display) < 0 ||
        state->theme_manager == NULL) {
        fputs("NixBench HTML theme protocol is unavailable\n", stderr);
        return false;
    }
    return true;
}

static gboolean publish_live_layout(gpointer user_data)
{
    struct renderer_state *state = user_data;
    const int allocated_width =
        gtk_widget_get_allocated_width(GTK_WIDGET(state->window));
    const int allocated_height =
        gtk_widget_get_allocated_height(GTK_WIDGET(state->window));
    const uint32_t generation_hi =
        (uint32_t)(state->render_generation >> 32U);
    const uint32_t generation_lo = (uint32_t)state->render_generation;
    size_t index;

    if (!state->layout_in_flight || state->theme_atlas == NULL) {
        state->publish_source = 0;
        return G_SOURCE_REMOVE;
    }
    if ((allocated_width != state->render_width ||
         allocated_height != state->render_height) &&
        ++state->publish_attempts < 40) {
        return G_SOURCE_CONTINUE;
    }
    if (allocated_width != state->render_width ||
        allocated_height != state->render_height) {
        fprintf(stderr,
                "HTML atlas allocation remained %dx%d instead of %dx%d\n",
                allocated_width,
                allocated_height,
                state->render_width,
                state->render_height);
        state->exit_status = 1;
        state->publish_source = 0;
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    if (renders_desktop(state) && state->state_shell_available) {
        nixbench_html_theme_atlas_v1_tile(
            state->theme_atlas,
            NIXBENCH_HTML_THEME_ATLAS_V1_TILE_KIND_DESKTOP,
            0,
            0,
            0,
            0,
            state->output_width,
            state->output_height);
    }
    for (index = 0; index < state->window_count; ++index) {
        const struct renderer_window *window = &state->windows[index];

        nixbench_html_theme_atlas_v1_tile(
            state->theme_atlas,
            NIXBENCH_HTML_THEME_ATLAS_V1_TILE_KIND_WINDOW,
            (uint32_t)(window->id >> 32U),
            (uint32_t)window->id,
            window->atlas_x,
            window->atlas_y,
            window->width,
            window->height);
    }
    nixbench_html_theme_atlas_v1_commit_layout(
        state->theme_atlas,
        generation_hi,
        generation_lo);
    if (wl_display_flush(state->display) < 0 && errno != EAGAIN) {
        fputs("Could not publish the HTML theme atlas layout\n", stderr);
        state->exit_status = 1;
        gtk_main_quit();
    }
    state->layout_in_flight = false;
    state->publish_source = 0;
    return G_SOURCE_REMOVE;
}

static bool begin_live_render(struct renderer_state *state)
{
    gchar *document;
    uint32_t generation_hi;
    uint32_t generation_lo;

    if (!state->state_theme_valid || !has_render_state(state) ||
        state->render_width <= 0 ||
        state->render_height <= 0 ||
        (uint32_t)state->render_width > state->maximum_width ||
        (uint32_t)state->render_height > state->maximum_height) {
        return false;
    }
    document = build_live_atlas_document(state);
    if (document == NULL) {
        return false;
    }
    ++state->render_generation;
    if (state->render_generation == 0) {
        state->render_generation = 1;
    }
    generation_hi = (uint32_t)(state->render_generation >> 32U);
    generation_lo = (uint32_t)state->render_generation;
    nixbench_html_theme_atlas_v1_begin_layout(
        state->theme_atlas,
        generation_hi,
        generation_lo,
        (uint32_t)state->render_width,
        (uint32_t)state->render_height);
    state->layout_in_flight = true;
    state->publish_attempts = 0;
    if (state->publish_source != 0) {
        g_source_remove(state->publish_source);
        state->publish_source = 0;
    }
    gtk_widget_set_size_request(GTK_WIDGET(state->web_view),
                                state->render_width,
                                state->render_height);
    gtk_window_resize(state->window,
                      state->render_width,
                      state->render_height);
    webkit_web_view_load_html(state->web_view, document, NULL);
    g_free(document);
    return wl_display_flush(state->display) >= 0 || errno == EAGAIN;
}

static gboolean begin_live_render_idle(gpointer user_data)
{
    struct renderer_state *state = user_data;

    state->render_source = 0;
    if (!has_render_state(state)) {
        return G_SOURCE_REMOVE;
    }
    if (!begin_live_render(state)) {
        fputs("Could not begin the live HTML theme render\n", stderr);
        state->exit_status = 1;
        gtk_main_quit();
    }
    return G_SOURCE_REMOVE;
}

static void schedule_live_render(struct renderer_state *state)
{
    if (!state->gtk_window_configured || !has_render_state(state) ||
        state->layout_in_flight || state->render_source != 0) {
        return;
    }
    state->render_source = g_idle_add(begin_live_render_idle, state);
}

static gboolean live_window_configure(GtkWidget *widget,
                                      GdkEventConfigure *event,
                                      gpointer user_data)
{
    struct renderer_state *state = user_data;

    (void)widget;
    (void)event;
    if (state->gtk_window_configured) {
        return FALSE;
    }
    state->gtk_window_configured = true;
    /*
     * GDK has now received and acknowledged the initial xdg_surface
     * configure on its private Wayland event queue.  Defer WebKit work once
     * more so no child surface or buffer commit is created from inside the
     * configure-event dispatch itself.
     */
    schedule_live_render(state);
    return FALSE;
}

static void atlas_configure(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial,
    uint32_t maximum_width,
    uint32_t maximum_height,
    uint32_t scale)
{
    struct renderer_state *state = data;

    (void)atlas;
    if (serial == 0 || maximum_width == 0 || maximum_height == 0 ||
        maximum_width > INT_MAX || maximum_height > INT_MAX || scale != 1) {
        state->exit_status = 1;
        gtk_main_quit();
        return;
    }
    state->state_serial = serial;
    state->maximum_width = maximum_width;
    state->maximum_height = maximum_height;
    state->state_theme_valid = false;
    state->state_shell_available = false;
    state->state_window_available = false;
    state->window_count = 0;
    state->render_width = 0;
    state->render_height = 0;
    state->layout_in_flight = false;
    if (state->render_source != 0) {
        g_source_remove(state->render_source);
        state->render_source = 0;
    }
    if (state->publish_source != 0) {
        g_source_remove(state->publish_source);
        state->publish_source = 0;
    }
}

static void atlas_theme(void *data,
                        struct nixbench_html_theme_atlas_v1 *atlas,
                        uint32_t serial,
                        const char *theme_id,
                        const char *directory)
{
    struct renderer_state *state = data;

    (void)atlas;
    state->state_theme_valid = serial == state->state_serial &&
                               strcmp(theme_id, state->bundle->id) == 0 &&
                               strcmp(directory, state->bundle->directory) == 0;
}

static void atlas_shell_state(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial,
    uint32_t output_width,
    uint32_t output_height,
    int32_t dock_x,
    int32_t dock_y,
    uint32_t dock_width,
    uint32_t dock_height,
    const char *clock_text)
{
    struct renderer_state *state = data;

    (void)atlas;
    if (serial != state->state_serial || output_width == 0 ||
        output_height == 0 || output_width > state->maximum_width ||
        output_height > state->maximum_height || dock_width == 0 ||
        dock_height == 0 || dock_x < 0 || dock_y < 0 ||
        dock_width > output_width || dock_height > output_height ||
        (uint32_t)dock_x > output_width - dock_width ||
        (uint32_t)dock_y > output_height - dock_height ||
        output_width > INT_MAX || output_height > INT_MAX ||
        dock_width > INT_MAX || dock_height > INT_MAX) {
        return;
    }
    state->output_width = (int)output_width;
    state->output_height = (int)output_height;
    state->dock_x = (int)dock_x;
    state->dock_y = (int)dock_y;
    state->dock_width = (int)dock_width;
    state->dock_height = (int)dock_height;
    (void)snprintf(state->clock_text,
                   sizeof(state->clock_text),
                   "%s",
                   clock_text != NULL ? clock_text : "00:00");
    state->state_shell_available = true;
}

static void atlas_clear_windows(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial)
{
    struct renderer_state *state = data;

    (void)atlas;
    if (serial == state->state_serial) {
        state->state_window_available = false;
        state->window_count = 0;
        state->minimized_window_count = 0;
        state->render_width = 0;
        state->render_height = 0;
    }
}

static void atlas_window(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial,
    uint32_t window_hi,
    uint32_t window_lo,
    uint32_t width,
    uint32_t height,
    uint32_t window_state,
    const char *title)
{
    struct renderer_state *state = data;
    struct renderer_window *window;
    const uint64_t window_id =
        ((uint64_t)window_hi << 32U) | window_lo;

    (void)atlas;
    if (serial != state->state_serial || window_id == 0 ||
        width == 0 || height == 0 || width > INT_MAX || height > INT_MAX ||
        (window_state &
         NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_FULLSCREEN) != 0) {
        return;
    }
    if ((window_state &
         NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_MINIMIZED) != 0) {
        struct renderer_minimized_window *minimized;

        if (!renders_desktop(state) ||
            state->minimized_window_count ==
                DESKTOP_MINIMIZED_WINDOW_CAPACITY) {
            return;
        }
        minimized =
            &state->minimized_windows[state->minimized_window_count++];
        minimized->id = window_id;
        (void)snprintf(minimized->title,
                       sizeof(minimized->title),
                       "%s",
                       title != NULL ? title : "Window");
        return;
    }
    if (state->window_count == NB_THEME_ATLAS_MAX_TILES) {
        return;
    }
    window = &state->windows[state->window_count++];
    *window = (struct renderer_window){
        .id = window_id,
        .state = window_state,
        .width = (int)width,
        .height = (int)height
    };
    (void)snprintf(window->title,
                   sizeof(window->title),
                   "%s",
                   title != NULL ? title : "Window");
}

static void atlas_state_done(
    void *data,
    struct nixbench_html_theme_atlas_v1 *atlas,
    uint32_t serial)
{
    struct renderer_state *state = data;

    if (serial != state->state_serial) {
        state->exit_status = 1;
        gtk_main_quit();
        return;
    }
    (void)pack_live_windows(state);
    nixbench_html_theme_atlas_v1_ack_state(atlas, serial);
    if (!has_render_state(state) && state->render_source != 0) {
        g_source_remove(state->render_source);
        state->render_source = 0;
    } else if (has_render_state(state)) {
        /*
         * Do not enter WebKit from a Wayland listener. GTK and WebKit share
         * this display and may perform their own synchronization while a
         * direct roundtrip is dispatching the initial atlas transaction.
         * Rendering is additionally gated by the GtkWindow configure event,
         * which proves GDK has completed the private surface's initial XDG
         * configure/acknowledge handshake on its own event queue.
         */
        schedule_live_render(state);
    }
}

static const struct nixbench_html_theme_atlas_v1_listener
atlas_listener = {
    .configure = atlas_configure,
    .theme = atlas_theme,
    .shell_state = atlas_shell_state,
    .clear_windows = atlas_clear_windows,
    .window = atlas_window,
    .state_done = atlas_state_done
};

static bool register_live_atlas(struct renderer_state *state,
                                GtkWidget *window,
                                const char *token)
{
    GdkWindow *gdk_window;
    struct wl_surface *surface;

    if (!ensure_theme_manager(state)) {
        return false;
    }
    gtk_widget_realize(window);
    gdk_window = gtk_widget_get_window(window);
    if (gdk_window == NULL || !GDK_IS_WAYLAND_WINDOW(gdk_window)) {
        fputs("Could not realize the HTML atlas Wayland surface\n", stderr);
        return false;
    }
    surface = gdk_wayland_window_get_wl_surface(gdk_window);
    if (surface == NULL) {
        fputs("Could not obtain the HTML atlas wl_surface\n", stderr);
        return false;
    }
    state->theme_atlas =
        nixbench_html_theme_manager_v1_register_atlas(
            state->theme_manager,
            surface,
            token);
    return state->theme_atlas != NULL &&
           nixbench_html_theme_atlas_v1_add_listener(
               state->theme_atlas,
               &atlas_listener,
               state) == 0;
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
    if (event != WEBKIT_LOAD_FINISHED) {
        return;
    }
    if (state->live_atlas && state->layout_in_flight) {
        if (state->publish_source != 0) {
            g_source_remove(state->publish_source);
        }
        state->publish_attempts = 0;
        state->publish_source = g_timeout_add(80,
                                              publish_live_layout,
                                              state);
    } else if (state->snapshot_path != NULL && !state->snapshot_started) {
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
    if (state->live_atlas && error != NULL &&
        error->domain == WEBKIT_NETWORK_ERROR &&
        error->code == WEBKIT_NETWORK_ERROR_CANCELLED) {
        return TRUE;
    }
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
    gchar *document = NULL;
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
    if (options.atlas_token == NULL) {
        document = build_document(
            &bundle,
            options.component,
            NULL,
            NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_SHOW_MINIMIZE |
                NIXBENCH_HTML_THEME_ATLAS_V1_WINDOW_STATE_SHOW_MAXIMIZE);
        if (document == NULL) {
            return 1;
        }
    }
    if (options.atlas_token != NULL &&
        getenv("NIXBENCH_TRACE_WAYLAND") != NULL &&
        strcmp(getenv("NIXBENCH_TRACE_WAYLAND"), "1") == 0 &&
        setenv("WAYLAND_DEBUG", "client", 0) != 0) {
        fprintf(stderr,
                "Could not enable HTML renderer Wayland tracing: %s\n",
                strerror(errno));
        g_free(document);
        return 1;
    }
    if (!gtk_init_check(&gtk_argc, &gtk_argv)) {
        fputs("Could not initialize GTK display for HTML theme renderer\n",
              stderr);
        g_free(document);
        return 1;
    }

    state.bundle = &bundle;
    state.snapshot_path = options.snapshot_path;
    state.live_atlas = options.atlas_token != NULL;

    context = webkit_web_context_new_ephemeral();
    if (state.live_atlas && strcmp(bundle.id, "fantasy") == 0) {
        state.fantasy_window_frame_bytes = theme_asset_bytes(
            &bundle, "assets/unicorn-window-frame.png");
        state.fantasy_compact_window_frame_bytes = theme_asset_bytes(
            &bundle, "assets/gnome-compact-window-frame.png");
        state.fantasy_dock_bytes = theme_asset_bytes(
            &bundle, "assets/enchanted-dock.png");
        state.fantasy_wallpaper_bytes = theme_asset_bytes(
            &bundle, "assets/woods.png");
        if (state.fantasy_window_frame_bytes == NULL ||
            state.fantasy_compact_window_frame_bytes == NULL ||
            state.fantasy_dock_bytes == NULL ||
            state.fantasy_wallpaper_bytes == NULL) {
            fputs("Could not load Fantasy theme assets\n", stderr);
            g_clear_pointer(&state.fantasy_window_frame_bytes, g_bytes_unref);
            g_clear_pointer(&state.fantasy_compact_window_frame_bytes,
                            g_bytes_unref);
            g_clear_pointer(&state.fantasy_dock_bytes, g_bytes_unref);
            g_clear_pointer(&state.fantasy_wallpaper_bytes, g_bytes_unref);
            g_free(document);
            g_object_unref(context);
            return 1;
        }
        state.fantasy_window_frame_uri = g_strdup(
            fantasy_window_frame_uri);
        state.fantasy_compact_window_frame_uri = g_strdup(
            fantasy_compact_window_frame_uri);
        state.fantasy_dock_uri = g_strdup(fantasy_dock_uri);
        state.fantasy_wallpaper_uri = g_strdup(fantasy_wallpaper_uri);
        webkit_web_context_register_uri_scheme(context,
                                               theme_asset_scheme,
                                               theme_asset_request,
                                               &state,
                                               NULL);
    }
    web_view_widget = webkit_web_view_new_with_context(context);
    web_view = WEBKIT_WEB_VIEW(web_view_widget);
    configure_settings(web_view);
    webkit_web_view_set_background_color(web_view, &transparent);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (options.atlas_token != NULL) {
        GdkScreen *screen = gtk_widget_get_screen(window);
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);

        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
        gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
        gtk_widget_set_app_paintable(window, TRUE);
        if (visual != NULL) {
            gtk_widget_set_visual(window, visual);
        }
    } else {
        gtk_window_set_title(GTK_WINDOW(window),
                             "NixBench HTML Theme Preview");
    }
    gtk_window_set_default_size(GTK_WINDOW(window), options.width, options.height);
    gtk_container_add(GTK_CONTAINER(window), web_view_widget);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    state.window = GTK_WINDOW(window);
    state.web_view = web_view;
    if (state.live_atlas &&
        (strcmp(bundle.id, "cde") == 0 ||
         strcmp(bundle.id, "fantasy") == 0)) {
        /* These are the Icon= names from the installed GTK applications. */
        state.sakura_icon_uri = gtk_icon_data_uri("terminal-tango");
        state.midori_icon_uri =
            gtk_icon_data_uri("org.midori_browser.Midori");
        state.thunar_icon_uri = gtk_icon_data_uri("org.xfce.thunar");
    }
    if (state.live_atlas) {
        g_signal_connect(window,
                         "configure-event",
                         G_CALLBACK(live_window_configure),
                         &state);
    }
    g_signal_connect(web_view, "load-changed", G_CALLBACK(load_changed), &state);
    g_signal_connect(web_view, "load-failed", G_CALLBACK(load_failed), &state);
    g_signal_connect(web_view, "decide-policy", G_CALLBACK(decide_policy), NULL);

    if (state.live_atlas &&
        !register_live_atlas(&state, window, options.atlas_token)) {
        fputs("Could not register the live HTML theme atlas\n", stderr);
        g_free(document);
        g_object_unref(context);
        return 1;
    }
    gtk_widget_show_all(window);
    if (state.live_atlas) {
        if (wl_display_roundtrip(state.display) < 0) {
            fputs("Could not receive the initial HTML theme state\n", stderr);
            state.exit_status = 1;
        }
    } else {
        webkit_web_view_load_html(web_view, document, NULL);
    }
    g_free(document);
    if (state.exit_status == 0) {
        gtk_main();
    }
    if (state.publish_source != 0) {
        g_source_remove(state.publish_source);
    }
    if (state.render_source != 0) {
        g_source_remove(state.render_source);
    }
    if (state.theme_atlas != NULL) {
        nixbench_html_theme_atlas_v1_destroy(state.theme_atlas);
    }
    if (state.theme_manager != NULL) {
        nixbench_html_theme_manager_v1_destroy(state.theme_manager);
    }
    if (state.registry != NULL) {
        wl_registry_destroy(state.registry);
    }
    if (state.display != NULL) {
        (void)wl_display_flush(state.display);
    }
    g_free(state.sakura_icon_uri);
    g_free(state.midori_icon_uri);
    g_free(state.thunar_icon_uri);
    g_free(state.fantasy_window_frame_uri);
    g_free(state.fantasy_compact_window_frame_uri);
    g_free(state.fantasy_dock_uri);
    g_free(state.fantasy_wallpaper_uri);
    g_clear_pointer(&state.fantasy_window_frame_bytes, g_bytes_unref);
    g_clear_pointer(&state.fantasy_compact_window_frame_bytes,
                    g_bytes_unref);
    g_clear_pointer(&state.fantasy_dock_bytes, g_bytes_unref);
    g_clear_pointer(&state.fantasy_wallpaper_bytes, g_bytes_unref);
    g_object_unref(context);
    return state.exit_status;
}
