#include "dictionary.h"

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

typedef struct {
  char *query;
  SoupSession *session;
  SoupMessage *message;
  GCancellable *cancellable;
  DictionaryFetchCallback callback;
  gpointer user_data;
} FetchContext;

static GQuark
dictionary_error_quark(void)
{
  return g_quark_from_static_string("mini-dict-dictionary-error");
}

static void
lookup_definition_free(gpointer data)
{
  LookupDefinition *definition = data;
  if (!definition) {
    return;
  }
  g_free(definition->definition);
  g_free(definition->example);
  g_free(definition);
}

static void
lookup_meaning_free(gpointer data)
{
  LookupMeaning *meaning = data;
  if (!meaning) {
    return;
  }
  g_free(meaning->part_of_speech);
  if (meaning->definitions) {
    g_ptr_array_unref(meaning->definitions);
  }
  g_free(meaning);
}

LookupResult *
lookup_result_new(const char *query)
{
  LookupResult *result = g_new0(LookupResult, 1);
  result->query = g_strdup(query);
  result->meanings = g_ptr_array_new_with_free_func(lookup_meaning_free);
  return result;
}

void
lookup_result_free(LookupResult *result)
{
  if (!result) {
    return;
  }
  g_free(result->query);
  g_free(result->word);
  g_free(result->uk_phonetic);
  g_free(result->us_phonetic);
  g_free(result->uk_audio_url);
  g_free(result->us_audio_url);
  if (result->meanings) {
    g_ptr_array_unref(result->meanings);
  }
  g_free(result);
}

void
dictionary_fetch_result_free(DictionaryFetchResult *result)
{
  if (!result) {
    return;
  }
  g_free(result->response_json);
  lookup_result_free(result->result);
  g_free(result);
}

static const char *
object_string_member(JsonObject *object, const char *member)
{
  if (!json_object_has_member(object, member)) {
    return NULL;
  }
  JsonNode *node = json_object_get_member(object, member);
  if (!JSON_NODE_HOLDS_VALUE(node)) {
    return NULL;
  }
  const char *value = json_object_get_string_member(object, member);
  return value && value[0] != '\0' ? value : NULL;
}

static gboolean
audio_looks_british(const char *audio)
{
  return audio && (g_strrstr(audio, "-uk.") || g_strrstr(audio, "/uk.") ||
                   g_strrstr(audio, "_uk.") || g_strrstr(audio, "uk.mp3"));
}

static gboolean
audio_looks_american(const char *audio)
{
  return audio && (g_strrstr(audio, "-us.") || g_strrstr(audio, "/us.") ||
                   g_strrstr(audio, "_us.") || g_strrstr(audio, "us.mp3"));
}

static void
capture_phonetics(LookupResult *result, JsonObject *entry)
{
  if (!json_object_has_member(entry, "phonetic") && !json_object_has_member(entry, "phonetics")) {
    return;
  }

  const char *entry_phonetic = object_string_member(entry, "phonetic");
  if (entry_phonetic && !result->uk_phonetic) {
    result->uk_phonetic = g_strdup(entry_phonetic);
  }

  if (!json_object_has_member(entry, "phonetics")) {
    return;
  }

  JsonArray *phonetics = json_object_get_array_member(entry, "phonetics");
  guint length = json_array_get_length(phonetics);
  const char *fallback_text = NULL;
  const char *fallback_audio = NULL;

  for (guint i = 0; i < length; i++) {
    JsonObject *phonetic = json_array_get_object_element(phonetics, i);
    if (!phonetic) {
      continue;
    }

    const char *text = object_string_member(phonetic, "text");
    const char *audio = object_string_member(phonetic, "audio");

    if (audio_looks_british(audio)) {
      if (text && !result->uk_phonetic) {
        result->uk_phonetic = g_strdup(text);
      }
      if (audio && !result->uk_audio_url) {
        result->uk_audio_url = g_strdup(audio);
      }
      continue;
    }

    if (audio_looks_american(audio)) {
      if (text && !result->us_phonetic) {
        result->us_phonetic = g_strdup(text);
      }
      if (audio && !result->us_audio_url) {
        result->us_audio_url = g_strdup(audio);
      }
      continue;
    }

    if (text && !fallback_text) {
      fallback_text = text;
    }
    if (audio && !fallback_audio) {
      fallback_audio = audio;
    }
  }

  if (!result->uk_phonetic && fallback_text) {
    result->uk_phonetic = g_strdup(fallback_text);
  } else if (!result->us_phonetic && fallback_text &&
             g_strcmp0(result->uk_phonetic, fallback_text) != 0) {
    result->us_phonetic = g_strdup(fallback_text);
  }

  if (!result->uk_audio_url && !result->us_audio_url && fallback_audio) {
    result->uk_audio_url = g_strdup(fallback_audio);
  }
}

