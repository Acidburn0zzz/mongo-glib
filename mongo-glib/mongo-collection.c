/* mongo-collection.c
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
#include "mongo-collection.h"
#include "mongo-debug.h"

G_DEFINE_TYPE(MongoCollection, mongo_collection, G_TYPE_OBJECT)

struct _MongoCollectionPrivate
{
   MongoDatabase *database;
   MongoClient *client;
   gchar *db_and_collection;
   gchar *name;
};

enum
{
   PROP_0,
   PROP_CLIENT,
   PROP_DATABASE,
   PROP_NAME,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

/**
 * mongo_collection_find:
 * @collection: (in): A #MongoCollection.
 * @query: (in): A #MongoBson containing the query.
 * @field_selector: (in) (allow-none): A #MongoBson or %NULL for all fields.
 * @skip: (in): The number of documents to skip.
 * @limit: (in): The maximum number of documents to return.
 * @flags: (in): A bitwise-or of #MongoQueryFlags.
 *
 * Find documents within the collection.
 *
 * Returns: (transfer full): A #MongoCursor.
 */
MongoCursor *
mongo_collection_find (MongoCollection *collection,
                       MongoBson       *query,
                       MongoBson       *field_selector,
                       guint            skip,
                       guint            limit,
                       MongoQueryFlags  flags)
{
   MongoCollectionPrivate *priv;
   MongoCursor *cursor;
   const gchar *db_name;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_COLLECTION(collection), NULL);
   g_return_val_if_fail(collection->priv->database, NULL);

   priv = collection->priv;

   db_name = mongo_database_get_name(priv->database);
   cursor = g_object_new(MONGO_TYPE_CURSOR,
                         "client", priv->client,
                         "collection", priv->name,
                         "database", db_name,
                         "query", query,
                         "fields", field_selector,
                         "skip", skip,
                         "limit", limit,
                         NULL);

   RETURN(cursor);
}

