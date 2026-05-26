#include "app.h"

#include "audio.h"
#include "cache.h"
#include "dictionary.h"

#include <gtk4-layer-shell.h>

#define CACHE_MAX_AGE_DAYS 30
#define INPUT_WINDOW_WIDTH 560
#define INPUT_WINDOW_HEIGHT 64
#define RESULT_WINDOW_WIDTH 640
#define RESULT_WINDOW_HEIGHT 520

typedef struct {
  GtkApplication *application;
  GtkWidget *window;
  GtkWidget *entry;
  GtkWidget *scrolled_window;
  GtkWidget *result_box;
  GtkWidget *status_label;
  LookupCache *cache;
  AudioPlayer *audio_player;
  GCancellable *lookup_cancellable;
  char *active_query_key;
  char *requested_monitor_name;
  gboolean css_installed;
  gboolean displaying_cached_result;
  gboolean hidden_by_shortcut;
} AppState;

static gboolean app_present_input(AppState *state);
static void ensure_css(AppState *state);

static void
clear_box(GtkWidget *box)
{
  GtkWidget *child = gtk_widget_get_first_child(box);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(box), child);
    child = next;
  }
}

static GtkWidget *
make_label(const char *text, const char *css_class)
{
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  if (css_class) {
    gtk_widget_add_css_class(label, css_class);
  }
  return label;
}

static char *
normalize_query(const char *input)
{
  g_autofree char *copy = g_strdup(input ? input : "");
  g_strstrip(copy);
  return g_utf8_strdown(copy, -1);
}

static gboolean
validate_query(const char *input, char **trimmed_query, char **message)
{
  *trimmed_query = NULL;
  *message = NULL;

  g_autofree char *copy = g_strdup(input ? input : "");
  g_strstrip(copy);
  if (copy[0] == '\0') {
    *message = g_strdup("Enter a word or short phrase.");
    return FALSE;
  }

  if (strpbrk(copy, ".?!") || strstr(copy, "。") || strstr(copy, "？") ||
      strstr(copy, "！") || strstr(copy, "，")) {
    *message = g_strdup("Current version only supports word lookup, not sentences.");
    return FALSE;
  }

  guint word_count = 0;
  g_auto(GStrv) parts = g_strsplit_set(copy, " \t\r\n", -1);
  for (guint i = 0; parts[i]; i++) {
    if (parts[i][0] != '\0') {
      word_count++;
    }
  }

  if (word_count > 6) {
    *message = g_strdup("Please enter one English word or a common short phrase.");
    return FALSE;
  }

  *trimmed_query = g_strdup(copy);
  return TRUE;
}

static void
show_status(AppState *state, const char *message)
{
  gtk_label_set_text(GTK_LABEL(state->status_label), message ? message : "");
  gtk_widget_set_visible(state->status_label, message && message[0] != '\0');
}

static void
show_result_area(AppState *state)
{
  gtk_widget_set_visible(state->scrolled_window, TRUE);
  gtk_window_set_default_size(GTK_WINDOW(state->window),
                              RESULT_WINDOW_WIDTH,
                              RESULT_WINDOW_HEIGHT);
}

static void
show_loading(AppState *state, const char *query)
{
  state->displaying_cached_result = FALSE;
  show_result_area(state);
  clear_box(state->result_box);
  gtk_box_append(GTK_BOX(state->result_box), make_label("Looking up...", "muted"));
  if (query) {
    g_autofree char *text = g_strdup_printf("Query: %s", query);
    gtk_box_append(GTK_BOX(state->result_box), make_label(text, "small"));
  }
}

static void
show_error(AppState *state, const char *message)
{
  state->displaying_cached_result = FALSE;
  show_result_area(state);
  clear_box(state->result_box);
  gtk_box_append(GTK_BOX(state->result_box), make_label(message, "error"));
}

