#include "mongo-query.h"
#include "mongo-reply.h"
#include "mongo-server.h"

static void
query_cb (MongoServer        *server,
          MongoClientContext *client,
          MongoMessage       *message)
{
   MongoQuery *query = (MongoQuery *)message;
   MongoBson *doc;
   gchar *str;

   g_assert(MONGO_IS_QUERY(query));

   g_print("collection = %s\n", mongo_query_get_collection(query));

   str = mongo_client_context_get_peer(client);
   doc = mongo_bson_new_empty();
   mongo_bson_append_string(doc, "you", str);
   mongo_bson_append_int(doc, "ok", 1);
   mongo_message_set_reply_bson(message, MONGO_REPLY_NONE, doc);
   mongo_bson_unref(doc);
   g_free(str);
}

gint
main (gint   argc,
      gchar *argv[])
{
   GSocketListener *server;
   GMainLoop *main_loop;
   GError *error = NULL;

   g_type_init();

   main_loop = g_main_loop_new(NULL, FALSE);
   server = g_object_new(MONGO_TYPE_SERVER, NULL);
   g_signal_connect(server, "request-query", G_CALLBACK(query_cb), NULL);
   if (!g_socket_listener_add_inet_port(server, 5201, NULL, &error)) {
      g_printerr("%s\n", error->message);
      return 1;
   }
   g_socket_service_start(G_SOCKET_SERVICE(server));
   g_main_loop_run(main_loop);

   g_object_unref(server);
   g_main_loop_unref(main_loop);

   return 0;
}
