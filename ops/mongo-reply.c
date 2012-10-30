/* mongo-reply.c
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
#include "mongo-operation.h"
#include "mongo-reply.h"

G_DEFINE_TYPE(MongoReply, mongo_reply, MONGO_TYPE_MESSAGE)

struct _MongoReplyPrivate
{
   guint32           count;
   guint64           cursor_id;
   MongoBson       **documents;
   MongoReplyFlags   flags;
   guint32           offset;
};

enum
{
   PROP_0,
   PROP_COUNT,
   PROP_CURSOR_ID,
   PROP_FLAGS,
   PROP_OFFSET,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

gsize
mongo_reply_get_count (MongoReply *reply)
{
   g_return_val_if_fail(MONGO_IS_REPLY(reply), 0);
   return reply->priv->count;
}

guint64
mongo_reply_get_cursor_id (MongoReply *reply)
{
   g_return_val_if_fail(MONGO_IS_REPLY(reply), 0);
   return reply->priv->cursor_id;
}

void
mongo_reply_set_cursor_id (MongoReply *reply,
                           guint64     cursor_id)
{
   g_return_if_fail(MONGO_IS_REPLY(reply));
   reply->priv->cursor_id = cursor_id;
   g_object_notify_by_pspec(G_OBJECT(reply), gParamSpecs[PROP_CURSOR_ID]);
}

/**
 * mongo_reply_get_documents:
 * @reply: (in): A #MongoReply.
 * @count: (out): Location for number of documents.
 *
 * Returns an array of documents for the reply. @count is set to the
 * number of documents returned.
 *
 * Returns: (transfer none) (array length=count): An array of MongoBson.
 */
MongoBson **
mongo_reply_get_documents (MongoReply *reply,
                           gsize      *count)
{
   g_return_val_if_fail(MONGO_IS_REPLY(reply), NULL);

   if (count) {
      *count = reply->priv->count;
   }

   return reply->priv->documents;
}

/**
 * mongo_reply_set_documents:
 * @reply: (in): A #MongoReply.
 * @documents: (in) (array length=count) (transfer full): Array of #MongoBson.
 * @count: (in): The number of #MongoBson in @documents.
 *
 * Sets the documents for the reply. Ownership of documents is taken and
 * will unref each document when the reply has finalized as well as freeing
 * the memory for the array with g_free().
 *
 * Count should be set to the number of documents in @documents.
 */
void
mongo_reply_set_documents (MongoReply  *reply,
                           MongoBson  **documents,
                           gsize        count)
{
   MongoReplyPrivate *priv;
   guint i;

   g_return_if_fail(MONGO_IS_REPLY(reply));

   priv = reply->priv;

   if (priv->documents) {
      for (i = 0; i < priv->count; i++) {
         mongo_bson_unref(priv->documents[i]);
      }
      g_free(priv->documents);
   }

   priv->documents = documents;
   priv->count = count;

   g_object_notify_by_pspec(G_OBJECT(reply), gParamSpecs[PROP_COUNT]);
}

MongoReplyFlags
mongo_reply_get_flags (MongoReply *reply)
{
   g_return_val_if_fail(MONGO_IS_REPLY(reply), 0);
   return reply->priv->flags;
}

void
mongo_reply_set_flags (MongoReply      *reply,
                       MongoReplyFlags  flags)
{
   g_return_if_fail(MONGO_IS_REPLY(reply));
   reply->priv->flags = flags;
   g_object_notify_by_pspec(G_OBJECT(reply), gParamSpecs[PROP_FLAGS]);
}

guint
mongo_reply_get_offset (MongoReply *reply)
{
   g_return_val_if_fail(MONGO_IS_REPLY(reply), 0);
   return reply->priv->offset;
}

void
mongo_reply_set_offset (MongoReply *reply,
                        guint       offset)
{
   g_return_if_fail(MONGO_IS_REPLY(reply));
   reply->priv->offset = offset;
   g_object_notify_by_pspec(G_OBJECT(reply), gParamSpecs[PROP_OFFSET]);
}

