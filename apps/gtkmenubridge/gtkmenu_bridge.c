#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkwayland.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <wayland-client.h>

#include "menu.h"

#include "nixbench-application-menu-v1-client-protocol.h"

#ifndef NB_WAYLAND_APPLICATION_MENU_VERSION
#define NB_WAYLAND_APPLICATION_MENU_VERSION 1
#endif

enum {
    NB_GTK_MENU_ACTION_CAPACITY = 128,
    NB_GTK_MENU_COMMAND_CAPACITY = NB_MENU_MAX_MENUS * NB_MENU_MAX_ITEMS
};

enum nb_gtk_menu_action_scope {
    NB_GTK_MENU_ACTION_SCOPE_APPLICATION,
    NB_GTK_MENU_ACTION_SCOPE_WINDOW
};

enum nb_gtk_menu_command_kind {
    NB_GTK_MENU_COMMAND_ACTION,
    NB_GTK_MENU_COMMAND_WIDGET
};

struct nb_gtk_menu_bridge;

struct nb_gtk_menu_command_entry {
    uint32_t command;
    enum nb_gtk_menu_command_kind kind;
    enum nb_gtk_menu_action_scope scope;
    char action[NB_GTK_MENU_ACTION_CAPACITY];
    GVariant *target;
    GtkWidget *widget;
};

struct nb_gtk_menu_window_state {
    struct nb_gtk_menu_bridge *bridge;
    GtkWindow *window;
    struct nixbench_application_menu_v1 *menu;
    struct nb_gtk_menu_command_entry
        commands[NB_GTK_MENU_COMMAND_CAPACITY];
    size_t command_count;
    uint32_t next_command;
    size_t menu_count;
    size_t current_item_count;
    gulong realize_handler;
    gulong destroy_handler;
};

struct nb_gtk_menu_bridge {
    GtkApplication *application;
    struct wl_display *display;
    struct wl_registry *registry;
    struct nixbench_application_menu_manager_v1 *menu_manager;
    gulong window_added_handler;
    gulong window_removed_handler;
    gulong menubar_handler;
    gulong app_menu_handler;
    gulong action_state_handler;
    gulong action_enabled_handler;
    guint sync_source;
    guint map_signal;
    gulong map_hook;
    guint show_signal;
    gulong show_hook;
    GPtrArray *popup_menus;
    gboolean debug;
};

static struct nb_gtk_menu_bridge *global_bridge;

static void bridge_on_window_realize(GtkWidget *widget, gpointer data);
static void bridge_on_window_destroy(GtkWidget *widget, gpointer data);
static void bridge_on_window_added(GtkApplication *application,
                                   GtkWindow *window,
                                   gpointer data);
static void bridge_on_window_removed(GtkApplication *application,
                                     GtkWindow *window,
                                     gpointer data);
static struct nb_gtk_menu_window_state *bridge_attach_window(
    struct nb_gtk_menu_bridge *bridge,
    GtkWindow *window);
static void bridge_schedule_sync(struct nb_gtk_menu_bridge *bridge);
static void bridge_track_popup_menu(struct nb_gtk_menu_bridge *bridge,
                                    GtkWidget *widget);

static void bridge_discover_popup_menus(struct nb_gtk_menu_bridge *bridge,
                                        GtkWidget *widget)
{
    GList *children;
    GList *iter;

    if (bridge == NULL || widget == NULL) {
        return;
    }
    if (GTK_IS_MENU(widget)) {
        bridge_track_popup_menu(bridge, widget);
    }
    if (!GTK_IS_CONTAINER(widget)) {
        return;
    }
    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (iter = children; iter != NULL; iter = iter->next) {
        if (GTK_IS_WIDGET(iter->data)) {
            bridge_discover_popup_menus(bridge, GTK_WIDGET(iter->data));
        }
    }
    g_list_free(children);
}

static void command_entry_reset(struct nb_gtk_menu_command_entry *entry)
{
    if (entry == NULL) {
        return;
    }
    if (entry->target != NULL) {
        g_variant_unref(entry->target);
    }
    if (entry->widget != NULL) {
        g_object_unref(entry->widget);
    }
    memset(entry, 0, sizeof(*entry));
}

static void window_state_reset_commands(struct nb_gtk_menu_window_state *state)
{
    size_t index;

    if (state == NULL) {
        return;
    }
    for (index = 0; index < state->command_count; ++index) {
        command_entry_reset(&state->commands[index]);
    }
    state->command_count = 0;
    state->next_command = 1;
    state->menu_count = 0;
    state->current_item_count = 0;
}

static void window_state_clear_menu(struct nb_gtk_menu_window_state *state)
{
    struct nixbench_application_menu_v1 *menu;

    if (state == NULL) {
        return;
    }
    menu = state->menu;
    state->menu = NULL;
    if (menu != NULL) {
        nixbench_application_menu_v1_destroy(menu);
    }
    window_state_reset_commands(state);
}

static void window_state_destroy(gpointer data)
{
    struct nb_gtk_menu_window_state *state = data;

    if (state == NULL) {
        return;
    }
    if (state->window != NULL) {
        if (state->realize_handler != 0 &&
            g_signal_handler_is_connected(state->window,
                                          state->realize_handler)) {
            g_signal_handler_disconnect(state->window, state->realize_handler);
        }
        if (state->destroy_handler != 0 &&
            g_signal_handler_is_connected(state->window,
                                          state->destroy_handler)) {
            g_signal_handler_disconnect(state->window, state->destroy_handler);
        }
    }
    window_state_clear_menu(state);
    g_free(state);
}

static struct nb_gtk_menu_window_state *window_state_from_widget(
    GtkWidget *widget)
{
    if (widget == NULL || !GTK_IS_WINDOW(widget)) {
        return NULL;
    }
    return g_object_get_data(G_OBJECT(widget), "nixbench-gtk-menu-state");
}

static gboolean model_has_actionable_items(GMenuModel *model)
{
    gint count;
    gint index;

    if (model == NULL) {
        return FALSE;
    }

    count = g_menu_model_get_n_items(model);
    for (index = 0; index < count; ++index) {
        GMenuModel *child =
            g_menu_model_get_item_link(model, index, G_MENU_LINK_SUBMENU);
        GMenuModel *section = NULL;
        GVariant *action;

        if (child == NULL) {
            child = g_menu_model_get_item_link(model,
                                               index,
                                               G_MENU_LINK_SECTION);
            section = child;
        }
        if (child != NULL) {
            const gboolean nested = model_has_actionable_items(child);

            g_object_unref(child);
            if (nested) {
                return TRUE;
            }
            continue;
        }

        action = g_menu_model_get_item_attribute_value(
            model,
            index,
            G_MENU_ATTRIBUTE_ACTION,
            G_VARIANT_TYPE_STRING);
        if (action != NULL) {
            g_variant_unref(action);
            return TRUE;
        }
        (void)section;
    }
    return FALSE;
}

