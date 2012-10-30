/* mongo-server.c
 *
 * Copyright (C) 2012 Christian Hergert <christian@hergert.me>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib/gi18n.h>

#include "mongo-debug.h"
#include "mongo-message.h"
#include "mongo-operation.h"
#include "mongo-query.h"
#include "mongo-server.h"

G_DEFINE_TYPE(MongoServer, mongo_server, G_TYPE_SOCKET_SERVICE)

struct _MongoServerPrivate
{
   GMainContext *async_context;
   GHashTable   *client_contexts;
};

struct _MongoClientContext
{
   volatile gint      ref_count;
   GCancellable      *cancellable;
   GSocketConnection *connection;
   MongoServer       *server;
   guint8            *incoming;

#pragma pack(push, 4)
   struct {
      guint32 msg_len;
      guint32 request_id;
      guint32 response_to;
      guint32 op_code;
   } header;
#pragma pack(pop)
};

enum
{
   PROP_0,
   PROP_ASYNC_CONTEXT,
   LAST_PROP
};

enum
{
   REQUEST_STARTED,
   REQUEST_READ,
   REQUEST_FINISHED,
   REQUEST_MSG,
   REQUEST_UPDATE,
   REQUEST_INSERT,
   REQUEST_QUERY,
   REQUEST_GETMORE,
   REQUEST_DELETE,
   REQUEST_KILL_CURSORS,
   LAST_SIGNAL
};

static GParamSpec *gParamSpecs[LAST_PROP];
static guint       gSignals[LAST_SIGNAL];

extern gboolean
_mongo_message_is_ready (MongoMessage *message);

static MongoClientContext *
mongo_client_context_new (MongoServer       *server,
                          GSocketConnection *connection);
static void
mongo_client_context_dispatch (MongoClientContext *client);

GMainContext *
mongo_server_get_async_context (MongoServer *server)
{
   g_return_val_if_fail(MONGO_IS_SERVER(server), NULL);
   return server->priv->async_context;
}

static void
mongo_server_read_msg_cb (GInputStream *stream,
                          GAsyncResult *result,
                          gpointer      user_data)
{
   MongoClientContext *client = user_data;
   GError *error = NULL;
   gssize ret;

   ENTRY;

   g_assert(G_INPUT_STREAM(stream));
   g_assert(G_IS_ASYNC_RESULT(result));

   ret = g_input_stream_read_finish(stream, result, &error);
   switch (ret) {
   case -1:
      /*
       * TODO: Fail the connection.
       */
      break;
   case 0:
      /*
       * TODO: End of connection, cleanup.
       */
      break;
   default:
      mongo_client_context_dispatch(client);
      break;
   }

   mongo_client_context_unref(client);

   EXIT;
}

static void
mongo_server_read_header_cb (GInputStream *stream,
                             GAsyncResult *result,
                             gpointer      user_data)
{
   MongoClientContext *client = user_data;
   GError *error = NULL;
   gssize ret;

   ENTRY;

   g_assert(G_INPUT_STREAM(stream));
   g_assert(G_IS_ASYNC_RESULT(result));

   ret = g_input_stream_read_finish(stream, result, &error);
   switch (ret) {
   case -1:
      /*
       * TODO: Fail the connection.
       */
      break;
   case 0:
      /*
       * TODO: End of connection, cleanup.
       */
      break;
   case 16:
      client->header.msg_len = GUINT32_FROM_LE(client->header.msg_len);
      client->header.request_id = GUINT32_FROM_LE(client->header.request_id);
      client->header.response_to = GUINT32_FROM_LE(client->header.response_to);
      client->header.op_code = GUINT32_FROM_LE(client->header.op_code);
      if (client->header.msg_len <= 16) {
         /*
          * TODO: Invalid message size, abort.
          */
      }

      g_assert(!client->incoming);

      client->incoming = g_malloc(client->header.msg_len);
      g_input_stream_read_async(stream,
                                client->incoming,
                                client->header.msg_len - sizeof client->header,
                                G_PRIORITY_DEFAULT,
                                client->cancellable,
                                (GAsyncReadyCallback)mongo_server_read_msg_cb,
                                mongo_client_context_ref(client));
      break;
   default:
      /*
       * TODO: Bad read back, close the connection.
       */
      break;
   }

   mongo_client_context_unref(client);

   EXIT;
}

static gboolean
mongo_server_incoming (GSocketService    *service,
                       GSocketConnection *connection,
                       GObject           *source_object)
{
   MongoServerPrivate *priv;
   MongoClientContext *client;
   GInputStream *stream;
   MongoServer *server = (MongoServer *)service;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_SERVER(server), FALSE);
   g_return_val_if_fail(G_IS_SOCKET_CONNECTION(connection), FALSE);

   priv = server->priv;

   /*
    * Store the client context for tracking things like cursors, last
    * operation id, and error code, for each client. By keeping this
    * information in a per-client structure, we can avoid likelihood of
    * leaking information from one client to another (such as cursor
    * ids of the other clients).
    */
   client = mongo_client_context_new(server, connection);
   g_hash_table_insert(priv->client_contexts, connection, client);

   /*
    * Start listening for the first 16 bytes containing the message
    * header. Once we have this data, we can start processing an incoming
    * message.
    */
   stream = g_io_stream_get_input_stream(G_IO_STREAM(connection));
   g_input_stream_read_async(stream,
                             (guint8 *)&client->header,
                             sizeof client->header,
                             G_PRIORITY_DEFAULT,
                             client->cancellable,
                             (GAsyncReadyCallback)mongo_server_read_header_cb,
                             mongo_client_context_ref(client));

   RETURN(TRUE);
}

