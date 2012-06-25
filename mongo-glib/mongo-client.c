/* mongo-client.c
 *
 * Copyright (C) 2012 Christian Hergert <chris@dronelabs.com>
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

#include "mongo-client.h"
#include "mongo-debug.h"
#include "mongo-protocol.h"
#include "mongo-source.h"

G_DEFINE_TYPE(MongoClient, mongo_client, G_TYPE_OBJECT)

#ifndef MONGO_PORT_DEFAULT
#define MONGO_PORT_DEFAULT 27017
#endif

struct _MongoClientPrivate
{
   GHashTable *databases;

   GSocketClient *socket_client;
   MongoProtocol *protocol;

   GCancellable *dispose_cancel;

   guint state;

   GQueue *queue;

   gchar *replica_set;
   gboolean slave_okay;
   guint connect_timeout_ms;
};

typedef struct
{
   MongoOperation oper;
   GSimpleAsyncResult *simple;
   GCancellable *cancellable;
   union {
      struct {
         gchar *db_and_collection;
         MongoUpdateFlags flags;
         MongoBson *selector;
         MongoBson *update;
      } update;
      struct {
         gchar *db_and_collection;
         MongoInsertFlags flags;
         GPtrArray *documents;
      } insert;
      struct {
         gchar *db_and_collection;
         MongoQueryFlags flags;
         guint32 skip;
         guint32 limit;
         MongoBson *query;
         MongoBson *field_selector;
      } query;
      struct {
         gchar *db_and_collection;
         guint32 limit;
         guint64 cursor_id;
      } getmore;
      struct {
         gchar *db_and_collection;
         MongoDeleteFlags flags;
         MongoBson *selector;
      } delete;
      struct {
         GArray *cursors;
      } kill_cursors;
   } u;
} Request;

enum
{
   PROP_0,
   PROP_REPLICA_SET,
   PROP_SLAVE_OKAY,
   LAST_PROP
};

enum
{
   STATE_0,
   STATE_CONNECTING,
   STATE_CONNECTED,
   STATE_DISPOSED
};

static GParamSpec *gParamSpecs[LAST_PROP];

static void
mongo_client_update_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoProtocol *protocol = (MongoProtocol *)object;
   gboolean ret;
   GError *error = NULL;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(ret = mongo_protocol_update_finish(protocol, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, ret);
   mongo_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

static void
mongo_client_insert_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoProtocol *protocol = (MongoProtocol *)object;
   gboolean ret;
   GError *error = NULL;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(ret = mongo_protocol_insert_finish(protocol, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, ret);
   mongo_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

static void
mongo_client_query_cb (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoProtocol *protocol = (MongoProtocol *)object;
   MongoReply *reply;
   GError *error = NULL;

   g_assert(MONGO_IS_PROTOCOL(protocol));
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(reply = mongo_protocol_query_finish(protocol, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   } else {
      g_simple_async_result_set_op_res_gpointer(
            simple, reply, (GDestroyNotify)mongo_reply_unref);
   }

   mongo_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

static void
mongo_client_getmore_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoProtocol *protocol = (MongoProtocol *)object;
   MongoReply *reply;
   GError *error = NULL;

   ENTRY;

   g_assert(MONGO_IS_PROTOCOL(protocol));
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(reply = mongo_protocol_getmore_finish(protocol, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   } else {
      g_simple_async_result_set_op_res_gpointer(
            simple, reply, (GDestroyNotify)mongo_reply_unref);
   }

   mongo_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

static void
mongo_client_delete_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoProtocol *protocol = (MongoProtocol *)object;
   gboolean ret;
   GError *error = NULL;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(ret = mongo_protocol_delete_finish(protocol, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, ret);
   mongo_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

static void
mongo_client_kill_cursors_cb (GObject      *object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoProtocol *protocol = (MongoProtocol *)object;
   gboolean ret;
   GError *error = NULL;

   ENTRY;

   g_assert(MONGO_IS_PROTOCOL(protocol));
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(ret = mongo_protocol_kill_cursors_finish(protocol, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, ret);
   mongo_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

static void
request_fail (Request      *request,
              const GError *error)
{
   g_simple_async_result_take_error(request->simple, g_error_copy(error));
   g_simple_async_result_complete_in_idle(request->simple);
}

static void
request_run (Request       *request,
             MongoProtocol *protocol)
{
   switch (request->oper) {
   case MONGO_OPERATION_UPDATE:
      mongo_protocol_update_async(
            protocol,
            request->u.update.db_and_collection,
            request->u.update.flags,
            request->u.update.selector,
            request->u.update.update,
            request->cancellable,
            mongo_client_update_cb,
            g_object_ref(request->simple));
      break;
   case MONGO_OPERATION_INSERT:
      mongo_protocol_insert_async(
            protocol,
            request->u.insert.db_and_collection,
            request->u.insert.flags,
            (MongoBson **)request->u.insert.documents->pdata,
            request->u.insert.documents->len,
            request->cancellable,
            mongo_client_insert_cb,
            g_object_ref(request->simple));
      break;
   case MONGO_OPERATION_QUERY:
      mongo_protocol_query_async(
            protocol,
            request->u.query.db_and_collection,
            request->u.query.flags,
            request->u.query.skip,
            request->u.query.limit,
            request->u.query.query,
            request->u.query.field_selector,
            request->cancellable,
            mongo_client_query_cb,
            g_object_ref(request->simple));
      break;
   case MONGO_OPERATION_GETMORE:
      mongo_protocol_getmore_async(
            protocol,
            request->u.getmore.db_and_collection,
            request->u.getmore.limit,
            request->u.getmore.cursor_id,
            request->cancellable,
            mongo_client_getmore_cb,
            g_object_ref(request->simple));
      break;
   case MONGO_OPERATION_DELETE:
      mongo_protocol_delete_async(
            protocol,
            request->u.delete.db_and_collection,
            request->u.delete.flags,
            request->u.delete.selector,
            request->cancellable,
            mongo_client_delete_cb,
            g_object_ref(request->simple));
      break;
   case MONGO_OPERATION_KILL_CURSORS:
      mongo_protocol_kill_cursors_async(
            protocol,
            (guint64 *)request->u.kill_cursors.cursors->data,
            request->u.kill_cursors.cursors->len,
            request->cancellable,
            mongo_client_kill_cursors_cb,
            g_object_ref(request->simple));
      break;
   case MONGO_OPERATION_REPLY:
   case MONGO_OPERATION_MSG:
   default:
      g_assert_not_reached();
      break;
   }
}

static Request *
request_new (gpointer             source,
             GCancellable        *cancellable,
             GAsyncReadyCallback  callback,
             gpointer             user_data,
             gpointer             tag)
{
   Request *request;

   g_assert(G_IS_OBJECT(source));
   g_assert(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_assert(callback);
   g_assert(user_data);
   g_assert(tag);

   request = g_slice_new0(Request);
   request->cancellable = cancellable ? g_object_ref(cancellable) : NULL;
   request->simple = g_simple_async_result_new(source,
                                               callback,
                                               user_data,
                                               tag);
   g_simple_async_result_set_check_cancellable(request->simple,
                                               cancellable);

   return request;
}

static void
request_free (Request *request)
{
   if (request) {
      g_clear_object(&request->simple);
      g_clear_object(&request->cancellable);
      switch (request->oper) {
      case MONGO_OPERATION_UPDATE:
         g_free(request->u.update.db_and_collection);
         if (request->u.update.selector) {
            mongo_bson_unref(request->u.update.selector);
         }
         if (request->u.update.update) {
            mongo_bson_unref(request->u.update.update);
         }
         break;
      case MONGO_OPERATION_INSERT:
         g_free(request->u.insert.db_and_collection);
         g_ptr_array_free(request->u.insert.documents, TRUE);
         break;
      case MONGO_OPERATION_QUERY:
         g_free(request->u.insert.db_and_collection);
         if (request->u.query.query) {
            mongo_bson_unref(request->u.query.query);
         }
         if (request->u.query.field_selector) {
            mongo_bson_unref(request->u.query.field_selector);
         }
         break;
      case MONGO_OPERATION_GETMORE:
         g_free(request->u.insert.db_and_collection);
         break;
      case MONGO_OPERATION_DELETE:
         g_free(request->u.insert.db_and_collection);
         if (request->u.delete.selector) {
            mongo_bson_unref(request->u.delete.selector);
         }
         break;
      case MONGO_OPERATION_KILL_CURSORS:
         g_array_free(request->u.kill_cursors.cursors, TRUE);
         break;
      case MONGO_OPERATION_REPLY:
      case MONGO_OPERATION_MSG:
      default:
         g_assert_not_reached();
         break;
      }
      memset(&request->u, 0, sizeof request->u);
      g_slice_free(Request, request);
   }
}

static void
mongo_client_protocol_notify_failure (MongoProtocol *protocol,
                                      GParamSpec    *pspec,
                                      MongoClient   *client)
{
   MongoClientPrivate *priv;

   ENTRY;

   g_assert(MONGO_IS_PROTOCOL(protocol));
   g_assert(MONGO_IS_CLIENT(client));

   priv = client->priv;

   /*
    * TODO: Clear the protocol so we can connect to the next host.
    */

   priv->state = STATE_0;
   g_clear_object(&priv->protocol);

   EXIT;
}

