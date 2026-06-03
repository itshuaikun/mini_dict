#include "dictionary_page_link.h"

#include <string.h>

static gboolean
ascii_has_prefix_ci(const char *value, const char *prefix)
{
  return g_ascii_strncasecmp(value, prefix, strlen(prefix)) == 0;
}

static char *
copy_without_query_or_fragment(const char *value)
{
  const char *end = value + strlen(value);
  for (const char *p = value; *p; p++) {
    if (*p == '?' || *p == '#') {
      end = p;
      break;
    }
  }
  return g_strndup(value, (gsize)(end - value));
}

static gboolean
path_is_under_dir(const char *path, const char *dir)
{
  if (!path || !dir || dir[0] == '\0') {
    return FALSE;
  }

  size_t dir_len = strlen(dir);
  return g_str_has_prefix(path, dir) &&
         (path[dir_len] == '\0' || G_IS_DIR_SEPARATOR(path[dir_len]));
}

static const char *
skip_leading_relative_markers(const char *value)
{
  const char *p = value;
  while (g_str_has_prefix(p, "./")) {
    p += 2;
  }
  while (*p == '/') {
    p++;
  }
  return p;
}

static gboolean
looks_like_parent_path(const char *value)
{
  return g_strcmp0(value, "..") == 0 ||
         g_str_has_prefix(value, "../") ||
         strstr(value, "/../") != NULL ||
         g_str_has_suffix(value, "/..");
}

static gboolean
looks_like_dictionary_asset(const char *value)
{
  if (value[0] == '\0') {
    return TRUE;
  }

  g_autofree char *lower = g_ascii_strdown(value, -1);
  if (g_str_has_prefix(lower, "media/") ||
      g_str_has_prefix(lower, "image/") ||
      g_str_has_prefix(lower, "images/") ||
      g_str_has_prefix(lower, "img/") ||
      g_str_has_prefix(lower, "_h5ai/")) {
    return TRUE;
  }

  const char *basename = strrchr(lower, '/');
  basename = basename ? basename + 1 : lower;
  const char *dot = strrchr(basename, '.');
  if (!dot) {
    return FALSE;
  }

  const char *asset_extensions[] = {
      ".css",  ".js",   ".mdd",  ".mdx",  ".ini",  ".html",
      ".htm",  ".mp3",  ".wav",  ".ogg",  ".oga",  ".m4a",
      ".png",  ".jpg",  ".jpeg", ".gif",  ".webp", ".svg",
      ".ico",  ".ttf",  ".otf",  ".woff", ".woff2", NULL,
  };
  for (guint i = 0; asset_extensions[i]; i++) {
    if (g_strcmp0(dot, asset_extensions[i]) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

static char *
query_from_reference(const char *reference)
{
  g_autofree char *without_suffix = copy_without_query_or_fragment(reference);
  g_strstrip(without_suffix);
  const char *trimmed = skip_leading_relative_markers(without_suffix);
  if (trimmed[0] == '\0' || trimmed[0] == '#') {
    return NULL;
  }

  g_autofree char *unescaped = g_uri_unescape_string(trimmed, NULL);
  g_autofree char *query = g_strdup(unescaped && unescaped[0] ? unescaped : trimmed);
  for (char *p = query; *p; p++) {
    if (*p == '\\') {
      *p = '/';
    }
  }
  g_strstrip(query);
  gsize len = strlen(query);
  while (len > 0 && query[len - 1] == '/') {
    query[--len] = '\0';
  }

  if (looks_like_parent_path(query) || looks_like_dictionary_asset(query)) {
    return NULL;
  }

  return g_steal_pointer(&query);
}

static char *
query_from_file_uri(const char *uri, const char *dict_dir)
{
  if (!dict_dir || dict_dir[0] == '\0') {
    return NULL;
  }

  g_autofree char *uri_without_suffix = copy_without_query_or_fragment(uri);
  GError *error = NULL;
  g_autofree char *path = g_filename_from_uri(uri_without_suffix, NULL, &error);
  if (!path) {
    g_clear_error(&error);
    return NULL;
  }

  g_autofree char *canonical_path = g_canonicalize_filename(path, NULL);
  g_autofree char *canonical_dir = g_canonicalize_filename(dict_dir, NULL);
  if (!path_is_under_dir(canonical_path, canonical_dir)) {
    return NULL;
  }

  const char *relative = canonical_path + strlen(canonical_dir);
  while (G_IS_DIR_SEPARATOR(*relative)) {
    relative++;
  }
  return query_from_reference(relative);
}

static char *
query_from_entry_uri(const char *uri)
{
  const char *target = strchr(uri, ':');
  target = target ? target + 1 : uri;
  while (*target == '/') {
    target++;
  }
  if (target[0] == '\0' || target[0] == '#') {
    return NULL;
  }
  return query_from_reference(target);
}

char *
dictionary_page_link_uri_to_query(const char *uri, const char *dict_dir)
{
  if (!uri) {
    return NULL;
  }

  g_autofree char *copy = g_strdup(uri);
  g_strstrip(copy);
  if (copy[0] == '\0' || copy[0] == '#') {
    return NULL;
  }

  if (ascii_has_prefix_ci(copy, "sound://")) {
    return NULL;
  }

  const char *scheme = g_uri_peek_scheme(copy);
  if (!scheme) {
    return query_from_reference(copy);
  }

  g_autofree char *scheme_lower = g_ascii_strdown(scheme, -1);
  if (g_strcmp0(scheme_lower, "file") == 0) {
    return query_from_file_uri(copy, dict_dir);
  }
  if (g_strcmp0(scheme_lower, "entry") == 0) {
    return query_from_entry_uri(copy);
  }

  return NULL;
}