static gboolean copy_menu_text(const char *source,
                               char *destination,
                               size_t capacity)
{
    const char *cursor;
    size_t used = 0;

    if (source == NULL || destination == NULL || capacity == 0) {
        return FALSE;
    }
    cursor = source;
    while (*cursor != '\0') {
        const char *next;
        size_t bytes;

        if (*cursor == '_') {
            if (cursor[1] != '_') {
                ++cursor;
                continue;
            }
            ++cursor;
        }
        next = g_utf8_next_char(cursor);
        bytes = (size_t)(next - cursor);
        if (used + bytes >= capacity) {
            break;
        }
        memcpy(destination + used, cursor, bytes);
        used += bytes;
        cursor = next;
    }
    destination[used] = '\0';
    g_strstrip(destination);
    return destination[0] != '\0';
}

static const char *root_menu_label(
    const struct nb_gtk_menu_window_state *state,
    char *buffer,
    size_t capacity)
{
    GtkApplication *application;
    const char *name = NULL;

    application = state != NULL && state->window != NULL
                      ? gtk_window_get_application(state->window)
                      : NULL;
    if (application != NULL) {
        name = g_application_get_application_id(G_APPLICATION(application));
    }
    if (name == NULL || name[0] == '\0') {
        name = g_get_application_name();
    }
    if ((name == NULL || name[0] == '\0') && state != NULL &&
        state->window != NULL) {
        name = gtk_window_get_title(state->window);
    }
    if (name == NULL || name[0] == '\0') {
        name = "Application";
    }
    if (!copy_menu_text(name, buffer, capacity)) {
        g_strlcpy(buffer, "Application", capacity);
    }
    return buffer;
}

static gboolean append_protocol_menu(
    struct nb_gtk_menu_window_state *state,
    struct nixbench_application_menu_v1 *menu,
    const char *label)
{
    char text[NB_MENU_TEXT_CAPACITY];

    if (state == NULL || menu == NULL || state->menu_count >= NB_MENU_MAX_MENUS ||
        !copy_menu_text(label, text, sizeof(text))) {
        return FALSE;
    }
    nixbench_application_menu_v1_append_menu(menu, text);
    ++state->menu_count;
    state->current_item_count = 0;
    return TRUE;
}

static gboolean append_protocol_item(
    struct nb_gtk_menu_window_state *state,
    struct nixbench_application_menu_v1 *menu,
    const char *label,
    uint32_t command,
    uint32_t flags)
{
    char text[NB_MENU_TEXT_CAPACITY];

    if (state == NULL || menu == NULL || state->menu_count == 0 ||
        state->current_item_count >= NB_MENU_MAX_ITEMS ||
        !copy_menu_text(label, text, sizeof(text))) {
        return FALSE;
    }
    nixbench_application_menu_v1_append_item(menu, text, command, flags);
    ++state->current_item_count;
    return TRUE;
}

static gboolean append_protocol_separator(
    struct nb_gtk_menu_window_state *state,
    struct nixbench_application_menu_v1 *menu)
{
    if (state == NULL || menu == NULL || state->menu_count == 0 ||
        state->current_item_count >= NB_MENU_MAX_ITEMS) {
        return FALSE;
    }
    nixbench_application_menu_v1_append_separator(menu);
    ++state->current_item_count;
    return TRUE;
}

static gboolean window_state_has_surface(const struct nb_gtk_menu_window_state *state)
{
    GtkWidget *widget;
    GdkWindow *gdk_window;

    if (state == NULL || state->window == NULL) {
        return FALSE;
    }
    widget = GTK_WIDGET(state->window);
    if (!gtk_widget_get_realized(widget)) {
        return FALSE;
    }
    gdk_window = gtk_widget_get_window(widget);
    if (gdk_window == NULL || !GDK_IS_WAYLAND_WINDOW(gdk_window)) {
        return FALSE;
    }
    return gdk_wayland_window_get_wl_surface(gdk_window) != NULL;
}

static struct wl_surface *window_state_surface(
    const struct nb_gtk_menu_window_state *state)
{
    GtkWidget *widget;
    GdkWindow *gdk_window;

    if (!window_state_has_surface(state)) {
        return NULL;
    }
    widget = GTK_WIDGET(state->window);
    gdk_window = gtk_widget_get_window(widget);
    return gdk_wayland_window_get_wl_surface(gdk_window);
}

static void bridge_registry_global(void *data,
                                   struct wl_registry *registry,
                                   uint32_t name,
                                   const char *interface,
                                   uint32_t version)
{
    struct nb_gtk_menu_bridge *bridge = data;

    if (bridge == NULL || bridge->menu_manager != NULL) {
        return;
    }
    if (strcmp(interface,
               nixbench_application_menu_manager_v1_interface.name) == 0) {
        bridge->menu_manager = wl_registry_bind(
            registry,
            name,
            &nixbench_application_menu_manager_v1_interface,
            version < NB_WAYLAND_APPLICATION_MENU_VERSION
                ? version
                : NB_WAYLAND_APPLICATION_MENU_VERSION);
    }
}

static void bridge_registry_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static gboolean bridge_ensure_manager(struct nb_gtk_menu_bridge *bridge)
{
    static const struct wl_registry_listener listener = {
        .global = bridge_registry_global,
        .global_remove = bridge_registry_global_remove
    };
    GdkDisplay *display;

    if (bridge == NULL) {
        return FALSE;
    }
    if (bridge->menu_manager != NULL) {
        return TRUE;
    }
    if (bridge->display == NULL) {
        display = gdk_display_get_default();
        if (display == NULL || !GDK_IS_WAYLAND_DISPLAY(display)) {
            return FALSE;
        }
        bridge->display = gdk_wayland_display_get_wl_display(display);
        if (bridge->display == NULL) {
            return FALSE;
        }
    }
    if (bridge->registry == NULL) {
        bridge->registry = wl_display_get_registry(bridge->display);
        if (bridge->registry == NULL) {
            return FALSE;
        }
        if (wl_registry_add_listener(bridge->registry, &listener, bridge) != 0) {
            return FALSE;
        }
    }
    if (wl_display_roundtrip(bridge->display) < 0) {
        if (bridge->debug) {
            g_printerr("NixBench GTK menu bridge: registry roundtrip failed\n");
        }
        return FALSE;
    }
    if (bridge->debug && bridge->menu_manager == NULL) {
        g_printerr("NixBench GTK menu bridge: menu protocol is unavailable\n");
    }
    return bridge->menu_manager != NULL;
}

static void activate_command(struct nb_gtk_menu_window_state *state,
                             const struct nb_gtk_menu_command_entry *entry)
{
    GActionGroup *group;

    if (state == NULL || entry == NULL) {
        return;
    }
    if (entry->kind == NB_GTK_MENU_COMMAND_WIDGET) {
        if (entry->widget != NULL &&
            gtk_widget_get_sensitive(entry->widget)) {
            gtk_menu_item_activate(GTK_MENU_ITEM(entry->widget));
        }
        return;
    }

    if (entry->scope == NB_GTK_MENU_ACTION_SCOPE_WINDOW &&
        G_IS_ACTION_GROUP(state->window)) {
        group = G_ACTION_GROUP(state->window);
    } else {
        GtkApplication *application =
            gtk_window_get_application(state->window);

        group = application != NULL && G_IS_ACTION_GROUP(application)
                    ? G_ACTION_GROUP(application)
                    : NULL;
    }
    if (group == NULL ||
        !g_action_group_has_action(group, entry->action)) {
        g_warning("NixBench GTK menu bridge could not find action %s",
                  entry->action);
        return;
    }
    g_action_group_activate_action(group, entry->action, entry->target);
}