static void
on_audio_error(const char *message, gpointer user_data)
{
  AppState *state = user_data;
  show_status(state, message && message[0] ? message : "Pronunciation audio is unavailable.");
}

static void
on_audio_button_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  const char *url = g_object_get_data(G_OBJECT(button), "audio-url");
  show_status(state, "");
  audio_player_play(state->audio_player, url, on_audio_error, state);
}

static GtkWidget *
make_audio_button(AppState *state, const char *url, const char *tooltip)
{
  if (!url || url[0] == '\0') {
    return NULL;
  }

  GtkWidget *button = gtk_button_new_from_icon_name("audio-volume-high-symbolic");
  gtk_widget_add_css_class(button, "flat");
  gtk_widget_set_tooltip_text(button, tooltip);
  g_object_set_data_full(G_OBJECT(button), "audio-url", g_strdup(url), g_free);
  g_signal_connect(button, "clicked", G_CALLBACK(on_audio_button_clicked), state);
  return button;
}

static void
append_phonetic(GtkWidget *row,
                AppState *state,
                const char *label,
                const char *phonetic,
                const char *audio_url)
{
  GtkWidget *item = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(item, "phonetic-item");

  g_autofree char *text = g_strdup_printf("%s %s", label, phonetic && phonetic[0] ? phonetic : "--");
  gtk_box_append(GTK_BOX(item), make_label(text, "phonetic"));
  GtkWidget *button = make_audio_button(state,
                                        audio_url,
                                        g_strcmp0(label, "UK") == 0 ? "Play British pronunciation" : "Play American pronunciation");
  if (button) {
    gtk_box_append(GTK_BOX(item), button);
  }
  gtk_box_append(GTK_BOX(row), item);
}

static void
render_lookup_result(AppState *state, LookupResult *result, gboolean from_cache)
{
  state->displaying_cached_result = from_cache;
  show_result_area(state);
  clear_box(state->result_box);
  show_status(state, from_cache ? "Showing cached result. Refreshing in the background..." : "");

  gtk_box_append(GTK_BOX(state->result_box),
                 make_label(result->word ? result->word : result->query, "title"));

  GtkWidget *phonetic_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  gtk_widget_add_css_class(phonetic_row, "phonetic-row");
  append_phonetic(phonetic_row, state, "UK", result->uk_phonetic, result->uk_audio_url);
  append_phonetic(phonetic_row, state, "US", result->us_phonetic, result->us_audio_url);
  gtk_box_append(GTK_BOX(state->result_box), phonetic_row);

  if (!result->uk_phonetic && !result->us_phonetic) {
    gtk_box_append(GTK_BOX(state->result_box), make_label("No phonetic transcription available.", "muted"));
  }

  for (guint i = 0; i < result->meanings->len; i++) {
    LookupMeaning *meaning = g_ptr_array_index(result->meanings, i);
    gtk_box_append(GTK_BOX(state->result_box),
                   make_label(meaning->part_of_speech ? meaning->part_of_speech : "meaning",
                              "part-of-speech"));

    for (guint j = 0; j < meaning->definitions->len; j++) {
      LookupDefinition *definition = g_ptr_array_index(meaning->definitions, j);
      GtkWidget *definition_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
      gtk_widget_add_css_class(definition_box, "definition-block");

      g_autofree char *definition_text = g_strdup_printf("%u. %s", j + 1, definition->definition);
      gtk_box_append(GTK_BOX(definition_box), make_label(definition_text, "definition"));

      if (definition->example && definition->example[0]) {
        g_autofree char *example_text = g_strdup_printf("Example: %s", definition->example);
        gtk_box_append(GTK_BOX(definition_box), make_label(example_text, "example"));
      }

      gtk_box_append(GTK_BOX(state->result_box), definition_box);
    }
  }
}