static guint8 *
mongo_reply_save_to_data (MongoMessage *message,
                          gsize        *length)
{
   MongoReplyPrivate *priv;
   const guint8 *buf;
   MongoReply *reply = (MongoReply *)message;
   GByteArray *bytes;
   gint32 v32;
   gint64 v64;
   gsize buflen;
   guint i;

   g_assert(MONGO_IS_REPLY(reply));
   g_assert(length);

   priv = reply->priv;

   bytes = g_byte_array_sized_new(64);

   v32 = 0;
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GINT32_TO_LE(mongo_message_get_request_id(message));
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GINT32_TO_LE(mongo_message_get_response_to(message));
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GUINT32_TO_LE(MONGO_OPERATION_REPLY);
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GUINT32_TO_LE(priv->flags);
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v64 = GUINT64_TO_LE(priv->cursor_id);
   g_byte_array_append(bytes, (guint8 *)&v64, sizeof v64);

   v32 = GINT32_TO_LE(priv->offset);
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   v32 = GUINT32_TO_LE(priv->count);
   g_byte_array_append(bytes, (guint8 *)&v32, sizeof v32);

   for (i = 0; i < priv->count; i++) {
      if ((buf = mongo_bson_get_data(priv->documents[i], &buflen))) {
         g_byte_array_append(bytes, buf, buflen);
      }
   }

   v32 = GUINT32_TO_LE(bytes->len);
   memcpy(bytes->data, &v32, sizeof v32);

   *length = bytes->len;

   DUMP_BYTES(buf, bytes->data, bytes->len);

   return g_byte_array_free(bytes, FALSE);
}

static void
mongo_reply_finalize (GObject *object)
{
   MongoReplyPrivate *priv;
   guint i;

   ENTRY;

   priv = MONGO_REPLY(object)->priv;

   for (i = 0; i < priv->count; i++) {
      mongo_bson_unref(priv->documents[i]);
   }

   g_free(priv->documents);

   G_OBJECT_CLASS(mongo_reply_parent_class)->finalize(object);

   EXIT;
}

static void
mongo_reply_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
   MongoReply *reply = MONGO_REPLY(object);

   switch (prop_id) {
   case PROP_COUNT:
      g_value_set_uint(value, mongo_reply_get_count(reply));
      break;
   case PROP_CURSOR_ID:
      g_value_set_uint64(value, mongo_reply_get_cursor_id(reply));
      break;
   case PROP_FLAGS:
      g_value_set_flags(value, mongo_reply_get_flags(reply));
      break;
   case PROP_OFFSET:
      g_value_set_uint(value, mongo_reply_get_offset(reply));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_reply_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
   MongoReply *reply = MONGO_REPLY(object);

   switch (prop_id) {
   case PROP_CURSOR_ID:
      mongo_reply_set_cursor_id(reply, g_value_get_uint64(value));
      break;
   case PROP_FLAGS:
      mongo_reply_set_flags(reply, g_value_get_flags(value));
      break;
   case PROP_OFFSET:
      mongo_reply_set_offset(reply, g_value_get_uint(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_reply_class_init (MongoReplyClass *klass)
{
   GObjectClass *object_class;
   MongoMessageClass *message_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_reply_finalize;
   object_class->get_property = mongo_reply_get_property;
   object_class->set_property = mongo_reply_set_property;
   g_type_class_add_private(object_class, sizeof(MongoReplyPrivate));

   message_class = MONGO_MESSAGE_CLASS(klass);
   message_class->save_to_data = mongo_reply_save_to_data;

   gParamSpecs[PROP_COUNT] =
      g_param_spec_uint("count",
                        _("Count"),
                        _("The number of documents."),
                        0,
                        G_MAXUINT32,
                        0,
                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_COUNT,
                                   gParamSpecs[PROP_COUNT]);

   gParamSpecs[PROP_CURSOR_ID] =
      g_param_spec_uint64("cursor-id",
                          _("Cursor Id"),
                          _("The cursor identifier."),
                          0,
                          G_MAXUINT64,
                          0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_CURSOR_ID,
                                   gParamSpecs[PROP_CURSOR_ID]);

   gParamSpecs[PROP_FLAGS] =
      g_param_spec_flags("flags",
                         _("Flags"),
                         _("The reply flags."),
                         MONGO_TYPE_REPLY_FLAGS,
                         MONGO_REPLY_NONE,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_FLAGS,
                                   gParamSpecs[PROP_FLAGS]);

   gParamSpecs[PROP_OFFSET] =
      g_param_spec_uint("offset",
                        _("Offset"),
                        _("The offset of the documents in the result set."),
                        0,
                        G_MAXUINT32,
                        0,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_OFFSET,
                                   gParamSpecs[PROP_OFFSET]);
}

static void
mongo_reply_init (MongoReply *reply)
{
   reply->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(reply,
                                  MONGO_TYPE_REPLY,
                                  MongoReplyPrivate);
}