static const struct nb_gtk_menu_command_entry *find_command(
    const struct nb_gtk_menu_window_state *state,
    uint32_t command)
{
    size_t index;

    if (state == NULL) {
        return NULL;
    }
    for (index = 0; index < state->command_count; ++index) {
        if (state->commands[index].command == command) {
            return &state->commands[index];
        }
    }
    return NULL;
}

static void menu_command(void *data,
                         struct nixbench_application_menu_v1 *menu,
                         uint32_t command)
{
    struct nb_gtk_menu_window_state *state = data;
    const struct nb_gtk_menu_command_entry *entry = find_command(state,
                                                                 command);

    (void)menu;
    if (entry == NULL) {
        g_warning("NixBench GTK menu bridge received unknown command %u",
                  command);
        return;
    }
    activate_command(state, entry);
    bridge_schedule_sync(state->bridge);
}

static const struct nixbench_application_menu_v1_listener menu_listener = {
    .command = menu_command
};

static GActionGroup *window_action_group(
    const struct nb_gtk_menu_window_state *state,
    enum nb_gtk_menu_action_scope scope)
{
    GtkApplication *application;

    if (state == NULL || state->window == NULL) {
        return NULL;
    }
    if (scope == NB_GTK_MENU_ACTION_SCOPE_WINDOW) {
        return G_IS_ACTION_GROUP(state->window)
                   ? G_ACTION_GROUP(state->window)
                   : NULL;
    }
    application = gtk_window_get_application(state->window);
    return application != NULL && G_IS_ACTION_GROUP(application)
               ? G_ACTION_GROUP(application)
               : NULL;
}

static uint32_t action_item_flags(
    const struct nb_gtk_menu_window_state *state,
    enum nb_gtk_menu_action_scope scope,
    const char *action,
    GVariant *target)
{
    GActionGroup *group = window_action_group(state, scope);
    GVariant *action_state;
    uint32_t flags = 0;

    if (group == NULL || !g_action_group_has_action(group, action)) {
        return 0;
    }
    if (g_action_group_get_action_enabled(group, action)) {
        flags |= NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED;
    }
    action_state = g_action_group_get_action_state(group, action);
    if (action_state != NULL) {
        if ((target != NULL && g_variant_equal(action_state, target)) ||
            (target == NULL &&
             g_variant_is_of_type(action_state, G_VARIANT_TYPE_BOOLEAN) &&
             g_variant_get_boolean(action_state))) {
            flags |= NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_CHECKED;
        }
        g_variant_unref(action_state);
    }
    return flags;
}

static gboolean append_menu_item(struct nb_gtk_menu_window_state *state,
                                 GMenuModel *model,
                                 gint index,
                                 struct nixbench_application_menu_v1 *menu)
{
    GVariant *label;
    GVariant *action;
    GVariant *target;
    const char *label_text = NULL;
    const char *action_text = NULL;
    struct nb_gtk_menu_command_entry *entry;

    if (state->command_count >= NB_GTK_MENU_COMMAND_CAPACITY) {
        return FALSE;
    }

    label = g_menu_model_get_item_attribute_value(model,
                                                  index,
                                                  G_MENU_ATTRIBUTE_LABEL,
                                                  G_VARIANT_TYPE_STRING);
    action = g_menu_model_get_item_attribute_value(model,
                                                   index,
                                                   G_MENU_ATTRIBUTE_ACTION,
                                                   G_VARIANT_TYPE_STRING);
    target = g_menu_model_get_item_attribute_value(model,
                                                   index,
                                                   G_MENU_ATTRIBUTE_TARGET,
                                                   NULL);

    if (label != NULL) {
        label_text = g_variant_get_string(label, NULL);
    }
    if (action != NULL) {
        action_text = g_variant_get_string(action, NULL);
    }
    if (label_text == NULL || label_text[0] == '\0' ||
        action_text == NULL || action_text[0] == '\0') {
        if (label != NULL) {
            g_variant_unref(label);
        }
        if (action != NULL) {
            g_variant_unref(action);
        }
        if (target != NULL) {
            g_variant_unref(target);
        }
        return FALSE;
    }

    entry = &state->commands[state->command_count];
    memset(entry, 0, sizeof(*entry));
    entry->command = state->next_command++;
    entry->kind = NB_GTK_MENU_COMMAND_ACTION;
    if (g_str_has_prefix(action_text, "win.")) {
        entry->scope = NB_GTK_MENU_ACTION_SCOPE_WINDOW;
        action_text += 4;
    } else if (g_str_has_prefix(action_text, "app.")) {
        entry->scope = NB_GTK_MENU_ACTION_SCOPE_APPLICATION;
        action_text += 4;
    } else {
        entry->scope = NB_GTK_MENU_ACTION_SCOPE_APPLICATION;
    }
    g_strlcpy(entry->action, action_text, sizeof(entry->action));
    if (target != NULL) {
        entry->target = g_variant_ref(target);
    }

    if (!append_protocol_item(
            state,
            menu,
            label_text,
            entry->command,
            action_item_flags(state,
                              entry->scope,
                              entry->action,
                              target))) {
        command_entry_reset(entry);
        if (label != NULL) {
            g_variant_unref(label);
        }
        if (action != NULL) {
            g_variant_unref(action);
        }
        if (target != NULL) {
            g_variant_unref(target);
        }
        return FALSE;
    }

    if (label != NULL) {
        g_variant_unref(label);
    }
    if (action != NULL) {
        g_variant_unref(action);
    }
    if (target != NULL) {
        g_variant_unref(target);
    }
    ++state->command_count;
    return TRUE;
}

static gboolean append_menu_contents(struct nb_gtk_menu_window_state *state,
                                     GMenuModel *model,
                                     struct nixbench_application_menu_v1 *menu)
{
    gint count;
    gint index;
    gboolean appended = FALSE;
    gboolean last_was_separator = FALSE;

    count = g_menu_model_get_n_items(model);
    for (index = 0; index < count; ++index) {
        GMenuModel *child =
            g_menu_model_get_item_link(model, index, G_MENU_LINK_SUBMENU);
        gboolean child_is_section = FALSE;

        if (child == NULL) {
            child = g_menu_model_get_item_link(model,
                                               index,
                                               G_MENU_LINK_SECTION);
            child_is_section = TRUE;
        }
        if (child != NULL) {
            if (model_has_actionable_items(child)) {
                if (appended && !last_was_separator) {
                    last_was_separator =
                        append_protocol_separator(state, menu);
                }
                if (append_menu_contents(state, child, menu)) {
                    appended = TRUE;
                    last_was_separator = FALSE;
                }
            }
            g_object_unref(child);
            (void)child_is_section;
            continue;
        }

        if (!append_menu_item(state, model, index, menu)) {
            continue;
        }
        appended = TRUE;
        last_was_separator = FALSE;
    }
    return appended;
}

