/* mongo-protocol.c
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

#include "mongo-debug.h"
#include "mongo-protocol.h"

G_DEFINE_TYPE(MongoProtocol, mongo_protocol, G_TYPE_OBJECT)

static void
mongo_protocol_fill_header_cb (GBufferedInputStream *input_stream,
                               GAsyncResult         *result,
                               MongoProtocol        *protocol);

typedef struct
{
   const guint8 *buffer;
   gssize count;
   gsize offset;
} Reader;

struct _MongoProtocolPrivate
{
   GIOStream *io_stream;
   GInputStream *input_stream;
   GOutputStream *output_stream;
   guint32 last_request_id;
   GCancellable *shutdown;
   GHashTable *requests;
   gint getlasterror_w;
   gboolean getlasterror_j;
   guint flush_handler;
};

enum
{
   OP_REPLY        = 1,
   OP_MSG          = 1000,
   OP_UPDATE       = 2001,
   OP_INSERT       = 2002,
   OP_QUERY        = 2004,
   OP_GETMORE      = 2005,
   OP_DELETE       = 2006,
   OP_KILL_CURSORS = 2007,
};

enum
{
   PROP_0,
   PROP_IO_STREAM,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

MongoReply *
mongo_reply_ref (MongoReply *reply)
{
   g_return_val_if_fail(reply, NULL);
   g_return_val_if_fail(reply->ref_count > 0, NULL);
   g_atomic_int_inc(&reply->ref_count);
   return reply;
}

void
mongo_reply_unref (MongoReply *reply)
{
   guint i;

   g_return_if_fail(reply);
   g_return_if_fail(reply->ref_count > 0);

   if (g_atomic_int_dec_and_test(&reply->ref_count)) {
      for (i = 0; i < reply->n_returned; i++) {
         mongo_bson_unref(reply->documents[i]);
      }
      g_slice_free(MongoReply, reply);
   }
}

GType
mongo_reply_get_type (void)
{
   static gsize initialized = FALSE;
   static GType type_id;

   if (g_once_init_enter(&initialized)) {
      type_id = g_boxed_type_register_static("MongoReply",
                                             (GBoxedCopyFunc)mongo_reply_ref,
                                             (GBoxedFreeFunc)mongo_reply_unref);
      g_once_init_leave(&initialized, TRUE);
   }

   return type_id;
}

static void
_g_byte_array_append_bson (GByteArray      *array,
                           const MongoBson *bson)
{
   const guint8 *data;
   gsize data_len = 0;

   ENTRY;

   data = mongo_bson_get_data(bson, &data_len);
   g_assert(data);
   g_assert_cmpint(data_len, >, 0);

   DUMP_BYTES(data, data, data_len);

   g_byte_array_append(array, data, data_len);

   EXIT;
}

static void
_g_byte_array_append_cstring (GByteArray  *array,
                              const gchar *value)
{
   ENTRY;
   g_byte_array_append(array, (guint8 *)value, strlen(value) + 1);
   EXIT;
}

static void
_g_byte_array_append_int32 (GByteArray *array,
                            gint32      value)
{
   ENTRY;
   g_byte_array_append(array, (guint8 *)&value, sizeof value);
   EXIT;
}

static void
_g_byte_array_append_int64 (GByteArray *array,
                            gint64      value)
{
   ENTRY;
   g_byte_array_append(array, (guint8 *)&value, sizeof value);
   EXIT;
}

static void
_g_byte_array_overwrite_int32 (GByteArray *array,
                               guint       offset,
                               gint32      value)
{
   ENTRY;
   g_assert_cmpint(offset, <, array->len);
   memcpy(array->data + offset, &value, sizeof value);
   EXIT;
}

static void
mongo_protocol_fail (MongoProtocol *protocol,
                     const GError  *error)
{
   ENTRY;
   /*
    * TODO: Fail the connection.
    */
   EXIT;
}

static void
mongo_protocol_flush_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
   GOutputStream *output_stream = (GOutputStream *)object;
   MongoProtocol *protocol = user_data;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));

   g_output_stream_flush_finish(output_stream, result, NULL);
   g_object_unref(protocol);

   EXIT;
}

void
mongo_protocol_flush_sync (MongoProtocol *protocol)
{
   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_output_stream_flush(protocol->priv->output_stream, NULL, NULL);
}