static void
mongo_client_ismaster_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
   MongoClientPrivate *priv;
   MongoProtocol *protocol = (MongoProtocol *)object;
   MongoBsonIter iter;
   MongoBsonIter iter2;
   MongoClient *client = user_data;
   const gchar *host;
   const gchar *primary;
   const gchar *replica_set;
   MongoReply *reply;
   gboolean ismaster = FALSE;
   Request *request;
   GError *error = NULL;

   ENTRY;

   g_assert(MONGO_IS_PROTOCOL(protocol));
   g_assert(MONGO_IS_CLIENT(client));

   priv = client->priv;

   if (!(reply = mongo_protocol_query_finish(protocol, result, &error))) {
      /*
       * TODO: Failed to query? log error somewhere?
       * TODO: Start the connection loop over again?
       */
      g_error_free(error);
      EXIT;
   }

   if (!reply->n_returned) {
      /*
       * TODO: No result document back, handle error?
       */
      mongo_reply_unref(reply);
      EXIT;
   }

   /*
    * Make sure we got a valid response back.
    */
   mongo_bson_iter_init(&iter, reply->documents[0]);
   if (mongo_bson_iter_find(&iter, "ok")) {
      if (!mongo_bson_iter_get_value_boolean(&iter)) {
         GOTO(failure);
      }
   }

   /*
    * Check to see if this host contains the repl set name.
    * If so, verify that it is the one we should be talking to.
    */
   if (priv->replica_set) {
      mongo_bson_iter_init(&iter, reply->documents[0]);
      if (mongo_bson_iter_find(&iter, "setName")) {
         replica_set = mongo_bson_iter_get_value_string(&iter, NULL);
         if (!!g_strcmp0(replica_set, priv->replica_set)) {
            /*
             * This is not our expected replica set!
             */
            GOTO(failure);
         }
      }
   }

   /*
    * Update who we think the primary is now so that we can reconnect
    * if this isn't the primary.
    */
   mongo_bson_iter_init(&iter, reply->documents[0]);
   if (mongo_bson_iter_find(&iter, "primary")) {
      primary = mongo_bson_iter_get_value_string(&iter, NULL);
      g_message("Node things %s is PRIMARY.", primary);
   }

   /*
    * Update who we think is in the list of nodes for this replica set
    * so that we can iterate through them upon inability to find master.
    */
   mongo_bson_iter_init(&iter, reply->documents[0]);
   if (mongo_bson_iter_find(&iter, "hosts")) {
      mongo_bson_iter_init(&iter2, mongo_bson_iter_get_value_bson(&iter));
      while (mongo_bson_iter_next(&iter2)) {
         host = mongo_bson_iter_get_value_string(&iter2, NULL);
         /*
          * TODO: Add the host to be connected to.
          */
         g_message("Found host in replSet: %s", host);
      }
   }

   /*
    * Check to see if this host is PRIMARY.
    */
   mongo_bson_iter_init(&iter, reply->documents[0]);
   if (mongo_bson_iter_find(&iter, "ismaster")) {
      if (!(ismaster = mongo_bson_iter_get_value_boolean(&iter))) {
         /*
          * TODO: Anything we need to do here?
          */
         g_message("Connected to PRIMARY.");
      }
   }

   /*
    * This is the master and we are connected, so lets store the
    * protocol for use by the client and change the state to
    * connected.
    */
   priv->protocol = g_object_ref(protocol);
   priv->state = STATE_CONNECTED;

   /*
    * Wire up failure of the protocol so that we can connect to
    * the next host.
    */
   g_signal_connect(protocol, "notify::failure",
                    G_CALLBACK(mongo_client_protocol_notify_failure),
                    client);

   /*
    * Flush any pending requests.
    */
   while ((priv->state == STATE_CONNECTED) &&
          (priv->protocol) &&
          (request = g_queue_pop_head(priv->queue))) {
      request_run(request, priv->protocol);
      request_free(request);
   }