static void
mongo_server_set_async_context (MongoServer  *server,
                                GMainContext *async_context)
{
   MongoServerPrivate *priv;

   g_return_if_fail(MONGO_IS_SERVER(server));

   priv = server->priv;

   if (priv->async_context) {
      g_main_context_unref(priv->async_context);
      priv->async_context = NULL;
   }

   if (async_context) {
      priv->async_context = g_main_context_ref(async_context);
   }

   g_object_notify_by_pspec(G_OBJECT(server),
                            gParamSpecs[PROP_ASYNC_CONTEXT]);
}

static void
mongo_server_finalize (GObject *object)
{
   MongoServerPrivate *priv;

   priv = MONGO_SERVER(object)->priv;

   if (priv->async_context) {
      g_main_context_unref(priv->async_context);
      priv->async_context = NULL;
   }

   G_OBJECT_CLASS(mongo_server_parent_class)->finalize(object);
}

static void
mongo_server_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
   MongoServer *server = MONGO_SERVER(object);

   switch (prop_id) {
   case PROP_ASYNC_CONTEXT:
      g_value_set_boxed(value, mongo_server_get_async_context(server));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_server_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
   MongoServer *server = MONGO_SERVER(object);

   switch (prop_id) {
   case PROP_ASYNC_CONTEXT:
      mongo_server_set_async_context(server, g_value_get_boxed(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_server_class_init (MongoServerClass *klass)
{
   GObjectClass *object_class;
   GSocketServiceClass *service_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_server_finalize;
   object_class->get_property = mongo_server_get_property;
   object_class->set_property = mongo_server_set_property;
   g_type_class_add_private(object_class, sizeof(MongoServerPrivate));

   service_class = G_SOCKET_SERVICE_CLASS(klass);
   service_class->incoming = mongo_server_incoming;

   /**
    * MongoServer:async-context:
    *
    * The "async-context" property is the #GMainContext that is used to
    * dispatch events to callbacks.
    */
   gParamSpecs[PROP_ASYNC_CONTEXT] =
      g_param_spec_boxed("async-context",
                         _("Async Context"),
                         _("The main context for callback execution."),
                         G_TYPE_MAIN_CONTEXT,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_ASYNC_CONTEXT,
                                   gParamSpecs[PROP_ASYNC_CONTEXT]);

   gSignals[REQUEST_STARTED] =
      g_signal_new("request-started",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_started),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_READ] =
      g_signal_new("request-read",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_read),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_FINISHED] =
      g_signal_new("request-finished",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_finished),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_MSG] =
      g_signal_new("request-msg",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_msg),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_UPDATE] =
      g_signal_new("request-update",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_update),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_INSERT] =
      g_signal_new("request-insert",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_insert),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_QUERY] =
      g_signal_new("request-query",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_query),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_GETMORE] =
      g_signal_new("request-getmore",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_getmore),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_DELETE] =
      g_signal_new("request-delete",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_delete),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);

   gSignals[REQUEST_KILL_CURSORS] =
      g_signal_new("request-kill_cursors",
                   MONGO_TYPE_SERVER,
                   G_SIGNAL_RUN_FIRST,
                   G_STRUCT_OFFSET(MongoServerClass, request_kill_cursors),
                   NULL,
                   NULL,
                   g_cclosure_marshal_generic,
                   G_TYPE_NONE,
                   2,
                   MONGO_TYPE_CLIENT_CONTEXT,
                   MONGO_TYPE_MESSAGE);
}

static void
mongo_server_init (MongoServer *server)
{
   server->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(server,
                                  MONGO_TYPE_SERVER,
                                  MongoServerPrivate);
   server->priv->client_contexts =
      g_hash_table_new_full(g_direct_hash,
                            g_direct_equal,
                            NULL,
                            (GDestroyNotify)mongo_client_context_unref);
}

static void
mongo_client_context_dispose (MongoClientContext *context)
{
   ENTRY;

   g_clear_object(&context->cancellable);

   if (context->server) {
      g_object_remove_weak_pointer(G_OBJECT(context->server),
                                   (gpointer *)&context->server);
   }

   EXIT;
}

static MongoClientContext *
mongo_client_context_new (MongoServer       *server,
                          GSocketConnection *connection)
{
   MongoClientContext *context;

   ENTRY;
   context = g_slice_new0(MongoClientContext);
   context->ref_count = 1;
   context->cancellable = g_cancellable_new();
   context->server = server;
   context->connection = g_object_ref(connection);
   g_object_add_weak_pointer(G_OBJECT(context->server),
                             (gpointer *)&context->server);
   RETURN(context);
}