static gboolean
mongo_protocol_flush (MongoProtocol *protocol)
{
   ENTRY;

   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), FALSE);

   g_output_stream_flush_async(protocol->priv->output_stream,
                               G_PRIORITY_DEFAULT,
                               protocol->priv->shutdown,
                               mongo_protocol_flush_cb,
                               protocol);
   protocol->priv->flush_handler = 0;

   RETURN(FALSE);
}

static void
mongo_protocol_queue_flush (MongoProtocol *protocol)
{
   MongoProtocolPrivate *priv;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));

   priv = protocol->priv;

   if (!priv->flush_handler) {
      priv->flush_handler =
         g_timeout_add(0, (GSourceFunc)mongo_protocol_flush,
                       g_object_ref(protocol));
   }
}

static void
mongo_protocol_write (MongoProtocol      *protocol,
                      guint32             request_id,
                      GSimpleAsyncResult *simple,
                      const guint8       *buffer,
                      gsize               buffer_len)
{
   MongoProtocolPrivate *priv;
   GError *error = NULL;
   gsize n_written = 0;

   ENTRY;

   g_assert(MONGO_IS_PROTOCOL(protocol));
   g_assert(request_id);
   g_assert(G_IS_SIMPLE_ASYNC_RESULT(simple));
   g_assert(buffer);
   g_assert(buffer_len);

   priv = protocol->priv;

   DUMP_BYTES(buffer, buffer, buffer_len);

   if (!g_output_stream_write_all(priv->output_stream,
                                  buffer,
                                  buffer_len,
                                  &n_written,
                                  NULL, &error)) {
      mongo_protocol_fail(protocol, error);
      g_simple_async_result_take_error(simple, error);
      g_simple_async_result_complete_in_idle(simple);
      g_object_unref(simple);
      EXIT;
   }

   mongo_protocol_queue_flush(protocol);

   EXIT;
}

static void
mongo_protocol_append_getlasterror (MongoProtocol *protocol,
                                    GByteArray    *array,
                                    const gchar   *db_and_collection)
{
   MongoProtocolPrivate *priv;
   MongoBson *bson;
   guint32 request_id;
   guint offset;

   ENTRY;

   g_assert(MONGO_IS_PROTOCOL(protocol));
   g_assert(array);
   g_assert(db_and_collection);

   priv = protocol->priv;

   offset = array->len;
   request_id = ++protocol->priv->last_request_id;

   /*
    * Build getlasterror command spec.
    */
   bson = mongo_bson_new_empty();
   mongo_bson_append_int(bson, "getlasterror", 1);
   mongo_bson_append_boolean(bson, "j", priv->getlasterror_j);
   if (priv->getlasterror_w < 0) {
      mongo_bson_append_string(bson, "w", "majority");
   } else if (priv->getlasterror_w > 0) {
      mongo_bson_append_int(bson, "w", priv->getlasterror_w);
   }

   /*
    * Build the OP_QUERY message.
    */
   _g_byte_array_append_int32(array, 0);
   _g_byte_array_append_int32(array, GINT32_TO_LE(request_id));
   _g_byte_array_append_int32(array, 0);
   _g_byte_array_append_int32(array, GINT32_TO_LE(OP_QUERY));
   _g_byte_array_append_int32(array, GINT32_TO_LE(MONGO_QUERY_NONE));
   _g_byte_array_append_cstring(array, "admin.$cmd");
   _g_byte_array_append_int32(array, 0);
   _g_byte_array_append_int32(array, 0);
   _g_byte_array_append_bson(array, bson);
   _g_byte_array_overwrite_int32(array, offset,
                                 GINT32_TO_LE(array->len - offset));

   mongo_bson_unref(bson);

   EXIT;
}