failure:
   mongo_reply_unref(reply);
}

static void
mongo_client_connect_to_host_cb (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      user_data)
{
   MongoClientPrivate *priv;
   GSocketConnection *conn;
   GSocketClient *socket_client = (GSocketClient *)object;
   MongoProtocol *protocol;
   MongoClient *client = user_data;
   MongoBson *command;
   GError *error = NULL;
   gchar *host_and_port = NULL;

   ENTRY;

   g_assert(G_IS_SOCKET_CLIENT(socket_client));
   g_assert(MONGO_IS_CLIENT(client));

   priv = client->priv;

   /*
    * Complete the asynchronous connection attempt.
    */
   if (!(conn = g_socket_client_connect_to_host_finish(socket_client,
                                                       result,
                                                       &error))) {
      /*
       * TODO: Get the next host to try.
       *       Update exponential backoff?
       */
      g_socket_client_connect_to_host_async(priv->socket_client,
                                            host_and_port,
                                            MONGO_PORT_DEFAULT,
                                            priv->dispose_cancel,
                                            mongo_client_connect_to_host_cb,
                                            client);
      /*
       * XXX: Log error somewhere?
       */
      g_error_free(error);

      EXIT;
   }

   /*
    * Build a protocol using our connection. We then need to check that
    * the server is PRIMARY and matches our requested replica set.
    */
   protocol = g_object_new(MONGO_TYPE_PROTOCOL,
                           "io-stream", conn,
                           NULL);
   command = mongo_bson_new_empty();
   mongo_bson_append_int(command, "ismaster", 1);
   mongo_protocol_query_async(protocol,
                              "admin.$cmd",
                              MONGO_QUERY_EXHAUST,
                              0,
                              1,
                              command,
                              NULL,
                              priv->dispose_cancel,
                              mongo_client_ismaster_cb,
                              client);
   mongo_bson_unref(command);
   g_object_unref(protocol);
   g_object_unref(conn);

   EXIT;
}

