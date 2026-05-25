#include "cache.h"

#include <glib/gstdio.h>
#include <sqlite3.h>
#include <time.h>

struct _LookupCache {
  sqlite3 *db;
};

static GQuark
cache_error_quark(void)
{
  return g_quark_from_static_string("mini-dict-cache-error");
}

static gboolean
exec_sql(sqlite3 *db, const char *sql, GError **error)
{
  char *message = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &message);
  if (rc != SQLITE_OK) {
    g_set_error(error,
                cache_error_quark(),
                rc,
                "%s",
                message ? message : sqlite3_errmsg(db));
    sqlite3_free(message);
    return FALSE;
  }
  return TRUE;
}

LookupCache *
lookup_cache_new(GError **error)
{
  g_autofree char *dir = g_build_filename(g_get_user_cache_dir(),
                                          "mini-dict",
                                          NULL);
  if (g_mkdir_with_parents(dir, 0700) != 0) {
    g_set_error(error,
                cache_error_quark(),
                1,
                "Failed to create cache directory: %s",
                dir);
    return NULL;
  }

  g_autofree char *path = g_build_filename(dir, "lookup-cache.sqlite3", NULL);
  LookupCache *cache = g_new0(LookupCache, 1);
  int rc = sqlite3_open(path, &cache->db);
  if (rc != SQLITE_OK) {
    g_set_error(error,
                cache_error_quark(),
                rc,
                "%s",
                cache->db ? sqlite3_errmsg(cache->db) : "Failed to open cache");
    lookup_cache_free(cache);
    return NULL;
  }

  if (!exec_sql(cache->db,
                "CREATE TABLE IF NOT EXISTS lookup_cache ("
                "query TEXT PRIMARY KEY,"
                "response_json TEXT NOT NULL,"
                "fetched_at INTEGER NOT NULL"
                ");",
                error)) {
    lookup_cache_free(cache);
    return NULL;
  }

  return cache;
}

void
lookup_cache_free(LookupCache *cache)
{
  if (!cache) {
    return;
  }
  if (cache->db) {
    sqlite3_close(cache->db);
  }
  g_free(cache);
}

gboolean
lookup_cache_get(LookupCache *cache,
                 const char *query,
                 int max_age_days,
                 char **response_json,
                 GError **error)
{
  *response_json = NULL;
  if (!cache || !query) {
    return FALSE;
  }

  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT response_json, fetched_at FROM lookup_cache WHERE query = ?";
  int rc = sqlite3_prepare_v2(cache->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    g_set_error(error, cache_error_quark(), rc, "%s", sqlite3_errmsg(cache->db));
    return FALSE;
  }

  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    sqlite3_int64 fetched_at = sqlite3_column_int64(stmt, 1);
    time_t now = time(NULL);
    sqlite3_int64 max_age = (sqlite3_int64)max_age_days * 24 * 60 * 60;
    if (max_age_days <= 0 || now - fetched_at <= max_age) {
      const unsigned char *text = sqlite3_column_text(stmt, 0);
      if (text) {
        *response_json = g_strdup((const char *)text);
      }
    }
  } else if (rc != SQLITE_DONE) {
    g_set_error(error, cache_error_quark(), rc, "%s", sqlite3_errmsg(cache->db));
    sqlite3_finalize(stmt);
    return FALSE;
  }

  sqlite3_finalize(stmt);
  return *response_json != NULL;
}

gboolean
lookup_cache_put(LookupCache *cache,
                 const char *query,
                 const char *response_json,
                 GError **error)
{
  if (!cache || !query || !response_json) {
    return FALSE;
  }

  sqlite3_stmt *stmt = NULL;
  const char *sql =
      "INSERT INTO lookup_cache(query, response_json, fetched_at) VALUES (?, ?, ?) "
      "ON CONFLICT(query) DO UPDATE SET "
      "response_json = excluded.response_json, fetched_at = excluded.fetched_at";
  int rc = sqlite3_prepare_v2(cache->db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    g_set_error(error, cache_error_quark(), rc, "%s", sqlite3_errmsg(cache->db));
    return FALSE;
  }

  sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, response_json, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, (sqlite3_int64)time(NULL));

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    g_set_error(error, cache_error_quark(), rc, "%s", sqlite3_errmsg(cache->db));
    sqlite3_finalize(stmt);
    return FALSE;
  }

  sqlite3_finalize(stmt);
  return TRUE;
}

gboolean
lookup_cache_clear(LookupCache *cache, GError **error)
{
  if (!cache) {
    return FALSE;
  }
  return exec_sql(cache->db, "DELETE FROM lookup_cache;", error);
}
