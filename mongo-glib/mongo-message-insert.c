/* mongo-message-insert.c
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
#include "mongo-message-insert.h"

G_DEFINE_TYPE(MongoMessageInsert, mongo_message_insert, MONGO_TYPE_MESSAGE)

struct _MongoMessageInsertPrivate
{
   MongoInsertFlags   flags;
   gchar             *collection;
   gsize              count;
   MongoBson        **documents;
};

enum
{
   PROP_0,
   PROP_FLAGS,
   PROP_COLLECTION,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

const gchar *
mongo_message_insert_get_collection (MongoMessageInsert *insert)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE_INSERT(insert), NULL);
   return insert->priv->collection;
}

void
mongo_message_insert_set_collection (MongoMessageInsert *insert,
                                     const gchar        *collection)
{
   g_return_if_fail(MONGO_IS_MESSAGE_INSERT(insert));
   g_free(insert->priv->collection);
   insert->priv->collection = g_strdup(collection);
   g_object_notify_by_pspec(G_OBJECT(insert), gParamSpecs[PROP_COLLECTION]);
}

MongoInsertFlags
mongo_message_insert_get_flags (MongoMessageInsert *insert)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE_INSERT(insert), 0);
   return insert->priv->flags;
}

void
mongo_message_insert_set_flags (MongoMessageInsert *insert,
                                MongoInsertFlags    flags)
{
   g_return_if_fail(MONGO_IS_MESSAGE_INSERT(insert));
   insert->priv->flags = flags;
   g_object_notify_by_pspec(G_OBJECT(insert), gParamSpecs[PROP_FLAGS]);
}

MongoBson **
mongo_message_insert_get_documents (MongoMessageInsert *insert,
                                    gsize              *length)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE_INSERT(insert), NULL);
   if (length) {
      *length = insert->priv->count;
   }
   return insert->priv->documents;
}

void
mongo_message_insert_set_documents (MongoMessageInsert  *insert,
                                    MongoBson          **documents,
                                    gsize                length)
{
   MongoMessageInsertPrivate *priv;
   guint i;

   g_return_if_fail(MONGO_IS_MESSAGE_INSERT(insert));

   priv = insert->priv;

   for (i = 0; i < priv->count; i++) {
      mongo_bson_unref(priv->documents[i]);
   }
   g_free(priv->documents);

   priv->documents = NULL;
   priv->count = 0;

   if (documents) {
      priv->documents = g_new(MongoBson*, length);
      priv->count = length;
      for (i = 0; i < length; i++) {
         priv->documents[i] = mongo_bson_ref(documents[i]);
      }
   }
}

static gboolean
mongo_message_insert_load_from_data (MongoMessage *message,
                                     const guint8 *data,
                                     gsize         length)
{
   MongoMessageInsert *insert = (MongoMessageInsert *)message;
   const gchar *name;
   GPtrArray *ar;
   MongoBson *bson;
   guint32 v32;

   ENTRY;

   g_assert(MONGO_IS_MESSAGE_INSERT(insert));
   g_assert(data);
   g_assert(length);

   if (length >= 4) {
      memcpy(&v32, data, sizeof v32);
      mongo_message_insert_set_flags(insert, GUINT32_FROM_LE(v32));
      length -= 4;
      data += 4;

      if (length >= 1) {
         /* Walk through collection name */
         for (name = (gchar *)data; length && *data; length--, data++) { }
         if (length) {
            mongo_message_insert_set_collection(insert, name);
            length--;
            data++;

            /* Insert flags */
            ar = g_ptr_array_new();
            g_ptr_array_set_free_func(ar, (GDestroyNotify)mongo_bson_unref);
            while (length >= 4) {
               memcpy(&v32, data, sizeof v32);
               v32 = GUINT32_FROM_LE(v32);
               if (length >= v32) {
                  if ((bson = mongo_bson_new_from_data(data, v32))) {
                     length -= v32;
                     data += v32;
                     g_ptr_array_add(ar, bson);
                  } else {
                     g_ptr_array_unref(ar);
                     RETURN(FALSE);
                  }
               } else if (length != 0) {
                  g_ptr_array_unref(ar);
                  RETURN(FALSE);
               }
            }
            mongo_message_insert_set_documents(insert,
                                               (MongoBson **)ar->pdata,
                                               ar->len);
            g_ptr_array_unref(ar);
            RETURN(TRUE);
         }
      }
   }

   RETURN(FALSE);
}