static void
mongo_client_start_connecting (MongoClient *client)
{
   MongoClientPrivate *priv;
   const gchar *host_and_port = "localhost:27017";

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));

   priv = client->priv;

   g_assert(priv->state == STATE_0);

   /*
    * TODO: Get the next host:port pair to connect to.
    */

   g_socket_client_connect_to_host_async(priv->socket_client,
                                         host_and_port,
                                         MONGO_PORT_DEFAULT,
                                         priv->dispose_cancel,
                                         mongo_client_connect_to_host_cb,
                                         client);

   EXIT;
}

static void
mongo_client_queue (MongoClient *client,
                    Request     *request)
{
   MongoClientPrivate *priv;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(request);

   priv = client->priv;

   switch (priv->state) {
   case STATE_0:
      g_queue_push_tail(priv->queue, request);
      mongo_client_start_connecting(client);
      break;
   case STATE_CONNECTING:
      g_queue_push_tail(priv->queue, request);
      break;
   case STATE_CONNECTED:
      request_run(request, priv->protocol);
      request_free(request);
      break;
   case STATE_DISPOSED:
   default:
      g_assert_not_reached();
      break;
   }
}

/**
 * mongo_client_new:
 *
 * Creates a new instance of #MongoClient which can be freed with
 * g_object_unref(). See mongo_client_add_seed() to add a single or more
 * hosts to connect to.
 *
 * Returns: A newly allocated #MongoClient.
 */
MongoClient *
mongo_client_new (void)
{
   MongoClient *ret;

   ENTRY;
   ret = g_object_new(MONGO_TYPE_CLIENT, NULL);
   RETURN(ret);
}

/**
 * mongo_client_get_database:
 * @client: (in): A #MongoClient.
 * @name: (in): The database name.
 *
 * Fetches a #MongoDatabase for the database available via @client.
 *
 * Returns: (transfer none): #MongoDatabase.
 */
MongoDatabase *
mongo_client_get_database (MongoClient *client,
                           const gchar *name)
{
   MongoClientPrivate *priv;
   MongoDatabase *database;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), NULL);
   g_return_val_if_fail(name, NULL);

   priv = client->priv;

   if (!(database = g_hash_table_lookup(priv->databases, name))) {
      database = g_object_new(MONGO_TYPE_DATABASE,
                              "client", client,
                              "name", name,
                              NULL);
      g_hash_table_insert(priv->databases, g_strdup(name), database);
   }

   RETURN(database);
}

static void
mongo_client_command_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoBsonIter iter;
   MongoClient *client = (MongoClient *)object;
   const gchar *errmsg;
   MongoReply *reply = NULL;
   GError *error = NULL;

   ENTRY;

   g_assert(MONGO_IS_CLIENT(client));
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));

   /*
    * Get the query reply, which may contain an error document.
    */
   if (!(reply = mongo_client_command_finish(client, result, &error))) {
      g_simple_async_result_take_error(simple, error);
      GOTO(finish);
   }

   /*
    * Check to see if the command provided a failure document.
    */
   if (reply->n_returned) {
      g_assert(reply->documents);
      g_assert(reply->documents[0]);

      mongo_bson_iter_init(&iter, reply->documents[0]);
      if (mongo_bson_iter_find(&iter, "ok")) {
         if (!mongo_bson_iter_get_value_boolean(&iter)) {
            mongo_bson_iter_init(&iter, reply->documents[0]);
            if (mongo_bson_iter_find(&iter, "errmsg") &&
                mongo_bson_iter_get_value_type(&iter) == MONGO_BSON_UTF8) {
               errmsg = mongo_bson_iter_get_value_string(&iter, NULL);
               g_simple_async_result_set_error(
                     simple,
                     MONGO_CLIENT_ERROR,
                     MONGO_CLIENT_ERROR_COMMAND_FAILED,
                     _("Command failed: %s"),
                     errmsg);
            } else {
               g_simple_async_result_set_error(
                     simple,
                     MONGO_CLIENT_ERROR,
                     MONGO_CLIENT_ERROR_COMMAND_FAILED,
                     _("Command failed."));
            }
            GOTO(finish);
         }
      }
   }

   g_simple_async_result_set_op_res_gpointer(
         simple, mongo_reply_ref(reply),
         (GDestroyNotify)mongo_reply_unref);

