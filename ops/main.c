#include "mongo-query.h"
#include "mongo-reply.h"
#include "mongo-server.h"

static void
whatsmyuri (MongoServer        *server,
            MongoClientContext *client,
            MongoMessage       *message)
{
   MongoBson *doc;
   gchar *str;

   str = mongo_client_context_get_uri(client);
   doc = mongo_bson_new_empty();
   mongo_bson_append_string(doc, "you", str);
   mongo_bson_append_int(doc, "ok", 1);
   mongo_message_set_reply_bson(message, MONGO_REPLY_NONE, doc);
   mongo_bson_unref(doc);
   g_free(str);
}

static void
replSetGetStatus (MongoServer        *server,
                  MongoClientContext *client,
                  MongoMessage       *message)
{
   MongoBson *doc;

   doc = mongo_bson_new_empty();
   mongo_bson_append_string(doc, "$err", "Not part of a replicaSet.");
   mongo_bson_append_int(doc, "ok", 0);
   mongo_message_set_reply_bson(message, MONGO_REPLY_NONE, doc);
   mongo_bson_unref(doc);
}

static void
query_cb (MongoServer        *server,
          MongoClientContext *client,
          MongoMessage       *message,
          gpointer            user_data)
{
   void (*func) (MongoServer*, MongoClientContext*, MongoMessage*);
   const gchar *name;
   GHashTable *commands = user_data;
   MongoQuery *query = (MongoQuery *)message;

   g_assert(MONGO_IS_SERVER(server));
   g_assert(MONGO_IS_QUERY(query));
   g_assert(client);

   if (mongo_query_is_command(query)) {
      name = mongo_query_get_command_name(query);
      if ((func = g_hash_table_lookup(commands, name))) {
         func(server, client, message);
      }
   }
}

gint
main (gint   argc,
      gchar *argv[])
{
   GSocketListener *server;
   GHashTable *commands;
   GMainLoop *main_loop;
   GError *error = NULL;

   g_type_init();

   /*
    * Create a hashtable of available commands that can be executed.
    */
   commands = g_hash_table_new(g_str_hash, g_str_equal);
   g_hash_table_insert(commands, "whatsmyuri", whatsmyuri);
   g_hash_table_insert(commands, "replSetGetStatus", replSetGetStatus);

   /*
    * Create our I/O main loop.
    */
   main_loop = g_main_loop_new(NULL, FALSE);

   /*
    * Create the mongo server instance and attach to the query callback
    * for handling incoming queries and commands.
    */
   server = g_object_new(MONGO_TYPE_SERVER, NULL);
   g_signal_connect(server, "request-query", G_CALLBACK(query_cb), commands);

   /*
    * Start listening on port 5201 and run the main loop.
    */
   g_socket_listener_add_inet_port(server, 5201, NULL, &error);
   g_socket_service_start(G_SOCKET_SERVICE(server));
   g_main_loop_run(main_loop);

   /*
    * Cleanup after ourselves.
    */
   g_object_unref(server);
   g_main_loop_unref(main_loop);
   g_hash_table_unref(commands);

   return 0;
}
