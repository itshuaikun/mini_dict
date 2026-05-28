#include "app.h"

#include "audio.h"
#include "cache.h"
#include "dictionary.h"
#include "local_dictionary.h"

#include <gtk4-layer-shell.h>
#include <glib/gstdio.h>

#include <errno.h>

#ifdef MINI_DICT_HAVE_WEBKITGTK
#include <webkit/webkit.h>
#endif

#define CACHE_MAX_AGE_DAYS 30
#define RESULT_WINDOW_WIDTH 640
#define RESULT_WINDOW_HEIGHT 520
#define INPUT_WINDOW_WIDTH RESULT_WINDOW_WIDTH
#define INPUT_WINDOW_HEIGHT 64
#define SOUND_URI_PREFIX "sound://"

typedef struct {
  GtkApplication *application;
  GtkWidget *window;
  GtkWidget *entry;
  GtkWidget *scrolled_window;
  GtkWidget *result_box;
#ifdef MINI_DICT_HAVE_WEBKITGTK
  GtkWidget *web_view;
#endif
  GtkWidget *status_label;
  LookupCache *cache;
  LocalDictionaryReader *local_reader;
  AudioPlayer *audio_player;
  GCancellable *lookup_cancellable;
  char *active_query_key;
  char *configured_dict_dir;
  char *requested_monitor_name;
  gboolean css_installed;
  gboolean displaying_cached_result;
  gboolean hidden_by_shortcut;
#ifdef MINI_DICT_HAVE_WEBKITGTK
  gboolean pending_web_result;
#endif
} AppState;

static gboolean app_present_input(AppState *state);
static void ensure_css(AppState *state);
static void perform_online_lookup(AppState *state, const char *query);
static gboolean warm_local_reader_idle(gpointer user_data);

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
show_native_result_area(AppState *state)
{
#ifdef MINI_DICT_HAVE_WEBKITGTK
  if (state->web_view) {
    gtk_widget_set_visible(state->web_view, FALSE);
  }
#endif
  gtk_widget_set_visible(state->scrolled_window, TRUE);
  gtk_window_set_default_size(GTK_WINDOW(state->window),
                              RESULT_WINDOW_WIDTH,
                              RESULT_WINDOW_HEIGHT);
}

#ifdef MINI_DICT_HAVE_WEBKITGTK
static void
show_web_result_area(AppState *state)
{
  gtk_widget_set_visible(state->scrolled_window, FALSE);
  gtk_widget_set_visible(state->web_view, TRUE);
  gtk_window_set_default_size(GTK_WINDOW(state->window),
                              RESULT_WINDOW_WIDTH,
                              RESULT_WINDOW_HEIGHT);
}
#endif

static void
show_loading(AppState *state, const char *query)
{
  state->displaying_cached_result = FALSE;
  show_native_result_area(state);
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
  show_native_result_area(state);
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

static const char *
extension_from_key(const char *key)
{
  if (!key) {
    return ".mp3";
  }
  const char *dot = strrchr(key, '.');
  if (!dot || dot[1] == '\0' || strlen(dot) > 8) {
    return ".mp3";
  }
  return dot;
}

static char *
sound_uri_to_asset_key(const char *uri)
{
  const char *key = uri;
  if (g_str_has_prefix(uri, SOUND_URI_PREFIX)) {
    key = uri + strlen(SOUND_URI_PREFIX);
  }

  g_autofree char *unescaped = g_uri_unescape_string(key, NULL);
  return g_strdup(unescaped && unescaped[0] ? unescaped : key);
}

static char *
write_audio_asset_to_cache(const char *asset_key, GBytes *bytes, GError **error)
{
  g_autofree char *dir = g_build_filename(g_get_user_cache_dir(),
                                          "mini-dict",
                                          "ldoce-audio",
                                          NULL);
  if (g_mkdir_with_parents(dir, 0700) != 0) {
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(errno),
                "Failed to create audio cache directory: %s",
                dir);
    return NULL;
  }

  g_autofree char *checksum =
      g_compute_checksum_for_string(G_CHECKSUM_SHA256, asset_key, -1);
  g_autofree char *filename =
      g_strdup_printf("%s%s", checksum, extension_from_key(asset_key));
  g_autofree char *path = g_build_filename(dir, filename, NULL);

  gsize len = 0;
  const char *data = g_bytes_get_data(bytes, &len);
  if (!g_file_set_contents(path, data, len, error)) {
    return NULL;
  }

  return g_steal_pointer(&path);
}

