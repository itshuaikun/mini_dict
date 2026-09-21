#include "chinese_index.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#define SQLITE_TRANSIENT_VALUE ((sqlite3_destructor_type)-1)

typedef struct {
  const char *key;
  const char *part_of_speech;
  const char *chinese_text;
} TestEntry;

static const TestEntry kTestEntries[] = {
    {"apple", "noun", "苹果"},
    {"pre", "abbr.", "缩写"},
    {"press", "noun", "新闻 新闻界"},
    {"pretty", "adj.", "漂亮的 可爱的"},
    {"President", "noun", "总统 董事长"},
    {"pre-empt", "verb", "先发制人 抢先"},
};

static char *
test_cache_dir(void)
{
  const char *cache_home = g_getenv("XDG_CACHE_HOME");
  g_assert_nonnull(cache_home);
  return g_build_filename(cache_home, "mini-dict", "chinese-index.sqlite3", NULL);
}

static void
seed_entries(ChineseIndex *index)
{
  (void)index;
  g_autofree char *db_path = test_cache_dir();
  sqlite3 *db = NULL;
  g_assert_cmpint(sqlite3_open(db_path, &db), ==, SQLITE_OK);

  sqlite3_stmt *stmt = NULL;
  g_assert_cmpint(sqlite3_prepare_v2(db,
                                     "INSERT OR REPLACE INTO chinese_entries"
                                     "(entry_key, part_of_speech, chinese_text, key_len, key_class) "
                                     "VALUES (?, ?, ?, ?, ?)",
                                     -1,
                                     &stmt,
                                     NULL),
                  ==,
                  SQLITE_OK);

  for (guint i = 0; i < G_N_ELEMENTS(kTestEntries); i++) {
    const TestEntry *entry = &kTestEntries[i];
    gboolean has_separator = strchr(entry->key, ' ') != NULL ||
                             strchr(entry->key, '-') != NULL ||
                             strchr(entry->key, '\'') != NULL;
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_text(stmt, 1, entry->key, -1, SQLITE_TRANSIENT_VALUE);
    sqlite3_bind_text(stmt, 2, entry->part_of_speech, -1, SQLITE_TRANSIENT_VALUE);
    sqlite3_bind_text(stmt, 3, entry->chinese_text, -1, SQLITE_TRANSIENT_VALUE);
    sqlite3_bind_int(stmt, 4, (int)strlen(entry->key));
    sqlite3_bind_int(stmt, 5, has_separator ? 1 : 0);
    g_assert_cmpint(sqlite3_step(stmt), ==, SQLITE_DONE);
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
}

static ChineseIndex *
make_seeded_index(void)
{
  GError *error = NULL;
  ChineseIndex *index = chinese_index_new(&error);
  g_assert_no_error(error);
  g_assert_nonnull(index);
  seed_entries(index);
  return index;
}

static const char *
candidate_key(GPtrArray *candidates, guint position)
{
  ChineseIndexCandidate *candidate = g_ptr_array_index(candidates, position);
  return candidate ? candidate->entry_key : NULL;
}

static void
test_prefix_matches_are_case_insensitive_and_ranked(void)
{
  ChineseIndex *index = make_seeded_index();

  GError *error = NULL;
  GPtrArray *candidates = chinese_index_prefix_query(index, "pre", 15, &error);
  g_assert_no_error(error);
  g_assert_nonnull(candidates);
  g_assert_cmpuint(candidates->len, ==, 5);
  g_assert_cmpstr(candidate_key(candidates, 0), ==, "pre");
  g_assert_cmpstr(candidate_key(candidates, 1), ==, "press");
  g_assert_cmpstr(candidate_key(candidates, 2), ==, "pretty");
  g_assert_cmpstr(candidate_key(candidates, 3), ==, "President");
  g_assert_cmpstr(candidate_key(candidates, 4), ==, "pre-empt");

  ChineseIndexCandidate *first = g_ptr_array_index(candidates, 0);
  g_assert_cmpstr(first->part_of_speech, ==, "abbr.");
  g_assert_cmpstr(first->snippet, ==, "缩写");

  g_ptr_array_unref(candidates);
  chinese_index_free(index);
}

static void
test_prefix_query_normalizes_case_and_limits(void)
{
  ChineseIndex *index = make_seeded_index();

  GError *error = NULL;
  GPtrArray *uppercase = chinese_index_prefix_query(index, "PRE", 15, &error);
  g_assert_no_error(error);
  g_assert_nonnull(uppercase);
  g_assert_cmpuint(uppercase->len, ==, 5);
  g_ptr_array_unref(uppercase);

  GPtrArray *limited = chinese_index_prefix_query(index, "pre", 2, &error);
  g_assert_no_error(error);
  g_assert_nonnull(limited);
  g_assert_cmpuint(limited->len, ==, 2);
  g_assert_cmpstr(candidate_key(limited, 0), ==, "pre");
  g_assert_cmpstr(candidate_key(limited, 1), ==, "press");
  g_ptr_array_unref(limited);

  chinese_index_free(index);
}

static void
test_prefix_query_empty_and_missing_results(void)
{
  ChineseIndex *index = make_seeded_index();

  GError *error = NULL;
  GPtrArray *no_match = chinese_index_prefix_query(index, "zzz", 15, &error);
  g_assert_no_error(error);
  g_assert_nonnull(no_match);
  g_assert_cmpuint(no_match->len, ==, 0);
  g_ptr_array_unref(no_match);

  GPtrArray *empty_prefix = chinese_index_prefix_query(index, "", 15, &error);
  g_assert_no_error(error);
  g_assert_nonnull(empty_prefix);
  g_assert_cmpuint(empty_prefix->len, ==, 0);
  g_ptr_array_unref(empty_prefix);

  GPtrArray *zero_limit = chinese_index_prefix_query(index, "pre", 0, &error);
  g_assert_no_error(error);
  g_assert_nonnull(zero_limit);
  g_assert_cmpuint(zero_limit->len, ==, 0);
  g_ptr_array_unref(zero_limit);

  chinese_index_free(index);
}

static void
test_prefix_query_truncates_long_snippets(void)
{
  ChineseIndex *index = make_seeded_index();

  g_autofree char *long_text =
      g_strdup("很长很长的中文释义很长很长的中文释义很长很长的中文释义很长很长的中文释义");
  g_autofree char *db_path = test_cache_dir();
  sqlite3 *db = NULL;
  g_assert_cmpint(sqlite3_open(db_path, &db), ==, SQLITE_OK);
  sqlite3_stmt *stmt = NULL;
  g_assert_cmpint(sqlite3_prepare_v2(db,
                                     "INSERT OR REPLACE INTO chinese_entries"
                                     "(entry_key, part_of_speech, chinese_text, key_len, key_class) "
                                     "VALUES ('precipitation', NULL, ?, 13, 0)",
                                     -1,
                                     &stmt,
                                     NULL),
                  ==,
                  SQLITE_OK);
  sqlite3_bind_text(stmt, 1, long_text, -1, SQLITE_TRANSIENT_VALUE);
  g_assert_cmpint(sqlite3_step(stmt), ==, SQLITE_DONE);
  sqlite3_finalize(stmt);
  sqlite3_close(db);

  GError *error = NULL;
  GPtrArray *candidates = chinese_index_prefix_query(index, "precip", 5, &error);
  g_assert_no_error(error);
  g_assert_nonnull(candidates);
  g_assert_cmpuint(candidates->len, ==, 1);
  ChineseIndexCandidate *candidate = g_ptr_array_index(candidates, 0);
  g_assert_cmpstr(candidate->entry_key, ==, "precipitation");
  g_assert_nonnull(candidate->snippet);
  g_assert_true(g_str_has_suffix(candidate->snippet, "..."));
  g_assert_cmpuint(g_utf8_strlen(candidate->snippet, -1), <=, 27);
  g_ptr_array_unref(candidates);

  /* Keep this test independent from the others that share the same database. */
  g_assert_cmpint(sqlite3_open(db_path, &db), ==, SQLITE_OK);
  g_assert_cmpint(sqlite3_exec(db, "DELETE FROM chinese_entries WHERE entry_key = 'precipitation'",
                               NULL, NULL, NULL),
                  ==,
                  SQLITE_OK);
  sqlite3_close(db);

  chinese_index_free(index);
}

int
main(int argc, char **argv)
{
  g_autofree char *cache_home = g_dir_make_tmp("mini-dict-chinese-index-XXXXXX", NULL);
  if (!cache_home) {
    g_error("failed to create temporary cache directory");
  }
  g_setenv("XDG_CACHE_HOME", cache_home, TRUE);

  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/chinese-index/prefix-case-insensitive-ranking",
                  test_prefix_matches_are_case_insensitive_and_ranked);
  g_test_add_func("/chinese-index/prefix-normalizes-and-limits",
                  test_prefix_query_normalizes_case_and_limits);
  g_test_add_func("/chinese-index/prefix-empty-and-missing",
                  test_prefix_query_empty_and_missing_results);
  g_test_add_func("/chinese-index/prefix-truncates-snippet",
                  test_prefix_query_truncates_long_snippets);

  int status = g_test_run();

  g_autofree char *db_path = test_cache_dir();
  g_remove(db_path);
  g_autofree char *index_dir = g_path_get_dirname(db_path);
  g_rmdir(index_dir);
  g_rmdir(cache_home);
  return status;
}