static gboolean publish_flat_menu(struct nb_gtk_menu_window_state *state,
                                  GMenuModel *model,
                                  struct nixbench_application_menu_v1 *menu)
{
    char label[NB_MENU_TEXT_CAPACITY];

    root_menu_label(state, label, sizeof(label));
    if (!append_protocol_menu(state, menu, label)) {
        return FALSE;
    }
    return append_menu_contents(state, model, menu);
}

static gboolean publish_menubar(struct nb_gtk_menu_window_state *state,
                                GMenuModel *model,
                                struct nixbench_application_menu_v1 *menu)
{
    gint count;
    gint index;
    gboolean appended = FALSE;

    count = g_menu_model_get_n_items(model);
    for (index = 0; index < count; ++index) {
        GVariant *label = g_menu_model_get_item_attribute_value(
            model,
            index,
            G_MENU_ATTRIBUTE_LABEL,
            G_VARIANT_TYPE_STRING);
        GMenuModel *submenu = g_menu_model_get_item_link(model,
                                                         index,
                                                         G_MENU_LINK_SUBMENU);
        GMenuModel *section = NULL;
        const char *label_text = NULL;

        if (submenu == NULL) {
            section = g_menu_model_get_item_link(model,
                                                 index,
                                                 G_MENU_LINK_SECTION);
        }
        if (label != NULL) {
            label_text = g_variant_get_string(label, NULL);
        }
        if ((submenu != NULL || section != NULL) &&
            label_text != NULL && label_text[0] != '\0') {
            GMenuModel *child = submenu != NULL ? submenu : section;

            if (model_has_actionable_items(child) &&
                append_protocol_menu(state, menu, label_text)) {
                appended |= append_menu_contents(state, child, menu);
            }
        } else if (submenu == NULL && section == NULL &&
                   model_has_actionable_items(model)) {
            char fallback_label[NB_MENU_TEXT_CAPACITY];

            root_menu_label(state, fallback_label, sizeof(fallback_label));
            if (append_protocol_menu(state, menu, fallback_label)) {
                appended |= append_menu_contents(state, model, menu);
            }
            if (label != NULL) {
                g_variant_unref(label);
            }
            if (submenu != NULL) {
                g_object_unref(submenu);
            }
            if (section != NULL) {
                g_object_unref(section);
            }
            return appended;
        }

        if (label != NULL) {
            g_variant_unref(label);
        }
        if (submenu != NULL) {
            g_object_unref(submenu);
        }
        if (section != NULL) {
            g_object_unref(section);
        }
    }
    return appended;
}

static GtkWidget *find_widget_menubar(GtkWidget *widget)
{
    GList *children;
    GList *iter;
    GtkWidget *result = NULL;

    if (widget == NULL) {
        return NULL;
    }
    if (GTK_IS_MENU_BAR(widget)) {
        return widget;
    }
    if (!GTK_IS_CONTAINER(widget)) {
        return NULL;
    }

    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (iter = children; iter != NULL && result == NULL; iter = iter->next) {
        if (GTK_IS_WIDGET(iter->data)) {
            result = find_widget_menubar(GTK_WIDGET(iter->data));
        }
    }
    g_list_free(children);
    return result;
}

static size_t menu_model_action_score(GMenuModel *model)
{
    size_t score = 0;
    gint count;
    gint index;

    if (model == NULL) {
        return 0;
    }
    count = g_menu_model_get_n_items(model);
    for (index = 0; index < count; ++index) {
        GMenuModel *submenu =
            g_menu_model_get_item_link(model, index, G_MENU_LINK_SUBMENU);
        GMenuModel *section =
            g_menu_model_get_item_link(model, index, G_MENU_LINK_SECTION);
        GVariant *action = g_menu_model_get_item_attribute_value(
            model,
            index,
            G_MENU_ATTRIBUTE_ACTION,
            NULL);

        if (action != NULL) {
            ++score;
            g_variant_unref(action);
        }
        if (submenu != NULL) {
            score += menu_model_action_score(submenu);
            g_object_unref(submenu);
        }
        if (section != NULL) {
            score += menu_model_action_score(section);
            g_object_unref(section);
        }
    }
    return score;
}

static void find_widget_menu_model(GtkWidget *widget,
                                   GMenuModel **best_model,
                                   size_t *best_score)
{
    GList *children;
    GList *iter;

    if (widget == NULL || best_model == NULL || best_score == NULL) {
        return;
    }
    if (GTK_IS_MENU_BUTTON(widget) && gtk_widget_get_visible(widget)) {
        GMenuModel *model =
            gtk_menu_button_get_menu_model(GTK_MENU_BUTTON(widget));
        const size_t score = menu_model_action_score(model);

        if (model != NULL && score > *best_score) {
            if (*best_model != NULL) {
                g_object_unref(*best_model);
            }
            *best_model = g_object_ref(model);
            *best_score = score;
        }
    }
    if (!GTK_IS_CONTAINER(widget)) {
        return;
    }
    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (iter = children; iter != NULL; iter = iter->next) {
        if (GTK_IS_WIDGET(iter->data)) {
            find_widget_menu_model(GTK_WIDGET(iter->data),
                                   best_model,
                                   best_score);
        }
    }
    g_list_free(children);
}

static GMenuModel *find_best_widget_menu_model(GtkWidget *widget,
                                                size_t *score)
{
    GMenuModel *model = NULL;
    size_t model_score = 0;

    find_widget_menu_model(widget, &model, &model_score);
    if (score != NULL) {
        *score = model_score;
    }
    return model;
}

static gboolean widget_menu_has_items(GtkWidget *widget)
{
    GList *children;
    GList *iter;
    gboolean found = FALSE;

    if (widget == NULL || !GTK_IS_MENU_SHELL(widget)) {
        return FALSE;
    }
    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (iter = children; iter != NULL && !found; iter = iter->next) {
        GtkWidget *item = GTK_IS_WIDGET(iter->data)
                              ? GTK_WIDGET(iter->data)
                              : NULL;
        GtkWidget *submenu;

        if (item == NULL || !gtk_widget_get_visible(item) ||
            GTK_IS_SEPARATOR_MENU_ITEM(item) || !GTK_IS_MENU_ITEM(item)) {
            continue;
        }
        submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
        found = submenu != NULL && GTK_IS_MENU_SHELL(submenu)
                    ? widget_menu_has_items(submenu)
                    : TRUE;
    }
    g_list_free(children);
    return found;
}

