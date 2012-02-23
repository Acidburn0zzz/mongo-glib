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

#include <errno.h>
#include <fcntl.h>
#include <glib/gi18n.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "mongo-client.h"

G_DEFINE_TYPE(MongoClient, mongo_client, G_TYPE_OBJECT)

struct _MongoClientPrivate
{
   GSimpleAsyncResult *connect;
   GSimpleAsyncResult *disconnect;
   GSocketAddress *address;
   MongoClientState state;
   GIOChannel *channel;
   gboolean flush;
   guint in_source;
   guint out_source;
   GQueue *bytes;
   guint next_id;
};

typedef struct
{
   gsize data_len;
   gsize offset;
   const guint8 *data;
   GSimpleAsyncResult *result;
   GDestroyNotify notify;
} Bytes;

enum
{
   PROP_0,
   PROP_STATE,
   LAST_PROP
};

static GParamSpec *gParamSpecs[LAST_PROP];
static const gchar *gStateNames[] = {
   "READY",
   "CONNECTING",
   "CONNECTED",
   "DISCONNECTING",
   "DISCONNECTED",
   "FINISHED",
   "FAILED",
};

static void mongo_client_set_state (MongoClient *, MongoClientState);

static void
mongo_client_disable_writing (MongoClient *client)
{
   MongoClientPrivate *priv;

   g_return_if_fail(MONGO_IS_CLIENT(client));

   priv = client->priv;

   if (priv->out_source) {
      g_source_remove(priv->out_source);
      priv->out_source = 0;
   }
}

static void
mongo_client_disconnect (MongoClient *client)
{
   MongoClientPrivate *priv;
   GIOChannel *channel;

   g_return_if_fail(MONGO_IS_CLIENT(client));

   priv = client->priv;

   if ((channel = priv->channel)) {
      priv->channel = NULL;
      g_io_channel_unref(channel);
      mongo_client_set_state(client, MONGO_CLIENT_DISCONNECTED);
      if (priv->disconnect) {
         g_simple_async_result_set_op_res_gboolean(priv->disconnect, TRUE);
         g_simple_async_result_complete_in_idle(priv->disconnect);
         g_clear_object(&priv->disconnect);
      }
   }
}

static void
mongo_client_flush (MongoClient *client)
{
   MongoClientPrivate *priv;
   GIOStatus status;
   gboolean wrote = FALSE;
   GError *error = NULL;
   Bytes *bytes;
   gsize bytes_written;

   g_return_if_fail(MONGO_IS_CLIENT(client));

   priv = client->priv;

   /*
    * Try to write the next chunk of data to the channel.
    */
again:
   if ((bytes = g_queue_peek_head(priv->bytes))) {
      status =
         g_io_channel_write_chars(priv->channel,
                                  (gchar *)(bytes->data + bytes->offset),
                                  bytes->data_len - bytes->offset,
                                  &bytes_written,
                                  &error);
      switch (status) {
      case G_IO_STATUS_NORMAL:
         wrote = TRUE;
         bytes->offset += bytes_written;
         g_assert_cmpint(bytes->offset, <=, bytes->data_len);
         if (bytes->offset == bytes->data_len) {
            g_queue_pop_head(priv->bytes);
            if (bytes->notify) {
               bytes->notify((gchar *)bytes->data);
            }
            g_simple_async_result_set_op_res_gboolean(bytes->result, TRUE);
            g_simple_async_result_complete_in_idle(bytes->result);
            g_clear_object(&bytes->result);
            g_slice_free(Bytes, bytes);
         }
         /* Fall through */
      case G_IO_STATUS_AGAIN:
         goto again;
      case G_IO_STATUS_ERROR:
      case G_IO_STATUS_EOF:
         g_queue_pop_head(priv->bytes);
         g_simple_async_result_take_error(bytes->result, error);
         g_simple_async_result_complete_in_idle(bytes->result);
         g_clear_object(&bytes->result);
         g_slice_free(Bytes, bytes);
         mongo_client_set_state(client, MONGO_CLIENT_FAILED);
         return;
      default:
         g_assert_not_reached();
      }
   }

   /*
    * Flush our sockets buffer.
    *
    * TODO: Check to see if this is blocking. My assumption is that O_NONBLOCK
    *       will mean it does not wait for it to leave our network card.
    */
   if (wrote) {
      if (!g_io_channel_flush(priv->channel, &error)) {
         g_warning("%s", error->message);
         g_clear_error(&error);
      }
   }

   /*
    * If we are disconnecting and have finished flushing,
    * go ahead and move to the disconnected state.
    */
   if ((priv->state == MONGO_CLIENT_DISCONNECTING)) {
      if (!g_queue_get_length(priv->bytes)) {
         mongo_client_disconnect(client);
      }
   }
}