finish:
   mongo_simple_async_result_complete_in_idle(simple);
   if (reply) {
      mongo_reply_unref(reply);
   }
   g_object_unref(simple);

   EXIT;
}

/**
 * mongo_client_command_async:
 * @client: A #MongoClient.
 * @db: The database execute the command within.
 * @command: (transfer none): A #MongoBson containing the command.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback.
 * @user_data: (allow-none): User data for @callback.
 *
 * Asynchronously requests that a command is executed on the remote Mongo
 * server.
 *
 * @callback MUST execute mongo_client_command_finish().
 */
void
mongo_client_command_async (MongoClient         *client,
                            const gchar         *db,
                            const MongoBson     *command,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
   GSimpleAsyncResult *simple;
   gchar *db_and_cmd;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db);
   g_return_if_fail(command);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_command_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);
   db_and_cmd = g_strdup_printf("%s.$cmd", db);
   mongo_client_query_async(client,
                            db_and_cmd,
                            MONGO_QUERY_EXHAUST,
                            0,
                            1,
                            command,
                            NULL,
                            cancellable,
                            mongo_client_command_cb,
                            simple);
   g_free(db_and_cmd);

   EXIT;
}

/**
 * mongo_client_command_finish:
 * @client: A #MongoClient.
 * @result: A #GAsyncResult.
 * @error: (out) (allow-none): A location for a #GError, or %NULL.
 *
 * Completes an asynchronous request to execute a command on a remote
 * Mongo server. Upon failure, %NULL is returned and @error is set.
 *
 * Returns: (transfer full): A #MongoReply if successful; otherwise %NULL.
 */