static void
mongo_client_context_dispatch (MongoClientContext *client)
{
   MongoMessage *message;
   guint8 *data;
   gsize data_len;
   GType type_id = G_TYPE_NONE;
   guint signal = 0;

   ENTRY;

   g_assert(client);

   data = client->incoming;
   data_len = client->header.msg_len - sizeof client->header;
   client->incoming = NULL;

   g_assert(data);
   g_assert(data_len);

   switch (client->header.op_code) {
   case MONGO_OPERATION_REPLY:
      /*
       * TODO: Fail the connection, shouldn't send replies to a server.
       */
      break;
   case MONGO_OPERATION_MSG:
      signal = gSignals[REQUEST_MSG];
      break;
   case MONGO_OPERATION_UPDATE:
      signal = gSignals[REQUEST_UPDATE];
      break;
   case MONGO_OPERATION_INSERT:
      signal = gSignals[REQUEST_INSERT];
      break;
   case MONGO_OPERATION_QUERY:
      signal = gSignals[REQUEST_QUERY];
      type_id = MONGO_TYPE_QUERY;
      break;
   case MONGO_OPERATION_GETMORE:
      signal = gSignals[REQUEST_GETMORE];
      break;
   case MONGO_OPERATION_DELETE:
      signal = gSignals[REQUEST_DELETE];
      break;
   case MONGO_OPERATION_KILL_CURSORS:
      signal = gSignals[REQUEST_KILL_CURSORS];
      break;
   default:
      /*
       * TODO: Fail the connection for protocol failure.
       */
      break;
   }

   message = g_object_new(type_id,
                          "request-id", client->header.request_id,
                          "response-to", client->header.response_to,
                          NULL);

   /*
    * Let the message subclass load the data.
    */
   if (!mongo_message_load_from_data(message, data, data_len)) {
      /*
       * TODO: Fail the connection for protocol failure.
       */
      g_print("Failed to load.\n");
   }

   /*
    * Emit request signals for this request.
    */
   g_signal_emit(client->server, gSignals[REQUEST_READ], 0, client, message);
   g_signal_emit(client->server, signal, 0, client, message);

   /*
    * TODO: If the message is not paused, we probably need to send the reply
    *       and emit the FINISHED. Finished might get emitted elsewhere in
    *       the send path, however.
    */

   if (_mongo_message_is_ready(message)) {
      g_print("Ready to send a reply.\n");
   }

   g_object_unref(message);
   g_free(data);

   EXIT;
}

gchar *
mongo_client_context_get_peer (MongoClientContext *client)
{
   GInetSocketAddress *saddr;
   GSocketAddress *addr;
   GInetAddress *iaddr;
   gchar *str;
   gchar *str2;
   guint port;

   g_assert(client);

   if (!(addr = g_socket_connection_get_remote_address(client->connection,
                                                       NULL))) {
      return NULL;
   }

   if ((saddr = G_INET_SOCKET_ADDRESS(addr))) {
      iaddr = g_inet_socket_address_get_address(saddr);
      str = g_inet_address_to_string(iaddr);
      port = g_inet_socket_address_get_port(saddr);
      str2 = g_strdup_printf("%s:%u", str, port);
      g_free(str);
      return str2;
   }

   /*
    * TODO: Support socket addresses.
    */

   return NULL;
}

/**
 * MongoClientContext_ref:
 * @context: A #MongoClientContext.
 *
 * Atomically increments the reference count of @context by one.
 *
 * Returns: (transfer full): A reference to @context.
 */
MongoClientContext *
mongo_client_context_ref (MongoClientContext *context)
{
   g_return_val_if_fail(context != NULL, NULL);
   g_return_val_if_fail(context->ref_count > 0, NULL);

   ENTRY;
   g_atomic_int_inc(&context->ref_count);
   RETURN(context);
}

/**
 * mongo_client_context_unref:
 * @context: A MongoClientContext.
 *
 * Atomically decrements the reference count of @context by one.  When the
 * reference count reaches zero, the structure will be destroyed and
 * freed.
 */
void
mongo_client_context_unref (MongoClientContext *context)
{
   g_return_if_fail(context != NULL);
   g_return_if_fail(context->ref_count > 0);

   ENTRY;
   if (g_atomic_int_dec_and_test(&context->ref_count)) {
      mongo_client_context_dispose(context);
      g_slice_free(MongoClientContext, context);
   }
   EXIT;
}

GType
mongo_client_context_get_type (void)
{
   static gsize initialized;
   static GType type_id;

   if (g_once_init_enter(&initialized)) {
      type_id = g_boxed_type_register_static(
            "MongoClientContext",
            (GBoxedCopyFunc)mongo_client_context_ref,
            (GBoxedFreeFunc)mongo_client_context_unref);
      g_once_init_leave(&initialized, TRUE);
   }

   return type_id;
}
