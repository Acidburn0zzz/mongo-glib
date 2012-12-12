/* mongo-output-stream.c
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
#include "mongo-output-stream.h"

G_DEFINE_TYPE(MongoOutputStream, mongo_output_stream, G_TYPE_DATA_OUTPUT_STREAM)

MongoOutputStream *
mongo_output_stream_new (GOutputStream *base_stream)
{
   return g_object_new(MONGO_TYPE_OUTPUT_STREAM,
                       "base-stream", base_stream,
                       NULL);
}

gboolean
mongo_output_stream_write_message (MongoOutputStream  *stream,
                                   MongoMessage       *message,
                                   GCancellable       *cancellable,
                                   GError            **error)
{
   GDataOutputStream *data_stream = (GDataOutputStream *)stream;
#if 0
   MongoOperation op_code;
   guint32 msg_len;
   gint32 request_id;
   gint32 response_to;
#endif
   guint8 *buffer;
   gsize length;
   gsize bytes_written;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_OUTPUT_STREAM(stream), FALSE);
   g_return_val_if_fail(G_IS_DATA_OUTPUT_STREAM(data_stream), FALSE);
   g_return_val_if_fail(MONGO_IS_MESSAGE(message), FALSE);

   if (!(buffer = mongo_message_save_to_data(message, &length))) {
      g_set_error(error,
                  MONGO_OUTPUT_STREAM_ERROR,
                  MONGO_OUTPUT_STREAM_ERROR_INVALID_MESSAGE,
                  _("Failed to serialize message."));
      g_output_stream_close(G_OUTPUT_STREAM(stream), cancellable, NULL);
      RETURN(FALSE);
   }

   /*
    * XXX: I would really like to make the messages not write headers.
    */
#if 0
   msg_len = 16 + length;
   request_id = mongo_message_get_request_id(message);
   response_to = mongo_message_get_response_to(message);
   op_code = MONGO_MESSAGE_GET_CLASS(message)->operation;

   /*
    * Write the message header to the stream.
    */
   if (!g_data_output_stream_put_uint32(data_stream, msg_len, cancellable, error) ||
       !g_data_output_stream_put_int32(data_stream, request_id, cancellable, error) ||
       !g_data_output_stream_put_int32(data_stream, response_to, cancellable, error) ||
       !g_data_output_stream_put_int32(data_stream, op_code, cancellable, error)) {
      g_output_stream_close(G_OUTPUT_STREAM(stream), cancellable, NULL);
      g_free(buffer);
      RETURN(FALSE);
   }
#endif

   /*
    * Write the message contents.
    */
   if (!g_output_stream_write_all(G_OUTPUT_STREAM(stream),
                                  buffer,
                                  length,
                                  &bytes_written,
                                  cancellable,
                                  error)) {
      g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, NULL);
      g_free(buffer);
      RETURN(FALSE);
   }

   g_free(buffer);

   if (bytes_written != length) {
      g_set_error(error,
                  MONGO_OUTPUT_STREAM_ERROR,
                  MONGO_OUTPUT_STREAM_ERROR_SHORT_WRITE,
                  _("Failed to write all data to stream."));
      RETURN(FALSE);
   }

   RETURN(TRUE);
}

static void
mongo_output_stream_class_init (MongoOutputStreamClass *klass)
{
}

static void
mongo_output_stream_init (MongoOutputStream *stream)
{
   g_data_output_stream_set_byte_order(G_DATA_OUTPUT_STREAM(stream),
                                       G_DATA_STREAM_BYTE_ORDER_LITTLE_ENDIAN);
}

GQuark
mongo_output_stream_error_quark (void)
{
   return g_quark_from_static_string("MongoOutputStream");
}
