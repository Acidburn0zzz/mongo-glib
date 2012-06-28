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

static void
test3_query_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   gboolean *success = user_data;
   GError *error = NULL;

   *success = mongo_client_delete_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(*success);

   g_main_loop_quit(gMainLoop);
}

static void
test3 (void)
{
   MongoClient *client;
   MongoBson *selector;
   gboolean success = FALSE;

   client = mongo_client_new();
   selector = mongo_bson_new_empty();
   mongo_client_delete_async(client,
                             "dbtest1.dbcollection1",
                             MONGO_DELETE_NONE,
                             selector,
                             NULL,
                             test3_query_cb,
                             &success);
   mongo_bson_unref(selector);

   g_main_loop_run(gMainLoop);

   g_assert_cmpint(success, ==, TRUE);
}

static void
test4_query_cb (GObject      *object,
                GAsyncResult *result,
                gpointer      user_data)
{
   MongoClient *client = (MongoClient *)object;
   gboolean *success = user_data;
   MongoReply *reply;
   GError *error = NULL;

   reply = mongo_client_command_finish(client, result, &error);
   g_assert_no_error(error);
   g_assert(reply);

   *success = TRUE;

   mongo_reply_unref(reply);
   g_main_loop_quit(gMainLoop);
}

static void
test4 (void)
{
   MongoClient *client;
   MongoBson *command;
   gboolean success = FALSE;

   client = mongo_client_new();
   command = mongo_bson_new_empty();
   mongo_bson_append_int(command, "ismaster", 1);
   mongo_client_command_async(client,
                              "dbtest1.dbcollection1",
                              command,
                              NULL,
                              test4_query_cb,
                              &success);
   mongo_bson_unref(command);

   g_main_loop_run(gMainLoop);

   g_assert_cmpint(success, ==, TRUE);
}

static void
test5 (void)
{
#define TEST_URI(str) \
   G_STMT_START { \
      MongoClient *c = mongo_client_new_from_uri(str); \
      g_assert(c); \
      g_object_unref(c); \
   } G_STMT_END

   TEST_URI("mongodb://127.0.0.1:27017");
   TEST_URI("mongodb://127.0.0.1:27017/");
   TEST_URI("mongodb://127.0.0.1:27017/?replicaSet=abc");
   TEST_URI("mongodb://127.0.0.1:27017/?replicaSet=abc"
                                      "&connectTimeoutMS=1000"
                                      "&fsync=false"
                                      "&journal=true"
                                      "&safe=true"
                                      "&socketTimeoutMS=5000"
                                      "&wTimeoutMS=1000");
   TEST_URI("mongodb://mongo/?replicaSet=abc");
   TEST_URI("mongodb://mongo:27017?replicaSet=abc");
   TEST_URI("mongodb://mongo:27017/?replicaSet=abc");
   TEST_URI("mongodb://mongo.example.com:27017?replicaSet=abc");
   TEST_URI("mongodb://mongo.example.com?replicaSet=abc");
   TEST_URI("mongodb://mongo.example.com/?replicaSet=abc");
   TEST_URI("mongodb://127.0.0.1,127.0.0.2:27017/?w=123");
   TEST_URI("mongodb://127.0.0.1,127.0.0.2:27017?w=123");

   /*
    * We do not yet support port per host like follows.
    */
#if 0
   TEST_URI("mongodb://user:pass@127.0.0.1:27017,127.0.0.2:27017/?w=123");
#endif

#undef TEST_URI
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
   g_test_add_func("/MongoClient/delete_async", test3);
   g_test_add_func("/MongoClient/command_async", test4);
   g_test_add_func("/MongoClient/uri", test5);
   return g_test_run();
}