static gboolean
play_local_dictionary_sound(AppState *state, const char *uri)
{
  if (!state->local_reader) {
    show_status(state, "Local dictionary is not ready for pronunciation audio.");
    return TRUE;
  }

  g_autofree char *asset_key = sound_uri_to_asset_key(uri);
  GError *error = NULL;
  g_autofree char *resolved_key = NULL;
  GBytes *bytes =
      local_dictionary_reader_load_asset(state->local_reader,
                                         asset_key,
                                         &resolved_key,
                                         &error);
  if (!bytes) {
    show_status(state, error ? error->message : "Pronunciation audio is unavailable.");
    g_clear_error(&error);
    return TRUE;
  }

  g_autoptr(GBytes) owned_bytes = bytes;
  g_autofree char *audio_path =
      write_audio_asset_to_cache(resolved_key ? resolved_key : asset_key,
                                 owned_bytes,
                                 &error);
  if (!audio_path) {
    show_status(state, error ? error->message : "Pronunciation audio is unavailable.");
    g_clear_error(&error);
    return TRUE;
  }

  g_autofree char *audio_uri = g_filename_to_uri(audio_path, NULL, &error);
  if (!audio_uri) {
    show_status(state, error ? error->message : "Pronunciation audio is unavailable.");
    g_clear_error(&error);
    return TRUE;
  }

  show_status(state, "");
  audio_player_play(state->audio_player, audio_uri, on_audio_error, state);
  return TRUE;
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
  show_native_result_area(state);
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
on_online_fallback_clicked(GtkButton *button, gpointer user_data)
{
  AppState *state = user_data;
  const char *query = g_object_get_data(G_OBJECT(button), "lookup-query");
  if (query && query[0]) {
    perform_online_lookup(state, query);
  }
}

static GtkWidget *
make_online_fallback_button(AppState *state, const char *query)
{
  GtkWidget *button = gtk_button_new_with_label("Try online lookup");
  gtk_widget_set_halign(button, GTK_ALIGN_START);
  g_object_set_data_full(G_OBJECT(button), "lookup-query", g_strdup(query), g_free);
  g_signal_connect(button, "clicked", G_CALLBACK(on_online_fallback_clicked), state);
  return button;
}

static void
show_no_local_entry(AppState *state, const char *query)
{
  state->displaying_cached_result = FALSE;
  show_native_result_area(state);
  clear_box(state->result_box);
  gtk_box_append(GTK_BOX(state->result_box), make_label("No local LDOCE entry found.", "muted"));
  gtk_box_append(GTK_BOX(state->result_box), make_online_fallback_button(state, query));
  show_status(state, "Local dictionary had no matching entry.");
}

static void
show_local_dictionary_issue(AppState *state, const char *message)
{
  state->displaying_cached_result = FALSE;
  show_native_result_area(state);
  clear_box(state->result_box);
  gtk_box_append(GTK_BOX(state->result_box),
                 make_label(message && message[0] ? message : "Local dictionary is unavailable.",
                            "error"));
  show_status(state, "Configure LDOCE with --dict-dir or MINI_DICT_DICT_DIR.");
}

static void
show_local_lookup_error(AppState *state, const char *message)
{
  state->displaying_cached_result = FALSE;
  show_native_result_area(state);
  clear_box(state->result_box);
  gtk_box_append(GTK_BOX(state->result_box),
                 make_label(message && message[0] ? message : "Local dictionary lookup failed.",
                            "error"));
  show_status(state, "Local dictionary lookup failed.");
}

static gboolean
ensure_local_reader(AppState *state)
{
  if (state->local_reader) {
    return TRUE;
  }

  g_autofree char *dict_dir = local_dictionary_resolve_dir(state->configured_dict_dir);
  GError *error = NULL;
  state->local_reader = local_dictionary_reader_new(dict_dir, &error);
  if (!state->local_reader) {
    show_local_dictionary_issue(state, error ? error->message : NULL);
    g_clear_error(&error);
    return FALSE;
  }

  return TRUE;
}

static gboolean
warm_local_reader_idle(gpointer user_data)
{
  AppState *state = user_data;
  if (!state->window || state->local_reader) {
    return G_SOURCE_REMOVE;
  }

  g_autofree char *dict_dir = local_dictionary_resolve_dir(state->configured_dict_dir);
  GError *error = NULL;
  state->local_reader = local_dictionary_reader_new(dict_dir, &error);
  if (!state->local_reader) {
    g_clear_error(&error);
    return G_SOURCE_REMOVE;
  }
  if (!local_dictionary_reader_warm_up(state->local_reader, &error)) {
    g_clear_error(&error);
  }
  return G_SOURCE_REMOVE;
}

#ifdef MINI_DICT_HAVE_WEBKITGTK
static char *
build_local_dictionary_document(LocalDictionaryLookupResult *result)
{
  return g_strdup_printf(
      "<!doctype html><html><head><meta charset=\"utf-8\">"
      "<style>"
      "html,body{margin:0;padding:0;background:#fff;color:#111;}"
      ".pagetitle{border-top-style:double;}"
      "</style>"
      "</head><body>%s</body></html>",
      result->entry_html ? result->entry_html : "");
}

static char *
local_dictionary_base_uri(AppState *state)
{
  const char *dict_dir = local_dictionary_reader_get_dir(state->local_reader);
  if (!dict_dir || dict_dir[0] == '\0') {
    return g_strdup("about:blank");
  }

  g_autofree char *dir_with_separator =
      g_str_has_suffix(dict_dir, G_DIR_SEPARATOR_S)
          ? g_strdup(dict_dir)
          : g_strconcat(dict_dir, G_DIR_SEPARATOR_S, NULL);
  GError *error = NULL;
  char *uri = g_filename_to_uri(dir_with_separator, NULL, &error);
  if (!uri) {
    g_warning("Failed to build local dictionary base URI: %s",
              error ? error->message : "unknown error");
    g_clear_error(&error);
    return g_strdup("about:blank");
  }
  return uri;
}

static void
render_local_dictionary_page(AppState *state, LocalDictionaryLookupResult *result)
{
  state->pending_web_result = TRUE;
  show_status(state, "");
  g_autofree char *document = build_local_dictionary_document(result);
  g_autofree char *base_uri = local_dictionary_base_uri(state);
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(state->web_view), document, base_uri);
}