static gboolean
mongo_client_write_ready (GIOChannel   *channel,
                        GIOCondition  condition,
                        gpointer      user_data)
{
   MongoClientPrivate *priv;
   MongoClient *client = user_data;
   gint optval = 0;
   socklen_t optlen = sizeof optval;
   gint sd;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);

   priv = client->priv;

   if ((condition & G_IO_OUT)) {
      switch (priv->state) {
      case MONGO_CLIENT_CONNECTING:
         sd = g_io_channel_unix_get_fd(channel);
         if ((-1 == getsockopt(sd, SOL_SOCKET, SO_ERROR, &optval, &optlen)) ||
             (optval != 0)) {
            if (priv->connect) {
               g_simple_async_result_set_error(priv->connect,
                                               MONGO_CLIENT_ERROR,
                                               MONGO_CLIENT_ERROR_ERRNO,
                                               "%s", strerror(optval));
               g_simple_async_result_complete_in_idle(priv->connect);
               g_clear_object(&priv->connect);
            }
            mongo_client_set_state(client, MONGO_CLIENT_FAILED);
            break;
         }
         mongo_client_set_state(client, MONGO_CLIENT_CONNECTED);
         return TRUE;
      case MONGO_CLIENT_CONNECTED:
         mongo_client_flush(client);
         if (g_queue_get_length(priv->bytes)) {
            return TRUE;
         }
         break;
      case MONGO_CLIENT_DISCONNECTING:
      case MONGO_CLIENT_DISCONNECTED:
      case MONGO_CLIENT_FINISHED:
      case MONGO_CLIENT_FAILED:
      case MONGO_CLIENT_READY:
      default:
         g_assert_not_reached();
         break;
      }
   }

   mongo_client_disable_writing(client);

   return FALSE;
}

static void
mongo_client_enable_writing (MongoClient *client)
{
   MongoClientPrivate *priv;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(client->priv->channel);

   priv = client->priv;

   if (!priv->out_source) {
      priv->out_source =
         g_io_add_watch(priv->channel,
                        G_IO_OUT | G_IO_ERR | G_IO_HUP,
                        mongo_client_write_ready,
                        client);
   }
}

static void
mongo_client_disable_reading (MongoClient *client)
{
   MongoClientPrivate *priv;

   g_return_if_fail(MONGO_IS_CLIENT(client));

   priv = client->priv;

   if (priv->in_source) {
      g_source_remove(priv->in_source);
      priv->in_source = 0;
   }
}

static gboolean
mongo_client_read_ready (GIOChannel   *channel,
                       GIOCondition  condition,
                       gpointer      user_data)
{
   MongoClientPrivate *priv;
   MongoClientClass *klass;
   MongoClient *client = user_data;
   GIOStatus status;
   GError *error = NULL;
   guint8 buffer[1024];
   gsize bytes_read;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);

   if (!(condition & G_IO_IN)) {
      mongo_client_disable_reading(client);
      return FALSE;
   }

   priv = client->priv;
   klass = MONGO_CLIENT_GET_CLASS(client);

