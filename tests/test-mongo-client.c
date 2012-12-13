#include <mongo-glib/mongo-glib.h>

static void
test_MongoClient_dispose (void)
{
   MongoClient *client;
   GSocketClient *socket_client;
   GSocketConnection *connection;

   socket_client = g_socket_client_new();
   connection = g_socket_client_connect_to_host(socket_client, "localhost", 27017, NULL, NULL);
   client = mongo_client_new_from_stream(G_IO_STREAM(connection));
   g_object_add_weak_pointer(G_OBJECT(client), (gpointer *)&client);

   g_clear_object(&connection);
   g_clear_object(&socket_client);

   g_object_unref(client);
   g_assert(!client);
}

gint
main (gint   argc,
      gchar *argv[])
{
   g_type_init();
   g_test_init(&argc, &argv, NULL);
   g_test_add_func("/MongoClient/dispose", test_MongoClient_dispose);
   return g_test_run();
}
