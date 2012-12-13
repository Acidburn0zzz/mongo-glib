/* mongo-input-stream.c
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
#include "mongo-input-stream.h"
#include "mongo-operation.h"
#include "mongo-source.h"

G_DEFINE_TYPE(MongoInputStream, mongo_input_stream, G_TYPE_FILTER_INPUT_STREAM)

struct _MongoInputStreamPrivate
{
   MongoSource *source;
};

enum
{
   PROP_0,
   PROP_ASYNC_CONTEXT,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

MongoInputStream *
mongo_input_stream_new (GInputStream *base_stream)
{
   return g_object_new(MONGO_TYPE_INPUT_STREAM,
                       "base-stream", base_stream,
                       NULL);
}

GMainContext *
mongo_input_stream_get_async_context (MongoInputStream *stream)
{
   g_return_val_if_fail(MONGO_IS_INPUT_STREAM(stream), NULL);
   return g_source_get_context((GSource *)stream->priv->source);
}

static void
mongo_input_stream_set_async_context (MongoInputStream *stream,
                                      GMainContext     *async_context)
{
   MongoInputStreamPrivate *priv;

   g_return_if_fail(MONGO_IS_INPUT_STREAM(stream));
   g_return_if_fail(!stream->priv->source);

   priv = stream->priv;

   if (!async_context) {
      async_context = g_main_context_default();
   }

   priv->source = mongo_source_new();
   g_source_set_name((GSource *)priv->source, "MongoInputStream");
   g_source_attach((GSource *)priv->source, async_context);

   g_object_notify_by_pspec(G_OBJECT(stream), gParamSpecs[PROP_ASYNC_CONTEXT]);
}

/**
 * mongo_input_stream_read_message_async:
 * @stream: A #MongoInputStream.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: (allow-none): A #GAsyncReadyCallback or %NULL.
 * @user_data: user data for @callback.
 *
 * Asynchronously reads the next message from the #MongoInputStream.
 */
void
mongo_input_stream_read_message_async (MongoInputStream    *stream,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
}

/**
 * mongo_input_stream_read_message_finish:
 * @stream: A #MongoInputStream.
 *
 * Completes an asynchronous request to read the next message from the
 * underlying #GInputStream.
 *
 * Upon failure, %NULL is returned and @error is set.
 *
 * Returns: (transfer full): A #MongoMessage if successful; otherwise
 *    %NULL and @error is set.
 */
MongoMessage *
mongo_input_stream_read_message_finish (MongoInputStream  *stream,
                                        GAsyncResult      *result,
                                        GError           **error)
{
   return NULL;
}

#if 0
/**
 * mongo_input_stream_read_message:
 * @stream: A #MongoInputStream.
 * @cancellable: (allow-none): A #GCancellable, or %NULL.
 * @error: (out): A location for a #GError, or %NULL.
 *
 * Read the next incoming message from the underlying #GInputStream. This
 * will block while reading from the stream.
 *
 * Upon error, %NULL is returned and @error is set.
 *
 * Returns: (transfer full): A #MongoInputStream if successful. Otherwise
 *   %NULL and @error is set.
 */
