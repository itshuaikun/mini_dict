#include "chinese_index.h"

#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#define SQLITE_TRANSIENT_VALUE ((sqlite3_destructor_type)-1)

struct _ChineseIndex {
  char *db_path;
};

typedef struct {
  sqlite3 *db;
  sqlite3_stmt *entry_stmt;
  sqlite3_stmt *fts_stmt;
  GError *error;
  guint inserted;
} BuildContext;

static GQuark
chinese_index_error_quark(void)
{
  return g_quark_from_static_string("mini-dict-chinese-index-error");
}

static gboolean
exec_sql(sqlite3 *db, const char *sql, GError **error)
{
  char *message = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &message);
  if (rc != SQLITE_OK) {
    g_set_error(error,
                chinese_index_error_quark(),
                rc,
                "%s",
                message ? message : sqlite3_errmsg(db));
    sqlite3_free(message);
    return FALSE;
  }
  return TRUE;
}

static gboolean
open_db(const char *path, sqlite3 **db, GError **error)
{
  *db = NULL;
  int rc = sqlite3_open(path, db);
  if (rc != SQLITE_OK) {
    g_set_error(error,
                chinese_index_error_quark(),
                rc,
                "%s",
                *db ? sqlite3_errmsg(*db) : "Failed to open Chinese index");
    if (*db) {
      sqlite3_close(*db);
      *db = NULL;
    }
    return FALSE;
  }
  return TRUE;
}

static gboolean
ensure_schema(sqlite3 *db, GError **error)
{
  return exec_sql(db,
                  "CREATE TABLE IF NOT EXISTS chinese_index_metadata ("
                  "key TEXT PRIMARY KEY,"
                  "value TEXT NOT NULL"
                  ");"
                  "CREATE TABLE IF NOT EXISTS chinese_entries ("
                  "entry_key TEXT PRIMARY KEY,"
                  "part_of_speech TEXT,"
                  "chinese_text TEXT NOT NULL,"
                  "key_len INTEGER NOT NULL,"
                  "key_class INTEGER NOT NULL"
                  ");"
                  "CREATE VIRTUAL TABLE IF NOT EXISTS chinese_entries_fts "
                  "USING fts5(entry_key UNINDEXED, chinese_text, tokenize='trigram');",
                  error);
}

ChineseIndex *
chinese_index_new(GError **error)
{
  g_autofree char *dir = g_build_filename(g_get_user_cache_dir(),
                                          "mini-dict",
                                          NULL);
  if (g_mkdir_with_parents(dir, 0700) != 0) {
    g_set_error(error,
                chinese_index_error_quark(),
                1,
                "Failed to create cache directory: %s",
                dir);
    return NULL;
  }

  ChineseIndex *index = g_new0(ChineseIndex, 1);
  index->db_path = g_build_filename(dir, "chinese-index.sqlite3", NULL);

  sqlite3 *db = NULL;
  if (!open_db(index->db_path, &db, error)) {
    chinese_index_free(index);
    return NULL;
  }
  if (!ensure_schema(db, error)) {
    sqlite3_close(db);
    chinese_index_free(index);
    return NULL;
  }
  sqlite3_close(db);
  return index;
}

void
chinese_index_free(ChineseIndex *index)
{
  if (!index) {
    return;
  }
  g_free(index->db_path);
  g_free(index);
}

void
chinese_index_candidate_free(gpointer data)
{
  ChineseIndexCandidate *candidate = data;
  if (!candidate) {
    return;
  }
  g_free(candidate->entry_key);
  g_free(candidate->part_of_speech);
  g_free(candidate->snippet);
  g_free(candidate);
}

static char *
metadata_get(sqlite3 *db, const char *key, GError **error)
{
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db,
                              "SELECT value FROM chinese_index_metadata WHERE key = ?",
                              -1,
                              &stmt,
                              NULL);
  if (rc != SQLITE_OK) {
    g_set_error(error, chinese_index_error_quark(), rc, "%s", sqlite3_errmsg(db));
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT_VALUE);
  char *value = NULL;
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    const unsigned char *text = sqlite3_column_text(stmt, 0);
    value = text ? g_strdup((const char *)text) : NULL;
  } else if (rc != SQLITE_DONE) {
    g_set_error(error, chinese_index_error_quark(), rc, "%s", sqlite3_errmsg(db));
  }
  sqlite3_finalize(stmt);
  return value;
}

