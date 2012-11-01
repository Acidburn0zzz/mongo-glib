/* mongo-message-delete.c
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
#include "mongo-message-delete.h"

G_DEFINE_TYPE(MongoMessageDelete, mongo_message_delete, MONGO_TYPE_MESSAGE)

struct _MongoMessageDeletePrivate
{
   gchar            *collection;
   MongoDeleteFlags  flags;
   MongoBson        *selector;
};

enum
{
   PROP_0,
   PROP_COLLECTION,
   PROP_FLAGS,
   PROP_SELECTOR,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

const gchar *
mongo_message_delete_get_collection (MongoMessageDelete *delete)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE_DELETE(delete), NULL);
   return delete->priv->collection;
}

void
mongo_message_delete_set_collection (MongoMessageDelete *delete,
                                     const gchar        *collection)
{
   MongoMessageDeletePrivate *priv;

   g_return_if_fail(MONGO_IS_MESSAGE_DELETE(delete));

   priv = delete->priv;

   g_free(priv->collection);
   priv->collection = g_strdup(collection);
   g_object_notify_by_pspec(G_OBJECT(delete), gParamSpecs[PROP_COLLECTION]);
}

MongoBson *
mongo_message_delete_get_selector (MongoMessageDelete *delete)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE_DELETE(delete), NULL);
   return delete->priv->selector;
}

void
mongo_message_delete_set_selector (MongoMessageDelete *delete,
                                   MongoBson          *selector)
{
   MongoMessageDeletePrivate *priv;

   g_return_if_fail(MONGO_IS_MESSAGE_DELETE(delete));

   priv = delete->priv;

   if (priv->selector) {
      mongo_bson_unref(priv->selector);
      priv->selector = NULL;
   }

   if (selector) {
      priv->selector = mongo_bson_ref(selector);
   }

   g_object_notify_by_pspec(G_OBJECT(delete), gParamSpecs[PROP_SELECTOR]);
}

MongoDeleteFlags
mongo_message_delete_get_flags (MongoMessageDelete *delete)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE_DELETE(delete), 0);
   return delete->priv->flags;
}

void
mongo_message_delete_set_flags (MongoMessageDelete *delete,
                                MongoDeleteFlags    flags)
{
   g_return_if_fail(MONGO_IS_MESSAGE_DELETE(delete));
   delete->priv->flags = flags;
   g_object_notify_by_pspec(G_OBJECT(delete), gParamSpecs[PROP_FLAGS]);
}

static gboolean
mongo_message_delete_load_from_data (MongoMessage *message,
                                     const guint8 *data,
                                     gsize         length)
{
   MongoMessageDeletePrivate *priv;
   MongoMessageDelete *delete = (MongoMessageDelete *)message;
   const gchar *name;
   MongoBson *bson;
   guint32 len;

   ENTRY;

   g_assert(MONGO_IS_MESSAGE_DELETE(delete));
   g_assert(data);
   g_assert(length);

   priv = delete->priv;

   if (length >= 4) {
      /* Skip, first 4 bytes are reserved. */
      data += 4;
      length -= 4;
      if (length > 1) {
         for (name = (gchar*)data; *data && length; length--, data++) {}
         if (length) {
            data++;
            length--;
            if (g_utf8_validate(name, ((gchar*)data) - name, NULL)) {
               mongo_message_delete_set_collection(delete, name);
               if (length >= 4) {
                  memcpy(&priv->flags, data, sizeof priv->flags);
                  priv->flags = GUINT32_FROM_LE(priv->flags);
                  data += 4;
                  length -= 4;
                  if (length >= 4) {
                     memcpy(&len, data, sizeof len);
                     len = GUINT32_FROM_LE(len);
                     if (len == length) {
                        bson = mongo_bson_new_from_data(data, len);
                        mongo_message_delete_set_selector(delete, bson);
                        mongo_bson_unref(bson);
                        RETURN(TRUE);
                     }
                  }
               }
            }
         }
      }
   }

   RETURN(FALSE);
}

static guint8 *
mongo_message_delete_save_to_data (MongoMessage *message,
                                   gsize        *length)
{
   static const guint8 empty_bson[] = { 5, 0, 0, 0, 0 };
   MongoMessageDeletePrivate *priv;
   MongoMessageDelete *delete = (MongoMessageDelete *)message;
   const guint8 *buf;
   GByteArray *bytes;
   gint32 v32;
   guint8 *ret;
   gsize buflen;

   ENTRY;

   g_assert(MONGO_IS_MESSAGE_DELETE(delete));
   g_assert(length);

   priv = delete->priv;

   bytes = g_byte_array_sized_new(64);

   v32 = 0;
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GINT32_TO_LE(mongo_message_get_request_id(message));
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GINT32_TO_LE(mongo_message_get_response_to(message));
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GUINT32_TO_LE(MONGO_OPERATION_DELETE);
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   /* ZERO, reserved for future use. */
   v32 = 0;
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   /* Collection name */
   g_byte_array_append(bytes, (guint8 *)(priv->collection ?: ""),
                       strlen(priv->collection ?: "") + 1);

   /* Delete flags */
   v32 = GUINT32_TO_LE(priv->flags);
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   /* Selector */
   if ((buf = mongo_bson_get_data(priv->selector, &buflen))) {
      g_byte_array_append(bytes, buf, buflen);
   } else {
      g_byte_array_append(bytes, empty_bson, G_N_ELEMENTS(empty_bson));
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
mongo_message_delete_finalize (GObject *object)
{
   MongoMessageDeletePrivate *priv;

   ENTRY;

   priv = MONGO_MESSAGE_DELETE(object)->priv;

   if (priv->selector) {
      mongo_bson_unref(priv->selector);
      priv->selector = NULL;
   }

   g_free(priv->collection);

   G_OBJECT_CLASS(mongo_message_delete_parent_class)->finalize(object);

   EXIT;
}

static void
mongo_message_delete_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
   MongoMessageDelete *delete = MONGO_MESSAGE_DELETE(object);

   switch (prop_id) {
   case PROP_COLLECTION:
      g_value_set_string(value, mongo_message_delete_get_collection(delete));
      break;
   case PROP_FLAGS:
      g_value_set_flags(value, mongo_message_delete_get_flags(delete));
      break;
   case PROP_SELECTOR:
      g_value_set_boxed(value, mongo_message_delete_get_selector(delete));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_message_delete_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
   MongoMessageDelete *delete = MONGO_MESSAGE_DELETE(object);

   switch (prop_id) {
   case PROP_COLLECTION:
      mongo_message_delete_set_collection(delete, g_value_get_string(value));
      break;
   case PROP_FLAGS:
      mongo_message_delete_set_flags(delete, g_value_get_flags(value));
      break;
   case PROP_SELECTOR:
      mongo_message_delete_set_selector(delete, g_value_get_boxed(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_message_delete_class_init (MongoMessageDeleteClass *klass)
{
   GObjectClass *object_class;
   MongoMessageClass *message_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_message_delete_finalize;
   object_class->get_property = mongo_message_delete_get_property;
   object_class->set_property = mongo_message_delete_set_property;
   g_type_class_add_private(object_class, sizeof(MongoMessageDeletePrivate));

   message_class = MONGO_MESSAGE_CLASS(klass);
   message_class->load_from_data = mongo_message_delete_load_from_data;
   message_class->save_to_data = mongo_message_delete_save_to_data;
}

static void
mongo_message_delete_init (MongoMessageDelete *delete)
{
   delete->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(delete,
                                  MONGO_TYPE_MESSAGE_DELETE,
                                  MongoMessageDeletePrivate);
}
