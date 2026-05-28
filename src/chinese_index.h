#ifndef MINI_DICT_CHINESE_INDEX_H
#define MINI_DICT_CHINESE_INDEX_H

#include "local_dictionary.h"

#include <glib.h>

typedef struct _ChineseIndex ChineseIndex;

typedef struct {
  char *entry_key;
  char *part_of_speech;
  char *snippet;
} ChineseIndexCandidate;

ChineseIndex *chinese_index_new(GError **error);
void chinese_index_free(ChineseIndex *index);

gboolean chinese_index_is_ready(ChineseIndex *index,
                                LocalDictionaryReader *reader,
                                gboolean *ready,
                                GError **error);

gboolean chinese_index_rebuild(ChineseIndex *index,
                               const char *dict_dir,
                               GError **error);

GPtrArray *chinese_index_query(ChineseIndex *index,
                               const char *query,
                               guint limit,
                               GError **error);

void chinese_index_candidate_free(gpointer data);

#endif