static gboolean
metadata_put(sqlite3 *db, const char *key, const char *value, GError **error)
{
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db,
                              "INSERT INTO chinese_index_metadata(key, value) VALUES (?, ?) "
                              "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
                              -1,
                              &stmt,
                              NULL);
  if (rc != SQLITE_OK) {
    g_set_error(error, chinese_index_error_quark(), rc, "%s", sqlite3_errmsg(db));
    return FALSE;
  }

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT_VALUE);
  sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT_VALUE);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    g_set_error(error, chinese_index_error_quark(), rc, "%s", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return FALSE;
  }

  sqlite3_finalize(stmt);
  return TRUE;
}

gboolean
chinese_index_is_ready(ChineseIndex *index,
                       LocalDictionaryReader *reader,
                       gboolean *ready,
                       GError **error)
{
  *ready = FALSE;
  if (!index || !reader) {
    return TRUE;
  }

  g_autofree char *source_path = NULL;
  gint64 source_size = 0;
  gint64 source_mtime = 0;
  if (!local_dictionary_reader_get_source_identity(reader,
                                                   &source_path,
                                                   &source_size,
                                                   &source_mtime,
                                                   error)) {
    return FALSE;
  }

  sqlite3 *db = NULL;
  if (!open_db(index->db_path, &db, error)) {
    return FALSE;
  }
  gboolean ok = ensure_schema(db, error);
  if (!ok) {
    sqlite3_close(db);
    return FALSE;
  }

  g_autofree char *stored_status = metadata_get(db, "status", error);
  if (error && *error) {
    sqlite3_close(db);
    return FALSE;
  }
  g_autofree char *stored_path = metadata_get(db, "source_path", error);
  if (error && *error) {
    sqlite3_close(db);
    return FALSE;
  }
  g_autofree char *stored_size = metadata_get(db, "source_size", error);
  if (error && *error) {
    sqlite3_close(db);
    return FALSE;
  }
  g_autofree char *stored_mtime = metadata_get(db, "source_mtime", error);
  if (error && *error) {
    sqlite3_close(db);
    return FALSE;
  }
  sqlite3_close(db);

  *ready = g_strcmp0(stored_status, "complete") == 0 &&
           g_strcmp0(stored_path, source_path) == 0 &&
           stored_size && g_ascii_strtoll(stored_size, NULL, 10) == source_size &&
           stored_mtime && g_ascii_strtoll(stored_mtime, NULL, 10) == source_mtime;
  return TRUE;
}

static gboolean
is_han(gunichar c)
{
  return (c >= 0x3400 && c <= 0x4DBF) ||
         (c >= 0x4E00 && c <= 0x9FFF) ||
         (c >= 0xF900 && c <= 0xFAFF) ||
         (c >= 0x20000 && c <= 0x2A6DF) ||
         (c >= 0x2A700 && c <= 0x2B73F) ||
         (c >= 0x2B740 && c <= 0x2B81F) ||
         (c >= 0x2B820 && c <= 0x2CEAF);
}

static gboolean
ascii_starts_with_ci(const char *value, const char *prefix)
{
  return g_ascii_strncasecmp(value, prefix, strlen(prefix)) == 0;
}

static char *
find_ascii_ci(const char *haystack, const char *needle)
{
  gsize needle_len = strlen(needle);
  if (needle_len == 0) {
    return (char *)haystack;
  }
  for (const char *p = haystack; *p; p++) {
    if (g_ascii_strncasecmp(p, needle, needle_len) == 0) {
      return (char *)p;
    }
  }
  return NULL;
}

static void
append_separator(GString *out)
{
  if (out->len > 0 && out->str[out->len - 1] != ' ') {
    g_string_append_c(out, ' ');
  }
}