again:
   status = g_io_channel_read_chars(channel,
                                    (gchar *)buffer,
                                    sizeof buffer,
                                    &bytes_read,
                                    &error);
   switch (status) {
   case G_IO_STATUS_NORMAL:
      if (bytes_read && klass->read) {
         klass->read(client, buffer, bytes_read);
      }
      break;
   case G_IO_STATUS_ERROR:
      g_warning("%s", error->message);
      g_clear_error(&error);
      /* Fall through */
   case G_IO_STATUS_EOF:
      priv->flush = FALSE;
      mongo_client_set_state(client, MONGO_CLIENT_DISCONNECTING);
      return FALSE;
   case G_IO_STATUS_AGAIN:
      goto again;
   default:
      g_assert_not_reached();
   }

   return TRUE;
}

static void
mongo_client_enable_reading (MongoClient *client)
{
   MongoClientPrivate *priv;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(client->priv->channel);

   priv = client->priv;

   if (!priv->in_source) {
      priv->in_source =
         g_io_add_watch(priv->channel,
                        G_IO_IN | G_IO_HUP | G_IO_ERR,
                        mongo_client_read_ready,
                        client);
   }
}

static void
mongo_client_start_connecting (MongoClient *client)
{
   MongoClientPrivate *priv;
   GSocketAddress *address;
   GSocketFamily family;
   gpointer saddr;
   GError *error = NULL;
   gsize saddrlen;
   gint sd;
   gint ret;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(client->priv->address);

   priv = client->priv;

   /*
    * Build our struct sockaddr_in for connecting.
    */
   address = G_SOCKET_ADDRESS(priv->address);
   saddrlen = g_socket_address_get_native_size(address);
   saddr = g_malloc0(saddrlen);
   if (!g_socket_address_to_native(address, saddr, saddrlen, &error)) {
      g_warning("%s(): %s", G_STRFUNC, error->message);
      if (priv->connect) {
         g_simple_async_result_set_from_error(priv->connect, error);
         g_simple_async_result_complete_in_idle(priv->connect);
         g_clear_object(&priv->connect);
      }
      g_clear_error(&error);
      mongo_client_set_state(client, MONGO_CLIENT_FAILED);
      return;
   }

   /*
    * Create our socket that will contain the connection.
    */
   g_object_get(address, "family", &family, NULL);
   sd = socket(family, SOCK_STREAM, IPPROTO_TCP);
   if (sd == -1) {
      g_simple_async_result_set_error(priv->connect,
                                      MONGO_CLIENT_ERROR,
                                      MONGO_CLIENT_ERROR_BAD_SOCKET,
                                      "%s", strerror(errno));
      g_simple_async_result_complete_in_idle(priv->connect);
      g_clear_object(&priv->connect);
      mongo_client_set_state(client, MONGO_CLIENT_FAILED);
      return;
   }

   /*
    * Start asynchronously connecting to the target.
    */
   fcntl(sd, F_SETFL, O_NONBLOCK);
   ret = connect(sd, saddr, saddrlen);
   if (ret == -1 && errno != EINPROGRESS) {
      g_simple_async_result_set_error(priv->connect,
                                      MONGO_CLIENT_ERROR,
                                      MONGO_CLIENT_ERROR_ERRNO,
                                      "%s", strerror(errno));
      g_simple_async_result_complete_in_idle(priv->connect);
      g_clear_object(&priv->connect);
      return;
   }

   /*
    * Create our IO channel for communicating with the target.
    */
   priv->channel = g_io_channel_unix_new(sd);
   g_io_channel_set_encoding(priv->channel, NULL, NULL);
   g_io_channel_set_close_on_unref(priv->channel, TRUE);

   /*
    * G_IO_OUT condition will be indicated once we have connected to
    * the target host.
    */
   mongo_client_enable_writing(client);
}

MongoClientState
mongo_client_get_state (MongoClient *client)
{
   g_return_val_if_fail(MONGO_IS_CLIENT(client), 0);
   return client->priv->state;
}