MongoReply *
mongo_client_command_finish (MongoClient   *client,
                             GAsyncResult  *result,
                             GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   MongoReply *ret;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gpointer(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   ret = ret ? mongo_reply_ref(ret) : NULL;

   RETURN(ret);
}

/**
 * mongo_client_delete_async:
 * @client: (in): A #MongoClient.
 * @db_and_collection: A string containing the "db.collection".
 * @flags: A bitwise-or of #MongoDeleteFlag.
 * @selector: A #MongoBson of fields to select for deletion.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to execute upon completion.
 * @user_data: (allow-none): User data for @callback.
 *
 * Asynchronously requests the removal of one or more documents in a Mongo
 * collection. If you only want to remove a single document from the Mongo
 * collection, then you MUST specify the %MONGO_DELETE_SINGLE_REMOVE flag
 * in @flags.
 *
 * Selector should be a #MongoBson containing the fields to match.
 *
 * @callback MUST call mongo_client_delete_finish().
 */
void
mongo_client_delete_async (MongoClient         *client,
                           const gchar         *db_and_collection,
                           MongoDeleteFlags     flags,
                           const MongoBson     *selector,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
   Request *request;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(strstr(db_and_collection, "."));
   g_return_if_fail(selector);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   request = request_new(client, cancellable, callback, user_data,
                         mongo_client_delete_async);
   request->oper = MONGO_OPERATION_DELETE;
   request->u.delete.db_and_collection = g_strdup(db_and_collection);
   request->u.delete.flags = flags;
   request->u.delete.selector = mongo_bson_dup(selector);
   mongo_client_queue(client, request);

   EXIT;
}

/**
 * mongo_client_delete_finish:
 * @client: A #MongoClient.
 * @result: A #GAsyncResult.
 * @error: (out) (allow-none): A location for a #GError, or %NULL.
 *
 * Completes an asynchronous request to remove one or more documents from a
 * Mongo collection.
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set.
 */
gboolean
mongo_client_delete_finish (MongoClient   *client,
                            GAsyncResult  *result,
                            GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   ENTRY;

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

/**
 * mongo_client_update_async:
 * @client: A #MongoClient.
 * @db_and_collection: A string containing the "db.collection".
 * @flags: A bitwise-or of #MongoUpdateFlag.
 * @selector: (allow-none): A #MongoBson or %NULL.
 * @update: A #MongoBson to apply as an update to documents matching @selector.
 * @cancellable: (allow-none): A #GCancellable, or %NULL.
 * @callback: A #GAsyncReadyCallback.
 * @user_data: (allow-none): User data for @callback.
 *
 * Asynchronously requests an update to all documents matching @selector.
 *
 * @callback MUST call mongo_client_update_finish().
 */
void
mongo_client_update_async (MongoClient         *client,
                           const gchar         *db_and_collection,
                           MongoUpdateFlags     flags,
                           const MongoBson     *selector,
                           const MongoBson     *update,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
   Request *request;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(strstr(db_and_collection, "."));
   g_return_if_fail(selector);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   request = request_new(client, cancellable, callback, user_data,
                         mongo_client_update_async);
   request->oper = MONGO_OPERATION_UPDATE;
   request->u.update.db_and_collection = g_strdup(db_and_collection);
   request->u.update.flags = flags;
   request->u.update.selector = mongo_bson_dup(selector);
   request->u.update.update = mongo_bson_dup(update);
   mongo_client_queue(client, request);

   EXIT;
}

/**
 * mongo_client_update_finish:
 * @client: A #MongoClient.
 * @result: A #GAsyncResult.
 * @error: (allow-none) (out): A location for a #GError, or %NULL.
 *
 * Completes an asynchronous request to mongo_client_update_async().
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set.
 */
gboolean
mongo_client_update_finish (MongoClient   *client,
                            GAsyncResult  *result,
                            GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   ENTRY;

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

/**
 * mongo_client_insert_async:
 * @client: A #MongoClient.
 * @db_and_collection: A string containing the "db.collection".
 * @flags: A bitwise-or of #MongoInsertFlags.
 * @documents: (array length=n_documents) (element-type MongoBson): Array  of
 * #MongoBson documents to insert.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback.
 * @user_data: (allow-none): User data for @callback.
 *
 * Asynchronously requests the insertion of a document into the Mongo server.
 *
 * @callback MUST call mongo_client_insert_finish().
 */
void
mongo_client_insert_async (MongoClient          *client,
                           const gchar          *db_and_collection,
                           MongoInsertFlags      flags,
                           MongoBson           **documents,
                           gsize                 n_documents,
                           GCancellable         *cancellable,
                           GAsyncReadyCallback   callback,
                           gpointer              user_data)
{
   Request *request;
   guint i;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(strstr(db_and_collection, "."));
   g_return_if_fail(documents);
   g_return_if_fail(n_documents);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   request = request_new(client, cancellable, callback, user_data,
                         mongo_client_insert_async);
   request->oper = MONGO_OPERATION_INSERT;
   request->u.insert.db_and_collection = g_strdup(db_and_collection);
   request->u.insert.flags = flags;
   request->u.insert.documents = g_ptr_array_sized_new(n_documents);
   g_ptr_array_set_free_func(request->u.insert.documents,
                             (GDestroyNotify)mongo_bson_unref);
   for (i = 0; i < n_documents; i++) {
      g_ptr_array_add(request->u.insert.documents,
                      mongo_bson_dup(documents[i]));
   }
   mongo_client_queue(client, request);

   EXIT;
}

/**
 * mongo_client_insert_finish:
 * @client: A #MongoClient.
 * @result: A #GAsyncResult.
 * @error: (out) (allow-none): A location for a #GError, or %NULL.
 *
 * Completes an asychronous request to insert a document.
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set.
 */
gboolean
mongo_client_insert_finish (MongoClient   *client,
                            GAsyncResult  *result,
                            GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   ENTRY;

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

/**
 * mongo_client_query_async:
 * @client: A #MongoClient.
 * @db_and_collection: A string containing "db.collection".
 * @flags: A bitwise-or of #MongoQueryFlags.
 * @skip: The number of documents to skip in the result set.
 * @limit: The maximum number of documents to retrieve.
 * @query: (allow-none): A #MongoBson containing the query.
 * @field_selector: (allow-none): A #MongoBson describing requested fields.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback.
 * @user_data: (allow-none): User data for @callback.
 *
 * Asynchronously queries Mongo for the documents that match. This retrieves
 * the first reply from the server side cursor. Further replies can be
 * retrieved with mongo_client_getmore_async().
 *
 * @callback MUST call mongo_client_query_finish().
 */
void
mongo_client_query_async (MongoClient         *client,
                          const gchar         *db_and_collection,
                          MongoQueryFlags      flags,
                          guint32              skip,
                          guint32              limit,
                          const MongoBson     *query,
                          const MongoBson     *field_selector,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
   MongoClientPrivate *priv;
   Request *request;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (priv->slave_okay) {
      flags |= MONGO_QUERY_SLAVE_OK;
   }

   request = request_new(client, cancellable, callback, user_data,
                         mongo_client_query_async);
   request->oper = MONGO_OPERATION_QUERY;
   request->u.query.db_and_collection = g_strdup(db_and_collection);
   request->u.query.flags = flags;
   request->u.query.skip = skip;
   request->u.query.limit = limit;
   request->u.query.query =
      query ? mongo_bson_dup(query) : mongo_bson_new_empty();
   request->u.query.field_selector = mongo_bson_dup(field_selector);
   mongo_client_queue(client, request);

   EXIT;
}

/**
 * mongo_client_query_finish:
 * @client: A #MongoClient.
 * @result: A #GAsyncResult.
 * @error: (out) (allow-none): A location for a #GError, or %NULL.
 *
 * Completes an asynchronous request to mongo_client_query_async().
 *
 * Returns: (transfer full): A #MongoReply.
 */
MongoReply *
mongo_client_query_finish (MongoClient   *client,
                           GAsyncResult  *result,
                           GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   MongoReply *reply;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), NULL);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), NULL);

   if (!(reply = g_simple_async_result_get_op_res_gpointer(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   reply = reply ? mongo_reply_ref(reply) : NULL;

   RETURN(reply);
}

/**
 * mongo_client_getmore_async:
 * @client: A #MongoClient.
 * @db_and_collection: A string containing the 'db.collection".
 * @limit: The maximum number of documents to return in the cursor.
 * @cursor_id: The cursor_id provided by the server.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback.
 * @user_data: (allow-none): User data for @callback.
 *
 * Asynchronously requests more results from a cursor on the Mongo server.
 *
 * @callback MUST call mongo_client_getmore_finish().
 */
void
mongo_client_getmore_async (MongoClient         *client,
                            const gchar         *db_and_collection,
                            guint32              limit,
                            guint64              cursor_id,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
   Request *request;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   request = request_new(client, cancellable, callback, user_data,
                         mongo_client_getmore_async);
   request->oper = MONGO_OPERATION_GETMORE;
   request->u.getmore.db_and_collection = g_strdup(db_and_collection);
   request->u.getmore.limit = limit;
   request->u.getmore.cursor_id = cursor_id;
   mongo_client_queue(client, request);

   EXIT;
}

/**
 * mongo_client_getmore_finish:
 * @client: A #MongoClient.
 * @result: A #GAsyncResult.
 * @error: (allow-none) (out): A location for a #GError, or %NULL.
 *
 * Completes an asynchronous request to mongo_client_getmore_finish().
 *
 * Returns: (transfer full): A #MongoReply.
 */
MongoReply *
mongo_client_getmore_finish (MongoClient   *client,
                             GAsyncResult  *result,
                             GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   MongoReply *reply;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), NULL);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), NULL);

   if (!(reply = g_simple_async_result_get_op_res_gpointer(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   reply = reply ? mongo_reply_ref(reply) : NULL;

   RETURN(reply);
}

/**
 * mongo_client_kill_cursors_async:
 * @client: A #MongoClient.
 * @cursors: (array length=n_cursors) (element-type guint64): Array of cursors.
 * @n_cursors: Number of elements in @cursors.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback.
 * @user_data: (allow-none): User data for @callback.
 *
 * Asynchronously requests that a series of cursors are killed on the Mongo
 * server.
 *
 * @callback MUST call mongo_client_kill_cursors_finish().
 */
void
mongo_client_kill_cursors_async (MongoClient         *client,
                                 guint64             *cursors,
                                 gsize                n_cursors,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
   Request *request;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(cursors);
   g_return_if_fail(n_cursors);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   request = request_new(client, cancellable, callback, user_data,
                         mongo_client_kill_cursors_async);
   request->oper = MONGO_OPERATION_KILL_CURSORS;
   request->u.kill_cursors.cursors = g_array_new(FALSE, FALSE, sizeof(guint64));
   g_array_append_vals(request->u.kill_cursors.cursors, cursors, n_cursors);
   mongo_client_queue(client, request);

   EXIT;
}

/**
 * mongo_client_kill_cursors_finish:
 * @client: A #MongoClient.
 * @result: A #GAsyncResult.
 * @error: (out) (allow-none): A location for a #GError, or %NULL.
 *
 * Completes an asynchronous request to mongo_client_kill_cursors_async().
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set.
 */
gboolean
mongo_client_kill_cursors_finish (MongoClient   *client,
                                  GAsyncResult  *result,
                                  GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

const gchar *
mongo_client_get_replica_set (MongoClient *client)
{
   g_return_val_if_fail(MONGO_IS_CLIENT(client), NULL);
   return client->priv->replica_set;
}

void
mongo_client_set_replica_set (MongoClient *client,
                              const gchar *replica_set)
{
   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_free(client->priv->replica_set);
   client->priv->replica_set = g_strdup(replica_set);
   g_object_notify_by_pspec(G_OBJECT(client), gParamSpecs[PROP_REPLICA_SET]);
}

/**
 * mongo_client_get_slave_okay:
 * @client: A #MongoClient.
 *
 * Retrieves the "slave-okay" property. If "slave-okay" is %TRUE, then
 * %MONGO_QUERY_SLAVE_OK will be set on all outgoing queries.
 *
 * Returns: %TRUE if "slave-okay" is set.
 */
gboolean
mongo_client_get_slave_okay (MongoClient *client)
{
   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   return client->priv->slave_okay;
}

/**
 * mongo_client_set_slave_okay:
 * @client: A #MongoClient.
 * @slave_okay: A #gboolean.
 *
 * Sets the "slave-okay" property. If @slave_okay is %TRUE, then all queries
 * will have the %MONGO_QUERY_SLAVE_OK flag set, allowing them to be executed
 * on slave servers.
 */
void
mongo_client_set_slave_okay (MongoClient *client,
                             gboolean     slave_okay)
{
   g_return_if_fail(MONGO_IS_CLIENT(client));
   client->priv->slave_okay = slave_okay;
   g_object_notify_by_pspec(G_OBJECT(client),
                            gParamSpecs[PROP_SLAVE_OKAY]);
}

static void
mongo_client_finalize (GObject *object)
{
   MongoClientPrivate *priv;
   GHashTable *hash;
   Request *request;

   ENTRY;

   priv = MONGO_CLIENT(object)->priv;

   if ((hash = priv->databases)) {
      priv->databases = NULL;
      g_hash_table_unref(hash);
   }

   /*
    * TODO: Move a lot of this to dispose.
    */

   while ((request = g_queue_pop_head(priv->queue))) {
      request_fail(request, NULL);
      request_free(request);
   }

   g_queue_free(priv->queue);
   priv->queue = NULL;

   g_clear_object(&priv->socket_client);
   g_clear_object(&priv->protocol);

   G_OBJECT_CLASS(mongo_client_parent_class)->finalize(object);

   EXIT;
}

static void
mongo_client_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
   MongoClient *client = MONGO_CLIENT(object);

   switch (prop_id) {
   case PROP_REPLICA_SET:
      g_value_set_string(value, mongo_client_get_replica_set(client));
      break;
   case PROP_SLAVE_OKAY:
      g_value_set_boolean(value, mongo_client_get_slave_okay(client));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_client_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
   MongoClient *client = MONGO_CLIENT(object);

   switch (prop_id) {
   case PROP_REPLICA_SET:
      mongo_client_set_replica_set(client, g_value_get_string(value));
      break;
   case PROP_SLAVE_OKAY:
      mongo_client_set_slave_okay(client, g_value_get_boolean(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_client_class_init (MongoClientClass *klass)
{
   GObjectClass *object_class;

   ENTRY;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_client_finalize;
   object_class->get_property = mongo_client_get_property;
   object_class->set_property = mongo_client_set_property;
   g_type_class_add_private(object_class, sizeof(MongoClientPrivate));

   gParamSpecs[PROP_REPLICA_SET] =
      g_param_spec_string("replica-set",
                          _("Replica Set"),
                          _("The replica set to enforce connecting to."),
                          NULL,
                          G_PARAM_READWRITE);
   g_object_class_install_property(object_class, PROP_REPLICA_SET,
                                   gParamSpecs[PROP_REPLICA_SET]);

   gParamSpecs[PROP_SLAVE_OKAY] =
      g_param_spec_boolean("slave-okay",
                          _("Slave Okay"),
                          _("If it is okay to query a Mongo slave."),
                          FALSE,
                          G_PARAM_READWRITE);
   g_object_class_install_property(object_class, PROP_SLAVE_OKAY,
                                   gParamSpecs[PROP_SLAVE_OKAY]);

   EXIT;
}

static void
mongo_client_init (MongoClient *client)
{
   ENTRY;

   client->priv = G_TYPE_INSTANCE_GET_PRIVATE(client,
                                              MONGO_TYPE_CLIENT,
                                              MongoClientPrivate);
   client->priv->databases = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                   g_free, g_object_unref);
   client->priv->queue = g_queue_new();
   client->priv->socket_client =
         g_object_new(G_TYPE_SOCKET_CLIENT,
                      "timeout", 0,
                      "family", G_SOCKET_FAMILY_IPV4,
                      "protocol", G_SOCKET_PROTOCOL_TCP,
                      "type", G_SOCKET_TYPE_STREAM,
                      NULL);

   EXIT;
}

GQuark
mongo_client_error_quark (void)
{
   return g_quark_from_static_string("mongo-client-error-quark");
}