static void
capture_meanings(LookupResult *result, JsonObject *entry)
{
  if (!json_object_has_member(entry, "meanings")) {
    return;
  }

  JsonArray *meanings = json_object_get_array_member(entry, "meanings");
  guint meaning_count = json_array_get_length(meanings);

  for (guint i = 0; i < meaning_count; i++) {
    JsonObject *meaning_object = json_array_get_object_element(meanings, i);
    if (!meaning_object) {
      continue;
    }

    LookupMeaning *meaning = g_new0(LookupMeaning, 1);
    meaning->part_of_speech = g_strdup(object_string_member(meaning_object, "partOfSpeech"));
    meaning->definitions = g_ptr_array_new_with_free_func(lookup_definition_free);

    if (json_object_has_member(meaning_object, "definitions")) {
      JsonArray *definitions = json_object_get_array_member(meaning_object, "definitions");
      guint definition_count = json_array_get_length(definitions);

      for (guint j = 0; j < definition_count; j++) {
        JsonObject *definition_object = json_array_get_object_element(definitions, j);
        if (!definition_object) {
          continue;
        }

        const char *definition_text = object_string_member(definition_object, "definition");
        if (!definition_text) {
          continue;
        }

        LookupDefinition *definition = g_new0(LookupDefinition, 1);
        definition->definition = g_strdup(definition_text);
        definition->example = g_strdup(object_string_member(definition_object, "example"));
        g_ptr_array_add(meaning->definitions, definition);
      }
    }

    if (meaning->definitions->len > 0) {
      g_ptr_array_add(result->meanings, meaning);
    } else {
      lookup_meaning_free(meaning);
    }
  }
}

LookupResult *
dictionary_parse_json(const char *query, const char *response_json, GError **error)
{
  JsonParser *parser = json_parser_new();
  if (!json_parser_load_from_data(parser, response_json, -1, error)) {
    g_object_unref(parser);
    return NULL;
  }

  JsonNode *root = json_parser_get_root(parser);
  if (!JSON_NODE_HOLDS_ARRAY(root)) {
    g_set_error(error,
                dictionary_error_quark(),
                1,
                "No dictionary entry found");
    g_object_unref(parser);
    return NULL;
  }

  LookupResult *result = lookup_result_new(query);
  JsonArray *entries = json_node_get_array(root);
  guint entry_count = json_array_get_length(entries);

  for (guint i = 0; i < entry_count; i++) {
    JsonObject *entry = json_array_get_object_element(entries, i);
    if (!entry) {
      continue;
    }

    if (!result->word) {
      result->word = g_strdup(object_string_member(entry, "word"));
    }
    capture_phonetics(result, entry);
    capture_meanings(result, entry);
  }

  if (!result->word) {
    result->word = g_strdup(query);
  }

  if (result->meanings->len == 0) {
    lookup_result_free(result);
    g_set_error(error,
                dictionary_error_quark(),
                2,
                "No definitions were found");
    g_object_unref(parser);
    return NULL;
  }

  g_object_unref(parser);
  return result;
}

static void
fetch_context_free(FetchContext *context)
{
  if (!context) {
    return;
  }
  g_free(context->query);
  g_clear_object(&context->session);
  g_clear_object(&context->message);
  g_clear_object(&context->cancellable);
  g_free(context);
}

static void
on_send_and_read_ready(GObject *source, GAsyncResult *async_result, gpointer user_data)
{
  SoupSession *session = SOUP_SESSION(source);
  FetchContext *context = user_data;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish(session, async_result, &error);

  if (error) {
    context->callback(NULL, error, context->user_data);
    g_error_free(error);
    fetch_context_free(context);
    return;
  }

  guint status = soup_message_get_status(context->message);
  if (status != SOUP_STATUS_OK) {
    g_set_error(&error,
                dictionary_error_quark(),
                (int)status,
                status == SOUP_STATUS_NOT_FOUND ? "No dictionary entry found" : "Dictionary service returned HTTP %u",
                status);
    context->callback(NULL, error, context->user_data);
    g_error_free(error);
    g_bytes_unref(bytes);
    fetch_context_free(context);
    return;
  }

  gsize size = 0;
  const char *data = g_bytes_get_data(bytes, &size);
  char *response_json = g_strndup(data, size);
  LookupResult *lookup = dictionary_parse_json(context->query, response_json, &error);
  if (!lookup) {
    context->callback(NULL, error, context->user_data);
    g_error_free(error);
    g_free(response_json);
    g_bytes_unref(bytes);
    fetch_context_free(context);
    return;
  }

  DictionaryFetchResult *result = g_new0(DictionaryFetchResult, 1);
  result->response_json = response_json;
  result->result = lookup;
  context->callback(result, NULL, context->user_data);
  dictionary_fetch_result_free(result);

  g_bytes_unref(bytes);
  fetch_context_free(context);
}

void
dictionary_fetch_async(const char *query,
                       GCancellable *cancellable,
                       DictionaryFetchCallback callback,
                       gpointer user_data)
{
  g_autofree char *escaped = g_uri_escape_string(query, NULL, TRUE);
  g_autofree char *url = g_strdup_printf("https://api.dictionaryapi.dev/api/v2/entries/en/%s",
                                         escaped);

  FetchContext *context = g_new0(FetchContext, 1);
  context->query = g_strdup(query);
  context->session = soup_session_new();
  context->message = soup_message_new("GET", url);
  context->cancellable = cancellable ? g_object_ref(cancellable) : NULL;
  context->callback = callback;
  context->user_data = user_data;

  soup_session_send_and_read_async(context->session,
                                   context->message,
                                   G_PRIORITY_DEFAULT,
                                   context->cancellable,
                                   on_send_and_read_ready,
                                   context);
}