static void
mongo_client_set_state (MongoClient      *client,
                        MongoClientState  state)
{
   MongoClientPrivate *priv;

   g_return_if_fail(MONGO_IS_CLIENT(client));

   priv = client->priv;

   switch (state) {
   case MONGO_CLIENT_READY:
      break;
   case MONGO_CLIENT_CONNECTING:
      g_assert(priv->state == MONGO_CLIENT_READY ||
               priv->state == MONGO_CLIENT_FINISHED);
      g_assert(priv->address);
      priv->state = state;
      mongo_client_start_connecting(client);
      break;
   case MONGO_CLIENT_CONNECTED:
      priv->state = state;
      if (priv->connect) {
         g_simple_async_result_set_op_res_gboolean(priv->connect, TRUE);
         g_simple_async_result_complete_in_idle(priv->connect);
         g_clear_object(&priv->connect);
      }
      mongo_client_enable_writing(client);
      mongo_client_enable_reading(client);
      break;
   case MONGO_CLIENT_DISCONNECTING:
      if (priv->flush) {
         priv->state = state;
         mongo_client_flush(client);
      } else {
         mongo_client_disconnect(client);
      }
      break;
   case MONGO_CLIENT_DISCONNECTED:
      priv->state = state;
      break;
   case MONGO_CLIENT_FINISHED:
      priv->state = state;
      break;
   case MONGO_CLIENT_FAILED:
      break;
   default:
      break;
   }

   g_object_notify_by_pspec(G_OBJECT(client),
                            gParamSpecs[PROP_STATE]);
}

static guint
mongo_client_get_next_id (MongoClient *client)
{
   g_return_val_if_fail(MONGO_IS_CLIENT(client), 0);
   return g_atomic_int_add((gint *)&client->priv->next_id, 1);
}

void
mongo_client_query_async (MongoClient         *client,
                          const gchar         *collection,
                          MongoBson           *query,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
   const guint8 *bson_buf;
   GByteArray *buffer;
   guint32 len;
   guint32 id;
   guint32 zero = 0;
   guint32 op;
   guint8 *send_buf;
   gsize bson_len;
   gsize send_len;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(query != NULL);
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   bson_buf = mongo_bson_get_data(query, &bson_len);
   buffer = g_byte_array_sized_new(20 + bson_len);

   len = GINT32_TO_LE(16 + 4 + strlen(collection) + 1 + bson_len);
   id = mongo_client_get_next_id(client);
   op = GUINT32_TO_LE(MONGO_OPERATION_QUERY);

   // hack
   len += 8;

   /*
    * Append header.
    */
   g_byte_array_append(buffer, (guint8 *)&len, sizeof len);
   g_byte_array_append(buffer, (guint8 *)&id, sizeof id);
   g_byte_array_append(buffer, (guint8 *)&zero, sizeof zero);
   g_byte_array_append(buffer, (guint8 *)&op, sizeof op);

   /*
    * Append Message.
    */
   g_byte_array_append(buffer, (guint8 *)&zero, sizeof zero); // TODO: query_opts
   g_byte_array_append(buffer, (guint8 *)collection, strlen(collection) + 1);
   g_byte_array_append(buffer, bson_buf, bson_len);

   send_len = buffer->len;
   send_buf = g_byte_array_free(buffer, FALSE);

   mongo_client_write_async(client, send_buf, send_len,
                            callback, user_data, g_free);
}

gboolean
mongo_client_query_finish (MongoClient   *client,
                           GAsyncResult  *result,
                           GError       **error)
{
   return mongo_client_write_finish(client, result, error);
}

