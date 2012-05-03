/* mongo-cursor.c
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
#include "mongo-cursor.h"
#include "mongo-debug.h"

G_DEFINE_TYPE(MongoCursor, mongo_cursor, G_TYPE_OBJECT)

struct _MongoCursorPrivate
{
   MongoClient *client;
   MongoBson *fields;
   MongoBson *query;
   gchar *collection;
   guint limit;
   guint skip;
   guint batch_size;
};

enum
{
   PROP_0,
   PROP_BATCH_SIZE,
   PROP_CLIENT,
   PROP_COLLECTION,
   PROP_FIELDS,
   PROP_LIMIT,
   PROP_QUERY,
   PROP_SKIP,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

guint
mongo_cursor_get_batch_size (MongoCursor *cursor)
{
   g_return_val_if_fail(MONGO_IS_CURSOR(cursor), NULL);
   return cursor->priv->batch_size;
}

/**
 * mongo_cursor_get_client:
 * @cursor: (in): A #MongoCursor.
 *
 * Fetches the client the cursor is using.
 *
 * Returns: (transfer none): A #MongoClient.
 */
MongoClient *
mongo_cursor_get_client (MongoCursor *cursor)
{
   g_return_val_if_fail(MONGO_IS_CURSOR(cursor), NULL);
   return cursor->priv->client;
}

const gchar *
mongo_cursor_get_collection (MongoCursor *cursor)
{
   g_return_val_if_fail(MONGO_IS_CURSOR(cursor), NULL);
   return cursor->priv->collection;
}

/**
 * mongo_cursor_get_fields:
 * @cursor: (in): A #MongoCursor.
 *
 * Fetches the field selector used by the cursor.
 *
 * Returns: (transfer none): A #MongoBson or %NULL.
 */
MongoBson *
mongo_cursor_get_fields (MongoCursor *cursor)
{
   g_return_val_if_fail(MONGO_IS_CURSOR(cursor), NULL);
   return cursor->priv->fields;
}

guint
mongo_cursor_get_limit (MongoCursor *cursor)
{
   g_return_val_if_fail(MONGO_IS_CURSOR(cursor), 0);
   return cursor->priv->limit;
}

/**
 * mongo_cursor_get_query:
 * @cursor: (in): A #MongoCursor.
 *
 * Fetches the query performed by the cursor.
 *
 * Returns: (transfer none): A #MongoBson.
 */
MongoBson *
mongo_cursor_get_query (MongoCursor *cursor)
{
   g_return_val_if_fail(MONGO_IS_CURSOR(cursor), NULL);
   return cursor->priv->query;
}

guint
mongo_cursor_get_skip (MongoCursor *cursor)
{
   g_return_val_if_fail(MONGO_IS_CURSOR(cursor), 0);
   return cursor->priv->skip;
}

static void
mongo_cursor_set_batch_size (MongoCursor *cursor,
                             guint        batch_size)
{
   g_return_if_fail(MONGO_IS_CURSOR(cursor));
   cursor->priv->batch_size = batch_size;
   g_object_notify_by_pspec(G_OBJECT(cursor),
                            gParamSpecs[PROP_BATCH_SIZE]);
}

static void
mongo_cursor_set_client (MongoCursor *cursor,
                         MongoClient *client)
{
   g_return_if_fail(MONGO_IS_CURSOR(cursor));
   g_return_if_fail(MONGO_IS_CLIENT(client));

   cursor->priv->client = client;
   g_object_add_weak_pointer(G_OBJECT(client),
                             (gpointer *)&cursor->priv->client);
}

static void
mongo_cursor_set_collection (MongoCursor *cursor,
                             const gchar *collection)
{
   g_return_if_fail(MONGO_IS_CURSOR(cursor));
   cursor->priv->collection = g_strdup(collection);
}

static void
mongo_cursor_set_fields (MongoCursor *cursor,
                         MongoBson   *fields)
{
   g_return_if_fail(MONGO_IS_CURSOR(cursor));
   if (fields) {
      cursor->priv->fields = mongo_bson_ref(fields);
   }
}

static void
mongo_cursor_set_limit (MongoCursor *cursor,
                        guint        limit)
{
   g_return_if_fail(MONGO_IS_CURSOR(cursor));
   cursor->priv->limit = limit;
}

static void
mongo_cursor_set_query (MongoCursor *cursor,
                        MongoBson   *query)
{
   g_return_if_fail(MONGO_IS_CURSOR(cursor));
   g_return_if_fail(query);
   cursor->priv->query = mongo_bson_ref(query);
}

static void
mongo_cursor_set_skip (MongoCursor *cursor,
                       guint        skip)
{
   g_return_if_fail(MONGO_IS_CURSOR(cursor));
   cursor->priv->skip = skip;
}

