#include <mongo-glib/mongo-glib.h>

static GMainLoop *gMainLoop;

static void
query_cb (GObject *object,
          GAsyncResult *result,
          gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   gboolean *success = user_data;
   GError *error = NULL;

   *success = mongo_client_query_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(*success);
   g_main_loop_quit(gMainLoop);
}

static void
connect_cb (GObject      *object,
            GAsyncResult *result,
            gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   MongoBson *query;
   gboolean *success = user_data;
   GError *error = NULL;

   *success = mongo_client_connect_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(*success);

   query = mongo_bson_new();
   mongo_bson_append_string(query, "type", "source");
   mongo_client_query_async(client, "errors", query, NULL, query_cb, user_data);
   mongo_bson_unref(query);
}

static void
test_mongo_client_connect_async (void)
{
   MongoClient *client;
   gboolean success = FALSE;
   GError *error = NULL;
   GList *list;

   list = g_resolver_lookup_by_name(g_resolver_get_default(),
                                    "localhost", NULL, &error);
   g_assert_no_error(error);
   g_assert(list);

   client = g_object_new(MONGO_TYPE_CLIENT, NULL);
   mongo_client_connect_async(client, list->data, 27017, NULL, connect_cb, &success);
   g_main_loop_run(gMainLoop);
   g_resolver_free_addresses(list);
   g_assert(success);
}

gint
main (gint   argc,
      gchar *argv[])
{
   g_test_init(&argc, &argv, NULL);

   g_type_init();
   gMainLoop = g_main_loop_new(NULL, FALSE);

   g_test_add_func("/MongoClient/connect_async", test_mongo_client_connect_async);

   return g_test_run();
}
