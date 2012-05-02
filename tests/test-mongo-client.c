#include <mongo-glib/mongo-glib.h>

static GMainLoop *gMainLoop;

static void
test1_connect_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   gboolean *success = user_data;
   GError *error = NULL;

   *success = mongo_client_connect_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(*success);

   g_main_loop_quit(gMainLoop);
}

static void
test1 (void)
{
   MongoClient *client;
   gboolean success = FALSE;

   client = mongo_client_new();
   mongo_client_add_seed(client, "localhost", 27017);
   mongo_client_connect_async(client, NULL, test1_connect_cb, &success);

   g_main_loop_run(gMainLoop);

   g_assert_cmpint(success, ==, TRUE);
}

static void
test2_connect_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   gboolean *success = user_data;
   GError *error = NULL;

   *success = mongo_client_connect_finish(client, result, &error);
   g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
   g_assert(!*success);

   g_main_loop_quit(gMainLoop);
}

static void
test2 (void)
{
   GCancellable *cancellable;
   MongoClient *client;
   gboolean success = FALSE;

   cancellable = g_cancellable_new();
   client = mongo_client_new();
   mongo_client_add_seed(client, "localhost", 27017);
   mongo_client_connect_async(client, cancellable, test2_connect_cb, &success);
   g_cancellable_cancel(cancellable);

   g_main_loop_run(gMainLoop);

   g_assert_cmpint(success, ==, FALSE);
}

gint
main (gint   argc,
      gchar *argv[])
{
   g_test_init(&argc, &argv, NULL);

   g_type_init();
   gMainLoop = g_main_loop_new(NULL, FALSE);

   g_test_add_func("/MongoClient/connect_async", test1);
   g_test_add_func("/MongoClient/connect_async_cancelled", test2);

   return g_test_run();
}