static void
handle_network_result(DictionaryFetchResult *fetch_result,
                      const GError *error,
                      gpointer user_data)
{
  AppState *state = user_data;

  if (error) {
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      return;
    }
    if (state->displaying_cached_result) {
      show_status(state, "Network refresh failed; cached result remains visible.");
    } else if (g_strcmp0(error->message, "No dictionary entry found") == 0) {
      show_error(state, "No dictionary entry found.");
    } else {
      show_error(state, "Network unavailable or dictionary service unavailable.");
    }
    return;
  }

  if (!fetch_result || !fetch_result->result) {
    show_error(state, "No dictionary entry found.");
    return;
  }

  g_autofree char *key = normalize_query(fetch_result->result->query);
  GError *cache_error = NULL;
  if (state->cache &&
      !lookup_cache_put(state->cache, key, fetch_result->response_json, &cache_error)) {
    g_warning("Failed to update cache: %s", cache_error->message);
    g_clear_error(&cache_error);
  }

  render_lookup_result(state, fetch_result->result, FALSE);
}

static void
start_network_lookup(AppState *state, const char *query)
{
  if (state->lookup_cancellable) {
    g_cancellable_cancel(state->lookup_cancellable);
    g_clear_object(&state->lookup_cancellable);
  }
  state->lookup_cancellable = g_cancellable_new();
  dictionary_fetch_async(query,
                         state->lookup_cancellable,
                         handle_network_result,
                         state);
}

static void
perform_lookup(AppState *state, const char *input)
{
  g_autofree char *query = NULL;
  g_autofree char *validation_message = NULL;
  if (!validate_query(input, &query, &validation_message)) {
    show_error(state, validation_message);
    return;
  }

  gtk_editable_set_text(GTK_EDITABLE(state->entry), query);
  g_free(state->active_query_key);
  state->active_query_key = normalize_query(query);
  show_loading(state, query);

  gboolean showed_cache = FALSE;
  if (state->cache) {
    GError *cache_error = NULL;
    g_autofree char *cached_json = NULL;
    if (lookup_cache_get(state->cache,
                         state->active_query_key,
                         CACHE_MAX_AGE_DAYS,
                         &cached_json,
                         &cache_error)) {
      GError *parse_error = NULL;
      LookupResult *cached_result = dictionary_parse_json(query, cached_json, &parse_error);
      if (cached_result) {
        render_lookup_result(state, cached_result, TRUE);
        lookup_result_free(cached_result);
        showed_cache = TRUE;
      } else {
        g_warning("Failed to parse cached result: %s", parse_error->message);
        g_clear_error(&parse_error);
      }
    } else if (cache_error) {
      g_warning("Failed to read cache: %s", cache_error->message);
      g_clear_error(&cache_error);
    }
  }

  if (!showed_cache) {
    show_loading(state, query);
  }
  start_network_lookup(state, query);
}

static void
on_entry_activate(GtkEntry *entry, gpointer user_data)
{
  AppState *state = user_data;
  perform_lookup(state, gtk_editable_get_text(GTK_EDITABLE(entry)));
}

static void
hide_window(AppState *state)
{
  state->hidden_by_shortcut = TRUE;
  gtk_widget_set_visible(state->window, FALSE);
}

static GdkMonitor *
get_first_monitor(GdkDisplay *display)
{
  GListModel *monitors = gdk_display_get_monitors(display);
  if (!monitors || g_list_model_get_n_items(monitors) == 0) {
    return NULL;
  }

  g_autoptr(GObject) item = g_list_model_get_item(monitors, 0);
  if (!item || !GDK_IS_MONITOR(item)) {
    return NULL;
  }

  return GDK_MONITOR(item);
}

static gboolean
monitor_name_matches(GdkMonitor *monitor, const char *name)
{
  if (!name || name[0] == '\0') {
    return FALSE;
  }

  const char *connector = gdk_monitor_get_connector(monitor);
  const char *description = gdk_monitor_get_description(monitor);
  const char *model = gdk_monitor_get_model(monitor);
  return g_strcmp0(connector, name) == 0 ||
         g_strcmp0(description, name) == 0 ||
         g_strcmp0(model, name) == 0;
}