static size_t widget_menu_score(GtkWidget *widget)
{
    GList *children;
    GList *iter;
    size_t score = 0;

    if (widget == NULL || !GTK_IS_MENU_SHELL(widget)) {
        return 0;
    }
    children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (iter = children; iter != NULL; iter = iter->next) {
        GtkWidget *item = GTK_IS_WIDGET(iter->data)
                              ? GTK_WIDGET(iter->data)
                              : NULL;
        GtkWidget *submenu;

        if (item == NULL || !gtk_widget_get_visible(item) ||
            GTK_IS_SEPARATOR_MENU_ITEM(item) || !GTK_IS_MENU_ITEM(item)) {
            continue;
        }
        submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
        if (submenu != NULL && GTK_IS_MENU_SHELL(submenu)) {
            score += widget_menu_score(submenu);
        } else {
            ++score;
        }
    }
    g_list_free(children);
    return score;
}

static GtkWidget *bridge_best_popup_menu(struct nb_gtk_menu_bridge *bridge)
{
    GtkWidget *best = NULL;
    size_t best_score = 0;
    guint index;

    if (bridge == NULL || bridge->popup_menus == NULL) {
        return NULL;
    }
    for (index = 0; index < bridge->popup_menus->len; ++index) {
        GtkWidget *menu = g_ptr_array_index(bridge->popup_menus, index);
        size_t score;

        if (menu == NULL || !GTK_IS_MENU(menu) ||
            gtk_menu_get_attach_widget(GTK_MENU(menu)) != NULL) {
            continue;
        }
        score = widget_menu_score(menu);
        if (score > best_score) {
            best = menu;
            best_score = score;
        }
    }
    if (bridge->debug) {
        g_printerr("NixBench GTK menu bridge: popup candidates=%u best=%p "
                   "score=%zu\n",
                   bridge->popup_menus != NULL ? bridge->popup_menus->len : 0,
                   (void *)best,
                   best_score);
    }
    return best;
}

static gboolean append_widget_item(
    struct nb_gtk_menu_window_state *state,
    GtkWidget *widget,
    struct nixbench_application_menu_v1 *menu)
{
    struct nb_gtk_menu_command_entry *entry;
    const char *label;
    uint32_t flags = 0;

    if (state == NULL || widget == NULL || !GTK_IS_MENU_ITEM(widget) ||
        state->command_count >= NB_GTK_MENU_COMMAND_CAPACITY) {
        return FALSE;
    }
    label = gtk_menu_item_get_label(GTK_MENU_ITEM(widget));
    if (label == NULL || label[0] == '\0') {
        return FALSE;
    }
    if (gtk_widget_get_sensitive(widget)) {
        flags |= NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED;
    }
    if (GTK_IS_CHECK_MENU_ITEM(widget) &&
        gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
        flags |= NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_CHECKED;
    }

    entry = &state->commands[state->command_count];
    memset(entry, 0, sizeof(*entry));
    entry->command = state->next_command++;
    entry->kind = NB_GTK_MENU_COMMAND_WIDGET;
    entry->widget = g_object_ref(widget);
    if (!append_protocol_item(state,
                              menu,
                              label,
                              entry->command,
                              flags)) {
        command_entry_reset(entry);
        return FALSE;
    }
    ++state->command_count;
    return TRUE;
}

static gboolean append_widget_menu_contents(
    struct nb_gtk_menu_window_state *state,
    GtkWidget *menu_shell,
    struct nixbench_application_menu_v1 *menu)
{
    GList *children;
    GList *iter;
    gboolean appended = FALSE;
    gboolean separator_pending = FALSE;

    children = gtk_container_get_children(GTK_CONTAINER(menu_shell));
    for (iter = children; iter != NULL; iter = iter->next) {
        GtkWidget *item = GTK_IS_WIDGET(iter->data)
                              ? GTK_WIDGET(iter->data)
                              : NULL;
        GtkWidget *submenu;
        gboolean child_appended;

        if (item == NULL || !gtk_widget_get_visible(item)) {
            continue;
        }
        if (GTK_IS_SEPARATOR_MENU_ITEM(item)) {
            separator_pending = appended;
            continue;
        }
        if (!GTK_IS_MENU_ITEM(item)) {
            continue;
        }

        submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
        if (submenu != NULL && GTK_IS_MENU_SHELL(submenu)) {
            if (!widget_menu_has_items(submenu)) {
                continue;
            }
            if (separator_pending) {
                (void)append_protocol_separator(state, menu);
                separator_pending = FALSE;
            }
            child_appended =
                append_widget_menu_contents(state, submenu, menu);
        } else {
            if (separator_pending) {
                (void)append_protocol_separator(state, menu);
                separator_pending = FALSE;
            }
            child_appended = append_widget_item(state, item, menu);
        }
        appended |= child_appended;
    }
    g_list_free(children);
    return appended;
}

static gboolean widget_menu_has_direct_items(GtkWidget *menu_shell)
{
    GList *children;
    GList *iter;
    gboolean found = FALSE;

    children = gtk_container_get_children(GTK_CONTAINER(menu_shell));
    for (iter = children; iter != NULL && !found; iter = iter->next) {
        GtkWidget *item = GTK_IS_WIDGET(iter->data)
                              ? GTK_WIDGET(iter->data)
                              : NULL;

        if (item != NULL && gtk_widget_get_visible(item) &&
            GTK_IS_MENU_ITEM(item) && !GTK_IS_SEPARATOR_MENU_ITEM(item) &&
            gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)) == NULL) {
            found = TRUE;
        }
    }
    g_list_free(children);
    return found;
}

static gboolean append_widget_direct_items(
    struct nb_gtk_menu_window_state *state,
    GtkWidget *menu_shell,
    struct nixbench_application_menu_v1 *menu)
{
    GList *children;
    GList *iter;
    gboolean appended = FALSE;
    gboolean separator_pending = FALSE;

    children = gtk_container_get_children(GTK_CONTAINER(menu_shell));
    for (iter = children; iter != NULL; iter = iter->next) {
        GtkWidget *item = GTK_IS_WIDGET(iter->data)
                              ? GTK_WIDGET(iter->data)
                              : NULL;

        if (item == NULL || !gtk_widget_get_visible(item)) {
            continue;
        }
        if (GTK_IS_SEPARATOR_MENU_ITEM(item)) {
            separator_pending = appended;
            continue;
        }
        if (!GTK_IS_MENU_ITEM(item) ||
            gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)) != NULL) {
            continue;
        }
        if (separator_pending) {
            (void)append_protocol_separator(state, menu);
            separator_pending = FALSE;
        }
        appended |= append_widget_item(state, item, menu);
    }
    g_list_free(children);
    return appended;
}

static gboolean publish_widget_submenus(
    struct nb_gtk_menu_window_state *state,
    GtkWidget *menu_shell,
    struct nixbench_application_menu_v1 *menu)
{
    GList *children;
    GList *iter;
    gboolean appended = FALSE;

    children = gtk_container_get_children(GTK_CONTAINER(menu_shell));
    for (iter = children; iter != NULL; iter = iter->next) {
        GtkWidget *item = GTK_IS_WIDGET(iter->data)
                              ? GTK_WIDGET(iter->data)
                              : NULL;
        GtkWidget *submenu;
        const char *label;

        if (item == NULL || !gtk_widget_get_visible(item) ||
            !GTK_IS_MENU_ITEM(item)) {
            continue;
        }
        submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
        if (submenu == NULL || !GTK_IS_MENU_SHELL(submenu)) {
            continue;
        }
        label = gtk_menu_item_get_label(GTK_MENU_ITEM(item));
        if (widget_menu_has_direct_items(submenu) &&
            append_protocol_menu(state, menu, label)) {
            appended |= append_widget_direct_items(state, submenu, menu);
        }
        appended |= publish_widget_submenus(state, submenu, menu);
    }
    g_list_free(children);
    return appended;
}