static char *
extract_chinese_text(const char *html)
{
  GString *out = g_string_new(NULL);
  const char *p = html ? html : "";

  while (*p) {
    if (*p == '<') {
      const char *tag = p + 1;
      while (*tag == '/' || g_ascii_isspace(*tag)) {
        tag++;
      }
      if (ascii_starts_with_ci(tag, "script")) {
        char *end = find_ascii_ci(tag, "</script");
        p = end ? strchr(end, '>') : strchr(tag, '>');
        p = p ? p + 1 : tag + strlen(tag);
        append_separator(out);
        continue;
      }
      if (ascii_starts_with_ci(tag, "style")) {
        char *end = find_ascii_ci(tag, "</style");
        p = end ? strchr(end, '>') : strchr(tag, '>');
        p = p ? p + 1 : tag + strlen(tag);
        append_separator(out);
        continue;
      }

      const char *end = strchr(p, '>');
      p = end ? end + 1 : p + 1;
      append_separator(out);
      continue;
    }

    gunichar c = g_utf8_get_char_validated(p, -1);
    if (c == (gunichar)-1 || c == (gunichar)-2) {
      p++;
      continue;
    }
    if (is_han(c)) {
      char encoded[6] = {0};
      gint len = g_unichar_to_utf8(c, encoded);
      g_string_append_len(out, encoded, len);
    } else {
      append_separator(out);
    }
    p = g_utf8_next_char(p);
  }

  g_strstrip(out->str);
  return g_string_free(out, FALSE);
}

static char *
extract_part_of_speech(const char *html)
{
  const char *marker = html ? strstr(html, "lm5pp_POS") : NULL;
  if (!marker) {
    marker = html ? strstr(html, "class=\"POS\"") : NULL;
  }
  if (!marker) {
    return NULL;
  }

  const char *start = strchr(marker, '>');
  if (!start) {
    return NULL;
  }
  start++;
  const char *end = strchr(start, '<');
  if (!end || end <= start || end - start > 48) {
    return NULL;
  }

  char *part_of_speech = g_strndup(start, end - start);
  g_strstrip(part_of_speech);
  if (part_of_speech[0] == '\0') {
    g_free(part_of_speech);
    return NULL;
  }
  return part_of_speech;
}

static int
entry_key_class(const char *key)
{
  return key && (strchr(key, ' ') || strchr(key, '-') || strchr(key, '\'')) ? 1 : 0;
}

static int
on_index_entry(const char *key, const char *html, gpointer user_data)
{
  BuildContext *context = user_data;
  g_autofree char *chinese_text = extract_chinese_text(html);
  if (!chinese_text || chinese_text[0] == '\0') {
    return 0;
  }

  g_autofree char *part_of_speech = extract_part_of_speech(html);
  int rc = SQLITE_OK;

  sqlite3_reset(context->entry_stmt);
  sqlite3_clear_bindings(context->entry_stmt);
  sqlite3_bind_text(context->entry_stmt, 1, key, -1, SQLITE_TRANSIENT_VALUE);
  if (part_of_speech) {
    sqlite3_bind_text(context->entry_stmt, 2, part_of_speech, -1, SQLITE_TRANSIENT_VALUE);
  } else {
    sqlite3_bind_null(context->entry_stmt, 2);
  }
  sqlite3_bind_text(context->entry_stmt, 3, chinese_text, -1, SQLITE_TRANSIENT_VALUE);
  sqlite3_bind_int(context->entry_stmt, 4, (int)strlen(key ? key : ""));
  sqlite3_bind_int(context->entry_stmt, 5, entry_key_class(key));
  rc = sqlite3_step(context->entry_stmt);
  if (rc != SQLITE_DONE) {
    g_set_error(&context->error,
                chinese_index_error_quark(),
                rc,
                "%s",
                sqlite3_errmsg(context->db));
    return 1;
  }

  sqlite3_reset(context->fts_stmt);
  sqlite3_clear_bindings(context->fts_stmt);
  sqlite3_bind_text(context->fts_stmt, 1, key, -1, SQLITE_TRANSIENT_VALUE);
  sqlite3_bind_text(context->fts_stmt, 2, chinese_text, -1, SQLITE_TRANSIENT_VALUE);
  rc = sqlite3_step(context->fts_stmt);
  if (rc != SQLITE_DONE) {
    g_set_error(&context->error,
                chinese_index_error_quark(),
                rc,
                "%s",
                sqlite3_errmsg(context->db));
    return 1;
  }

  context->inserted++;
  return 0;
}

