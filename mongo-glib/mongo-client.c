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
#include "mongo-input-stream.h"
#include "mongo-output-stream.h"

G_DEFINE_TYPE(MongoClient, mongo_client, G_TYPE_OBJECT)

struct _MongoClientPrivate
{
   MongoInputStream  *input;
   MongoOutputStream *output;
   MongoWriteConcern *concern;
};

enum
{
   PROP_0,
   PROP_STREAM,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

static void
mongo_client_set_stream (MongoClient *client,
                         GIOStream   *stream)
{
   MongoClientPrivate *priv;

   ENTRY;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(G_IS_IO_STREAM(stream));

   priv = client->priv;

   g_clear_object(&priv->input);
   g_clear_object(&priv->output);

   priv->input = g_object_ref(g_io_stream_get_input_stream(stream));
   priv->output = g_object_ref(g_io_stream_get_output_stream(stream));

   g_object_notify_by_pspec(G_OBJECT(client), gParamSpecs[PROP_STREAM]);

   EXIT;
}

gboolean
mongo_client_send (MongoClient   *client,
                   MongoMessage  *message,
                   MongoMessage **reply,
                   GCancellable  *cancellable,
                   GError       **error)
{
   MongoClientPrivate *priv;
   MongoMessage *r;
   gint32 request_id;

   ENTRY;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(MONGO_IS_MESSAGE(message), FALSE);
   g_return_val_if_fail(!reply || !*reply, FALSE);
   g_return_val_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable), FALSE);
   g_return_val_if_fail(!error || !*error, FALSE);

   priv = client->priv;

   if (!priv->output || !priv->input) {
      g_set_error(error,
                  MONGO_CLIENT_ERROR,
                  MONGO_CLIENT_ERROR_NOT_CONNECTED,
                  _("Cannot send message, not connected."));
      RETURN(FALSE);
   }

   if (!mongo_output_stream_write_message(priv->output,
                                          message,
                                          priv->concern,
                                          cancellable,
                                          error)) {
      RETURN(FALSE);
   }

   request_id = mongo_message_get_request_id(message);
   while ((r = mongo_input_stream_read_message(priv->input,
                                               cancellable,
                                               error))) {
      if (mongo_message_get_response_to(r) == request_id) {
         if (*reply) {
            *reply = r;
            break;
         }
      }
      g_object_unref(r);
   }

   RETURN(FALSE);
}

static void
mongo_client_finalize (GObject *object)
{
   MongoClientPrivate *priv;

   ENTRY;

   priv = MONGO_CLIENT(object)->priv;

   g_clear_object(&priv->input);
   g_clear_object(&priv->output);

   if (priv->concern) {
      mongo_write_concern_free(priv->concern);
      priv->concern = NULL;
   }

   G_OBJECT_CLASS(mongo_client_parent_class)->finalize(object);

   EXIT;
}

static void
mongo_client_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
   //MongoClient *client = MONGO_CLIENT(object);

   switch (prop_id) {
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_client_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
   MongoClient *client = MONGO_CLIENT(object);

   switch (prop_id) {
   case PROP_STREAM:
      mongo_client_set_stream(client, g_value_get_object(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_client_class_init (MongoClientClass *klass)
{
   GObjectClass *object_class;

   ENTRY;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_client_finalize;
   object_class->get_property = mongo_client_get_property;
   object_class->set_property = mongo_client_set_property;
   g_type_class_add_private(object_class, sizeof(MongoClientPrivate));

   gParamSpecs[PROP_STREAM] =
      g_param_spec_object("stream",
                          _("Stream"),
                          _("The underlying stream."),
                          G_TYPE_IO_STREAM,
                          G_PARAM_WRITABLE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_STREAM,
                                   gParamSpecs[PROP_STREAM]);

   EXIT;
}

static void
mongo_client_init (MongoClient *client)
{
   ENTRY;
   client->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(client,
                                  MONGO_TYPE_CLIENT,
                                  MongoClientPrivate);
   client->priv->concern = mongo_write_concern_new();
   EXIT;
}

GQuark
mongo_client_error_quark (void)
{
   return g_quark_from_static_string("MongoClientError");
}