static GdkMonitor *
find_monitor_by_name(GdkDisplay *display, const char *name)
{
  GListModel *monitors = gdk_display_get_monitors(display);
  if (!monitors) {
    return NULL;
  }

  guint monitor_count = g_list_model_get_n_items(monitors);
  for (guint i = 0; i < monitor_count; i++) {
    g_autoptr(GObject) item = g_list_model_get_item(monitors, i);
    if (item && GDK_IS_MONITOR(item) &&
        monitor_name_matches(GDK_MONITOR(item), name)) {
      return GDK_MONITOR(item);
    }
  }
  return NULL;
}

static char *
get_kwin_active_output_name(void)
{
  GError *error = NULL;
  g_autoptr(GDBusConnection) connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (!connection) {
    g_clear_error(&error);
    return NULL;
  }

  g_autoptr(GVariant) result =
      g_dbus_connection_call_sync(connection,
                                  "org.kde.KWin",
                                  "/KWin",
                                  "org.kde.KWin",
                                  "activeOutputName",
                                  NULL,
                                  G_VARIANT_TYPE("(s)"),
                                  G_DBUS_CALL_FLAGS_NONE,
                                  250,
                                  NULL,
                                  &error);
  if (!result) {
    g_clear_error(&error);
    return NULL;
  }

  const char *output_name = NULL;
  g_variant_get(result, "(&s)", &output_name);
  return output_name && output_name[0] ? g_strdup(output_name) : NULL;
}

static GdkMonitor *
select_layer_monitor(AppState *state)
{
  GdkDisplay *display = gdk_display_get_default();
  if (!display) {
    return NULL;
  }

  const char *requested_name = state->requested_monitor_name;
  if (!requested_name || requested_name[0] == '\0') {
    requested_name = g_getenv("MINI_DICT_MONITOR");
  }

  GdkMonitor *monitor = find_monitor_by_name(display, requested_name);
  if (!monitor) {
    g_autofree char *kwin_output_name = get_kwin_active_output_name();
    monitor = find_monitor_by_name(display, kwin_output_name);
  }
  if (!monitor) {
    monitor = get_first_monitor(display);
  }

  return monitor;
}

static gboolean
apply_layer_position(AppState *state)
{
  GdkMonitor *monitor = select_layer_monitor(state);
  if (!monitor) {
    g_critical("No Wayland monitor is available for the lookup window");
    return FALSE;
  }

  GdkRectangle geometry;
  gdk_monitor_get_geometry(monitor, &geometry);
  gtk_layer_set_monitor(GTK_WINDOW(state->window), monitor);
  gtk_layer_set_margin(GTK_WINDOW(state->window), GTK_LAYER_SHELL_EDGE_TOP, geometry.height / 4);
  return TRUE;
}

static gboolean
configure_layer_window(AppState *state)
{
  if (!gtk_layer_is_supported()) {
    g_critical("gtk4-layer-shell is not supported by this Wayland compositor");
    return FALSE;
  }

  GtkWindow *window = GTK_WINDOW(state->window);
  gtk_layer_init_for_window(window);
  gtk_layer_set_namespace(window, "mini-dict");
  gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_TOP);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
  gtk_layer_set_exclusive_zone(window, -1);

  GtkLayerShellKeyboardMode keyboard_mode =
      gtk_layer_get_protocol_version() >= 4
          ? GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND
          : GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE;
  gtk_layer_set_keyboard_mode(window, keyboard_mode);

  return apply_layer_position(state);
}

static gboolean
on_key_pressed(GtkEventControllerKey *controller,
               guint keyval,
               guint keycode,
               GdkModifierType state_modifiers,
               gpointer user_data)
{
  (void)controller;
  (void)keycode;
  (void)state_modifiers;
  AppState *state = user_data;

  if (keyval == GDK_KEY_Escape) {
    hide_window(state);
    return TRUE;
  }
  return FALSE;
}

