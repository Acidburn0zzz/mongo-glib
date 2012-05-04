#include <mongo-glib/mongo-glib.h>

static GMainLoop *gMainLoop;
static MongoClient *gClient;

static void
test1_count_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
   MongoCollection *col = (MongoCollection *)object;
   gboolean *success = user_data;
   GError *error = NULL;
   guint64 count = 0;

   *success = mongo_collection_count_finish(col, result, &count, &error);
   g_assert_no_error(error);
   g_assert(*success);

   g_assert_cmpint(count, >, 0);

   g_main_loop_quit(gMainLoop);
}

static void
test1_connect_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
   MongoCollection *col;
   MongoDatabase *db;
   MongoBson *query = NULL;
   gboolean ret;
   GError *error = NULL;

   ret = mongo_client_connect_finish(gClient, result, &error);
   g_assert_no_error(error);
   g_assert(ret);

   db = mongo_client_get_database(gClient, "dbtest1");
   g_assert(db);

   col = mongo_database_get_collection(db, "dbcollection1");
   g_assert(col);

   mongo_collection_count_async(col, query, NULL, test1_count_cb, user_data);
}

static void
test1 (void)
{
   gboolean success = FALSE;

   gClient = mongo_client_new();
   mongo_client_add_seed(gClient, "localhost", 27017);
   mongo_client_connect_async(gClient, NULL, test1_connect_cb, &success);

   g_main_loop_run(gMainLoop);

   g_assert_cmpint(success, ==, TRUE);
}

gint
main (gint   argc,
      gchar *argv[])
{
   g_test_init(&argc, &argv, NULL);
   g_type_init();

   gMainLoop = g_main_loop_new(NULL, FALSE);

   g_test_add_func("/MongoCollection/count", test1);

   return g_test_run();
}
