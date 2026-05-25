#ifndef MINI_DICT_DICTIONARY_H
#define MINI_DICT_DICTIONARY_H

#include <gio/gio.h>
#include <glib.h>

typedef struct {
  char *definition;
  char *example;
} LookupDefinition;

typedef struct {
  char *part_of_speech;
  GPtrArray *definitions;
} LookupMeaning;

typedef struct {
  char *query;
  char *word;
  char *uk_phonetic;
  char *us_phonetic;
  char *uk_audio_url;
  char *us_audio_url;
  GPtrArray *meanings;
} LookupResult;

typedef struct {
  char *response_json;
  LookupResult *result;
} DictionaryFetchResult;

typedef void (*DictionaryFetchCallback)(DictionaryFetchResult *result,
                                        const GError *error,
                                        gpointer user_data);

LookupResult *lookup_result_new(const char *query);
void lookup_result_free(LookupResult *result);
void dictionary_fetch_result_free(DictionaryFetchResult *result);

LookupResult *dictionary_parse_json(const char *query,
                                    const char *response_json,
                                    GError **error);

void dictionary_fetch_async(const char *query,
                            GCancellable *cancellable,
                            DictionaryFetchCallback callback,
                            gpointer user_data);

#endif
