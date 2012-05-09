#include "test-helper.h"

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

static void
test3_disconnect_cb (GObject      *object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   gboolean *success = user_data;
   GError *error = NULL;

   *success = mongo_client_disconnect_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(*success);

   g_main_loop_quit(gMainLoop);
}

static void
test3_connect_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   gboolean ret;
   GError *error = NULL;

   ret = mongo_client_connect_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(ret);

   mongo_client_disconnect_async(client, TRUE, NULL,
                                 test3_disconnect_cb,
                                 user_data);
}

static void
test3 (void)
{
   MongoClient *client;
   gboolean success = FALSE;

   client = mongo_client_new();
   mongo_client_add_seed(client, "localhost", 27017);
   mongo_client_connect_async(client, NULL, test3_connect_cb, &success);

   g_main_loop_run(gMainLoop);

   g_assert_cmpint(success, ==, TRUE);
}

static void
test4_insert_cb (GObject      *object,
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
test4_connect_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   MongoBson *bson;
   gboolean ret;
   GError *error = NULL;

   ret = mongo_client_connect_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(ret);

   bson = mongo_bson_new();
   mongo_bson_append_int(bson, "key1", 1234);
   mongo_bson_append_string(bson, "key2", "Some test string");
   mongo_client_insert_async(client, "dbtest1.dbcollection1",
                             MONGO_INSERT_NONE, &bson, 1, NULL,
                             test4_insert_cb, user_data);
   mongo_bson_unref(bson);
}

static void
test4 (void)
{
   MongoClient *client;
   gboolean success = FALSE;

   client = mongo_client_new();
   mongo_client_add_seed(client, "localhost", 27017);
   mongo_client_connect_async(client, NULL, test4_connect_cb, &success);

   g_main_loop_run(gMainLoop);

   g_assert_cmpint(success, ==, TRUE);
}

static void
test5_query_cb (GObject      *object,
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
test5_connect_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   MongoBson *bson;
   gboolean ret;
   GError *error = NULL;

   ret = mongo_client_connect_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(ret);

   bson = mongo_bson_new_empty();
   mongo_bson_append_int(bson, "key1", 1234);
   mongo_client_query_async(client, "dbtest1.dbcollection1", MONGO_QUERY_NONE,
                            0, 0, bson, NULL, NULL, test5_query_cb, user_data);
   mongo_bson_unref(bson);
}

static void
test5 (void)
{
   MongoClient *client;
   gboolean success = FALSE;

   client = mongo_client_new();
   mongo_client_add_seed(client, "localhost", 27017);
   mongo_client_connect_async(client, NULL, test5_connect_cb, &success);

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

   g_test_add_func("/MongoClient/connect_async", test1);
   g_test_add_func("/MongoClient/connect_async_cancelled", test2);
   g_test_add_func("/MongoClient/disconnect_async", test3);
   g_test_add_func("/MongoClient/insert_async", test4);
   g_test_add_func("/MongoClient/query_async", test5);

   return g_test_run();
}