static gboolean publish_widget_popup(
    struct nb_gtk_menu_window_state *state,
    GtkWidget *popup,
    struct nixbench_application_menu_v1 *menu)
{
    char label[NB_MENU_TEXT_CAPACITY];
    gboolean appended = FALSE;

    if (popup == NULL || !GTK_IS_MENU(popup)) {
        return FALSE;
    }
    root_menu_label(state, label, sizeof(label));
    if (widget_menu_has_direct_items(popup) &&
        append_protocol_menu(state, menu, label)) {
        appended |= append_widget_direct_items(state, popup, menu);
    }
    appended |= publish_widget_submenus(state, popup, menu);
    return appended;
}

static gboolean publish_widget_menubar(
    struct nb_gtk_menu_window_state *state,
    GtkWidget *menubar,
    struct nixbench_application_menu_v1 *menu)
{
    GList *children;
    GList *iter;
    gboolean appended = FALSE;

    if (menubar == NULL || !GTK_IS_MENU_BAR(menubar)) {
        return FALSE;
    }
    children = gtk_container_get_children(GTK_CONTAINER(menubar));
    for (iter = children; iter != NULL; iter = iter->next) {
        GtkWidget *item = GTK_IS_WIDGET(iter->data)
                              ? GTK_WIDGET(iter->data)
                              : NULL;
        GtkWidget *submenu;
        const char *label;

        if (item == NULL || !gtk_widget_get_visible(item) ||
            !GTK_IS_MENU_ITEM(item)) {
            continue;
        }
        submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
        label = gtk_menu_item_get_label(GTK_MENU_ITEM(item));
        if (submenu == NULL || !GTK_IS_MENU_SHELL(submenu) ||
            !widget_menu_has_items(submenu) ||
            !append_protocol_menu(state, menu, label)) {
            continue;
        }
        appended |= append_widget_menu_contents(state, submenu, menu);
    }
    g_list_free(children);
    return appended;
}

static gboolean publish_window_menu(struct nb_gtk_menu_window_state *state)
{
    GMenuModel *model = NULL;
    GtkApplication *application;
    GtkWidget *menubar;
    GtkWidget *popup;
    struct nixbench_application_menu_v1 *menu;
    gboolean model_is_menubar = FALSE;
    gboolean model_is_menu_button = FALSE;
    gboolean published;
    const char *source;

    if (state == NULL || state->bridge == NULL || state->window == NULL) {
        return FALSE;
    }
    if (!bridge_ensure_manager(state->bridge)) {
        if (state != NULL && state->bridge != NULL && state->bridge->debug) {
            g_printerr("NixBench GTK menu bridge: cannot publish without "
                       "menu manager\n");
        }
        return FALSE;
    }

    application = gtk_window_get_application(state->window);
    if (application != NULL) {
        g_object_get(application, "menubar", &model, NULL);
        model_is_menubar = model != NULL;
        if (model == NULL) {
            g_object_get(application, "app-menu", &model, NULL);
        }
    }
    menubar = model == NULL
                  ? find_widget_menubar(GTK_WIDGET(state->window))
                  : NULL;
    if (model == NULL && menubar == NULL) {
        size_t score = 0;

        model = find_best_widget_menu_model(GTK_WIDGET(state->window),
                                            &score);
        model_is_menu_button = model != NULL;
        if (state->bridge->debug) {
            g_printerr("NixBench GTK menu bridge: menu-button model=%p "
                       "score=%zu for window %p\n",
                       (void *)model,
                       score,
                       (void *)state->window);
        }
    }
    popup = model == NULL && menubar == NULL
                ? bridge_best_popup_menu(state->bridge)
                : NULL;
    if (model == NULL && menubar == NULL && popup == NULL) {
        if (state->bridge->debug) {
            g_printerr("NixBench GTK menu bridge: no menu source for window "
                       "%p\n",
                       (void *)state->window);
        }
        window_state_clear_menu(state);
        return FALSE;
    }
    if (!window_state_has_surface(state)) {
        if (state->bridge->debug) {
            g_printerr("NixBench GTK menu bridge: window %p has no Wayland "
                       "surface yet\n",
                       (void *)state->window);
        }
        if (model != NULL) {
            g_object_unref(model);
        }
        return FALSE;
    }

    if (state->menu != NULL) {
        window_state_clear_menu(state);
    }

    menu = nixbench_application_menu_manager_v1_get_menu(
        state->bridge->menu_manager,
        window_state_surface(state));
    if (menu == NULL) {
        if (model != NULL) {
            g_object_unref(model);
        }
        return FALSE;
    }
    state->menu = menu;
    nixbench_application_menu_v1_add_listener(state->menu,
                                              &menu_listener,
                                              state);
    window_state_reset_commands(state);

    if (model == NULL && menubar != NULL) {
        source = "GtkMenuBar";
        published = publish_widget_menubar(state, menubar, menu);
    } else if (model == NULL) {
        source = "GtkMenu popup";
        published = publish_widget_popup(state, popup, menu);
    } else if (model_is_menubar) {
        source = "GtkApplication menubar";
        published = publish_menubar(state, model, menu);
    } else if (model_is_menu_button) {
        source = "GtkMenuButton model";
        published = publish_flat_menu(state, model, menu);
    } else {
        source = "GtkApplication app-menu";
        published = publish_flat_menu(state, model, menu);
    }
    if (model != NULL) {
        g_object_unref(model);
    }

    if (!published || state->command_count == 0) {
        window_state_clear_menu(state);
        return FALSE;
    }

    nixbench_application_menu_v1_commit(state->menu);
    g_printerr("NixBench GTK menu bridge: published %zu menu(s) and %zu "
               "command(s) from %s\n",
               state->menu_count,
               state->command_count,
               source);
    return TRUE;
}

static void bridge_sync_window(GtkWindow *window)
{
    struct nb_gtk_menu_window_state *state =
        window_state_from_widget(GTK_WIDGET(window));

    if (state != NULL) {
        (void)publish_window_menu(state);
    }
}

