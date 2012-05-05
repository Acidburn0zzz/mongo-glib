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

#if !defined (MONGO_INSIDE) && !defined (MONGO_COMPILATION)
#error "Only <mongo-glib/mongo-glib.h> can be included directly."
#endif

#ifndef MONGO_CLIENT_H
#define MONGO_CLIENT_H

#include <glib-object.h>
#include <gio/gio.h>

#include "mongo-client.h"
#include "mongo-collection.h"
#include "mongo-database.h"
#include "mongo-protocol.h"

G_BEGIN_DECLS

#define MONGO_TYPE_CLIENT            (mongo_client_get_type())
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

enum _MongoClientError
{
   MONGO_CLIENT_ERROR_NO_SEEDS = 1,
   MONGO_CLIENT_ERROR_NOT_CONNECTED,
   MONGO_CLIENT_ERROR_COMMAND_FAILED,
   MONGO_CLIENT_ERROR_INVALID_REPLY,
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
};

void             mongo_client_add_seed          (MongoClient          *client,
                                                 const gchar          *hostname,
                                                 guint16               port);
void             mongo_client_command_async     (MongoClient          *client,
                                                 const gchar          *db,
                                                 const MongoBson      *command,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
MongoReply      *mongo_client_command_finish    (MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
void             mongo_client_connect_async     (MongoClient          *client,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
gboolean         mongo_client_connect_finish    (MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
void             mongo_client_disconnect_async  (MongoClient          *client,
                                                 gboolean              kill_cursors,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
gboolean         mongo_client_disconnect_finish (MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
void             mongo_client_getmore_async     (MongoClient          *client,
                                                 const gchar          *db_and_collection,
                                                 guint32               limit,
                                                 guint64               cursor_id,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
MongoReply      *mongo_client_getmore_finish    (MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
void             mongo_client_insert_async      (MongoClient          *client,
                                                 const gchar          *db_and_collection,
                                                 MongoInsertFlags      flags,
                                                 MongoBson           **documents,
                                                 gsize                 n_documents,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
gboolean         mongo_client_insert_finish     (MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
void             mongo_client_remove_async      (MongoClient          *client,
                                                 const gchar          *db_and_collection,
                                                 MongoDeleteFlags      flags,
                                                 const MongoBson      *selector,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
gboolean         mongo_client_remove_finish     (MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
void             mongo_client_update_async      (MongoClient          *client,
                                                 const gchar          *db_and_collection,
                                                 MongoUpdateFlags      flags,
                                                 const MongoBson      *selector,
                                                 const MongoBson      *update,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
gboolean         mongo_client_update_finish     (MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
void             mongo_client_query_async       (MongoClient          *client,
                                                 const gchar          *db_and_collection,
                                                 MongoQueryFlags       flags,
                                                 guint32               skip,
                                                 guint32               limit,
                                                 const MongoBson      *query,
                                                 const MongoBson      *field_selector,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
void            mongo_client_kill_cursors_async (MongoClient          *client,
                                                 guint64              *cursors,
                                                 gsize                 n_cursors,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
gboolean        mongo_client_kill_cursors_finish(MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
MongoReply      *mongo_client_query_finish      (MongoClient          *client,
                                                 GAsyncResult         *result,
                                                 GError              **error);
MongoDatabase   *mongo_client_get_database      (MongoClient          *client,
                                                 const gchar          *name);
GType            mongo_client_get_type          (void) G_GNUC_CONST;
GQuark           mongo_client_error_quark       (void) G_GNUC_CONST;
MongoClient     *mongo_client_new               (void);
gboolean         mongo_client_get_slave_okay    (MongoClient          *client);
void             mongo_client_set_slave_okay    (MongoClient          *client,
                                                 gboolean              slave_okay);
MongoClient     *mongo_database_get_client      (MongoDatabase        *database);
MongoClient     *mongo_collection_get_client    (MongoCollection      *collection);

G_END_DECLS

#endif /* MONGO_CLIENT_H */
