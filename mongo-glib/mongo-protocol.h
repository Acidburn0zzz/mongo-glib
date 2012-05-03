/* mongo-protocol.h
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

#ifndef MONGO_PROTOCOL_H
#define MONGO_PROTOCOL_H

#include <glib-object.h>
#include <gio/gio.h>

#include "mongo-bson.h"

G_BEGIN_DECLS

#define MONGO_TYPE_PROTOCOL            (mongo_protocol_get_type())
#define MONGO_PROTOCOL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_PROTOCOL, MongoProtocol))
#define MONGO_PROTOCOL_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_PROTOCOL, MongoProtocol const))
#define MONGO_PROTOCOL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MONGO_TYPE_PROTOCOL, MongoProtocolClass))
#define MONGO_IS_PROTOCOL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONGO_TYPE_PROTOCOL))
#define MONGO_IS_PROTOCOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MONGO_TYPE_PROTOCOL))
#define MONGO_PROTOCOL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MONGO_TYPE_PROTOCOL, MongoProtocolClass))

typedef struct _MongoProtocol        MongoProtocol;
typedef struct _MongoProtocolClass   MongoProtocolClass;
typedef struct _MongoProtocolPrivate MongoProtocolPrivate;
typedef struct _MongoReply           MongoReply;
typedef enum   _MongoDeleteFlags     MongoDeleteFlags;
typedef enum   _MongoInsertFlags     MongoInsertFlags;
typedef enum   _MongoQueryFlags      MongoQueryFlags;
typedef enum   _MongoReplyFlags      MongoReplyFlags;
typedef enum   _MongoUpdateFlags     MongoUpdateFlags;

enum _MongoDeleteFlags
{
   MONGO_DELETE_NONE          = 0,
   MONGO_DELETE_SINGLE_REMOVE = 1 << 0,
};

enum _MongoInsertFlags
{
   MONGO_INSERT_NONE              = 0,
   MONGO_INSERT_CONTINUE_ON_ERROR = 1 << 0,
};

enum _MongoQueryFlags
{
   MONGO_QUERY_NONE              = 0,
   MONGO_QUERY_TAILABLE_CURSOR   = 1 << 1,
   MONGO_QUERY_SLAVE_OK          = 1 << 2,
   MONGO_QUERY_OPLOG_REPLAY      = 1 << 3,
   MONGO_QUERY_NO_CURSOR_TIMEOUT = 1 << 4,
   MONGO_QUERY_AWAIT_DATA        = 1 << 5,
   MONGO_QUERY_EXHAUST           = 1 << 6,
   MONGO_QUERY_PARTIAL           = 1 << 7,
};

enum _MongoReplyFlags
{
   MONGO_REPLY_NONE               = 0,
   MONGO_REPLY_CURSOR_NOT_FOUND   = 1 << 0,
   MONGO_REPLY_QUERY_FAILURE      = 1 << 1,
   MONGO_REPLY_SHARD_CONFIG_STALE = 1 << 2,
   MONGO_REPLY_AWAIT_CAPABLE      = 1 << 3,
};

enum _MongoUpdateFlags
{
   MONGO_UPDATE_NONE         = 0,
   MONGO_UPDATE_UPSERT       = 1 << 0,
   MONGO_UPDATE_MULTI_UPDATE = 1 << 1,
};

struct _MongoReply
{
   gint ref_count;
   MongoReplyFlags flags;
   guint64 cursor_id;
   guint32 starting_from;
   guint32 n_returned;
   MongoBson **documents;
};

struct _MongoProtocol
{
   GObject parent;

   /*< private >*/
   MongoProtocolPrivate *priv;
};

struct _MongoProtocolClass
{
   GObjectClass parent_class;
};

MongoReply *mongo_reply_ref                    (MongoReply *reply);
void        mongo_reply_unref                  (MongoReply *reply);
GType       mongo_reply_get_type               (void) G_GNUC_CONST;
GType       mongo_protocol_get_type            (void) G_GNUC_CONST;
GIOStream  *mongo_protocol_get_io_stream       (MongoProtocol        *protocol);
void        mongo_protocol_update_async        (MongoProtocol        *protocol,
                                                const gchar          *db_and_collection,
                                                MongoUpdateFlags      flags,
                                                const MongoBson      *selector,
                                                const MongoBson      *update,
                                                GCancellable         *cancellable,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data);
gboolean    mongo_protocol_update_finish       (MongoProtocol        *protocol,
                                                GAsyncResult         *result,
                                                GError              **error);
void        mongo_protocol_insert_async        (MongoProtocol        *protocol,
                                                const gchar          *db_and_collection,
                                                MongoInsertFlags      flags,
                                                MongoBson           **documents,
                                                gsize                 n_documents,
                                                GCancellable         *cancellable,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data);
gboolean    mongo_protocol_insert_finish       (MongoProtocol        *protocol,
                                                GAsyncResult         *result,
                                                GError              **error);
void        mongo_protocol_query_async         (MongoProtocol        *protocol,
                                                const gchar          *db_and_collection,
                                                MongoQueryFlags       flags,
                                                guint32               skip,
                                                guint32               limit,
                                                const MongoBson      *query,
                                                const MongoBson      *field_selector,
                                                GCancellable         *cancellable,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data);
MongoReply *mongo_protocol_query_finish        (MongoProtocol        *protocol,
                                                GAsyncResult         *result,
                                                GError              **error);
void        mongo_protocol_getmore_async       (MongoProtocol        *protocol,
                                                const gchar          *db_and_collection,
                                                guint32               limit,
                                                guint64               cursor_id,
                                                GCancellable         *cancellable,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data);
MongoReply *mongo_protocol_getmore_finish      (MongoProtocol        *protocol,
                                                GAsyncResult         *result,
                                                GError              **error);
void        mongo_protocol_delete_async        (MongoProtocol        *protocol,
                                                const gchar          *db_and_collection,
                                                MongoDeleteFlags      flags,
                                                const MongoBson      *selector,
                                                GCancellable         *cancellable,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data);
gboolean    mongo_protocol_delete_finish       (MongoProtocol        *protocol,
                                                GAsyncResult         *result,
                                                GError              **error);
void        mongo_protocol_kill_cursors_async  (MongoProtocol        *protocol,
                                                guint64              *cursors,
                                                gsize                 n_cursors,
                                                GCancellable         *cancellable,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data);
gboolean    mongo_protocol_kill_cursors_finish (MongoProtocol        *protocol,
                                                GAsyncResult         *result,
                                                GError              **error);
void        mongo_protocol_msg_async           (MongoProtocol        *protocol,
                                                const gchar          *message,
                                                GCancellable         *cancellable,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data);
gboolean    mongo_protocol_msg_finish          (MongoProtocol        *protocol,
                                                GAsyncResult         *result,
                                                GError              **error);
void        mongo_protocol_flush_sync          (MongoProtocol        *protocol);

G_END_DECLS

#endif /* MONGO_PROTOCOL_H */
