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

struct _MongoClientPrivate
{
   GPtrArray *seeds;
   GHashTable *databases;
   MongoProtocol *protocol;
   GSocketClient *socket_client;
   gboolean slave_okay;
};

enum
{
   PROP_0,
   PROP_SLAVE_OKAY,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

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
 * mongo_client_add_seed:
 * @client: (in): A #MongoClient.
 *
 * Adds a host:port combination to connect to. Upon failure, the next host
 * in the list will be tried after the automatically discovered replSet.
 *
 * NOTE: Reconnection and replSets are not yet supported.
 */
void
mongo_client_add_seed (MongoClient *client,
                       const gchar *hostname,
                       guint16      port)
{
   gchar *seed;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(hostname);

   seed = g_strdup_printf("%s:%u", hostname, port ? port : 27017);
   g_ptr_array_add(client->priv->seeds, seed);

   EXIT;
}

static void
mongo_client_connect_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple = user_data;
   GSocketConnection *connection;
   GSocketClient *socket_client = (GSocketClient *)object;
   GObject *client;
   GError *error = NULL;

   ENTRY;

   g_assert(G_IS_SOCKET_CLIENT(socket_client));
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));

   /*
    * Fetch the source object from the GAsyncResult. Keep in mind that this
    * increments the reference count of client.
    */
   client = g_async_result_get_source_object(user_data);
   g_assert(MONGO_IS_CLIENT(client));

   priv = MONGO_CLIENT(client)->priv;

   /*
    * Complete the asynchronous request to connect.
    */
   connection = g_socket_client_connect_to_host_finish(socket_client,
                                                       result,
                                                       &error);
   if (!connection) {
      /*
       * TODO: Connect to the next seed, taking into account exponential
       *       backoff if we are at the end of the list.
       */
      g_simple_async_result_take_error(simple, error);
      mongo_simple_async_result_complete_in_idle(simple);
      g_object_unref(simple);
      g_object_unref(client);
      EXIT;
   }

   /*
    * Create a new protocol using this connection as the transport.
    */
   priv->protocol = g_object_new(MONGO_TYPE_PROTOCOL,
                                 "io-stream", connection,
                                 NULL);

   /*
    * Complete the asynchronous request.
    */
   g_simple_async_result_set_op_res_gboolean(simple, TRUE);
   mongo_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);
   g_object_unref(client);

   EXIT;
}

/**
 * mongo_client_connect_async:
 * @client: (in): A #MongoClient.
 * @cancellable: (in) (allow-none): A #GCancellable or %NULL.
 * @callback: (in): A #GAsyncCallback.
 * @user_data: (in) (allow-none): Data for @callback.
 *
 * Attempts to asynchronously connect to the first host in the seed of
 * host:port combinations.
 */
void
mongo_client_connect_async (MongoClient         *client,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(!cancellable ||G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->seeds->len) {
      simple = g_simple_async_result_new_error(G_OBJECT(client),
                                               callback,
                                               user_data,
                                               MONGO_CLIENT_ERROR,
                                               MONGO_CLIENT_ERROR_NO_SEEDS,
                                               _("No hosts have been seeded"));
      mongo_simple_async_result_complete_in_idle(simple);
      g_object_unref(simple);
      EXIT;
   }

   /*
    * TODO: Iterate through the list of seeds after a connection fails.
    *       Also, add exponential backoff.
    */

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_connect_async);
   g_socket_client_connect_to_host_async(priv->socket_client,
                                         g_ptr_array_index(priv->seeds, 0),
                                         27017,
                                         cancellable,
                                         mongo_client_connect_cb,
                                         simple);

   EXIT;
}

/**
 * mongo_client_connect_finish:
 * @client: (in): A #MongoClient.
 * @result: (in): A #GAsyncResult provided to callback.
 * @error: (out) (allow-none): A location for a #GError, or %NULL.
 *
 * Completes an asynchronous attempt to connect to a Mongo server.
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set.
 */