MongoMessage *
mongo_input_stream_read_message (MongoInputStream  *stream,
                                 GCancellable      *cancellable,
                                 GError           **error)
{
   GDataInputStream *data_stream = (GDataInputStream *)stream;
   MongoOperation op_code;
   MongoMessage *message;
   gint32 msg_len;
   gint32 request_id;
   gint32 response_to;
   GError *local_error = NULL;
   guint8 *buffer;
   gsize bytes_read;
   GType gtype;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_INPUT_STREAM(stream), NULL);
   g_return_val_if_fail(G_IS_DATA_INPUT_STREAM(data_stream), NULL);

   /*
    * Read the message length.
    */
   if (!(msg_len = mongo_input_stream_read_int32(data_stream,
                                                 cancellable,
                                                 error))) {
      RETURN(NULL);
   }

   /*
    * Make sure the message length is valid.
    */
   if (msg_len <= 16) {
      g_set_error(error,
                  MONGO_INPUT_STREAM_ERROR,
                  MONGO_INPUT_STREAM_ERROR_INVALID_MESSAGE,
                  _("Received short message from server."));
      RETURN(NULL);
   }

   /*
    * Read the request_id field.
    */
   if (!(request_id = mongo_input_stream_read_int32(data_stream,
                                                    cancellable,
                                                    &local_error))) {
      if (local_error) {
         g_propagate_error(error, local_error);
         RETURN(NULL);
      }
   }

   /*
    * Read the response_to field.
    */
   if (!(response_to = mongo_input_stream_read_int32(data_stream,
                                                     cancellable,
                                                     &local_error))) {
      if (local_error) {
         g_propagate_error(error, local_error);
         RETURN(NULL);
      }
   }

   /*
    * Read the op_code field.
    */
   if (!(op_code = mongo_input_stream_read_int32(data_stream,
                                                 cancellable,
                                                 &local_error))) {
      if (local_error) {
         g_propagate_error(error, local_error);
         RETURN(NULL);
      }
   }

   /*
    * Make sure this is an op_code we know about.
    */
   if (!(gtype = mongo_operation_get_message_type(op_code))) {
      g_set_error(error,
                  MONGO_INPUT_STREAM_ERROR,
                  MONGO_INPUT_STREAM_ERROR_UNKNOWN_OPERATION,
                  _("Operation code %u is unknown."),
                  op_code);
      /*
       * Try to skip this message gracefully.
       */
      if (!g_input_stream_skip(G_INPUT_STREAM(stream),
                               msg_len,
                               cancellable,
                               NULL)) {
         g_input_stream_close(G_INPUT_STREAM(stream), NULL, NULL);
      }
      RETURN(NULL);
   }

   /*
    * Allocate memory for the incoming buffer.
    */
   buffer = g_malloc(msg_len);
   if (!g_input_stream_read_all(G_INPUT_STREAM(stream),
                                buffer,
                                msg_len - 16,
                                &bytes_read,
                                cancellable,
                                error)) {
      g_free(buffer);
      RETURN(NULL);
   }

   /*
    * If we didn't read all of the data, then fail now.
    */
   if (bytes_read != (msg_len - 16)) {
      g_set_error(error,
                  MONGO_INPUT_STREAM_ERROR,
                  MONGO_INPUT_STREAM_ERROR_INSUFFICIENT_DATA,
                  _("Not enough data was read to complete the message."));
      g_free(buffer);
      RETURN(NULL);
   }

   /*
    * Load the data into a new MongoMessage.
    */
   message = g_object_new(gtype,
                          "request-id", request_id,
                          "response-to", response_to,
                          NULL);
   if (!mongo_message_load_from_data(message, buffer, msg_len - 16)) {
      g_set_error(error,
                  MONGO_INPUT_STREAM_ERROR,
                  MONGO_INPUT_STREAM_ERROR_INVALID_MESSAGE,
                  _("Message contents were corrupted."));
      g_object_unref(message);
      g_free(buffer);
      RETURN(NULL);
   }

   RETURN(message);
}
#endif

static void
mongo_input_stream_finalize (GObject *object)
{
   MongoInputStreamPrivate *priv;

   ENTRY;

   priv = MONGO_INPUT_STREAM(object)->priv;

   g_source_destroy((GSource *)priv->source);
   priv->source = NULL;

   G_OBJECT_CLASS(mongo_input_stream_parent_class)->finalize(object);

   EXIT;
}

static void
mongo_input_stream_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
   MongoInputStream *stream = MONGO_INPUT_STREAM(object);

   switch (prop_id) {
   case PROP_ASYNC_CONTEXT:
      g_value_set_boxed(value, mongo_input_stream_get_async_context(stream));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_input_stream_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
   MongoInputStream *stream = MONGO_INPUT_STREAM(object);

   switch (prop_id) {
   case PROP_ASYNC_CONTEXT:
      mongo_input_stream_set_async_context(stream, g_value_get_boxed(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_input_stream_class_init (MongoInputStreamClass *klass)
{
   GObjectClass *object_class;

   ENTRY;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_input_stream_finalize;
   object_class->get_property = mongo_input_stream_get_property;
   object_class->set_property = mongo_input_stream_set_property;
   g_type_class_add_private(object_class, sizeof(MongoInputStreamPrivate));

   gParamSpecs[PROP_ASYNC_CONTEXT] =
      g_param_spec_boxed("async-context",
                          _("Async Context"),
                          _("The main context to perform callbacks within."),
                          G_TYPE_MAIN_CONTEXT,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_ASYNC_CONTEXT,
                                   gParamSpecs[PROP_ASYNC_CONTEXT]);

   EXIT;
}

static void
mongo_input_stream_init (MongoInputStream *stream)
{
   ENTRY;
   stream->priv = G_TYPE_INSTANCE_GET_PRIVATE(stream,
                                              MONGO_TYPE_INPUT_STREAM,
                                              MongoInputStreamPrivate);
   EXIT;
}

GQuark
mongo_input_stream_error_quark (void)
{
   return g_quark_from_static_string("MongoInputStreamError");
}
