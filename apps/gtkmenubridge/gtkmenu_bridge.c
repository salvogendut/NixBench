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

enum {
    NB_GTK_MENU_ACTION_CAPACITY = 128,
    NB_GTK_MENU_COMMAND_CAPACITY = NB_MENU_MAX_MENUS * NB_MENU_MAX_ITEMS
};

enum nb_gtk_menu_action_scope {
    NB_GTK_MENU_ACTION_SCOPE_APPLICATION,
    NB_GTK_MENU_ACTION_SCOPE_WINDOW
};

struct nb_gtk_menu_bridge;

struct nb_gtk_menu_command_entry {
    uint32_t command;
    enum nb_gtk_menu_action_scope scope;
    char action[NB_GTK_MENU_ACTION_CAPACITY];
    GVariant *target;
};

struct nb_gtk_menu_window_state {
    struct nb_gtk_menu_bridge *bridge;
    GtkWindow *window;
    struct nixbench_application_menu_v1 *menu;
    struct nb_gtk_menu_command_entry
        commands[NB_GTK_MENU_COMMAND_CAPACITY];
    size_t command_count;
    uint32_t next_command;
    gulong realize_handler;
    gulong destroy_handler;
};

struct nb_gtk_menu_bridge {
    GtkApplication *application;
    struct wl_display *display;
    struct wl_registry *registry;
    struct nixbench_application_menu_manager_v1 *menu_manager;
};

static struct nb_gtk_menu_bridge *global_bridge;

static void bridge_on_window_realize(GtkWidget *widget, gpointer data);
static void bridge_on_window_destroy(GtkWidget *widget, gpointer data);

