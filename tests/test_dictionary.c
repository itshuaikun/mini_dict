#include "dictionary.h"

#include <glib.h>

static void
test_hello_parse(void)
{
  const char *json =
      "[{"
      "\"word\":\"hello\","
      "\"phonetics\":["
      "{\"audio\":\"https://api.dictionaryapi.dev/media/pronunciations/en/hello-au.mp3\"},"
      "{\"text\":\"/həˈləʊ/\",\"audio\":\"https://api.dictionaryapi.dev/media/pronunciations/en/hello-uk.mp3\"},"
      "{\"text\":\"/həˈloʊ/\",\"audio\":\"\"}"
      "],"
      "\"meanings\":[{\"partOfSpeech\":\"interjection\",\"definitions\":["
      "{\"definition\":\"A greeting.\",\"example\":\"Hello, everyone.\"},"
      "{\"definition\":\"A call for response.\"}"
      "]}]"
      "}]";

  GError *error = NULL;
  LookupResult *result = dictionary_parse_json("hello", json, &error);
  g_assert_no_error(error);
  g_assert_nonnull(result);
  g_assert_cmpstr(result->word, ==, "hello");
  g_assert_cmpstr(result->uk_phonetic, ==, "/həˈləʊ/");
  g_assert_cmpstr(result->us_phonetic, ==, "/həˈloʊ/");
  g_assert_cmpstr(result->uk_audio_url, ==, "https://api.dictionaryapi.dev/media/pronunciations/en/hello-uk.mp3");
  g_assert_null(result->us_audio_url);
  g_assert_cmpuint(result->meanings->len, ==, 1);

  LookupMeaning *meaning = g_ptr_array_index(result->meanings, 0);
  g_assert_cmpstr(meaning->part_of_speech, ==, "interjection");
  g_assert_cmpuint(meaning->definitions->len, ==, 2);

  LookupDefinition *definition = g_ptr_array_index(meaning->definitions, 0);
  g_assert_cmpstr(definition->definition, ==, "A greeting.");
  g_assert_cmpstr(definition->example, ==, "Hello, everyone.");

  lookup_result_free(result);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/dictionary/parse-hello", test_hello_parse);
  return g_test_run();
}