static void
on_web_view_load_changed(WebKitWebView *web_view,
                         WebKitLoadEvent load_event,
                         gpointer user_data)
{
  (void)web_view;
  AppState *state = user_data;
  if (load_event != WEBKIT_LOAD_FINISHED || !state->pending_web_result) {
    return;
  }

  state->pending_web_result = FALSE;
  show_web_result_area(state);
  show_status(state, "");
  gtk_widget_grab_focus(state->entry);
}

static gboolean
on_web_view_decide_policy(WebKitWebView *web_view,
                          WebKitPolicyDecision *decision,
                          WebKitPolicyDecisionType decision_type,
                          gpointer user_data)
{
  (void)web_view;
  AppState *state = user_data;
  if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION ||
      !WEBKIT_IS_NAVIGATION_POLICY_DECISION(decision)) {
    return FALSE;
  }

  WebKitNavigationAction *action =
      webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
  WebKitURIRequest *request = action ? webkit_navigation_action_get_request(action) : NULL;
  const char *uri = request ? webkit_uri_request_get_uri(request) : NULL;
  if (uri && g_str_has_prefix(uri, SOUND_URI_PREFIX)) {
    webkit_policy_decision_ignore(decision);
    return play_local_dictionary_sound(state, uri);
  }

  return FALSE;
}