static void command_entry_reset(struct nb_gtk_menu_command_entry *entry)
{
    if (entry == NULL) {
        return;
    }
    if (entry->target != NULL) {
        g_variant_unref(entry->target);
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
        if (state->realize_handler != 0) {
            g_signal_handler_disconnect(state->window, state->realize_handler);
        }
        if (state->destroy_handler != 0) {
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

static gboolean model_has_nested_menus(GMenuModel *model)
{
    gint count;
    gint index;

    if (model == NULL) {
        return FALSE;
    }
    count = g_menu_model_get_n_items(model);
    for (index = 0; index < count; ++index) {
        GMenuModel *submenu =
            g_menu_model_get_item_link(model, index, G_MENU_LINK_SUBMENU);
        GMenuModel *section;

        if (submenu != NULL) {
            g_object_unref(submenu);
            return TRUE;
        }
        section = g_menu_model_get_item_link(model,
                                             index,
                                             G_MENU_LINK_SECTION);
        if (section != NULL) {
            g_object_unref(section);
            return TRUE;
        }
    }
    return FALSE;
}

static const char *root_menu_label(GtkApplication *application,
                                   char *buffer,
                                   size_t capacity)
{
    const char *name;

    name = g_application_get_application_id(G_APPLICATION(application));
    if (name == NULL || name[0] == '\0') {
        name = g_get_application_name();
    }
    if (name == NULL || name[0] == '\0') {
        name = "Application";
    }
    g_strlcpy(buffer, name, capacity);
    return buffer;
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
        return FALSE;
    }
    return bridge->menu_manager != NULL;
}

static struct nb_gtk_menu_window_state *window_state_attach(
    struct nb_gtk_menu_bridge *bridge,
    GtkWindow *window)
{
    struct nb_gtk_menu_window_state *state;

    if (bridge == NULL || window == NULL) {
        return NULL;
    }
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
    return state;
}

static void activate_command(struct nb_gtk_menu_window_state *state,
                             const struct nb_gtk_menu_command_entry *entry)
{
    GtkWidget *widget;

    if (state == NULL || entry == NULL) {
        return;
    }
    widget = GTK_WIDGET(state->window);
    if (entry->scope == NB_GTK_MENU_ACTION_SCOPE_WINDOW) {
        gtk_widget_activate_action(widget, entry->action, entry->target);
        return;
    }
    g_action_group_activate_action(G_ACTION_GROUP(state->bridge->application),
                                   entry->action,
                                   entry->target);
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
}

static const struct nixbench_application_menu_v1_listener menu_listener = {
    .command = menu_command
};

static gboolean append_menu_item(struct nb_gtk_menu_window_state *state,
                                 GMenuModel *model,
                                 gint index,
                                 struct nixbench_application_menu_v1 *menu)
{
    GVariant *label;
    GVariant *action;
    GVariant *target;
    GVariant *toggle_state;
    const char *label_text = NULL;
    const char *action_text = NULL;
    gboolean checked = FALSE;
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
    toggle_state = g_menu_model_get_item_attribute_value(model,
                                                         index,
                                                         G_MENU_ATTRIBUTE_TOGGLE_STATE,
                                                         NULL);

    if (label != NULL) {
        label_text = g_variant_get_string(label, NULL);
    }
    if (action != NULL) {
        action_text = g_variant_get_string(action, NULL);
    }
    if (toggle_state != NULL && g_variant_is_of_type(toggle_state, G_VARIANT_TYPE_BOOLEAN)) {
        checked = g_variant_get_boolean(toggle_state);
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
        if (toggle_state != NULL) {
            g_variant_unref(toggle_state);
        }
        return FALSE;
    }

    entry = &state->commands[state->command_count];
    memset(entry, 0, sizeof(*entry));
    entry->command = state->next_command++;
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

    nixbench_application_menu_v1_append_item(
        menu,
        label_text,
        entry->command,
        NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_ENABLED |
            (checked ? NIXBENCH_APPLICATION_MENU_V1_ITEM_FLAGS_CHECKED : 0));

    if (label != NULL) {
        g_variant_unref(label);
    }
    if (action != NULL) {
        g_variant_unref(action);
    }
    if (target != NULL) {
        g_variant_unref(target);
    }
    if (toggle_state != NULL) {
        g_variant_unref(toggle_state);
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
                    nixbench_application_menu_v1_append_separator(menu);
                    last_was_separator = TRUE;
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

    root_menu_label(state->bridge->application, label, sizeof(label));
    nixbench_application_menu_v1_append_menu(menu, label);
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

            nixbench_application_menu_v1_append_menu(menu, label_text);
            appended |= append_menu_contents(state, child, menu);
        } else if (submenu == NULL && section == NULL &&
                   model_has_actionable_items(model)) {
            char fallback_label[NB_MENU_TEXT_CAPACITY];

            root_menu_label(state->bridge->application,
                            fallback_label,
                            sizeof(fallback_label));
            nixbench_application_menu_v1_append_menu(menu, fallback_label);
            appended |= append_menu_contents(state, model, menu);
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

static gboolean publish_window_menu(struct nb_gtk_menu_window_state *state)
{
    GMenuModel *model = NULL;
    struct nixbench_application_menu_v1 *menu;
    gboolean has_nested;
    gboolean published;

    if (state == NULL || state->bridge == NULL || state->window == NULL) {
        return FALSE;
    }
    if (!bridge_ensure_manager(state->bridge)) {
        return FALSE;
    }

    g_object_get(state->bridge->application, "menubar", &model, NULL);
    if (model == NULL) {
        g_object_get(state->bridge->application, "app-menu", &model, NULL);
    }
    if (model == NULL) {
        window_state_clear_menu(state);
        return FALSE;
    }
    if (!window_state_has_surface(state)) {
        g_object_unref(model);
        return FALSE;
    }

    has_nested = model_has_nested_menus(model);
    if (state->menu != NULL) {
        window_state_clear_menu(state);
    }

    menu = nixbench_application_menu_manager_v1_get_menu(
        state->bridge->menu_manager,
        window_state_surface(state));
    if (menu == NULL) {
        g_object_unref(model);
        return FALSE;
    }
    state->menu = menu;
    nixbench_application_menu_v1_add_listener(state->menu,
                                              &menu_listener,
                                              state);
    window_state_reset_commands(state);

    if (has_nested) {
        published = publish_menubar(state, model, menu);
    } else {
        published = publish_flat_menu(state, model, menu);
    }
    g_object_unref(model);

    if (!published || state->command_count == 0) {
        window_state_clear_menu(state);
        return FALSE;
    }

    nixbench_application_menu_v1_commit(state->menu);
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

    if (bridge == NULL || bridge->application == NULL) {
        return;
    }
    windows = gtk_application_get_windows(bridge->application);
    for (iter = windows; iter != NULL; iter = iter->next) {
        if (GTK_IS_WINDOW(iter->data)) {
            bridge_sync_window(GTK_WINDOW(iter->data));
        }
    }
}

static void bridge_on_window_realize(GtkWidget *widget, gpointer data)
{
    (void)data;
    if (GTK_IS_WINDOW(widget)) {
        bridge_sync_window(GTK_WINDOW(widget));
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
    bridge_sync_all_windows(bridge);
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
    bridge_sync_all_windows(bridge);
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
    bridge_sync_all_windows(bridge);
}

static struct nb_gtk_menu_window_state *bridge_attach_window(
    struct nb_gtk_menu_bridge *bridge,
    GtkWindow *window)
{
    struct nb_gtk_menu_window_state *state;

    if (bridge == NULL || window == NULL) {
        return NULL;
    }
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
        bridge_sync_window(window);
    }
    return state;
}

static void bridge_attach_existing_windows(struct nb_gtk_menu_bridge *bridge)
{
    GList *windows;
    GList *iter;

    if (bridge == NULL || bridge->application == NULL) {
        return;
    }
    windows = gtk_application_get_windows(bridge->application);
    for (iter = windows; iter != NULL; iter = iter->next) {
        if (GTK_IS_WINDOW(iter->data)) {
            bridge_attach_window(bridge, GTK_WINDOW(iter->data));
        }
    }
}

static void bridge_on_window_added(GtkApplication *application,
                                   GtkWindow *window,
                                   gpointer data)
{
    struct nb_gtk_menu_bridge *bridge = data;

    (void)application;
    bridge_attach_window(bridge, window);
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

static struct nb_gtk_menu_bridge *bridge_create(GtkApplication *application)
{
    struct nb_gtk_menu_bridge *bridge;

    bridge = g_new0(struct nb_gtk_menu_bridge, 1);
    bridge->application = application;
    return bridge;
}

static void bridge_destroy(struct nb_gtk_menu_bridge *bridge)
{
    if (bridge == NULL) {
        return;
    }
    if (bridge->menu_manager != NULL) {
        nixbench_application_menu_manager_v1_destroy(bridge->menu_manager);
        bridge->menu_manager = NULL;
    }
    if (bridge->registry != NULL) {
        wl_registry_destroy(bridge->registry);
        bridge->registry = NULL;
    }
    if (bridge->application != NULL) {
        g_signal_handlers_disconnect_by_func(bridge->application,
                                             bridge_on_window_added,
                                             bridge);
        g_signal_handlers_disconnect_by_func(bridge->application,
                                             bridge_on_window_removed,
                                             bridge);
        g_signal_handlers_disconnect_by_func(bridge->application,
                                             bridge_on_menu_model_changed,
                                             bridge);
        g_signal_handlers_disconnect_by_func(bridge->application,
                                             bridge_on_action_state_changed,
                                             bridge);
        g_signal_handlers_disconnect_by_func(bridge->application,
                                             bridge_on_action_enabled_changed,
                                             bridge);
        g_object_unref(bridge->application);
        bridge->application = NULL;
    }
    g_free(bridge);
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
{
    GApplication *default_application;
    GtkApplication *application;
    GList *windows;

    (void)argc;
    (void)argv;
    if (global_bridge != NULL) {
        return;
    }

    default_application = g_application_get_default();
    if (default_application == NULL ||
        !GTK_IS_APPLICATION(default_application)) {
        return;
    }

    application = GTK_APPLICATION(g_object_ref(default_application));
    global_bridge = bridge_create(application);
    g_signal_connect(application,
                     "window-added",
                     G_CALLBACK(bridge_on_window_added),
                     global_bridge);
    g_signal_connect(application,
                     "window-removed",
                     G_CALLBACK(bridge_on_window_removed),
                     global_bridge);
    g_signal_connect(application,
                     "notify::menubar",
                     G_CALLBACK(bridge_on_menu_model_changed),
                     global_bridge);
    g_signal_connect(application,
                     "notify::app-menu",
                     G_CALLBACK(bridge_on_menu_model_changed),
                     global_bridge);
    g_signal_connect(application,
                     "action-state-changed",
                     G_CALLBACK(bridge_on_action_state_changed),
                     global_bridge);
    g_signal_connect(application,
                     "action-enabled-changed",
                     G_CALLBACK(bridge_on_action_enabled_changed),
                     global_bridge);

    bridge_attach_existing_windows(global_bridge);
    (void)bridge_ensure_manager(global_bridge);
    bridge_sync_all_windows(global_bridge);
}

G_MODULE_EXPORT void gtk_module_exit(void)
{
    bridge_destroy(global_bridge);
    global_bridge = NULL;
}
