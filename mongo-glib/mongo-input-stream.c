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

G_DEFINE_TYPE(MongoInputStream, mongo_input_stream, G_TYPE_DATA_INPUT_STREAM)

MongoInputStream *
mongo_input_stream_new (GInputStream *base_stream)
{
   return g_object_new(MONGO_TYPE_INPUT_STREAM,
                       "base-stream", base_stream,
                       "byte-order", G_DATA_STREAM_BYTE_ORDER_LITTLE_ENDIAN,
                       NULL);
}

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
   if (!(msg_len = g_data_input_stream_read_uint32(data_stream,
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
   if (!(request_id = g_data_input_stream_read_int32(data_stream,
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
   if (!(response_to = g_data_input_stream_read_int32(data_stream,
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
   if (!(op_code = g_data_input_stream_read_int32(data_stream,
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

static void
mongo_input_stream_class_init (MongoInputStreamClass *klass)
{
}

static void
mongo_input_stream_init (MongoInputStream *stream)
{
   g_data_input_stream_set_byte_order(G_DATA_INPUT_STREAM(stream),
                                      G_DATA_STREAM_BYTE_ORDER_LITTLE_ENDIAN);
}

GQuark
mongo_input_stream_error_quark (void)
{
   return g_quark_from_static_string("MongoInputStreamError");
}