static void
on_sound_script_message(WebKitUserContentManager *manager,
                        JSCValue *value,
                        gpointer user_data)
{
  (void)manager;
  AppState *state = user_data;
  if (!value) {
    show_status(state, "Pronunciation audio is unavailable.");
    return;
  }

  g_autofree char *uri = jsc_value_to_string(value);
  if (!uri || uri[0] == '\0') {
    show_status(state, "Pronunciation audio is unavailable.");
    return;
  }

  play_local_dictionary_sound(state, uri);
}

static void
install_ldoce_page_scripts(WebKitWebView *web_view, AppState *state)
{
  WebKitUserContentManager *manager =
      webkit_web_view_get_user_content_manager(web_view);
  g_signal_connect(manager,
                   "script-message-received::miniDictSound",
                   G_CALLBACK(on_sound_script_message),
                   state);
  webkit_user_content_manager_register_script_message_handler(manager,
                                                              "miniDictSound",
                                                              NULL);

  const char *script_source =
      "(function(){"
      "function soundHrefFrom(target){"
      "  var el = target && target.closest ? target.closest('a.speaker, a.PronCodes, .speaker, .PronCodes') : null;"
      "  if (!el) return null;"
      "  var href = el.getAttribute('href') || el.getAttribute('hrefalt');"
      "  if (!href || href === '#') {"
      "    var parent = el.closest ? (el.closest('.Head') || el.parentElement) : el.parentElement;"
      "    var speaker = parent ? parent.querySelector('a.speaker[href]:not([href=\"#\"]), a.PronCodes[href]:not([href=\"#\"])') : null;"
      "    href = speaker ? speaker.getAttribute('href') : href;"
      "  }"
      "  return href;"
      "}"
      "document.addEventListener('click', function(event){"
      "  var href = soundHrefFrom(event.target);"
      "  if (!href) return;"
      "  if (href.indexOf('sound://') === 0 || href.indexOf('media/') >= 0 || href.indexOf('media\\\\') >= 0) {"
      "    event.preventDefault();"
      "    event.stopImmediatePropagation();"
      "    window.webkit.messageHandlers.miniDictSound.postMessage(href);"
      "  }"
      "}, true);"
      "})();";

  WebKitUserScript *script =
      webkit_user_script_new(script_source,
                             WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                             WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
                             NULL,
                             NULL);
  webkit_user_content_manager_add_script(manager, script);
  webkit_user_script_unref(script);
}
#else
static void
render_local_dictionary_page(AppState *state, LocalDictionaryLookupResult *result)
{
  (void)result;
  show_local_lookup_error(state,
                          "Local dictionary HTML rendering requires webkitgtk-6.0, which was not found at build time.");
}
#endif