gboolean
mongo_client_connect_finish (MongoClient   *client,
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
   MongoProtocol *protocol = (MongoProtocol *)object;
   MongoBsonIter iter;
   const gchar *errmsg;
   MongoReply *reply = NULL;
   GError *error = NULL;

   ENTRY;

   g_assert(MONGO_IS_PROTOCOL(protocol));
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));

   /*
    * Get the query reply, which may contain an error document.
    */
   if (!(reply = mongo_protocol_query_finish(protocol, result, &error))) {
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
                     _("Command failed with: %s"),
                     errmsg);
            } else {
               g_simple_async_result_set_error(
                     simple,
                     MONGO_CLIENT_ERROR,
                     MONGO_CLIENT_ERROR_COMMAND_FAILED,
                     _("Command failed with no errmsg"));
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
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple;
   gchar *db_and_cmd;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db);
   g_return_if_fail(command);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->protocol) {
      g_simple_async_report_error_in_idle(G_OBJECT(client),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("The mongo client is not "
                                            "currently connected."));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_command_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);

   db_and_cmd = g_strdup_printf("%s.$cmd", db);
   mongo_protocol_query_async(priv->protocol,
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

static void
mongo_client_remove_cb (GObject      *object,
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

/**
 * mongo_client_remove_async:
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
 * @callback MUST call mongo_client_remove_finish().
 */
void
mongo_client_remove_async (MongoClient         *client,
                           const gchar         *db_and_collection,
                           MongoDeleteFlags     flags,
                           const MongoBson     *selector,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(strstr(db_and_collection, "."));
   g_return_if_fail(selector);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->protocol) {
      g_simple_async_report_error_in_idle(G_OBJECT(client),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Not currently connected."));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_remove_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);
   mongo_protocol_delete_async(priv->protocol,
                               db_and_collection,
                               flags,
                               selector,
                               cancellable,
                               mongo_client_remove_cb,
                               simple);

   EXIT;
}

/**
 * mongo_client_remove_finish:
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
mongo_client_remove_finish (MongoClient   *client,
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
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(strstr(db_and_collection, "."));
   g_return_if_fail(selector);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->protocol) {
      g_simple_async_report_error_in_idle(G_OBJECT(client),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Not currently connected."));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_update_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);
   mongo_protocol_update_async(priv->protocol,
                               db_and_collection,
                               flags,
                               selector,
                               update,
                               cancellable,
                               mongo_client_update_cb,
                               simple);

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
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(strstr(db_and_collection, "."));
   g_return_if_fail(documents);
   g_return_if_fail(n_documents);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->protocol) {
      g_simple_async_report_error_in_idle(G_OBJECT(client),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Not currently connected."));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_insert_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);
   mongo_protocol_insert_async(priv->protocol,
                               db_and_collection,
                               flags,
                               documents,
                               n_documents,
                               cancellable,
                               mongo_client_insert_cb,
                               simple);

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

static void
mongo_client_disconnect_close_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   GIOStream *io_stream = (GIOStream *)object;
   gboolean ret;
   GError *error = NULL;

   ENTRY;

   g_return_if_fail(G_IS_IO_STREAM(io_stream));
   g_return_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(ret = g_io_stream_close_finish(io_stream, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, ret);
   mongo_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

static void
mongo_client_disconnect_kill_cursors_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoProtocol *protocol = (MongoProtocol *)object;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple));

   mongo_protocol_kill_cursors_finish(protocol, result, NULL);
   g_io_stream_close_async(mongo_protocol_get_io_stream(protocol),
                           G_PRIORITY_DEFAULT,
                           NULL,
                           mongo_client_disconnect_close_cb,
                           simple);

   EXIT;
}

void
mongo_client_disconnect_async (MongoClient         *client,
                               gboolean             kill_cursors,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple;
   guint64 *cursors = NULL;
   gsize n_cursors = 0;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->protocol) {
      g_simple_async_report_error_in_idle(G_OBJECT(client), callback, user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Not currently connected."));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_disconnect_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);

   /*
    * TODO: Get the list of cursors that should be destroyed.
    */

   if (kill_cursors && cursors) {
      mongo_protocol_kill_cursors_async(
            priv->protocol,
            cursors,
            n_cursors,
            cancellable,
            mongo_client_disconnect_kill_cursors_cb,
            simple);
      EXIT;
   } else {
      g_simple_async_result_set_op_res_gboolean(simple, TRUE);
      mongo_simple_async_result_complete_in_idle(simple);
      g_object_unref(simple);
      EXIT;
   }

   g_assert_not_reached();
}

gboolean
mongo_client_disconnect_finish (MongoClient   *client,
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
   GSimpleAsyncResult *simple;
   MongoBson *q = NULL;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->protocol) {
      g_simple_async_report_error_in_idle(G_OBJECT(client),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Not currently connected"));
      EXIT;
   }

   if (priv->slave_okay) {
      flags |= MONGO_QUERY_SLAVE_OK;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_query_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);

   if (!query) {
      query = q = mongo_bson_new_empty();
   }

   mongo_protocol_query_async(priv->protocol,
                              db_and_collection,
                              flags,
                              skip,
                              limit,
                              query,
                              field_selector,
                              cancellable,
                              mongo_client_query_cb,
                              simple);

   if (q) {
      mongo_bson_unref(q);
   }

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
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->protocol) {
      g_simple_async_report_error_in_idle(G_OBJECT(client),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Not currently connected"));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_getmore_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);

   mongo_protocol_getmore_async(priv->protocol,
                                db_and_collection,
                                limit,
                                cursor_id,
                                cancellable,
                                mongo_client_getmore_cb,
                                simple);

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
   MongoClientPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(cursors);
   g_return_if_fail(n_cursors);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (!priv->protocol) {
      g_simple_async_report_error_in_idle(G_OBJECT(client),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Not currently connected"));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_getmore_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);

   mongo_protocol_kill_cursors_async(priv->protocol,
                                     cursors,
                                     n_cursors,
                                     cancellable,
                                     mongo_client_kill_cursors_cb,
                                     simple);

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
   GPtrArray *ar;

   ENTRY;

   priv = MONGO_CLIENT(object)->priv;

   if ((ar = priv->seeds)) {
      priv->seeds = NULL;
      g_ptr_array_unref(ar);
   }

   if ((hash = priv->databases)) {
      priv->databases = NULL;
      g_hash_table_unref(hash);
   }

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
   client->priv->seeds = g_ptr_array_new_with_free_func(g_free);
   client->priv->socket_client = g_socket_client_new();

   EXIT;
}

GQuark
mongo_client_error_quark (void)
{
   return g_quark_from_static_string("mongo-client-error-quark");
}