void
mongo_protocol_update_async (MongoProtocol       *protocol,
                             const gchar         *db_and_collection,
                             MongoUpdateFlags     flags,
                             const MongoBson     *selector,
                             const MongoBson     *update,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
   MongoProtocolPrivate *priv;
   GSimpleAsyncResult *simple;
   GByteArray *buffer;
   guint32 request_id;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(selector);
   g_return_if_fail(update);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = protocol->priv;

   simple = g_simple_async_result_new(G_OBJECT(protocol), callback, user_data,
                                      mongo_protocol_update_async);

   request_id = ++priv->last_request_id;

   buffer = g_byte_array_new();
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(request_id));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(OP_UPDATE));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_cstring(buffer, db_and_collection);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(flags));
   _g_byte_array_append_bson(buffer, selector);
   _g_byte_array_append_bson(buffer, update);
   _g_byte_array_overwrite_int32(buffer, 0, GINT32_TO_LE(buffer->len));
   mongo_protocol_append_getlasterror(protocol, buffer, db_and_collection);

   /*
    * We get our response from the getlasterror command, so use it's request
    * id as the key in the hashtable.
    */
   g_hash_table_insert(priv->requests,
                       GINT_TO_POINTER(request_id + 1),
                       simple);

   /*
    * Write the bytes to the buffered stream.
    */
   mongo_protocol_write(protocol, request_id, simple,
                        buffer->data, buffer->len);

   EXIT;
}

gboolean
mongo_protocol_update_finish (MongoProtocol  *protocol,
                              GAsyncResult   *result,
                              GError        **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

void
mongo_protocol_insert_async (MongoProtocol        *protocol,
                             const gchar          *db_and_collection,
                             MongoInsertFlags      flags,
                             MongoBson           **documents,
                             gsize                 n_documents,
                             GCancellable         *cancellable,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data)
{
   MongoProtocolPrivate *priv;
   GSimpleAsyncResult *simple;
   GByteArray *buffer;
   guint32 request_id;
   guint i;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(documents);
   g_return_if_fail(n_documents >= 1);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = protocol->priv;

   simple = g_simple_async_result_new(G_OBJECT(protocol), callback, user_data,
                                      mongo_protocol_insert_async);

   request_id = ++priv->last_request_id;

   buffer = g_byte_array_new();
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(request_id));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(OP_INSERT));
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(flags));
   _g_byte_array_append_cstring(buffer, db_and_collection);
   for (i = 0; i < n_documents; i++) {
      _g_byte_array_append_bson(buffer, documents[i]);
   }
   _g_byte_array_overwrite_int32(buffer, 0, GINT32_TO_LE(buffer->len));
   mongo_protocol_append_getlasterror(protocol, buffer, db_and_collection);

   /*
    * We get our response from the getlasterror command, so use it's request
    * id as the key in the hashtable.
    */
   g_hash_table_insert(priv->requests,
                       GINT_TO_POINTER(request_id + 1),
                       simple);

   /*
    * Write the bytes to the buffered stream.
    */
   mongo_protocol_write(protocol, request_id, simple,
                        buffer->data, buffer->len);

   EXIT;
}

gboolean
mongo_protocol_insert_finish (MongoProtocol  *protocol,
                              GAsyncResult   *result,
                              GError        **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

void
mongo_protocol_query_async (MongoProtocol       *protocol,
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
   MongoProtocolPrivate *priv;
   GSimpleAsyncResult *simple;
   GByteArray *buffer;
   guint32 request_id;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(query);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = protocol->priv;

   simple = g_simple_async_result_new(G_OBJECT(protocol), callback, user_data,
                                      mongo_protocol_query_async);

   request_id = ++priv->last_request_id;

   buffer = g_byte_array_new();
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(request_id));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(OP_QUERY));
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(flags));
   _g_byte_array_append_cstring(buffer, db_and_collection);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(skip));
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(limit));
   _g_byte_array_append_bson(buffer, query);
   if (field_selector) {
      _g_byte_array_append_bson(buffer, field_selector);
   }
   _g_byte_array_overwrite_int32(buffer, 0, GINT32_TO_LE(buffer->len));

   g_hash_table_insert(priv->requests, GINT_TO_POINTER(request_id), simple);
   mongo_protocol_write(protocol, request_id, simple,
                        buffer->data, buffer->len);

   EXIT;
}