static gboolean
ensure_window(AppState *state)
{
  if (state->window) {
    return TRUE;
  }

  ensure_css(state);
  state->window = gtk_application_window_new(state->application);
  if (!configure_layer_window(state)) {
    gtk_window_destroy(GTK_WINDOW(state->window));
    state->window = NULL;
    g_application_quit(G_APPLICATION(state->application));
    return FALSE;
  }

  gtk_window_set_title(GTK_WINDOW(state->window), "Mini Dict");
  gtk_window_set_default_size(GTK_WINDOW(state->window),
                              INPUT_WINDOW_WIDTH,
                              INPUT_WINDOW_HEIGHT);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(root, "app-root");
  gtk_widget_set_margin_top(root, 10);
  gtk_widget_set_margin_bottom(root, 10);
  gtk_widget_set_margin_start(root, 12);
  gtk_widget_set_margin_end(root, 12);
  gtk_window_set_child(GTK_WINDOW(state->window), root);

  state->entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(state->entry), "");
  gtk_widget_add_css_class(state->entry, "lookup-entry");
  gtk_box_append(GTK_BOX(root), state->entry);
  g_signal_connect(state->entry, "activate", G_CALLBACK(on_entry_activate), state);

  state->status_label = make_label("", "status");
  gtk_widget_set_visible(state->status_label, FALSE);
  gtk_box_append(GTK_BOX(root), state->status_label);

  state->scrolled_window = gtk_scrolled_window_new();
  gtk_widget_add_css_class(state->scrolled_window, "result-scroll");
  gtk_widget_set_vexpand(state->scrolled_window, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->scrolled_window),
                                 GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_visible(state->scrolled_window, FALSE);
  gtk_box_append(GTK_BOX(root), state->scrolled_window);

  state->result_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(state->result_box, "result-box");
  gtk_widget_set_margin_top(state->result_box, 6);
  gtk_widget_set_margin_bottom(state->result_box, 12);
  gtk_widget_set_margin_start(state->result_box, 2);
  gtk_widget_set_margin_end(state->result_box, 2);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(state->scrolled_window),
                                state->result_box);

  GtkEventController *key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), state);
  gtk_widget_add_controller(state->window, key_controller);

  return TRUE;
}

static gboolean
app_present_input(AppState *state)
{
  if (!ensure_window(state)) {
    return FALSE;
  }
  if (!apply_layer_position(state)) {
    return FALSE;
  }
  state->hidden_by_shortcut = FALSE;
  gtk_window_present(GTK_WINDOW(state->window));
  gtk_widget_grab_focus(state->entry);
  gtk_editable_select_region(GTK_EDITABLE(state->entry), 0, -1);
  return TRUE;
}

static gboolean
app_toggle(AppState *state)
{
  if (!ensure_window(state)) {
    return FALSE;
  }
  if (gtk_widget_get_visible(state->window) &&
      !state->hidden_by_shortcut) {
    hide_window(state);
    return TRUE;
  }
  return app_present_input(state);
}

