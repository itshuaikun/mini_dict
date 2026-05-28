#include "local_dictionary.h"

#include <glib/gstdio.h>

#ifdef MINI_DICT_HAVE_LDOCE_READER
#include "ldoce_reader_ffi.h"
#endif

#ifndef MINI_DICT_SOURCE_DIR
#define MINI_DICT_SOURCE_DIR "."
#endif

#define LDOCE_DIR_NAME "LDOCE 5++ V2.15"
#define LDOCE_MDX_NAME "LDOCE5++ V 2-15.mdx"
#define LDOCE_MDD_NAME "LDOCE5++ V 2-15.mdd"
#define LDOCE_CSS_NAME "LM5style.css"
#define LDOCE_JS_NAME "LM5Switch.js"
#define LDOCE_JQUERY_NAME "jquery-3.2.1.min.js"
#define LDOCE_CONFIG_NAME "LM5pp_config.ini"

struct _LocalDictionaryReader {
  char *dict_dir;
  char *mdx_path;
  char *mdd_path;
#ifdef MINI_DICT_HAVE_LDOCE_READER
  MiniDictLdoceReader *reader;
#endif
};

static GQuark
local_dictionary_error_quark(void)
{
  return g_quark_from_static_string("mini-dict-local-dictionary-error");
}

static gboolean
file_exists_in_dir(const char *dir, const char *name)
{
  g_autofree char *path = g_build_filename(dir, name, NULL);
  return g_file_test(path, G_FILE_TEST_IS_REGULAR);
}

static gboolean
looks_like_ldoce_dir(const char *dir)
{
  return dir && g_file_test(dir, G_FILE_TEST_IS_DIR) &&
         file_exists_in_dir(dir, LDOCE_MDX_NAME) &&
         file_exists_in_dir(dir, LDOCE_MDD_NAME) &&
         file_exists_in_dir(dir, LDOCE_CSS_NAME) &&
         file_exists_in_dir(dir, LDOCE_JS_NAME) &&
         file_exists_in_dir(dir, LDOCE_JQUERY_NAME) &&
         file_exists_in_dir(dir, LDOCE_CONFIG_NAME);
}

static char *
canonical_existing_dir(const char *dir)
{
  if (!looks_like_ldoce_dir(dir)) {
    return NULL;
  }

  char *canonical = g_canonicalize_filename(dir, NULL);
  return canonical;
}

char *
local_dictionary_resolve_dir(const char *explicit_dir)
{
  if (explicit_dir && explicit_dir[0]) {
    char *dir = canonical_existing_dir(explicit_dir);
    if (dir) {
      return dir;
    }
    return g_strdup(explicit_dir);
  }

  const char *env_dir = g_getenv("MINI_DICT_DICT_DIR");
  if (env_dir && env_dir[0]) {
    char *dir = canonical_existing_dir(env_dir);
    if (dir) {
      return dir;
    }
    return g_strdup(env_dir);
  }

  g_autofree char *source_candidate =
      g_build_filename(MINI_DICT_SOURCE_DIR, "dict", LDOCE_DIR_NAME, NULL);
  char *dir = canonical_existing_dir(source_candidate);
  if (dir) {
    return dir;
  }

  g_autofree char *cwd = g_get_current_dir();
  g_autofree char *cwd_candidate =
      g_build_filename(cwd, "dict", LDOCE_DIR_NAME, NULL);
  dir = canonical_existing_dir(cwd_candidate);
  if (dir) {
    return dir;
  }

  g_autofree char *xdg_candidate =
      g_build_filename(g_get_user_data_dir(), "mini-dict", LDOCE_DIR_NAME, NULL);
  dir = canonical_existing_dir(xdg_candidate);
  if (dir) {
    return dir;
  }

  return NULL;
}