gboolean
chinese_index_rebuild(ChineseIndex *index,
                      const char *dict_dir,
                      GError **error)
{
  if (!index) {
    return FALSE;
  }

  GError *reader_error = NULL;
  LocalDictionaryReader *reader = local_dictionary_reader_new(dict_dir, &reader_error);
  if (!reader) {
    g_propagate_error(error, reader_error);
    return FALSE;
  }

  g_autofree char *source_path = NULL;
  gint64 source_size = 0;
  gint64 source_mtime = 0;
  if (!local_dictionary_reader_get_source_identity(reader,
                                                   &source_path,
                                                   &source_size,
                                                   &source_mtime,
                                                   error)) {
    local_dictionary_reader_free(reader);
    return FALSE;
  }

  sqlite3 *db = NULL;
  if (!open_db(index->db_path, &db, error)) {
    local_dictionary_reader_free(reader);
    return FALSE;
  }

  gboolean ok = ensure_schema(db, error) &&
                exec_sql(db,
                         "PRAGMA journal_mode=WAL;"
                         "PRAGMA synchronous=NORMAL;"
                         "BEGIN IMMEDIATE;"
                         "DELETE FROM chinese_index_metadata;"
                         "DELETE FROM chinese_entries;"
                         "DELETE FROM chinese_entries_fts;",
                         error);
  if (!ok) {
    sqlite3_close(db);
    local_dictionary_reader_free(reader);
    return FALSE;
  }

  BuildContext context = {.db = db};
  int rc = sqlite3_prepare_v2(db,
                              "INSERT OR REPLACE INTO chinese_entries"
                              "(entry_key, part_of_speech, chinese_text, key_len, key_class) "
                              "VALUES (?, ?, ?, ?, ?)",
                              -1,
                              &context.entry_stmt,
                              NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_prepare_v2(db,
                            "INSERT INTO chinese_entries_fts(entry_key, chinese_text) VALUES (?, ?)",
                            -1,
                            &context.fts_stmt,
                            NULL);
  }
  if (rc != SQLITE_OK) {
    g_set_error(error, chinese_index_error_quark(), rc, "%s", sqlite3_errmsg(db));
    exec_sql(db, "ROLLBACK;", NULL);
    sqlite3_finalize(context.entry_stmt);
    sqlite3_finalize(context.fts_stmt);
    sqlite3_close(db);
    local_dictionary_reader_free(reader);
    return FALSE;
  }

  ok = local_dictionary_reader_iter_entries(reader, on_index_entry, &context, error);
  if (context.error) {
    g_clear_error(error);
    g_propagate_error(error, context.error);
    ok = FALSE;
  }

  sqlite3_finalize(context.entry_stmt);
  sqlite3_finalize(context.fts_stmt);

  if (ok) {
    g_autofree char *source_size_text = g_strdup_printf("%" G_GINT64_FORMAT, source_size);
    g_autofree char *source_mtime_text = g_strdup_printf("%" G_GINT64_FORMAT, source_mtime);
    g_autofree char *entry_count_text = g_strdup_printf("%u", context.inserted);
    ok = metadata_put(db, "source_path", source_path, error) &&
         metadata_put(db, "source_size", source_size_text, error) &&
         metadata_put(db, "source_mtime", source_mtime_text, error) &&
         metadata_put(db, "entry_count", entry_count_text, error) &&
         metadata_put(db, "status", "complete", error);
  }

  if (ok) {
    ok = exec_sql(db, "COMMIT;", error);
  } else {
    exec_sql(db, "ROLLBACK;", NULL);
  }

  sqlite3_close(db);
  local_dictionary_reader_free(reader);
  return ok;
}