MongoReply *
mongo_protocol_query_finish (MongoProtocol    *protocol,
                             GAsyncResult     *result,
                             GError          **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   MongoReply *reply;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), NULL);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), NULL);

   if (!(reply = g_simple_async_result_get_op_res_gpointer(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   reply = reply ? mongo_reply_ref(reply) : NULL;

   RETURN(reply);
}

void
mongo_protocol_getmore_async (MongoProtocol       *protocol,
                              const gchar         *db_and_collection,
                              guint32              limit,
                              guint64              cursor_id,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
   MongoProtocolPrivate *priv;
   GSimpleAsyncResult *simple;
   GByteArray *buffer;
   guint32 request_id;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = protocol->priv;

   simple = g_simple_async_result_new(G_OBJECT(protocol), callback, user_data,
                                      mongo_protocol_getmore_async);

   request_id = ++priv->last_request_id;

   buffer = g_byte_array_new();
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(request_id));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(OP_GETMORE));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_cstring(buffer, db_and_collection);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(limit));
   _g_byte_array_append_int64(buffer, GINT64_TO_LE(cursor_id));
   _g_byte_array_overwrite_int32(buffer, 0, GINT32_TO_LE(buffer->len));

   g_hash_table_insert(priv->requests, GINT_TO_POINTER(request_id), simple);
   mongo_protocol_write(protocol, request_id, simple,
                        buffer->data, buffer->len);

   EXIT;
}

MongoReply *
mongo_protocol_getmore_finish (MongoProtocol  *protocol,
                               GAsyncResult   *result,
                               GError        **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   MongoReply *reply;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), NULL);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), NULL);

   if (!(reply = g_simple_async_result_get_op_res_gpointer(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   reply = reply ? mongo_reply_ref(reply) : NULL;

   RETURN(reply);
}

void
mongo_protocol_delete_async (MongoProtocol       *protocol,
                             const gchar         *db_and_collection,
                             MongoDeleteFlags     flags,
                             const MongoBson     *selector,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
   MongoProtocolPrivate *priv;
   GSimpleAsyncResult *simple;
   GByteArray *buffer;
   guint32 request_id;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(db_and_collection);
   g_return_if_fail(selector);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = protocol->priv;

   simple = g_simple_async_result_new(G_OBJECT(protocol), callback, user_data,
                                      mongo_protocol_delete_async);

   request_id = ++priv->last_request_id;

   buffer = g_byte_array_new();
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(request_id));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(OP_DELETE));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_cstring(buffer, db_and_collection);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(flags));
   _g_byte_array_append_bson(buffer, selector);
   _g_byte_array_overwrite_int32(buffer, 0, GINT32_TO_LE(buffer->len));
   mongo_protocol_append_getlasterror(protocol, buffer, db_and_collection);

   /*
    * We get our response from the getlasterror command, so use it's request
    * id as the key in the hashtable.
    */
   g_hash_table_insert(priv->requests,
                       GINT_TO_POINTER(request_id + 1),
                       simple);

   /*
    * Write the bytes to the buffered stream.
    */
   mongo_protocol_write(protocol, request_id, simple,
                        buffer->data, buffer->len);

   EXIT;
}

gboolean
mongo_protocol_delete_finish (MongoProtocol  *protocol,
                              GAsyncResult   *result,
                              GError        **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

void
mongo_protocol_kill_cursors_async (MongoProtocol       *protocol,
                                   guint64             *cursors,
                                   gsize                n_cursors,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
   MongoProtocolPrivate *priv;
   GSimpleAsyncResult *simple;
   GByteArray *buffer;
   guint32 request_id;
   guint i;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(cursors);
   g_return_if_fail(n_cursors);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = protocol->priv;

   simple = g_simple_async_result_new(G_OBJECT(protocol), callback, user_data,
                                      mongo_protocol_kill_cursors_async);

   request_id = ++priv->last_request_id;

   buffer = g_byte_array_new();
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(request_id));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(OP_KILL_CURSORS));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, n_cursors);
   for (i = 0; i < n_cursors; i++) {
      _g_byte_array_append_int64(buffer, cursors[i]);
   }
   _g_byte_array_overwrite_int32(buffer, 0, GINT32_TO_LE(buffer->len));

   g_hash_table_insert(priv->requests, GINT_TO_POINTER(request_id), simple);
   mongo_protocol_write(protocol, request_id, simple,
                        buffer->data, buffer->len);

   EXIT;
}