LocalDictionaryReader *
local_dictionary_reader_new(const char *dict_dir, GError **error)
{
  if (!dict_dir || dict_dir[0] == '\0') {
    g_set_error(error,
                local_dictionary_error_quark(),
                1,
                "Local dictionary directory is not configured. Use --dict-dir or MINI_DICT_DICT_DIR.");
    return NULL;
  }

  if (!g_file_test(dict_dir, G_FILE_TEST_IS_DIR)) {
    g_set_error(error,
                local_dictionary_error_quark(),
                2,
                "Local dictionary directory does not exist: %s",
                dict_dir);
    return NULL;
  }

  const char *required_files[] = {
      LDOCE_MDX_NAME,
      LDOCE_MDD_NAME,
      LDOCE_CSS_NAME,
      "LM5style_switch.css",
      LDOCE_JS_NAME,
      LDOCE_JQUERY_NAME,
      LDOCE_CONFIG_NAME,
      NULL,
  };

  for (guint i = 0; required_files[i]; i++) {
    if (!file_exists_in_dir(dict_dir, required_files[i])) {
      g_set_error(error,
                  local_dictionary_error_quark(),
                  3,
                  "Local dictionary directory is missing required LDOCE file: %s",
                  required_files[i]);
      return NULL;
    }
  }

  LocalDictionaryReader *reader = g_new0(LocalDictionaryReader, 1);
  reader->dict_dir = g_canonicalize_filename(dict_dir, NULL);
  reader->mdx_path = g_build_filename(reader->dict_dir, LDOCE_MDX_NAME, NULL);
  reader->mdd_path = g_build_filename(reader->dict_dir, LDOCE_MDD_NAME, NULL);
  return reader;
}

void
local_dictionary_reader_free(LocalDictionaryReader *reader)
{
  if (!reader) {
    return;
  }
#ifdef MINI_DICT_HAVE_LDOCE_READER
  mini_dict_ldoce_reader_free(reader->reader);
#endif
  g_free(reader->dict_dir);
  g_free(reader->mdx_path);
  g_free(reader->mdd_path);
  g_free(reader);
}

const char *
local_dictionary_reader_get_dir(LocalDictionaryReader *reader)
{
  return reader ? reader->dict_dir : NULL;
}

gboolean
local_dictionary_reader_warm_up(LocalDictionaryReader *reader, GError **error)
{
  if (!reader) {
    g_set_error(error,
                local_dictionary_error_quark(),
                4,
                "Local dictionary is not configured.");
    return FALSE;
  }

#ifdef MINI_DICT_HAVE_LDOCE_READER
  if (!reader->reader) {
    char *error_message = NULL;
    reader->reader = mini_dict_ldoce_reader_open(reader->mdx_path,
                                                 reader->mdd_path,
                                                 &error_message);
    if (!reader->reader) {
      g_set_error(error,
                  local_dictionary_error_quark(),
                  5,
                  "%s",
                  error_message ? error_message : "Failed to open local LDOCE dictionary.");
      mini_dict_ldoce_string_free(error_message);
      return FALSE;
    }
  }
#endif

  return TRUE;
}

static LocalDictionaryLookupResult *
local_dictionary_lookup_result_new(LocalDictionaryLookupStatus status,
                                   const char *query,
                                   const char *message)
{
  LocalDictionaryLookupResult *result = g_new0(LocalDictionaryLookupResult, 1);
  result->status = status;
  result->query = g_strdup(query);
  result->message = g_strdup(message);
  return result;
}