static void
mongo_collection_count_cb (GObject      *object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoClient *client = (MongoClient *)object;
   MongoReply *reply;
   GError *error = NULL;

   ENTRY;

   g_assert(MONGO_IS_CLIENT(client));
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(reply = mongo_client_command_finish(client, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   } else {
      g_simple_async_result_set_op_res_gpointer(
            simple, reply, (GDestroyNotify)mongo_reply_unref);
   }

   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

void
mongo_collection_count_async (MongoCollection     *collection,
                              const MongoBson     *query,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
   MongoCollectionPrivate *priv;
   GSimpleAsyncResult *simple;
   MongoBson *command;

   ENTRY;

   g_return_if_fail(MONGO_IS_COLLECTION(collection));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = collection->priv;

   if (!priv->client || !priv->database) {
      g_simple_async_report_error_in_idle(G_OBJECT(collection),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Not currently connected."));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(collection), callback, user_data,
                                      mongo_collection_count_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);

   command = mongo_bson_new_empty();
   mongo_bson_append_string(command, "count", priv->name);
   if (query) {
      mongo_bson_append_bson(command, "query", query);
   }
   mongo_client_command_async(priv->client,
                              mongo_database_get_name(priv->database),
                              command,
                              cancellable,
                              mongo_collection_count_cb,
                              simple);
   mongo_bson_unref(command);

   EXIT;
}

gboolean
mongo_collection_count_finish (MongoCollection  *collection,
                               GAsyncResult     *result,
                               guint64          *count,
                               GError          **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   MongoBsonIter iter;
   MongoReply *reply;
   gboolean ret = FALSE;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_COLLECTION(collection), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(reply = g_simple_async_result_get_op_res_gpointer(simple))) {
      g_simple_async_result_propagate_error(simple, error);
      GOTO(failure);
   }

   if (!reply->n_returned) {
      GOTO(failure);
   }

   mongo_bson_iter_init(&iter, reply->documents[0]);
   if (!mongo_bson_iter_find(&iter, "n") ||
       (mongo_bson_iter_get_value_type(&iter) != MONGO_BSON_DOUBLE)) {
      GOTO(failure);
   }

   *count = mongo_bson_iter_get_value_double(&iter);
   ret = TRUE;

failure:
   if (reply) {
      mongo_reply_unref(reply);
   }

   RETURN(ret);
}

/**
 * mongo_collection_get_client:
 * @collection: (in): A #MongoCollection.
 *
 * Fetches the client the collection communicates over.
 *
 * Returns: (transfer none): A #MongoClient.
 */
MongoClient *
mongo_collection_get_client (MongoCollection *collection)
{
   g_return_val_if_fail(MONGO_IS_COLLECTION(collection), NULL);
   return collection->priv->client;
}

static void
mongo_collection_set_client (MongoCollection *collection,
                             MongoClient     *client)
{
   MongoCollectionPrivate *priv;

   g_return_if_fail(MONGO_IS_COLLECTION(collection));
   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(!collection->priv->client);

   priv = collection->priv;

   priv->client = client;
   g_object_add_weak_pointer(G_OBJECT(priv->client),
                             (gpointer *)&priv->client);
}

static void
mongo_collection_update_name (MongoCollection *collection)
{
   MongoCollectionPrivate *priv;

   g_return_if_fail(MONGO_IS_COLLECTION(collection));

   priv = collection->priv;

   if (priv->database && priv->name) {
      g_free(priv->db_and_collection);
      priv->db_and_collection =
         g_strdup_printf("%s.%s",
                         mongo_database_get_name(priv->database),
                         priv->name);
   }
}

/**
 * mongo_collection_get_database:
 * @collection: (in): A #MongoCollection.
 *
 * Fetches the database that this collection belongs to.
 *
 * Returns: (transfer none): A #MongoDatabase.
 */
MongoDatabase *
mongo_collection_get_database (MongoCollection *collection)
{
   g_return_val_if_fail(MONGO_IS_COLLECTION(collection), NULL);
   return collection->priv->database;
}

static void
mongo_collection_set_database (MongoCollection *collection,
                             MongoDatabase     *database)
{
   MongoCollectionPrivate *priv;

   g_return_if_fail(MONGO_IS_COLLECTION(collection));
   g_return_if_fail(MONGO_IS_DATABASE(database));
   g_return_if_fail(!collection->priv->database);

   priv = collection->priv;

   priv->database = database;
   g_object_add_weak_pointer(G_OBJECT(priv->database),
                             (gpointer *)&priv->database);
   mongo_collection_update_name(collection);
}

const gchar *
mongo_collection_get_name (MongoCollection *collection)
{
   g_return_val_if_fail(MONGO_IS_COLLECTION(collection), NULL);
   return collection->priv->name;
}

static void
mongo_collection_set_name (MongoCollection *collection,
                           const gchar     *name)
{
   g_return_if_fail(MONGO_IS_COLLECTION(collection));
   g_return_if_fail(!collection->priv->name);
   collection->priv->name = g_strdup(name);
   mongo_collection_update_name(collection);
}

static void
mongo_collection_remove_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoClient *client = (MongoClient *)object;
   gboolean ret;
   GError *error = NULL;

   ENTRY;

   if (!(ret = mongo_client_remove_finish(client, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, ret);
   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

void
mongo_collection_remove_async (MongoCollection      *collection,
                               const MongoBson      *selector,
                               MongoDeleteFlags      flags,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
   MongoCollectionPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_COLLECTION(collection));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = collection->priv;

   if (!priv->client) {
      g_simple_async_report_error_in_idle(G_OBJECT(collection),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Missing Mongo client"));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(collection),
                                      callback,
                                      user_data,
                                      mongo_collection_remove_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);
   mongo_client_remove_async(priv->client,
                             priv->db_and_collection,
                             flags,
                             selector,
                             cancellable,
                             mongo_collection_remove_cb,
                             simple);

   EXIT;
}

gboolean
mongo_collection_remove_finish (MongoCollection  *collection,
                                GAsyncResult     *result,
                                GError          **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_COLLECTION(collection), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   return ret;
}

static void
mongo_collection_update_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoClient *client = (MongoClient *)object;
   gboolean ret;
   GError *error = NULL;

   ENTRY;

   if (!(ret = mongo_client_update_finish(client, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, ret);
   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

void
mongo_collection_update_async (MongoCollection      *collection,
                               const MongoBson      *selector,
                               const MongoBson      *update,
                               MongoUpdateFlags      flags,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
   MongoCollectionPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_COLLECTION(collection));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = collection->priv;

   if (!priv->client) {
      g_simple_async_report_error_in_idle(G_OBJECT(collection),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Missing Mongo client"));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(collection),
                                      callback,
                                      user_data,
                                      mongo_collection_update_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);
   mongo_client_update_async(priv->client,
                             priv->db_and_collection,
                             flags,
                             selector,
                             update,
                             cancellable,
                             mongo_collection_update_cb,
                             simple);

   EXIT;
}

gboolean
mongo_collection_update_finish (MongoCollection  *collection,
                                GAsyncResult     *result,
                                GError          **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_COLLECTION(collection), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   return ret;
}

static void
mongo_collection_insert_cb (GObject      *object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoClient *client = (MongoClient *)object;
   gboolean ret;
   GError *error = NULL;

   ENTRY;

   if (!(ret = mongo_client_insert_finish(client, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, ret);
   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   EXIT;
}

void
mongo_collection_insert_async (MongoCollection      *collection,
                               MongoBson           **documents,
                               gsize                 n_documents,
                               MongoInsertFlags      flags,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data)
{
   MongoCollectionPrivate *priv;
   GSimpleAsyncResult *simple;

   ENTRY;

   g_return_if_fail(MONGO_IS_COLLECTION(collection));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = collection->priv;

   if (!priv->client) {
      g_simple_async_report_error_in_idle(G_OBJECT(collection),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("Missing Mongo client"));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(collection),
                                      callback,
                                      user_data,
                                      mongo_collection_insert_async);
   g_simple_async_result_set_check_cancellable(simple, cancellable);
   mongo_client_insert_async(priv->client,
                             priv->db_and_collection,
                             flags,
                             documents,
                             n_documents,
                             cancellable,
                             mongo_collection_insert_cb,
                             simple);

   EXIT;
}

gboolean
mongo_collection_insert_finish (MongoCollection  *collection,
                                GAsyncResult     *result,
                                GError          **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_COLLECTION(collection), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   return ret;
}

static void
mongo_collection_drop_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
   GSimpleAsyncResult *simple = user_data;
   MongoClient *client = (MongoClient *)object;
   MongoReply *reply;
   GError *error = NULL;

   ENTRY;

   g_assert(MONGO_IS_CLIENT(client));
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));

   if (!(reply = mongo_client_command_finish(client, result, &error))) {
      g_simple_async_result_take_error(simple, error);
   }

   g_simple_async_result_set_op_res_gboolean(simple, !!reply);
   g_simple_async_result_complete_in_idle(simple);
   g_object_unref(simple);

   if (reply) {
      mongo_reply_unref(reply);
   }

   EXIT;
}

void
mongo_collection_drop_async (MongoCollection     *collection,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
   MongoCollectionPrivate *priv;
   GSimpleAsyncResult *simple;
   MongoBson *bson;

   ENTRY;

   g_return_if_fail(MONGO_IS_COLLECTION(collection));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = collection->priv;

   if (!priv->client || !priv->database) {
      g_simple_async_report_error_in_idle(G_OBJECT(collection),
                                          callback,
                                          user_data,
                                          MONGO_CLIENT_ERROR,
                                          MONGO_CLIENT_ERROR_NOT_CONNECTED,
                                          _("The client has been lost."));
      EXIT;
   }

   simple = g_simple_async_result_new(G_OBJECT(collection), callback, user_data,
                                      mongo_collection_drop_async);

   bson = mongo_bson_new_empty();
   mongo_bson_append_string(bson, "drop", priv->name);
   mongo_client_command_async(priv->client,
                              mongo_database_get_name(priv->database),
                              bson,
                              cancellable,
                              mongo_collection_drop_cb,
                              simple);

   mongo_bson_unref(bson);
}

gboolean
mongo_collection_drop_finish (MongoCollection  *collection,
                              GAsyncResult     *result,
                              GError          **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   ENTRY;

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

static void
mongo_collection_finalize (GObject *object)
{
   MongoCollectionPrivate *priv;

   ENTRY;

   priv = MONGO_COLLECTION(object)->priv;

   g_free(priv->db_and_collection);
   g_free(priv->name);

   if (priv->client) {
      g_object_remove_weak_pointer(G_OBJECT(priv->client),
                                   (gpointer *)&priv->client);
      priv->client = NULL;
   }

   if (priv->database) {
      g_object_remove_weak_pointer(G_OBJECT(priv->database),
                                   (gpointer *)&priv->database);
      priv->database = NULL;
   }

   G_OBJECT_CLASS(mongo_collection_parent_class)->finalize(object);

   EXIT;
}

static void
mongo_collection_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
   MongoCollection *collection = MONGO_COLLECTION(object);

   switch (prop_id) {
   case PROP_CLIENT:
      g_value_set_object(value, mongo_collection_get_client(collection));
      break;
   case PROP_DATABASE:
      g_value_set_object(value, mongo_collection_get_database(collection));
      break;
   case PROP_NAME:
      g_value_set_string(value, mongo_collection_get_name(collection));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_collection_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
   MongoCollection *collection = MONGO_COLLECTION(object);

   switch (prop_id) {
   case PROP_CLIENT:
      mongo_collection_set_client(collection, g_value_get_object(value));
      break;
   case PROP_DATABASE:
      mongo_collection_set_database(collection, g_value_get_object(value));
      break;
   case PROP_NAME:
      mongo_collection_set_name(collection, g_value_get_string(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_collection_class_init (MongoCollectionClass *klass)
{
   GObjectClass *object_class;

   ENTRY;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_collection_finalize;
   object_class->get_property = mongo_collection_get_property;
   object_class->set_property = mongo_collection_set_property;
   g_type_class_add_private(object_class, sizeof(MongoCollectionPrivate));

   gParamSpecs[PROP_CLIENT] =
      g_param_spec_object("client",
                          _("Client"),
                          _("The client that owns the collection."),
                          MONGO_TYPE_CLIENT,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_CLIENT,
                                   gParamSpecs[PROP_CLIENT]);

   gParamSpecs[PROP_DATABASE] =
      g_param_spec_object("database",
                          _("Database"),
                          _("The database containing the collection."),
                          MONGO_TYPE_DATABASE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_DATABASE,
                                   gParamSpecs[PROP_DATABASE]);

   gParamSpecs[PROP_NAME] =
      g_param_spec_string("name",
                          _("Name"),
                          _("Name"),
                          NULL,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_NAME,
                                   gParamSpecs[PROP_NAME]);

   EXIT;
}

static void
mongo_collection_init (MongoCollection *collection)
{
   ENTRY;
   collection->priv = G_TYPE_INSTANCE_GET_PRIVATE(collection,
                                                  MONGO_TYPE_COLLECTION,
                                                  MongoCollectionPrivate);
   EXIT;
}
