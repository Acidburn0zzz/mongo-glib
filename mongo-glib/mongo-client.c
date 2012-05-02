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

G_DEFINE_TYPE(MongoClient, mongo_client, G_TYPE_OBJECT)

struct _MongoClientPrivate
{
   GPtrArray *seeds;
   GHashTable *databases;
   MongoProtocol *protocol;
   GSocketClient *socket_client;
};

enum
{
   PROP_0,
   LAST_PROP
};

//static GParamSpec *gParamSpecs[LAST_PROP];

MongoClient *
mongo_client_new (void)
{
   MongoClient *ret;

   ENTRY;
   ret = g_object_new(MONGO_TYPE_CLIENT, NULL);
   RETURN(ret);
}

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
      g_simple_async_result_complete_in_idle(simple);
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
   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);
   g_object_unref(client);

   EXIT;
}

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
      g_simple_async_result_complete_in_idle(simple);
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
   MongoReply *reply;
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

   g_simple_async_result_set_op_res_gpointer(simple,
                                             mongo_reply_ref(reply),
                                             (GDestroyNotify)mongo_reply_unref);

finish:
   g_simple_async_result_complete_in_idle(simple);
   mongo_reply_unref(reply);
   g_object_unref(simple);

   EXIT;
}

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
                              MONGO_QUERY_NONE,
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
   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

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
   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

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
   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

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
mongo_client_class_init (MongoClientClass *klass)
{
   GObjectClass *object_class;

   ENTRY;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_client_finalize;
   g_type_class_add_private(object_class, sizeof(MongoClientPrivate));

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
   client->priv->seeds = g_ptr_array_new();
   client->priv->socket_client = g_socket_client_new();

   EXIT;
}

GQuark
mongo_client_error_quark (void)
{
   return g_quark_from_static_string("mongo-client-error-quark");
}