LocalDictionaryLookupResult *
local_dictionary_reader_lookup(LocalDictionaryReader *reader,
                               const char *query)
{
  if (!reader) {
    return local_dictionary_lookup_result_new(
        LOCAL_DICTIONARY_LOOKUP_SETUP_ISSUE,
        query,
        "Local dictionary is not configured.");
  }

#ifdef MINI_DICT_HAVE_LDOCE_READER
  if (!reader->reader) {
    GError *open_error = NULL;
    if (!local_dictionary_reader_warm_up(reader, &open_error)) {
      LocalDictionaryLookupResult *result = local_dictionary_lookup_result_new(
          LOCAL_DICTIONARY_LOOKUP_ERROR,
          query,
          open_error ? open_error->message : "Failed to open local LDOCE dictionary.");
      g_clear_error(&open_error);
      return result;
    }
  }

  MiniDictLdoceLookup *lookup = mini_dict_ldoce_reader_lookup(reader->reader, query);
  if (!lookup) {
    return local_dictionary_lookup_result_new(
        LOCAL_DICTIONARY_LOOKUP_ERROR,
        query,
        "Local LDOCE lookup failed.");
  }

  int status = mini_dict_ldoce_lookup_status(lookup);
  const char *key = mini_dict_ldoce_lookup_key(lookup);
  const char *html = mini_dict_ldoce_lookup_html(lookup);
  const char *message = mini_dict_ldoce_lookup_message(lookup);

  LocalDictionaryLookupResult *result = NULL;
  if (status == 0) {
    result = local_dictionary_lookup_result_new(
        LOCAL_DICTIONARY_LOOKUP_OK,
        key && key[0] ? key : query,
        NULL);
    result->entry_html = g_strdup(html);
  } else if (status == 1) {
    result = local_dictionary_lookup_result_new(
        LOCAL_DICTIONARY_LOOKUP_NO_ENTRY,
        query,
        message && message[0] ? message : "No local LDOCE entry found.");
  } else {
    result = local_dictionary_lookup_result_new(
        LOCAL_DICTIONARY_LOOKUP_ERROR,
        query,
        message && message[0] ? message : "Local LDOCE lookup failed.");
  }

  mini_dict_ldoce_lookup_free(lookup);
  return result;
#else
  return local_dictionary_lookup_result_new(
      LOCAL_DICTIONARY_LOOKUP_UNSUPPORTED,
      query,
      "Local LDOCE directory is configured, but MDX/MDD entry lookup is not implemented yet.");
#endif
}

GBytes *
local_dictionary_reader_load_asset(LocalDictionaryReader *reader,
                                   const char *asset_key,
                                   char **resolved_key,
                                   GError **error)
{
  if (resolved_key) {
    *resolved_key = NULL;
  }
  if (!reader || !asset_key || asset_key[0] == '\0') {
    g_set_error(error,
                local_dictionary_error_quark(),
                4,
                "Local dictionary asset key is empty");
    return NULL;
  }

#ifdef MINI_DICT_HAVE_LDOCE_READER
  if (!reader->reader) {
    GError *open_error = NULL;
    if (!local_dictionary_reader_warm_up(reader, &open_error)) {
      g_set_error(error,
                  local_dictionary_error_quark(),
                  open_error ? open_error->code : 5,
                  "%s",
                  open_error ? open_error->message : "Failed to open local LDOCE dictionary.");
      g_clear_error(&open_error);
      return NULL;
    }
  }

  MiniDictLdoceAsset *asset =
      mini_dict_ldoce_reader_lookup_asset(reader->reader, asset_key);
  if (!asset) {
    g_set_error(error,
                local_dictionary_error_quark(),
                6,
                "Local dictionary asset lookup failed");
    return NULL;
  }

  int status = mini_dict_ldoce_asset_status(asset);
  if (status != 0) {
    const char *message = mini_dict_ldoce_asset_message(asset);
    g_set_error(error,
                local_dictionary_error_quark(),
                status == 1 ? 7 : 8,
                "%s",
                message && message[0] ? message : "Local dictionary asset lookup failed");
    mini_dict_ldoce_asset_free(asset);
    return NULL;
  }

  const unsigned char *data = mini_dict_ldoce_asset_data(asset);
  size_t len = mini_dict_ldoce_asset_len(asset);
  const char *key = mini_dict_ldoce_asset_key(asset);
  if (!data || len == 0) {
    g_set_error(error,
                local_dictionary_error_quark(),
                9,
                "Local dictionary asset is empty");
    mini_dict_ldoce_asset_free(asset);
    return NULL;
  }

  GBytes *bytes = g_bytes_new(data, len);
  if (resolved_key && key && key[0]) {
    *resolved_key = g_strdup(key);
  }
  mini_dict_ldoce_asset_free(asset);
  return bytes;
#else
  g_set_error(error,
              local_dictionary_error_quark(),
              10,
              "Local dictionary asset lookup is not available in this build");
  return NULL;
#endif
}

void
local_dictionary_lookup_result_free(LocalDictionaryLookupResult *result)
{
  if (!result) {
    return;
  }
  g_free(result->query);
  g_free(result->entry_html);
  g_free(result->message);
  g_free(result);
}
