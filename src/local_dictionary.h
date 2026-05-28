#ifndef MINI_DICT_LOCAL_DICTIONARY_H
#define MINI_DICT_LOCAL_DICTIONARY_H

#include <glib.h>

typedef struct _LocalDictionaryReader LocalDictionaryReader;

typedef enum {
  LOCAL_DICTIONARY_LOOKUP_OK,
  LOCAL_DICTIONARY_LOOKUP_NO_ENTRY,
  LOCAL_DICTIONARY_LOOKUP_SETUP_ISSUE,
  LOCAL_DICTIONARY_LOOKUP_UNSUPPORTED,
  LOCAL_DICTIONARY_LOOKUP_ERROR
} LocalDictionaryLookupStatus;

typedef struct {
  LocalDictionaryLookupStatus status;
  char *query;
  char *entry_html;
  char *message;
} LocalDictionaryLookupResult;

char *local_dictionary_resolve_dir(const char *explicit_dir);

LocalDictionaryReader *local_dictionary_reader_new(const char *dict_dir,
                                                   GError **error);
void local_dictionary_reader_free(LocalDictionaryReader *reader);

const char *local_dictionary_reader_get_dir(LocalDictionaryReader *reader);
gboolean local_dictionary_reader_warm_up(LocalDictionaryReader *reader,
                                         GError **error);

LocalDictionaryLookupResult *
local_dictionary_reader_lookup(LocalDictionaryReader *reader,
                               const char *query);
void local_dictionary_lookup_result_free(LocalDictionaryLookupResult *result);

GBytes *local_dictionary_reader_load_asset(LocalDictionaryReader *reader,
                                           const char *asset_key,
                                           char **resolved_key,
                                           GError **error);

#endif