static char *
make_snippet(const char *text, const char *query)
{
  const char *match = strstr(text, query);
  if (!match) {
    match = text;
  }

  const char *start = match;
  for (guint i = 0; i < 10 && start > text; i++) {
    start = g_utf8_prev_char(start);
  }

  const char *end = match + strlen(query);
  for (guint i = 0; i < 24 && *end; i++) {
    end = g_utf8_next_char(end);
  }

  g_autofree char *body = g_strndup(start, end - start);
  g_strstrip(body);

  gboolean has_prefix = start > text;
  gboolean has_suffix = *end != '\0';
  if (has_prefix && has_suffix) {
    return g_strdup_printf("...%s...", body);
  }
  if (has_prefix) {
    return g_strdup_printf("...%s", body);
  }
  if (has_suffix) {
    return g_strdup_printf("%s...", body);
  }
  return g_strdup(body);
}

GPtrArray *
chinese_index_query(ChineseIndex *index,
                    const char *query,
                    guint limit,
                    GError **error)
{
  GPtrArray *candidates = g_ptr_array_new_with_free_func(chinese_index_candidate_free);
  if (!index || !query || query[0] == '\0' || limit == 0) {
    return candidates;
  }

  sqlite3 *db = NULL;
  if (!open_db(index->db_path, &db, error)) {
    g_ptr_array_unref(candidates);
    return NULL;
  }
  if (!ensure_schema(db, error)) {
    sqlite3_close(db);
    g_ptr_array_unref(candidates);
    return NULL;
  }

  sqlite3_stmt *stmt = NULL;
  const char *sql =
      "SELECT e.entry_key, e.part_of_speech, e.chinese_text "
      "FROM chinese_entries_fts f "
      "JOIN chinese_entries e ON e.entry_key = f.entry_key "
      "WHERE f.chinese_text LIKE ? "
      "GROUP BY e.entry_key "
      "ORDER BY "
      "((length(substr(e.chinese_text, 1, 160)) - "
      "length(replace(substr(e.chinese_text, 1, 160), ?, ''))) / length(?)) DESC, "
      "instr(e.chinese_text, ?), e.key_class, e.key_len, e.entry_key "
      "LIMIT ?";
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    g_set_error(error, chinese_index_error_quark(), rc, "%s", sqlite3_errmsg(db));
    sqlite3_close(db);
    g_ptr_array_unref(candidates);
    return NULL;
  }

  g_autofree char *like_query = g_strdup_printf("%%%s%%", query);
  sqlite3_bind_text(stmt, 1, like_query, -1, SQLITE_TRANSIENT_VALUE);
  sqlite3_bind_text(stmt, 2, query, -1, SQLITE_TRANSIENT_VALUE);
  sqlite3_bind_text(stmt, 3, query, -1, SQLITE_TRANSIENT_VALUE);
  sqlite3_bind_text(stmt, 4, query, -1, SQLITE_TRANSIENT_VALUE);
  sqlite3_bind_int(stmt, 5, (int)limit);

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const unsigned char *entry_key = sqlite3_column_text(stmt, 0);
    const unsigned char *part_of_speech = sqlite3_column_text(stmt, 1);
    const unsigned char *chinese_text = sqlite3_column_text(stmt, 2);
    if (!entry_key || !chinese_text) {
      continue;
    }

    ChineseIndexCandidate *candidate = g_new0(ChineseIndexCandidate, 1);
    candidate->entry_key = g_strdup((const char *)entry_key);
    candidate->part_of_speech =
        part_of_speech && part_of_speech[0] ? g_strdup((const char *)part_of_speech) : NULL;
    candidate->snippet = make_snippet((const char *)chinese_text, query);
    g_ptr_array_add(candidates, candidate);
  }

  if (rc != SQLITE_DONE) {
    g_set_error(error, chinese_index_error_quark(), rc, "%s", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    g_ptr_array_unref(candidates);
    return NULL;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return candidates;
}
