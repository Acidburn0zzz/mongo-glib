#include "test-helper.h"

#include <mongo-glib/mongo-glib.h>

static GMainLoop *gMainLoop;

static void
test1_insert_cb (GObject      *object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   gboolean *success = user_data;
   GError *error = NULL;

   *success = mongo_client_insert_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(*success);

   g_main_loop_quit(gMainLoop);
}

static void
test1 (void)
{
   MongoClient *client;
   MongoBson *bson;
   gboolean success = FALSE;

   client = mongo_client_new();
   bson = mongo_bson_new();
   mongo_bson_append_int(bson, "key1", 1234);
   mongo_bson_append_string(bson, "key2", "Some test string");
   mongo_client_insert_async(client, "dbtest1.dbcollection1",
                             MONGO_INSERT_NONE, &bson, 1, NULL,
                             test1_insert_cb, &success);
   mongo_bson_unref(bson);

   g_main_loop_run(gMainLoop);

   g_assert_cmpint(success, ==, TRUE);
}

static void
test2_query_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   MongoReply *reply;
   gboolean *success = user_data;
   GError *error = NULL;
   guint i;

   reply = mongo_client_query_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(reply);

   for (i = 0; i < reply->n_returned; i++) {
      g_assert(reply->documents[i]);
   }

   mongo_reply_unref(reply);

   *success = TRUE;
   g_main_loop_quit(gMainLoop);
}

static void
test2 (void)
{
   MongoClient *client;
   MongoBson *bson;
   gboolean success = FALSE;

   client = mongo_client_new();
   bson = mongo_bson_new_empty();
   mongo_bson_append_int(bson, "key1", 1234);
   mongo_client_query_async(client, "dbtest1.dbcollection1", MONGO_QUERY_NONE,
                            0, 0, bson, NULL, NULL, test2_query_cb, &success);
   mongo_bson_unref(bson);

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
   g_test_add_func("/MongoClient/insert_async", test1);
   g_test_add_func("/MongoClient/query_async", test2);
   return g_test_run();
}