gboolean
mongo_protocol_kill_cursors_finish (MongoProtocol  *protocol,
                                    GAsyncResult   *result,
                                    GError        **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

void
mongo_protocol_msg_async (MongoProtocol       *protocol,
                          const gchar         *message,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
   MongoProtocolPrivate *priv;
   GSimpleAsyncResult *simple;
   GByteArray *buffer;
   guint32 request_id;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(message);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = protocol->priv;

   simple = g_simple_async_result_new(G_OBJECT(protocol), callback, user_data,
                                      mongo_protocol_msg_async);

   request_id = ++priv->last_request_id;

   buffer = g_byte_array_new();
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(request_id));
   _g_byte_array_append_int32(buffer, 0);
   _g_byte_array_append_int32(buffer, GINT32_TO_LE(OP_MSG));
   _g_byte_array_append_cstring(buffer, message);
   _g_byte_array_overwrite_int32(buffer, 0, GINT32_TO_LE(buffer->len));

   g_hash_table_insert(priv->requests, GINT_TO_POINTER(request_id), simple);
   mongo_protocol_write(protocol, request_id, simple,
                        buffer->data, buffer->len);

   EXIT;
}

gboolean
mongo_protocol_msg_finish (MongoProtocol  *protocol,
                           GAsyncResult   *result,
                           GError        **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   RETURN(ret);
}

/**
 * mongo_protocol_get_io_stream:
 * @protocol: (in): A #MongoProtocol.
 *
 * Fetch the #GIOStream used by @protocol.
 *
 * Returns: (transfer none): A #GIOStream.
 */
GIOStream *
mongo_protocol_get_io_stream (MongoProtocol *protocol)
{
   g_return_val_if_fail(MONGO_IS_PROTOCOL(protocol), NULL);
   return protocol->priv->io_stream;
}

static void
reader_init (Reader       *reader,
             const guint8 *buffer,
             gssize        count)
{
   ENTRY;
   reader->buffer = buffer;
   reader->count = count;
   reader->offset = 0;
   EXIT;
}

static MongoBson *
reader_next (Reader *reader)
{
   MongoBson *bson;
   guint32 bson_size;

   ENTRY;

   if (reader->count < 0) {
      RETURN(NULL);
   }

   if ((reader->offset + sizeof bson_size) <= reader->count) {
      bson_size = GUINT32_FROM_LE(*(const guint32 *)(reader->buffer + reader->offset));
      if ((reader->offset + bson_size) <= reader->count) {
         bson = mongo_bson_new_from_data(reader->buffer + reader->offset, bson_size);
         reader->offset += bson_size;
         RETURN(bson);
      }
   }

   RETURN(NULL);
}