void
mongo_client_connect_async (MongoClient         *client,
                            GInetAddress        *address,
                            guint16              port,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
   GSimpleAsyncResult *simple;
   MongoClientPrivate *priv;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(G_IS_INET_ADDRESS(address));
   g_return_if_fail(!cancellable || G_IS_CANCELLABLE(cancellable));
   g_return_if_fail(callback);

   priv = client->priv;

   if (priv->state != MONGO_CLIENT_READY) {
      simple =
         g_simple_async_result_new_error(G_OBJECT(client), callback, user_data,
                                         MONGO_CLIENT_ERROR,
                                         MONGO_CLIENT_ERROR_INVALID_STATE,
                                         _("mongo_client_connect_async() called "
                                           "while in \"%s\" state."),
                                         gStateNames[priv->state]);
      g_simple_async_result_complete_in_idle(simple);
      g_object_unref(simple);
      return;
   }

   priv->address = g_inet_socket_address_new(address, port);
   priv->connect =
      g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                mongo_client_connect_async);
   mongo_client_set_state(client, MONGO_CLIENT_CONNECTING);
}

gboolean
mongo_client_connect_finish (MongoClient     *client,
                           GAsyncResult  *result,
                           GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   return ret;
}

void
mongo_client_disconnect_async (MongoClient           *client,
                             gboolean             flush,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
   GSimpleAsyncResult *simple;
   MongoClientPrivate *priv;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(callback);

   priv = client->priv;

   if (priv->disconnect) {
      simple = g_simple_async_result_new_error(G_OBJECT(client),
                                               callback, user_data,
                                               MONGO_CLIENT_ERROR,
                                               MONGO_CLIENT_ERROR_INVALID_STATE,
                                               _("%s() was already called."),
                                               G_STRFUNC);
      g_simple_async_result_complete_in_idle(simple);
      g_object_unref(simple);
      return;
   }

   priv->disconnect =
      g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                mongo_client_disconnect_async);
   priv->flush = flush;
   mongo_client_set_state(client, MONGO_CLIENT_DISCONNECTING);
}

gboolean
mongo_client_disconnect_finish (MongoClient     *client,
                              GAsyncResult  *result,
                              GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   return ret;
}

void
mongo_client_write_async (MongoClient         *client,
                          const guint8        *data,
                          gsize                data_len,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data,
                          GDestroyNotify       notify)
{
   GSimpleAsyncResult *simple;
   MongoClientPrivate *priv;
   Bytes *bytes;

   g_return_if_fail(MONGO_IS_CLIENT(client));
   g_return_if_fail(data);
   g_return_if_fail(data_len);
   g_return_if_fail(callback);

   priv = client->priv;

   switch (priv->state) {
   case MONGO_CLIENT_READY:
   case MONGO_CLIENT_CONNECTING:
   case MONGO_CLIENT_CONNECTED:
      break;
   case MONGO_CLIENT_DISCONNECTING:
   case MONGO_CLIENT_DISCONNECTED:
   case MONGO_CLIENT_FINISHED:
   case MONGO_CLIENT_FAILED:
      simple = g_simple_async_result_new_error(G_OBJECT(client),
                                               callback, user_data,
                                               MONGO_CLIENT_ERROR,
                                               MONGO_CLIENT_ERROR_INVALID_STATE,
                                               _("%s() cannot write while %s."),
                                               G_STRFUNC,
                                               gStateNames[priv->state]);
      g_simple_async_result_complete_in_idle(simple);
      g_object_unref(simple);
      return;
   default:
      g_assert_not_reached();
      break;
   }

   simple = g_simple_async_result_new(G_OBJECT(client), callback, user_data,
                                      mongo_client_write_async);

   bytes = g_slice_new0(Bytes);
   bytes->data = data;
   bytes->data_len = data_len;
   bytes->notify = notify;
   bytes->offset = 0;
   bytes->result = simple;

   g_queue_push_tail(client->priv->bytes, bytes);
   mongo_client_enable_writing(client);
}

gboolean
mongo_client_write_finish (MongoClient     *client,
                         GAsyncResult  *result,
                         GError       **error)
{
   GSimpleAsyncResult *simple = (GSimpleAsyncResult *)result;
   gboolean ret;

   g_return_val_if_fail(MONGO_IS_CLIENT(client), FALSE);
   g_return_val_if_fail(G_IS_SIMPLE_ASYNC_RESULT(simple), FALSE);

   if (!(ret = g_simple_async_result_get_op_res_gboolean(simple))) {
      g_simple_async_result_propagate_error(simple, error);
   }

   return ret;
}