static void bridge_sync_all_windows(struct nb_gtk_menu_bridge *bridge)
{
    GList *windows;
    GList *iter;

    if (bridge == NULL) {
        return;
    }
    windows = gtk_window_list_toplevels();
    if (bridge->debug) {
        g_printerr("NixBench GTK menu bridge: syncing %u toplevel(s), %u "
                   "popup(s)\n",
                   g_list_length(windows),
                   bridge->popup_menus != NULL ? bridge->popup_menus->len : 0);
    }
    /*
     * Some GTK3 programs construct their detached menus before GTK invokes
     * gtk_module_init(). Their popup host windows are still present in the
     * toplevel widget trees, so discover those existing menus before looking
     * for a source to publish. The signal hooks handle menus created later.
     */
    for (iter = windows; iter != NULL; iter = iter->next) {
        if (GTK_IS_WIDGET(iter->data)) {
            bridge_discover_popup_menus(bridge, GTK_WIDGET(iter->data));
        }
    }
    for (iter = windows; iter != NULL; iter = iter->next) {
        if (GTK_IS_WINDOW(iter->data)) {
            bridge_attach_window(bridge, GTK_WINDOW(iter->data));
            bridge_sync_window(GTK_WINDOW(iter->data));
        }
    }
    g_list_free(windows);
}

static gboolean bridge_sync_idle(gpointer data)
{
    struct nb_gtk_menu_bridge *bridge = data;

    if (bridge == NULL) {
        return G_SOURCE_REMOVE;
    }
    bridge->sync_source = 0;
    bridge_sync_all_windows(bridge);
    return G_SOURCE_REMOVE;
}

static void bridge_schedule_sync(struct nb_gtk_menu_bridge *bridge)
{
    if (bridge != NULL && bridge->sync_source == 0) {
        bridge->sync_source = g_idle_add(bridge_sync_idle, bridge);
    }
}

static void bridge_popup_menu_destroyed(gpointer data, GObject *object)
{
    struct nb_gtk_menu_bridge *bridge = data;
    guint index;

    if (bridge == NULL || bridge->popup_menus == NULL) {
        return;
    }
    for (index = 0; index < bridge->popup_menus->len; ++index) {
        if (g_ptr_array_index(bridge->popup_menus, index) == object) {
            g_ptr_array_remove_index_fast(bridge->popup_menus, index);
            break;
        }
    }
    bridge_schedule_sync(bridge);
}

static void bridge_track_popup_menu(struct nb_gtk_menu_bridge *bridge,
                                    GtkWidget *widget)
{
    guint index;

    if (bridge == NULL || widget == NULL || !GTK_IS_MENU(widget)) {
        return;
    }
    if (bridge->popup_menus == NULL) {
        bridge->popup_menus = g_ptr_array_new();
    }
    for (index = 0; index < bridge->popup_menus->len; ++index) {
        if (g_ptr_array_index(bridge->popup_menus, index) == widget) {
            return;
        }
    }
    g_ptr_array_add(bridge->popup_menus, widget);
    if (bridge->debug) {
        g_printerr("NixBench GTK menu bridge: tracked popup %p attach=%p "
                   "score=%zu\n",
                   (void *)widget,
                   (void *)gtk_menu_get_attach_widget(GTK_MENU(widget)),
                   widget_menu_score(widget));
    }
    g_object_weak_ref(G_OBJECT(widget),
                      bridge_popup_menu_destroyed,
                      bridge);
}

static gboolean bridge_on_widget_map(GSignalInvocationHint *hint,
                                     guint parameter_count,
                                     const GValue *parameters,
                                     gpointer data)
{
    struct nb_gtk_menu_bridge *bridge = data;
    GObject *object;

    (void)hint;
    if (bridge == NULL || parameter_count == 0 || parameters == NULL) {
        return TRUE;
    }
    object = g_value_get_object(&parameters[0]);
    if (object != NULL) {
        if (bridge->debug &&
            (GTK_IS_WINDOW(object) || GTK_IS_MENU(object))) {
            g_printerr("NixBench GTK menu bridge: widget signal for %s %p\n",
                       G_OBJECT_TYPE_NAME(object),
                       (void *)object);
        }
        if (GTK_IS_WINDOW(object)) {
            bridge_attach_window(bridge, GTK_WINDOW(object));
        } else if (GTK_IS_MENU(object)) {
            bridge_track_popup_menu(bridge, GTK_WIDGET(object));
        }
    }
    bridge_schedule_sync(bridge);
    return TRUE;
}

static gboolean bridge_on_widget_show(GSignalInvocationHint *hint,
                                      guint parameter_count,
                                      const GValue *parameters,
                                      gpointer data)
{
    return bridge_on_widget_map(hint,
                                parameter_count,
                                parameters,
                                data);
}

static void bridge_on_window_realize(GtkWidget *widget, gpointer data)
{
    struct nb_gtk_menu_window_state *state = data;

    if (GTK_IS_WINDOW(widget)) {
        bridge_schedule_sync(state != NULL ? state->bridge : NULL);
    }
}

static void bridge_on_window_destroy(GtkWidget *widget, gpointer data)
{
    struct nb_gtk_menu_window_state *state = data;

    (void)widget;
    if (state != NULL) {
        window_state_clear_menu(state);
    }
}

static void bridge_on_menu_model_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer data)
{
    struct nb_gtk_menu_bridge *bridge = data;

    (void)object;
    (void)pspec;
    bridge_schedule_sync(bridge);
}

static void bridge_on_action_state_changed(GActionGroup *group,
                                           const gchar *action_name,
                                           GVariant *state,
                                           gpointer data)
{
    struct nb_gtk_menu_bridge *bridge = data;

    (void)group;
    (void)action_name;
    (void)state;
    bridge_schedule_sync(bridge);
}

static void bridge_on_action_enabled_changed(GActionGroup *group,
                                             const gchar *action_name,
                                             gboolean enabled,
                                             gpointer data)
{
    struct nb_gtk_menu_bridge *bridge = data;

    (void)group;
    (void)action_name;
    (void)enabled;
    bridge_schedule_sync(bridge);
}

static void bridge_bind_application(struct nb_gtk_menu_bridge *bridge,
                                    GtkApplication *application)
{
    if (bridge == NULL || application == NULL ||
        bridge->application == application) {
        return;
    }
    if (bridge->application != NULL) {
        g_warning("NixBench GTK menu bridge encountered multiple "
                  "GtkApplication objects");
        return;
    }

    bridge->application = g_object_ref(application);
    bridge->window_added_handler = g_signal_connect(
        application,
        "window-added",
        G_CALLBACK(bridge_on_window_added),
        bridge);
    bridge->window_removed_handler = g_signal_connect(
        application,
        "window-removed",
        G_CALLBACK(bridge_on_window_removed),
        bridge);
    bridge->menubar_handler = g_signal_connect(
        application,
        "notify::menubar",
        G_CALLBACK(bridge_on_menu_model_changed),
        bridge);
    bridge->app_menu_handler = g_signal_connect(
        application,
        "notify::app-menu",
        G_CALLBACK(bridge_on_menu_model_changed),
        bridge);
    bridge->action_state_handler = g_signal_connect(
        application,
        "action-state-changed",
        G_CALLBACK(bridge_on_action_state_changed),
        bridge);
    bridge->action_enabled_handler = g_signal_connect(
        application,
        "action-enabled-changed",
        G_CALLBACK(bridge_on_action_enabled_changed),
        bridge);
}

