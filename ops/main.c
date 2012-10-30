#include "mongo-server.h"

static void
query_cb (MongoServer        *server,
          MongoClientContext *client,
          MongoMessage       *message)
{
   g_print("QUERY [%d] [%d]\n",
           mongo_message_get_request_id(message),
           mongo_message_get_response_to(message));
}

gint
main (gint   argc,
      gchar *argv[])
{
   MongoServer *server;
   GMainLoop *main_loop;
   GError *error = NULL;

   g_type_init();

   main_loop = g_main_loop_new(NULL, FALSE);
   server = g_object_new(MONGO_TYPE_SERVER, NULL);
   g_signal_connect(server, "request-query", G_CALLBACK(query_cb), NULL);
   if (!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(server), 5201, NULL, &error)) {
      g_printerr("%s\n", error->message);
      return 1;
   }
   g_socket_service_start(G_SOCKET_SERVICE(server));
   g_main_loop_run(main_loop);

   g_object_unref(server);
   g_main_loop_unref(main_loop);

   return 0;
}
