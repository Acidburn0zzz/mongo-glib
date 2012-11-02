#include <mongo-glib/mongo-glib.h>
#include <signal.h>

static GHashTable *gCollections;
static GMainLoop  *gMainLoop;

static void
getlasterror (MongoMessage *message)
{
   MongoBson *b;

   b = mongo_bson_new_empty();
   mongo_bson_append_int(b, "n", 0);
   mongo_bson_append_null(b, "err");
   mongo_bson_append_int(b, "ok", 1);
   mongo_message_set_reply_bson(message, 0, b);
   mongo_bson_unref(b);
}

static void
whatsmyuri (MongoMessage       *message,
            MongoClientContext *client)
{
   MongoBson *b;
   gchar *you;

   you = mongo_client_context_get_uri(client);
   b = mongo_bson_new_empty();
   mongo_bson_append_string(b, "you", you);
   g_free(you);
   mongo_message_set_reply_bson(message, 0, b);
   mongo_bson_unref(b);
}

static void
replSetGetStatus (MongoMessage *message)
{
   MongoBson *b;

   b = mongo_bson_new_empty();
   mongo_bson_append_string(b, "errmsg", "not running with --replSet");
   mongo_bson_append_int(b, "ok", 0);
   mongo_message_set_reply_bson(message, 0, b);
   mongo_bson_unref(b);
}

static gboolean
insert_cb (MongoServer        *server,
           MongoClientContext *client,
           MongoMessage       *message)
{
   MongoMessageInsert *insert = (MongoMessageInsert *)message;
   const gchar *name;
   GPtrArray *ptr;
   GList *list;
   GList *iter;

   g_assert(MONGO_IS_MESSAGE_INSERT(insert));

   name = mongo_message_insert_get_collection(insert);
   if (!(ptr = g_hash_table_lookup(gCollections, name))) {
      ptr = g_ptr_array_new();
      g_ptr_array_set_free_func(ptr, (GDestroyNotify)mongo_bson_unref);
      g_hash_table_insert(gCollections, g_strdup(name), ptr);
   }

   list = mongo_message_insert_get_documents(insert);

   for (iter = list; iter; iter = iter->next) {
      g_ptr_array_add(ptr, mongo_bson_ref(iter->data));
   }

   return TRUE;
}

static gboolean
query_cb (MongoServer        *server,
          MongoClientContext *client,
          MongoMessage       *message)
{
   MongoMessageQuery *query = (MongoMessageQuery *)message;
   MongoMessageReply *reply;
   const gchar *name;
   GPtrArray *ptr;
   GList *list = NULL;
   guint i;

   g_assert(MONGO_IS_MESSAGE_QUERY(query));

   if (mongo_message_query_is_command(query)) {
      name = mongo_message_query_get_command_name(query);
      if (!g_strcmp0(name, "getlasterror")) {
         getlasterror(message);
         return TRUE;
      } else if (!g_strcmp0(name, "whatsmyuri")) {
         whatsmyuri(message, client);
         return TRUE;
      } else if (!g_strcmp0(name, "replSetGetStatus")) {
         replSetGetStatus(message);
         return TRUE;
      }
   }

   name = mongo_message_query_get_collection(query);
   if ((ptr = g_hash_table_lookup(gCollections, name))) {
      reply = g_object_new(MONGO_TYPE_MESSAGE_REPLY, NULL);
      for (i = 0; i < ptr->len; i++) {
         list = g_list_prepend(list, g_ptr_array_index(ptr, i));
      }
      list = g_list_reverse(list);
      mongo_message_reply_set_documents(reply, list);
      g_list_free(list);
      mongo_message_set_reply(message, MONGO_MESSAGE(reply));
      return TRUE;
   }

   return FALSE;
}

static gboolean
getmore_cb (MongoServer        *server,
            MongoClientContext *client,
            MongoMessage       *message)
{
   MongoMessage *reply;

   reply = g_object_new(MONGO_TYPE_MESSAGE_REPLY, NULL);
   mongo_message_set_reply(message, reply);
   g_object_unref(reply);

   return TRUE;
}

static void
sighup_cb (int signum)
{
   g_printerr("SIGHUP; shutting down.\n");
   g_main_loop_quit(gMainLoop);
}

gint
main (gint   argc,
      gchar *argv[])
{
   MongoServer *server;

   g_type_init();

   gCollections = g_hash_table_new_full(g_str_hash,
                                        g_str_equal,
                                        g_free,
                                        (GDestroyNotify)g_ptr_array_unref);

   server = g_object_new(MONGO_TYPE_SERVER, NULL);
   g_signal_connect(server, "request-insert", G_CALLBACK(insert_cb), NULL);
   g_signal_connect(server, "request-query", G_CALLBACK(query_cb), NULL);
   g_signal_connect(server, "request-getmore", G_CALLBACK(getmore_cb), NULL);
   g_socket_listener_add_inet_port(G_SOCKET_LISTENER(server), 5201, NULL, NULL);
   g_socket_service_start(G_SOCKET_SERVICE(server));

   signal(SIGHUP, sighup_cb);

   gMainLoop = g_main_loop_new(NULL, FALSE);
   g_main_loop_run(gMainLoop);

   g_socket_service_stop(G_SOCKET_SERVICE(server));
   g_clear_object(&server);
   g_main_loop_unref(gMainLoop);
   g_hash_table_unref(gCollections);

   gMainLoop = NULL;
   gCollections = NULL;

   return 0;
}