static void
mongo_client_finalize (GObject *object)
{
   G_OBJECT_CLASS(mongo_client_parent_class)->finalize(object);
}

static void
mongo_client_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
   MongoClient *client = MONGO_CLIENT(object);

   switch (prop_id) {
   case PROP_STATE:
      g_value_set_enum(value, mongo_client_get_state(client));
      break;
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_client_notify (GObject    *object,
                   GParamSpec *pspec)
{
}

static void
mongo_client_class_init (MongoClientClass *klass)
{
   GObjectClass *object_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_client_finalize;
   object_class->get_property = mongo_client_get_property;
   object_class->notify = mongo_client_notify;
   g_type_class_add_private(object_class, sizeof(MongoClientPrivate));

   gParamSpecs[PROP_STATE] =
      g_param_spec_enum("state",
                        _("State"),
                        _("The current connection state."),
                        MONGO_TYPE_CLIENT_STATE,
                        MONGO_CLIENT_READY,
                        G_PARAM_READABLE);
   g_object_class_install_property(object_class, PROP_STATE,
                                   gParamSpecs[PROP_STATE]);
}

static void
mongo_client_init (MongoClient *client)
{
   client->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(client,
                                  MONGO_TYPE_CLIENT,
                                  MongoClientPrivate);
   client->priv->bytes = g_queue_new();
}

GType
mongo_client_state_get_type (void)
{
   static gsize initialized = FALSE;
   static GType type_id;
   static const GEnumValue values[] = {
      { MONGO_CLIENT_READY, "MONGO_CLIENT_READY", "READY" },
      { MONGO_CLIENT_CONNECTING, "MONGO_CLIENT_CONNECTING", "CONNECTING" },
      { MONGO_CLIENT_CONNECTED, "MONGO_CLIENT_CONNECTED", "CONNECTED" },
      { MONGO_CLIENT_DISCONNECTING, "MONGO_CLIENT_DISCONNECTING", "DISCONNECTING" },
      { MONGO_CLIENT_DISCONNECTED, "MONGO_CLIENT_DISCONNECTED", "DISCONNECTED" },
      { MONGO_CLIENT_FINISHED, "MONGO_CLIENT_FINISHED", "FINISHED" },
      { MONGO_CLIENT_FAILED, "MONGO_CLIENT_FAILED", "FAILED" },
      { 0 }
   };

   if (g_once_init_enter(&initialized)) {
      type_id = g_enum_register_static("MongoClientState", values);
      g_once_init_leave(&initialized, TRUE);
   }

   return type_id;
}

GQuark
mongo_client_error_quark (void)
{
   return g_quark_from_static_string("mongo-client-error-quark");
}

GType
mongo_operation_get_type (void)
{
   static GType type_id = 0;
   static gsize initialized = FALSE;
   static const GEnumValue values[] = {
      { MONGO_OPERATION_UPDATE,       "MONGO_OPERATION_UPDATE",       "UPDATE" },
      { MONGO_OPERATION_INSERT,       "MONGO_OPERATION_INSERT",       "INSERT" },
      { MONGO_OPERATION_QUERY,        "MONGO_OPERATION_QUERY",        "QUERY" },
      { MONGO_OPERATION_GET_MORE,     "MONGO_OPERATION_GET_MORE",     "GET_MORE" },
      { MONGO_OPERATION_DELETE,       "MONGO_OPERATION_DELETE",       "DELETE" },
      { MONGO_OPERATION_KILL_CURSORS, "MONGO_OPERATION_KILL_CURSORS", "KILL_CURSORS" },
      { 0 }
   };

   if (g_once_init_enter(&initialized)) {
      type_id = g_enum_register_static("MongoOperation", values);
      g_once_init_leave(&initialized, TRUE);
   }

   return type_id;
}