static void
mongo_protocol_fill_message_cb (GBufferedInputStream *input_stream,
                                GAsyncResult         *result,
                                MongoProtocol        *protocol)
{
   MongoProtocolPrivate *priv;
   GSimpleAsyncResult *request;
   const guint8 *buffer;
   MongoReply *r;
   Reader reader;
#pragma pack(1)
   struct {
      guint32 len;
      guint32 request_id;
      guint32 response_to;
      guint32 op_code;
      guint32 response_flags;
      guint64 cursor_id;
      guint32 starting_from;
      guint32 n_returned;
   } reply;
#pragma pack()
   GPtrArray *docs;
   MongoBson *bson;
   GError *error = NULL;
   guint8 *doc_buffer;
   gsize count;

   ENTRY;

   g_assert(G_IS_BUFFERED_INPUT_STREAM(input_stream));
   g_assert(G_IS_ASYNC_RESULT(result));
   g_assert(MONGO_IS_PROTOCOL(protocol));

   priv = protocol->priv;

   /*
    * Check if succeeded filling buffered input with Mongo reply message.
    */
   if (!g_buffered_input_stream_fill_finish(input_stream, result, &error)) {
      /*
       * TODO: Check if this was a cancellation from our finalizer.
       */
      g_assert_not_reached();
      EXIT;
   }

   buffer = g_buffered_input_stream_peek_buffer(input_stream, &count);
   g_assert(buffer);
   g_assert_cmpint(count, >=, 36);

   /*
    * Process the incoming OP_REPLY.
    */
   memcpy(&reply, buffer, sizeof reply);
#if G_BYTE_ORDER != G_LITTLE_ENDIAN
   reply.len = GUINT32_FROM_LE(reply.len);
   reply.request_id = GUINT32_FROM_LE(reply.request_id);
   reply.response_to = GUINT32_FROM_LE(reply.response_to);
   reply.op_code = GUINT32_FROM_LE(reply.op_code);
   reply.response_flags = GUINT32_FROM_LE(reply.response_flags);
   reply.cursor_id = GUINT64_FROM_LE(reply.cursor_id);
   reply.starting_from = GUINT32_FROM_LE(reply.starting_from);
   reply.n_returned = GUINT32_FROM_LE(reply.n_returned);
#endif

   if (reply.op_code != OP_REPLY) {
      GOTO(failure);
   }

   g_input_stream_skip(G_INPUT_STREAM(input_stream),
                       sizeof reply, NULL, NULL);

   count = 0;
   doc_buffer = g_malloc(reply.len);
   g_input_stream_read_all(G_INPUT_STREAM(input_stream),
                           doc_buffer, reply.len - sizeof reply,
                           &count, NULL, &error);

   DUMP_BYTES(buffer, buffer, count);

   if (count != (reply.len - sizeof reply)) {
      g_free(doc_buffer);
      GOTO(failure);
   }

   docs = g_ptr_array_new();
   reader_init(&reader, doc_buffer, count);
   while ((bson = reader_next(&reader))) {
      g_ptr_array_add(docs, bson);
   }

   g_free(doc_buffer);
   g_assert_cmpint(docs->len, ==, reply.n_returned);

   /*
    * See if there was someone waiting for this request.
    */
   if ((request = g_hash_table_lookup(priv->requests,
                                      GINT_TO_POINTER(reply.response_to)))) {
      r = g_slice_new(MongoReply);
      r->ref_count = 1;
      r->flags = reply.response_flags;
      r->cursor_id = reply.cursor_id;
      r->starting_from = reply.starting_from;
      r->n_returned = docs->len;
      r->documents = (MongoBson **)g_ptr_array_free(docs, FALSE);
      g_simple_async_result_set_op_res_gpointer(
            request, r, (GDestroyNotify)mongo_reply_unref);
      g_simple_async_result_complete_in_idle(request);
      g_hash_table_remove(priv->requests, GINT_TO_POINTER(reply.response_to));
   }

   /*
    * Wait for the next message to arrive.
    */
   g_buffered_input_stream_fill_async(
         input_stream,
         16, /* sizeof MsgHeader */
         G_PRIORITY_DEFAULT,
         priv->shutdown,
         (GAsyncReadyCallback)mongo_protocol_fill_header_cb,
         protocol);

   EXIT;

failure:
   mongo_protocol_fail(protocol, error);
   EXIT;
}

static void
mongo_protocol_fill_header_cb (GBufferedInputStream *input_stream,
                               GAsyncResult         *result,
                               MongoProtocol        *protocol)
{
   MongoProtocolPrivate *priv;
   const guint8 *buffer;
   guint32 msg_len;
   guint32 op_code;
   GError *error = NULL;
   gsize count;

   ENTRY;

   g_assert(G_IS_BUFFERED_INPUT_STREAM(input_stream));
   g_assert(G_IS_ASYNC_RESULT(result));
   g_assert(MONGO_IS_PROTOCOL(protocol));

   priv = protocol->priv;

   /*
    * Check if we succeeded filling buffered input with Mongo reply header.
    */
   if (!g_buffered_input_stream_fill_finish(input_stream, result, &error)) {
      /*
       * TODO: Check if this was a cancellation from our finalizer.
       */
      g_assert_not_reached();
      EXIT;
   }

   /*
    * Read the message length so that we may wait until that many bytes have
    * been filled into the buffer.
    */
   buffer = g_buffered_input_stream_peek_buffer(input_stream, &count);
   g_assert_cmpint(count, >=, 16);

   DUMP_BYTES(buffer, buffer, count);

   /*
    * Determine the size of incoming message and op_code.
    */
   msg_len = GUINT32_FROM_LE(*(const guint32 *)buffer);
   op_code = GUINT32_FROM_LE(*(const guint32 *)(buffer + 12));

   /*
    * We only know about OP_REPLY from the server. Everything else is a
    * protocol error.
    */
   if (op_code != OP_REPLY) {
      mongo_protocol_fail(protocol, NULL);
   }

   /*
    * Wait until the entire message has been received.
    */
   g_buffered_input_stream_fill_async(
         input_stream,
         MAX(36, msg_len),
         G_PRIORITY_DEFAULT,
         priv->shutdown,
         (GAsyncReadyCallback)mongo_protocol_fill_message_cb,
         protocol);

   EXIT;
}

