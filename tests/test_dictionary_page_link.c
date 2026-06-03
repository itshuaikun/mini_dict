#include "dictionary_page_link.h"

#include <glib.h>

static void
assert_query(const char *uri, const char *dict_dir, const char *expected)
{
  g_autofree char *query = dictionary_page_link_uri_to_query(uri, dict_dir);
  g_assert_cmpstr(query, ==, expected);
}

static void
assert_no_query(const char *uri, const char *dict_dir)
{
  g_autofree char *query = dictionary_page_link_uri_to_query(uri, dict_dir);
  g_assert_null(query);
}

static void
test_relative_entry_links(void)
{
  assert_query("antonym", "/tmp/LDOCE 5++ V2.15", "antonym");
  assert_query("syn-", "/tmp/LDOCE 5++ V2.15", "syn-");
  assert_query("multi%20word#anchor", "/tmp/LDOCE 5++ V2.15", "multi word");
}

static void
test_file_entry_links_inside_dictionary_dir(void)
{
  assert_query("file:///tmp/LDOCE%205++%20V2.15/antonym",
               "/tmp/LDOCE 5++ V2.15",
               "antonym");
  assert_query("file:///tmp/LDOCE%205++%20V2.15/syn-",
               "/tmp/LDOCE 5++ V2.15",
               "syn-");
}

static void
test_entry_scheme_links(void)
{
  assert_query("entry://antonym", "/tmp/LDOCE 5++ V2.15", "antonym");
  assert_query("entry://antonym/", "/tmp/LDOCE 5++ V2.15", "antonym");
  assert_query("entry://syn#syn__2__a", "/tmp/LDOCE 5++ V2.15", "syn");
  assert_query("entry:syn-", "/tmp/LDOCE 5++ V2.15", "syn-");
  assert_no_query("entry://#ldoce-anchor", "/tmp/LDOCE 5++ V2.15");
}

static void
test_ignores_assets_and_external_links(void)
{
  assert_no_query("sound://media/english/breProns/foo.mp3",
                  "/tmp/LDOCE 5++ V2.15");
  assert_no_query("file:///tmp/LDOCE%205++%20V2.15/LM5style.css",
                  "/tmp/LDOCE 5++ V2.15");
  assert_no_query("file:///tmp/LDOCE%205++%20V2.15/media/english/foo.mp3",
                  "/tmp/LDOCE 5++ V2.15");
  assert_no_query("file:///tmp/other/antonym", "/tmp/LDOCE 5++ V2.15");
  assert_no_query("https://example.com/antonym", "/tmp/LDOCE 5++ V2.15");
  assert_no_query("#local-anchor", "/tmp/LDOCE 5++ V2.15");
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/dictionary-page-link/relative-entry-links",
                  test_relative_entry_links);
  g_test_add_func("/dictionary-page-link/file-entry-links-inside-dictionary-dir",
                  test_file_entry_links_inside_dictionary_dir);
  g_test_add_func("/dictionary-page-link/entry-scheme-links",
                  test_entry_scheme_links);
  g_test_add_func("/dictionary-page-link/ignores-assets-and-external-links",
                  test_ignores_assets_and_external_links);
  return g_test_run();
}
