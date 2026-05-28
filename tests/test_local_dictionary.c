#include "local_dictionary.h"

#include <glib.h>
#include <glib/gstdio.h>

static void
write_empty_file(const char *dir, const char *name)
{
  g_autofree char *path = g_build_filename(dir, name, NULL);
  g_assert_true(g_file_set_contents(path, "", 0, NULL));
}

static void
test_reader_rejects_missing_dir(void)
{
  GError *error = NULL;
  LocalDictionaryReader *reader =
      local_dictionary_reader_new("/tmp/mini-dict-missing-ldoce-dir", &error);
  g_assert_null(reader);
  g_assert_error(error, g_quark_from_static_string("mini-dict-local-dictionary-error"), 2);
  g_clear_error(&error);
}

static void
test_reader_accepts_ldoce_shape(void)
{
  g_autofree char *tmp_dir = g_dir_make_tmp("mini-dict-local-dictionary-XXXXXX", NULL);
  g_assert_nonnull(tmp_dir);

  write_empty_file(tmp_dir, "LDOCE5++ V 2-15.mdx");
  write_empty_file(tmp_dir, "LDOCE5++ V 2-15.mdd");
  write_empty_file(tmp_dir, "LM5style.css");
  write_empty_file(tmp_dir, "LM5style_switch.css");
  write_empty_file(tmp_dir, "LM5Switch.js");
  write_empty_file(tmp_dir, "jquery-3.2.1.min.js");
  write_empty_file(tmp_dir, "LM5pp_config.ini");

  GError *error = NULL;
  LocalDictionaryReader *reader = local_dictionary_reader_new(tmp_dir, &error);
  g_assert_no_error(error);
  g_assert_nonnull(reader);
  g_assert_cmpstr(local_dictionary_reader_get_dir(reader), !=, NULL);

  LocalDictionaryLookupResult *result =
      local_dictionary_reader_lookup(reader, "hello");
  g_assert_nonnull(result);
  g_assert_true(result->status == LOCAL_DICTIONARY_LOOKUP_UNSUPPORTED ||
                result->status == LOCAL_DICTIONARY_LOOKUP_ERROR);
  g_assert_cmpstr(result->query, ==, "hello");

  local_dictionary_lookup_result_free(result);
  local_dictionary_reader_free(reader);

  const char *files[] = {
      "LDOCE5++ V 2-15.mdx",
      "LDOCE5++ V 2-15.mdd",
      "LM5style.css",
      "LM5style_switch.css",
      "LM5Switch.js",
      "jquery-3.2.1.min.js",
      "LM5pp_config.ini",
      NULL,
  };
  for (guint i = 0; files[i]; i++) {
    g_autofree char *path = g_build_filename(tmp_dir, files[i], NULL);
    g_remove(path);
  }
  g_rmdir(tmp_dir);
}

static void
test_real_ldoce_lookup_when_configured(void)
{
  const char *dict_dir = g_getenv("MINI_DICT_TEST_LDOCE_DIR");
  if (!dict_dir || dict_dir[0] == '\0') {
    g_test_skip("MINI_DICT_TEST_LDOCE_DIR is not set");
    return;
  }

  GError *error = NULL;
  LocalDictionaryReader *reader = local_dictionary_reader_new(dict_dir, &error);
  g_assert_no_error(error);
  g_assert_nonnull(reader);

  LocalDictionaryLookupResult *result =
      local_dictionary_reader_lookup(reader, "apple");
  g_assert_nonnull(result);
  g_assert_cmpint(result->status, ==, LOCAL_DICTIONARY_LOOKUP_OK);
  g_assert_nonnull(result->entry_html);
  g_assert_true(strstr(result->entry_html, "lm5ppbody") != NULL ||
                strstr(result->entry_html, "apple") != NULL);

  local_dictionary_lookup_result_free(result);

  GError *asset_error = NULL;
  g_autofree char *resolved_key = NULL;
  GBytes *asset =
      local_dictionary_reader_load_asset(reader,
                                         "media/english/ameProns/-central1004a.mp3",
                                         &resolved_key,
                                         &asset_error);
  g_assert_no_error(asset_error);
  g_assert_nonnull(asset);
  g_assert_nonnull(resolved_key);
  g_assert_cmpuint(g_bytes_get_size(asset), >, 0);
  g_bytes_unref(asset);

  local_dictionary_reader_free(reader);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/local-dictionary/rejects-missing-dir", test_reader_rejects_missing_dir);
  g_test_add_func("/local-dictionary/accepts-ldoce-shape", test_reader_accepts_ldoce_shape);
  g_test_add_func("/local-dictionary/real-ldoce-lookup-when-configured",
                  test_real_ldoce_lookup_when_configured);
  return g_test_run();
}