static void
mongo_cursor_finalize (GObject *object)
{
   MongoCursorPrivate *priv;

   ENTRY;

   priv = MONGO_CURSOR(object)->priv;

   if (priv->client) {
      g_object_remove_weak_pointer(G_OBJECT(priv->client),
                                   (gpointer *)&priv->client);
      priv->client = NULL;
   }

   g_free(priv->collection);
   priv->collection = NULL;

   if (priv->fields) {
      mongo_bson_unref(priv->fields);
      priv->fields = NULL;
   }

   if (priv->query) {
      mongo_bson_unref(priv->query);
      priv->query = NULL;
   }

   G_OBJECT_CLASS(mongo_cursor_parent_class)->finalize(object);

   EXIT;
}

static void
mongo_cursor_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
   MongoCursor *cursor = MONGO_CURSOR(object);

   switch (prop_id) {
   case PROP_BATCH_SIZE:
      g_value_set_uint(value, mongo_cursor_get_batch_size(cursor));
      break;
   case PROP_CLIENT:
      g_value_set_object(value, mongo_cursor_get_client(cursor));
      break;
   case PROP_COLLECTION:
      g_value_set_string(value, mongo_cursor_get_collection(cursor));
      break;
   case PROP_FIELDS:
      g_value_set_boxed(value, mongo_cursor_get_fields(cursor));
      break;
   case PROP_LIMIT:
      g_value_set_uint(value, mongo_cursor_get_limit(cursor));
      break;
   case PROP_QUERY:
      g_value_set_boxed(value, mongo_cursor_get_query(cursor));
      break;
   case PROP_SKIP:
      g_value_set_uint(value, mongo_cursor_get_skip(cursor));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_cursor_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
   MongoCursor *cursor = MONGO_CURSOR(object);

   switch (prop_id) {
   case PROP_BATCH_SIZE:
      mongo_cursor_set_batch_size(cursor, g_value_get_uint(value));
      break;
   case PROP_CLIENT:
      mongo_cursor_set_client(cursor, g_value_get_object(value));
      break;
   case PROP_COLLECTION:
      mongo_cursor_set_collection(cursor, g_value_get_string(value));
      break;
   case PROP_FIELDS:
      mongo_cursor_set_fields(cursor, g_value_get_boxed(value));
      break;
   case PROP_LIMIT:
      mongo_cursor_set_limit(cursor, g_value_get_uint(value));
      break;
   case PROP_QUERY:
      mongo_cursor_set_query(cursor, g_value_get_boxed(value));
      break;
   case PROP_SKIP:
      mongo_cursor_set_skip(cursor, g_value_get_uint(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_cursor_class_init (MongoCursorClass *klass)
{
   GObjectClass *object_class;

   ENTRY;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_cursor_finalize;
   object_class->get_property = mongo_cursor_get_property;
   object_class->set_property = mongo_cursor_set_property;
   g_type_class_add_private(object_class, sizeof(MongoCursorPrivate));

   gParamSpecs[PROP_BATCH_SIZE] =
      g_param_spec_uint("batch-size",
                        _("Batch Size"),
                        _("The requested number of items in the batch."),
                        0,
                        G_MAXUINT32,
                        0,
                        G_PARAM_READWRITE);
   g_object_class_install_property(object_class, PROP_BATCH_SIZE,
                                   gParamSpecs[PROP_BATCH_SIZE]);

   gParamSpecs[PROP_CLIENT] =
      g_param_spec_object("client",
                          _("Client"),
                          _("A MongoClient to communicate with."),
                          MONGO_TYPE_CLIENT,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_CLIENT,
                                   gParamSpecs[PROP_CLIENT]);

   gParamSpecs[PROP_COLLECTION] =
      g_param_spec_string("collection",
                          _("Collection"),
                          _("The database and collection name."),
                          NULL,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_COLLECTION,
                                   gParamSpecs[PROP_COLLECTION]);

   gParamSpecs[PROP_FIELDS] =
      g_param_spec_boxed("fields",
                         _("Fields"),
                         _("The fields selector for the query."),
                         MONGO_TYPE_BSON,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_FIELDS,
                                   gParamSpecs[PROP_FIELDS]);

   gParamSpecs[PROP_LIMIT] =
      g_param_spec_uint("limit",
                        _("Limit"),
                        _("The maximum number of documents to retrieve"),
                        0,
                        G_MAXUINT32,
                        0,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_LIMIT,
                                   gParamSpecs[PROP_LIMIT]);

   gParamSpecs[PROP_QUERY] =
      g_param_spec_boxed("query",
                         _("Query"),
                         _("The query to perform."),
                         MONGO_TYPE_BSON,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_QUERY,
                                   gParamSpecs[PROP_QUERY]);

   gParamSpecs[PROP_SKIP] =
      g_param_spec_uint("skip",
                        _("Skip"),
                        _("The number of documents to skip"),
                        0,
                        G_MAXUINT32,
                        0,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_SKIP,
                                   gParamSpecs[PROP_SKIP]);

   EXIT;
}

static void
mongo_cursor_init (MongoCursor *cursor)
{
   ENTRY;
   cursor->priv = G_TYPE_INSTANCE_GET_PRIVATE(cursor,
                                              MONGO_TYPE_CURSOR,
                                              MongoCursorPrivate);
   EXIT;
}