static struct nb_gtk_menu_window_state *bridge_attach_window(
    struct nb_gtk_menu_bridge *bridge,
    GtkWindow *window)
{
    struct nb_gtk_menu_window_state *state;

    if (bridge == NULL || window == NULL) {
        return NULL;
    }
    bridge_bind_application(bridge, gtk_window_get_application(window));
    state = g_object_get_data(G_OBJECT(window), "nixbench-gtk-menu-state");
    if (state != NULL) {
        return state;
    }

    state = g_new0(struct nb_gtk_menu_window_state, 1);
    state->bridge = bridge;
    state->window = window;
    state->next_command = 1;
    g_object_set_data_full(G_OBJECT(window),
                           "nixbench-gtk-menu-state",
                           state,
                           window_state_destroy);
    state->realize_handler = g_signal_connect(
        window,
        "realize",
        G_CALLBACK(bridge_on_window_realize),
        state);
    state->destroy_handler = g_signal_connect(
        window,
        "destroy",
        G_CALLBACK(bridge_on_window_destroy),
        state);

    if (gtk_widget_get_realized(GTK_WIDGET(window))) {
        bridge_schedule_sync(bridge);
    }
    return state;
}

static void bridge_on_window_added(GtkApplication *application,
                                   GtkWindow *window,
                                   gpointer data)
{
    struct nb_gtk_menu_bridge *bridge = data;

    (void)application;
    bridge_attach_window(bridge, window);
    bridge_schedule_sync(bridge);
}

static void bridge_on_window_removed(GtkApplication *application,
                                     GtkWindow *window,
                                     gpointer data)
{
    struct nb_gtk_menu_bridge *bridge = data;
    struct nb_gtk_menu_window_state *state;

    (void)application;
    if (window == NULL) {
        return;
    }
    state = g_object_get_data(G_OBJECT(window), "nixbench-gtk-menu-state");
    if (state != NULL && state->bridge == bridge) {
        window_state_clear_menu(state);
    }
}

static struct nb_gtk_menu_bridge *bridge_create(void)
{
    return g_new0(struct nb_gtk_menu_bridge, 1);
}

static void bridge_destroy(struct nb_gtk_menu_bridge *bridge)
{
    GList *windows;
    GList *iter;

    if (bridge == NULL) {
        return;
    }
    if (bridge->sync_source != 0) {
        g_source_remove(bridge->sync_source);
        bridge->sync_source = 0;
    }
    if (bridge->map_signal != 0 && bridge->map_hook != 0) {
        g_signal_remove_emission_hook(bridge->map_signal, bridge->map_hook);
        bridge->map_hook = 0;
    }
    if (bridge->show_signal != 0 && bridge->show_hook != 0) {
        g_signal_remove_emission_hook(bridge->show_signal,
                                      bridge->show_hook);
        bridge->show_hook = 0;
    }

    windows = gtk_window_list_toplevels();
    for (iter = windows; iter != NULL; iter = iter->next) {
        struct nb_gtk_menu_window_state *state;

        if (!GTK_IS_WINDOW(iter->data)) {
            continue;
        }
        state = g_object_get_data(G_OBJECT(iter->data),
                                  "nixbench-gtk-menu-state");
        if (state != NULL && state->bridge == bridge) {
            g_object_set_data(G_OBJECT(iter->data),
                              "nixbench-gtk-menu-state",
                              NULL);
        }
    }
    g_list_free(windows);

    if (bridge->application != NULL) {
        const gulong handlers[] = {
            bridge->window_added_handler,
            bridge->window_removed_handler,
            bridge->menubar_handler,
            bridge->app_menu_handler,
            bridge->action_state_handler,
            bridge->action_enabled_handler
        };
        size_t index;

        for (index = 0; index < G_N_ELEMENTS(handlers); ++index) {
            if (handlers[index] != 0) {
                g_signal_handler_disconnect(bridge->application,
                                            handlers[index]);
            }
        }
        g_object_unref(bridge->application);
        bridge->application = NULL;
    }
    if (bridge->menu_manager != NULL) {
        nixbench_application_menu_manager_v1_destroy(bridge->menu_manager);
        bridge->menu_manager = NULL;
    }
    if (bridge->registry != NULL) {
        wl_registry_destroy(bridge->registry);
        bridge->registry = NULL;
    }
    if (bridge->popup_menus != NULL) {
        guint index;

        for (index = 0; index < bridge->popup_menus->len; ++index) {
            GObject *menu = g_ptr_array_index(bridge->popup_menus, index);

            g_object_weak_unref(menu,
                                bridge_popup_menu_destroyed,
                                bridge);
        }
        g_ptr_array_free(bridge->popup_menus, TRUE);
        bridge->popup_menus = NULL;
    }
    g_free(bridge);
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
{
    GApplication *default_application;
    GSignalQuery query = {0};
    gpointer widget_class;

    (void)argc;
    (void)argv;
    if (global_bridge != NULL) {
        return;
    }

    g_printerr("NixBench GTK menu bridge: loaded\n");
    global_bridge = bridge_create();
    global_bridge->debug =
        g_strcmp0(g_getenv("NIXBENCH_GTK_MENU_BRIDGE_DEBUG"), "1") == 0;
    default_application = g_application_get_default();
    if (default_application != NULL &&
        GTK_IS_APPLICATION(default_application)) {
        bridge_bind_application(global_bridge,
                                GTK_APPLICATION(default_application));
    }

    /*
     * GTK can initialize loadable modules before GtkWidget's class has been
     * referenced. Its inherited signals do not exist in the signal registry
     * until class initialization has run, so g_signal_lookup() would silently
     * return zero and the bridge would miss every subsequently created menu.
     */
    widget_class = g_type_class_ref(GTK_TYPE_WIDGET);
    global_bridge->map_signal = g_signal_lookup("map", GTK_TYPE_WIDGET);
    if (global_bridge->map_signal != 0) {
        g_signal_query(global_bridge->map_signal, &query);
        if ((query.signal_flags & G_SIGNAL_NO_HOOKS) == 0) {
            global_bridge->map_hook = g_signal_add_emission_hook(
                global_bridge->map_signal,
                0,
                bridge_on_widget_map,
                global_bridge,
                NULL);
        }
    }
    global_bridge->show_signal = g_signal_lookup("show", GTK_TYPE_WIDGET);
    if (global_bridge->show_signal != 0) {
        g_signal_query(global_bridge->show_signal, &query);
        if ((query.signal_flags & G_SIGNAL_NO_HOOKS) == 0) {
            global_bridge->show_hook = g_signal_add_emission_hook(
                global_bridge->show_signal,
                0,
                bridge_on_widget_show,
                global_bridge,
                NULL);
        }
    }
    if (widget_class != NULL) {
        g_type_class_unref(widget_class);
    }
    if (global_bridge->debug) {
        g_printerr("NixBench GTK menu bridge: hooks map=%u/%lu show=%u/%lu\n",
                   global_bridge->map_signal,
                   global_bridge->map_hook,
                   global_bridge->show_signal,
                   global_bridge->show_hook);
    }
    bridge_schedule_sync(global_bridge);
}

G_MODULE_EXPORT void gtk_module_exit(void)
{
    bridge_destroy(global_bridge);
    global_bridge = NULL;
}