static void
mongo_protocol_set_io_stream (MongoProtocol *protocol,
                              GIOStream     *io_stream)
{
   MongoProtocolPrivate *priv;
   GOutputStream *output_stream;
   GInputStream *input_stream;

   ENTRY;

   g_return_if_fail(MONGO_IS_PROTOCOL(protocol));
   g_return_if_fail(G_IS_IO_STREAM(io_stream));
   g_return_if_fail(!protocol->priv->io_stream);

   priv = protocol->priv;

   priv->io_stream = g_object_ref(io_stream);

   input_stream = g_io_stream_get_input_stream(io_stream);
   priv->input_stream = g_buffered_input_stream_new(input_stream);
#if 0
   g_buffered_input_stream_set_buffer_size(
         G_BUFFERED_INPUT_STREAM(input_stream),
         4096);
#endif

   output_stream = g_io_stream_get_output_stream(io_stream);
   priv->output_stream = g_buffered_output_stream_new(output_stream);
   g_buffered_output_stream_set_auto_grow(
         G_BUFFERED_OUTPUT_STREAM(priv->output_stream),
         TRUE);

   g_buffered_input_stream_fill_async(
         G_BUFFERED_INPUT_STREAM(priv->input_stream),
         16, /* sizeof MsgHeader */
         G_PRIORITY_DEFAULT,
         priv->shutdown,
         (GAsyncReadyCallback)mongo_protocol_fill_header_cb,
         protocol);


   EXIT;
}

static void
mongo_protocol_finalize (GObject *object)
{
   MongoProtocolPrivate *priv;
   GHashTable *hash;

   ENTRY;

   priv = MONGO_PROTOCOL(object)->priv;

   g_cancellable_cancel(priv->shutdown);

   if ((hash = priv->requests)) {
      priv->requests = NULL;
      g_hash_table_unref(hash);
   }

   g_clear_object(&priv->shutdown);
   g_clear_object(&priv->input_stream);
   g_clear_object(&priv->output_stream);
   g_clear_object(&priv->io_stream);

   G_OBJECT_CLASS(mongo_protocol_parent_class)->finalize(object);

   EXIT;
}

static void
mongo_protocol_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
   MongoProtocol *protocol = MONGO_PROTOCOL(object);

   switch (prop_id) {
   case PROP_IO_STREAM:
      g_value_set_object(value, mongo_protocol_get_io_stream(protocol));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_protocol_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
   MongoProtocol *protocol = MONGO_PROTOCOL(object);

   switch (prop_id) {
   case PROP_IO_STREAM:
      mongo_protocol_set_io_stream(protocol, g_value_get_object(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_protocol_class_init (MongoProtocolClass *klass)
{
   GObjectClass *object_class;

   ENTRY;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_protocol_finalize;
   object_class->get_property = mongo_protocol_get_property;
   object_class->set_property = mongo_protocol_set_property;
   g_type_class_add_private(object_class, sizeof(MongoProtocolPrivate));

   gParamSpecs[PROP_IO_STREAM] =
      g_param_spec_object("io-stream",
                          _("I/O Stream"),
                          _("I/O stream to communicate over."),
                          G_TYPE_IO_STREAM,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
   g_object_class_install_property(object_class, PROP_IO_STREAM,
                                   gParamSpecs[PROP_IO_STREAM]);

   EXIT;
}

static void
mongo_protocol_init (MongoProtocol *protocol)
{
   ENTRY;

   protocol->priv = G_TYPE_INSTANCE_GET_PRIVATE(protocol,
                                                MONGO_TYPE_PROTOCOL,
                                                MongoProtocolPrivate);
   protocol->priv->last_request_id = g_random_int();
   protocol->priv->getlasterror_w = 0;
   protocol->priv->getlasterror_j = TRUE;
   protocol->priv->shutdown = g_cancellable_new();
   protocol->priv->requests = g_hash_table_new_full(g_direct_hash,
                                                    g_direct_equal,
                                                    NULL,
                                                    g_object_unref);

   EXIT;
}
