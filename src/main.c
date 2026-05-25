#include "app.h"

#include <gst/gst.h>

int
main(int argc, char **argv)
{
  gst_init(&argc, &argv);

  GtkApplication *application = mini_dict_app_new();
  int status = g_application_run(G_APPLICATION(application), argc, argv);
  g_object_unref(application);
  return status;
}