static guint8 *
mongo_message_insert_save_to_data (MongoMessage *message,
                                   gsize        *length)
{
   MongoMessageInsertPrivate *priv;
   MongoMessageInsert *insert = (MongoMessageInsert *)message;
   const guint8 *buf;
   GByteArray *bytes;
   guint32 v32;
   guint8 *ret;
   gsize buflen;
   guint i;

   g_assert(MONGO_IS_MESSAGE_INSERT(insert));
   g_assert(length);

   priv = insert->priv;

   if (!priv->documents || !priv->count) {
      *length = 0;
      RETURN(NULL);
   }

   bytes = g_byte_array_sized_new(64);

   v32 = 0;
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GINT32_TO_LE(mongo_message_get_request_id(message));
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GINT32_TO_LE(mongo_message_get_response_to(message));
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GUINT32_TO_LE(MONGO_OPERATION_INSERT);
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   /* Insert flags. */
   v32 = GUINT32_TO_LE(priv->flags);
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   /* Collection name */
   g_byte_array_append(bytes, (guint8 *)(priv->collection ?: ""),
                       strlen(priv->collection ?: "") + 1);

   /* Documents to insert. */
   for (i = 0; i < priv->count; i++) {
      if ((buf = mongo_bson_get_data(priv->documents[i], &buflen))) {
         g_byte_array_append(bytes, buf, buflen);
      } else {
         *length = 0;
         g_byte_array_free(bytes, TRUE);
         RETURN(NULL);
      }
   }

   /* Update the message length */
   v32 = GUINT32_TO_LE(bytes->len);
   memcpy(bytes->data, &v32, sizeof v32);

   *length = bytes->len;

   DUMP_BYTES(buf, bytes->data, bytes->len);

   ret = g_byte_array_free(bytes, FALSE);
   RETURN(ret);
}

static void
mongo_message_insert_finalize (GObject *object)
{
   MongoMessageInsertPrivate *priv;
   guint i;

   ENTRY;
   priv = MONGO_MESSAGE_INSERT(object)->priv;
   g_free(priv->collection);
   for (i = 0; i < priv->count; i++) {
      mongo_bson_unref(priv->documents[i]);
   }
   g_free(priv->documents);
   G_OBJECT_CLASS(mongo_message_insert_parent_class)->finalize(object);
   EXIT;
}

static void
mongo_message_insert_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
   MongoMessageInsert *insert = MONGO_MESSAGE_INSERT(object);

   switch (prop_id) {
   case PROP_COLLECTION:
      g_value_set_string(value, mongo_message_insert_get_collection(insert));
      break;
   case PROP_FLAGS:
      g_value_set_flags(value, mongo_message_insert_get_flags(insert));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_message_insert_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
   MongoMessageInsert *insert = MONGO_MESSAGE_INSERT(object);

   switch (prop_id) {
   case PROP_COLLECTION:
      mongo_message_insert_set_collection(insert, g_value_get_string(value));
      break;
   case PROP_FLAGS:
      mongo_message_insert_set_flags(insert, g_value_get_flags(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_message_insert_class_init (MongoMessageInsertClass *klass)
{
   GObjectClass *object_class;
   MongoMessageClass *message_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_message_insert_finalize;
   object_class->get_property = mongo_message_insert_get_property;
   object_class->set_property = mongo_message_insert_set_property;
   g_type_class_add_private(object_class, sizeof(MongoMessageInsertPrivate));

   message_class = MONGO_MESSAGE_CLASS(klass);
   message_class->operation = MONGO_OPERATION_INSERT;
   message_class->load_from_data = mongo_message_insert_load_from_data;
   message_class->save_to_data = mongo_message_insert_save_to_data;
}

static void
mongo_message_insert_init (MongoMessageInsert *insert)
{
   insert->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(insert,
                                  MONGO_TYPE_MESSAGE_INSERT,
                                  MongoMessageInsertPrivate);
}
