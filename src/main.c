#include "app.h"

#include <gst/gst.h>

#include <stdio.h>
#include <string.h>

static gboolean
has_cli_arg(int argc, char **argv, const char *name)
{
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], name) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

static void
print_help(void)
{
  printf(
      "Usage:\n"
      "  mini-dict [OPTION...]\n"
      "\n"
      "A small GTK4 dictionary lookup window for Linux Wayland desktops.\n"
      "\n"
      "Options:\n"
      "  -h, --help                  Show this help and exit\n"
      "      --version               Show version information and exit\n"
      "      --toggle                Toggle the lookup window\n"
      "      --dict-dir DIR          Use a local LDOCE dictionary directory\n"
      "      --monitor OUTPUT        Show the lookup window on this output\n"
      "      --check-dict WORD       Check a local dictionary entry and exit\n"
      "      --rebuild-chinese-index Rebuild the Chinese reverse lookup index and exit\n"
      "      --clear-cache           Clear cached online fallback results and exit\n"
      "\n"
      "Environment:\n"
      "  MINI_DICT_DICT_DIR          Default local LDOCE dictionary directory\n"
      "  MINI_DICT_MONITOR           Default lookup window output\n"
      "\n"
      "Examples:\n"
      "  mini-dict --toggle\n"
      "  mini-dict --dict-dir \"/path/to/LDOCE 5++ V2.15\" --check-dict apple\n");
}

static void
print_version(void)
{
  printf("mini-dict %s\n", MINI_DICT_VERSION);
}

int
main(int argc, char **argv)
{
  if (has_cli_arg(argc, argv, "--help") || has_cli_arg(argc, argv, "-h")) {
    print_help();
    return 0;
  }
  if (has_cli_arg(argc, argv, "--version")) {
    print_version();
    return 0;
  }

  gst_init(&argc, &argv);

  GtkApplication *application = mini_dict_app_new();
  int status = g_application_run(G_APPLICATION(application), argc, argv);
  g_object_unref(application);
  return status;
}