static void
perform_local_lookup(AppState *state, const char *query)
{
  if (!ensure_local_reader(state)) {
    return;
  }

  LocalDictionaryLookupResult *local_result =
      local_dictionary_reader_lookup(state->local_reader, query);
  if (!local_result) {
    show_local_lookup_error(state, "Local dictionary lookup failed.");
    return;
  }

  switch (local_result->status) {
  case LOCAL_DICTIONARY_LOOKUP_OK:
    render_local_dictionary_page(state, local_result);
    break;
  case LOCAL_DICTIONARY_LOOKUP_NO_ENTRY:
    show_no_local_entry(state, query);
    break;
  case LOCAL_DICTIONARY_LOOKUP_SETUP_ISSUE:
    show_local_dictionary_issue(state, local_result->message);
    break;
  case LOCAL_DICTIONARY_LOOKUP_UNSUPPORTED:
  case LOCAL_DICTIONARY_LOOKUP_ERROR:
  default:
    show_local_lookup_error(state, local_result->message);
    break;
  }

  local_dictionary_lookup_result_free(local_result);
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
  show_status(state, "Showing online fallback result.");
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
perform_online_lookup(AppState *state, const char *query)
{
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
  show_status(state, "");
  perform_local_lookup(state, query);
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

#ifdef MINI_DICT_HAVE_WEBKITGTK
  state->web_view = webkit_web_view_new();
  GdkRGBA web_background = {1.0, 1.0, 1.0, 1.0};
  webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(state->web_view),
                                       &web_background);
  gtk_widget_set_vexpand(state->web_view, TRUE);
  gtk_widget_set_visible(state->web_view, FALSE);
  g_signal_connect(state->web_view,
                   "load-changed",
                   G_CALLBACK(on_web_view_load_changed),
                   state);
  g_signal_connect(state->web_view,
                   "decide-policy",
                   G_CALLBACK(on_web_view_decide_policy),
                   state);
  install_ldoce_page_scripts(WEBKIT_WEB_VIEW(state->web_view), state);
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(state->web_view),
                            "<!doctype html><html><body style=\"margin:0;background:#fff\"></body></html>",
                            "about:blank");
  gtk_box_append(GTK_BOX(root), state->web_view);
#endif

  GtkEventController *key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), state);
  gtk_widget_add_controller(state->window, key_controller);
  g_idle_add(warm_local_reader_idle, state);

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
set_configured_dict_dir(AppState *state, const char *dict_dir)
{
  if (!dict_dir || dict_dir[0] == '\0') {
    return;
  }
  if (g_strcmp0(state->configured_dict_dir, dict_dir) == 0) {
    return;
  }

  g_free(state->configured_dict_dir);
  state->configured_dict_dir = g_strdup(dict_dir);
  local_dictionary_reader_free(state->local_reader);
  state->local_reader = NULL;
}

static int
check_local_dictionary_command(AppState *state,
                               GApplicationCommandLine *command_line,
                               const char *query)
{
  if (!query || query[0] == '\0') {
    g_application_command_line_printerr(command_line,
                                        "Usage: mini-dict --check-dict WORD [--dict-dir DIR]\n");
    return 1;
  }

  g_autofree char *dict_dir = local_dictionary_resolve_dir(state->configured_dict_dir);
  GError *error = NULL;
  LocalDictionaryReader *reader = local_dictionary_reader_new(dict_dir, &error);
  if (!reader) {
    g_application_command_line_printerr(command_line,
                                        "Dictionary setup issue: %s\n",
                                        error ? error->message : "unknown error");
    g_clear_error(&error);
    return 1;
  }

  LocalDictionaryLookupResult *result = local_dictionary_reader_lookup(reader, query);
  int status = 1;
  if (!result) {
    g_application_command_line_printerr(command_line, "Local lookup failed.\n");
  } else if (result->status == LOCAL_DICTIONARY_LOOKUP_OK) {
    g_application_command_line_print(command_line,
                                     "Local lookup OK: %s (%zu HTML bytes)\n",
                                     result->query ? result->query : query,
                                     result->entry_html ? strlen(result->entry_html) : 0);
    status = 0;
  } else if (result->status == LOCAL_DICTIONARY_LOOKUP_NO_ENTRY) {
    g_application_command_line_print(command_line,
                                     "No local entry: %s\n",
                                     result->message ? result->message : query);
    status = 2;
  } else {
    g_application_command_line_printerr(command_line,
                                        "Local lookup failed: %s\n",
                                        result->message ? result->message : "unknown error");
  }

  local_dictionary_lookup_result_free(result);
  local_dictionary_reader_free(reader);
  return status;
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
  set_configured_dict_dir(state, arg_value(args, "--dict-dir"));

  const char *check_query = arg_value(args, "--check-dict");
  if (check_query) {
    int status = check_local_dictionary_command(state, command_line, check_query);
    g_strfreev(args);
    return status;
  }

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
  local_dictionary_reader_free(state->local_reader);
  audio_player_free(state->audio_player);
  g_free(state->active_query_key);
  g_free(state->configured_dict_dir);
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
