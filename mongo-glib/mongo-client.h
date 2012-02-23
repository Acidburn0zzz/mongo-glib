/* mongo-client.h
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

#ifndef MONGO_CLIENT_H
#define MONGO_CLIENT_H

#include <gio/gio.h>

#include "mongo-bson.h"

G_BEGIN_DECLS

#define MONGO_TYPE_CLIENT            (mongo_client_get_type())
#define MONGO_TYPE_CLIENT_STATE      (mongo_client_state_get_type())
#define MONGO_CLIENT_ERROR           (mongo_client_error_quark())
#define MONGO_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_CLIENT, MongoClient))
#define MONGO_CLIENT_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_CLIENT, MongoClient const))
#define MONGO_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MONGO_TYPE_CLIENT, MongoClientClass))
#define MONGO_IS_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONGO_TYPE_CLIENT))
#define MONGO_IS_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MONGO_TYPE_CLIENT))
#define MONGO_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MONGO_TYPE_CLIENT, MongoClientClass))

typedef struct _MongoClient        MongoClient;
typedef struct _MongoClientClass   MongoClientClass;
typedef enum   _MongoClientError   MongoClientError;
typedef struct _MongoClientPrivate MongoClientPrivate;
typedef enum   _MongoClientState   MongoClientState;
typedef enum   _MongoOperation     MongoOperation;

enum _MongoClientError
{
   MONGO_CLIENT_ERROR_INVALID_STATE = 1,
   MONGO_CLIENT_ERROR_BAD_SOCKET,
   MONGO_CLIENT_ERROR_ERRNO,
};

enum _MongoClientState
{
   MONGO_CLIENT_READY,
   MONGO_CLIENT_CONNECTING,
   MONGO_CLIENT_CONNECTED,
   MONGO_CLIENT_DISCONNECTING,
   MONGO_CLIENT_DISCONNECTED,
   MONGO_CLIENT_FINISHED,
   MONGO_CLIENT_FAILED,
};

enum _MongoOperation
{
   MONGO_OPERATION_UPDATE       = 2001,
   MONGO_OPERATION_INSERT       = 2002,
   MONGO_OPERATION_QUERY        = 2004,
   MONGO_OPERATION_GET_MORE     = 2005,
   MONGO_OPERATION_DELETE       = 2006,
   MONGO_OPERATION_KILL_CURSORS = 2007,
};

struct _MongoClient
{
   GObject parent;

   /*< private >*/
   MongoClientPrivate *priv;
};

struct _MongoClientClass
{
   GObjectClass parent_class;

   void (*read) (MongoClient  *client,
                 const guint8 *data,
                 gsize         data_len);
};

GQuark   mongo_client_error_quark       (void) G_GNUC_CONST;
GType    mongo_client_get_type          (void) G_GNUC_CONST;
GType    mongo_client_state_get_type    (void) G_GNUC_CONST;
void     mongo_client_connect_async     (MongoClient          *client,
                                         GInetAddress         *address,
                                         guint16               port,
                                         GCancellable         *cancellable,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data);
gboolean mongo_client_connect_finish    (MongoClient          *client,
                                         GAsyncResult         *result,
                                         GError              **error);
void     mongo_client_disconnect_async  (MongoClient          *client,
                                         gboolean              flush,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data);
gboolean mongo_client_disconnect_finish (MongoClient          *client,
                                         GAsyncResult         *result,
                                         GError              **error);
void     mongo_client_write_async       (MongoClient          *client,
                                         const guint8         *buffer,
                                         gsize                 buffer_length,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data,
                                         GDestroyNotify        notify);
gboolean mongo_client_write_finish      (MongoClient          *client,
                                         GAsyncResult         *result,
                                         GError              **error);
void     mongo_client_query_async       (MongoClient *client,
                                         const gchar *collection,
                                         MongoBson *query,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data);
gboolean mongo_client_query_finish (MongoClient *client,
                                    GAsyncResult *result,
                                    GError **error);
GType    mongo_operation_get_type       (void) G_GNUC_CONST;

G_END_DECLS

#endif /* MONGO_CLIENT_H */
