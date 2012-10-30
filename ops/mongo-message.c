/* mongo-message.c
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
#include "mongo-message.h"

G_DEFINE_ABSTRACT_TYPE(MongoMessage, mongo_message, G_TYPE_INITIALLY_UNOWNED)

struct _MongoMessagePrivate
{
   gint32 request_id;
   gint32 response_to;

   gboolean          one_shot;
   MongoReplyFlags   reply_flags;
   MongoBson       **reply_docs;
   guint64           reply_cursor;
   guint             reply_count;
   guint             reply_skip;
};

enum
{
   PROP_0,
   PROP_REQUEST_ID,
   PROP_RESPONSE_TO,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];

gboolean
_mongo_message_is_ready (MongoMessage *message)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE(message), FALSE);
   return message->priv->one_shot;
}

void
mongo_message_reply_one (MongoMessage    *message,
                         MongoReplyFlags  flags,
                         MongoBson       *document)
{
   MongoMessagePrivate *priv;

   g_return_if_fail(MONGO_IS_MESSAGE(message));
   g_return_if_fail(document);

   priv = message->priv;

   if (priv->one_shot) {
      g_warning("Cannot call %s() after setting reply.\n", G_STRFUNC);
      return;
   }

   priv->one_shot = TRUE;
   priv->reply_flags = flags;
   priv->reply_docs = g_new0(MongoBson*, 1);
   priv->reply_docs[0] = mongo_bson_ref(document);
   priv->reply_count = 1;
   priv->reply_cursor = 0;
   priv->reply_skip = 0;
}

gint
mongo_message_get_request_id (MongoMessage *message)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE(message), 0);
   return message->priv->request_id;
}

void
mongo_message_set_request_id (MongoMessage *message,
                              gint          request_id)
{
   g_return_if_fail(MONGO_IS_MESSAGE(message));
   message->priv->request_id = request_id;
   g_object_notify_by_pspec(G_OBJECT(message),
                            gParamSpecs[PROP_REQUEST_ID]);
}

gint
mongo_message_get_response_to (MongoMessage *message)
{
   g_return_val_if_fail(MONGO_IS_MESSAGE(message), 0);
   return message->priv->response_to;
}

void
mongo_message_set_response_to (MongoMessage *message,
                               gint          response_to)
{
   g_return_if_fail(MONGO_IS_MESSAGE(message));
   message->priv->response_to = response_to;
   g_object_notify_by_pspec(G_OBJECT(message),
                            gParamSpecs[PROP_RESPONSE_TO]);
}

gboolean
mongo_message_load_from_data (MongoMessage *message,
                              const guint8 *data,
                              gsize         length)
{
   MongoMessageClass *klass;
   gboolean ret = FALSE;

   g_return_val_if_fail(MONGO_IS_MESSAGE(message), FALSE);
   g_return_val_if_fail(data, FALSE);

   if ((klass = MONGO_MESSAGE_GET_CLASS(message))->load_from_data) {
      ret = klass->load_from_data(message, data, length);
   }

   return ret;
}

static void
mongo_message_finalize (GObject *object)
{
   MongoMessagePrivate *priv;
   guint i;

   ENTRY;

   priv = MONGO_MESSAGE(object)->priv;

   if (priv->reply_docs) {
      for (i = 0; i < priv->reply_count; i++) {
         mongo_bson_unref(priv->reply_docs[i]);
      }
      g_free(priv->reply_docs);
   }

   G_OBJECT_CLASS(mongo_message_parent_class)->finalize(object);
   EXIT;
}

static void
mongo_message_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
   MongoMessage *message = MONGO_MESSAGE(object);

   switch (prop_id) {
   case PROP_REQUEST_ID:
      g_value_set_int(value, mongo_message_get_request_id(message));
      break;
   case PROP_RESPONSE_TO:
      g_value_set_int(value, mongo_message_get_response_to(message));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_message_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
   MongoMessage *message = MONGO_MESSAGE(object);

   switch (prop_id) {
   case PROP_REQUEST_ID:
      mongo_message_set_request_id(message, g_value_get_int(value));
      break;
   case PROP_RESPONSE_TO:
      mongo_message_set_response_to(message, g_value_get_int(value));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_message_class_init (MongoMessageClass *klass)
{
   GObjectClass *object_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_message_finalize;
   object_class->get_property = mongo_message_get_property;
   object_class->set_property = mongo_message_set_property;
   g_type_class_add_private(object_class, sizeof(MongoMessagePrivate));

   /**
    * MongoMessage:request-id:
    *
    * The "request-id" property is the client generated identifier for the
    * message delivered to the mongo server. The mongo server replies with
    * a #MongoReply placing "request-id" as the "response-to".
    */
   gParamSpecs[PROP_REQUEST_ID] =
      g_param_spec_int("request-id",
                       _("Request Id"),
                       _("The client generated request id of the message."),
                       G_MININT32,
                       G_MAXINT32,
                       0,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_REQUEST_ID,
                                   gParamSpecs[PROP_REQUEST_ID]);

   /**
    * MongoMessage:response-to:
    *
    * The "response-to" property contains the "request-id" that the message is
    * in response to.
    */
   gParamSpecs[PROP_RESPONSE_TO] =
      g_param_spec_int("response-to",
                       _("Response To"),
                       _("The request id the message is in response to."),
                       G_MININT32,
                       G_MAXINT32,
                       0,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
   g_object_class_install_property(object_class, PROP_RESPONSE_TO,
                                   gParamSpecs[PROP_RESPONSE_TO]);
}

static void
mongo_message_init (MongoMessage *message)
{
   message->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(message,
                                  MONGO_TYPE_MESSAGE,
                                  MongoMessagePrivate);
}