static void
install_css(void)
{
  GdkDisplay *display = gdk_display_get_default();
  if (!display) {
    return;
  }

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider,
      "window, window.background { background-color: transparent; color: #202124; border-radius: 0; box-shadow: none; outline: none; }"
      ".app-root { background-color: rgba(244, 244, 244, 0.84); color: #202124; border-radius: 0; }"
      ".result-scroll, .result-scroll viewport, .result-box { background-color: transparent; color: #202124; border-radius: 0; }"
      ".lookup-entry { font-size: 18px; padding: 8px 10px; background-color: rgba(255, 255, 255, 0.88); color: #202124; border-radius: 0; }"
      ".title { font-size: 26px; font-weight: 700; }"
      ".phonetic-row { margin-bottom: 8px; }"
      ".phonetic { font-size: 14px; color: #202124; }"
      ".part-of-speech { margin-top: 12px; font-weight: 700; color: #1a5fb4; }"
      ".definition { font-size: 15px; }"
      ".example { font-size: 14px; color: #3a3a3a; margin-left: 14px; }"
      ".definition-block { padding: 2px 0 4px 0; }"
      ".muted, .small, .status { color: #3a3a3a; }"
      ".small { font-size: 13px; }"
      ".error { color: #b3261e; font-weight: 600; }");

  gtk_style_context_add_provider_for_display(display,
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static void
ensure_css(AppState *state)
{
  if (state->css_installed) {
    return;
  }
  install_css();
  state->css_installed = TRUE;
}

static gboolean
has_arg(char **args, const char *needle)
{
  for (guint i = 1; args[i]; i++) {
    if (g_strcmp0(args[i], needle) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

static const char *
arg_value(char **args, const char *name)
{
  g_autofree char *prefix = g_strdup_printf("%s=", name);
  for (guint i = 1; args[i]; i++) {
    if (g_strcmp0(args[i], name) == 0) {
      return args[i + 1];
    }
    if (g_str_has_prefix(args[i], prefix)) {
      return args[i] + strlen(prefix);
    }
  }
  return NULL;
}

static void
set_requested_monitor(AppState *state, const char *monitor_name)
{
  g_free(state->requested_monitor_name);
  state->requested_monitor_name = monitor_name && monitor_name[0] ? g_strdup(monitor_name) : NULL;
}

static void
on_activate(GtkApplication *application, gpointer user_data)
{
  (void)application;
  app_present_input(user_data);
}

static int
on_command_line(GApplication *application,
                GApplicationCommandLine *command_line,
                gpointer user_data)
{
  (void)application;
  AppState *state = user_data;
  int argc = 0;
  char **args = g_application_command_line_get_arguments(command_line, &argc);
  set_requested_monitor(state, arg_value(args, "--monitor"));

  if (has_arg(args, "--clear-cache")) {
    GError *error = NULL;
    if (!lookup_cache_clear(state->cache, &error)) {
      g_application_command_line_printerr(command_line,
                                          "Failed to clear cache: %s\n",
                                          error ? error->message : "unknown error");
      g_clear_error(&error);
      g_strfreev(args);
      return 1;
    }
    g_application_command_line_print(command_line, "Cache cleared.\n");
    g_strfreev(args);
    return 0;
  }

  if (has_arg(args, "--toggle")) {
    if (!app_toggle(state)) {
      g_strfreev(args);
      return 1;
    }
    g_strfreev(args);
    return 0;
  }

  if (!app_present_input(state)) {
    g_strfreev(args);
    return 1;
  }
  g_strfreev(args);
  return 0;
}

static void
app_state_free(AppState *state)
{
  if (!state) {
    return;
  }
  if (state->lookup_cancellable) {
    g_cancellable_cancel(state->lookup_cancellable);
    g_clear_object(&state->lookup_cancellable);
  }
  lookup_cache_free(state->cache);
  audio_player_free(state->audio_player);
  g_free(state->active_query_key);
  g_free(state->requested_monitor_name);
  g_free(state);
}

GtkApplication *
mini_dict_app_new(void)
{
  GtkApplication *application = gtk_application_new(MINI_DICT_APP_ID,
                                                    G_APPLICATION_HANDLES_COMMAND_LINE);
  AppState *state = g_new0(AppState, 1);
  state->application = application;
  state->audio_player = audio_player_new();

  GError *error = NULL;
  state->cache = lookup_cache_new(&error);
  if (!state->cache) {
    g_warning("Cache disabled: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
  }

  g_object_set_data_full(G_OBJECT(application), "app-state", state, (GDestroyNotify)app_state_free);
  g_signal_connect(application, "activate", G_CALLBACK(on_activate), state);
  g_signal_connect(application, "command-line", G_CALLBACK(on_command_line), state);

  return application;
}
