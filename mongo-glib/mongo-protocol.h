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

#define MONGO_TYPE_QUERY_FLAGS         (mongo_query_flags_get_type())
#define MONGO_TYPE_PROTOCOL            (mongo_protocol_get_type())
#define MONGO_PROTOCOL_ERROR           (mongo_protocol_error_quark())
#define MONGO_PROTOCOL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_PROTOCOL, MongoProtocol))
#define MONGO_PROTOCOL_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_PROTOCOL, MongoProtocol const))
#define MONGO_PROTOCOL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MONGO_TYPE_PROTOCOL, MongoProtocolClass))
#define MONGO_IS_PROTOCOL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONGO_TYPE_PROTOCOL))
#define MONGO_IS_PROTOCOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MONGO_TYPE_PROTOCOL))
#define MONGO_PROTOCOL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MONGO_TYPE_PROTOCOL, MongoProtocolClass))

typedef struct _MongoProtocol        MongoProtocol;
typedef struct _MongoProtocolClass   MongoProtocolClass;
typedef enum   _MongoProtocolError   MongoProtocolError;
typedef struct _MongoProtocolPrivate MongoProtocolPrivate;
typedef struct _MongoReply           MongoReply;
typedef enum   _MongoDeleteFlags     MongoDeleteFlags;
typedef enum   _MongoInsertFlags     MongoInsertFlags;
typedef enum   _MongoOperation       MongoOperation;
typedef enum   _MongoQueryFlags      MongoQueryFlags;
typedef enum   _MongoReplyFlags      MongoReplyFlags;
typedef enum   _MongoUpdateFlags     MongoUpdateFlags;

enum _MongoProtocolError
{
   MONGO_PROTOCOL_ERROR_UNEXPECTED = 1,
};

/**
 * MongoDeleteFlags:
 * @MONGO_DELETE_NONE: Specify no delete flags.
 * @MONGO_DELETE_SINGLE_REMOVE: Only remove the first document matching the
 *    document selector.
 *
 * #MongoDeleteFlags are used when performing a delete operation.
 */
enum _MongoDeleteFlags
{
   MONGO_DELETE_NONE          = 0,
   MONGO_DELETE_SINGLE_REMOVE = 1 << 0,
};

/**
 * MongoInsertFlags:
 * @MONGO_INSERT_NONE: Specify no insert flags.
 * @MONGO_INSERT_CONTINUE_ON_ERROR: Continue inserting documents from
 *    the insertion set even if one fails.
 *
 * #MongoInsertFlags are used when performing an insert operation.
 */
enum _MongoInsertFlags
{
   MONGO_INSERT_NONE              = 0,
   MONGO_INSERT_CONTINUE_ON_ERROR = 1 << 0,
};

/**
 * MongoOperation:
 * @MONGO_OPERATION_REPLY: OP_REPLY from Mongo.
 * @MONGO_OPERATION_MSG: Generic message operation.
 * @MONGO_OPERATION_UPDATE: Operation to update documents.
 * @MONGO_OPERATION_INSERT: Operation to insert documents.
 * @MONGO_OPERATION_QUERY: Operation to find documents.
 * @MONGO_OPERATION_GETMORE: Operation to getmore documents on a cursor.
 * @MONGO_OPERATION_DELETE: Operation to delete documents.
 * @MONGO_OPERATION_KILL_CURSORS: Operation to kill an array of cursors.
 *
 * #MongoOperation represents the operation command identifiers used by
 * the Mongo wire protocol. This is mainly provided for completeness sake
 * and is unlikely to be needed by most consumers of this library.
 */
enum _MongoOperation
{
   MONGO_OPERATION_REPLY        = 1,
   MONGO_OPERATION_MSG          = 1000,
   MONGO_OPERATION_UPDATE       = 2001,
   MONGO_OPERATION_INSERT       = 2002,
   MONGO_OPERATION_QUERY        = 2004,
   MONGO_OPERATION_GETMORE      = 2005,
   MONGO_OPERATION_DELETE       = 2006,
   MONGO_OPERATION_KILL_CURSORS = 2007,
};

/**
 * MongoQueryFlags:
 * @MONGO_QUERY_NONE: No query flags supplied.
 * @MONGO_QUERY_TAILABLE_CURSOR: Cursor will not be closed when the last
 *    data is retrieved. You can resume this cursor later.
 * @MONGO_QUERY_SLAVE_OK: Allow query of replica slave.
 * @MONGO_QUERY_OPLOG_REPLAY: Used internally by Mongo.
 * @MONGO_QUERY_NO_CURSOR_TIMEOUT: The server normally times out idle
 *    cursors after an inactivity period (10 minutes). This prevents that.
 * @MONGO_QUERY_AWAIT_DATA: Use with %MONGO_QUERY_TAILABLE_CURSOR. Block
 *    rather than returning no data. After a period, time out.
 * @MONGO_QUERY_EXHAUST: Stream the data down full blast in multiple
 *    "more" packages. Faster when you are pulling a lot of data and
 *    know you want to pull it all down.
 * @MONGO_QUERY_PARTIAL: Get partial results from mongos if some shards
 *    are down (instead of throwing an error).
 *
 * #MongoQueryFlags is used for querying a Mongo instance.
 */
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

/**
 * MongoReplyFlags:
 * @MONGO_REPLY_NONE: No flags set.
 * @MONGO_REPLY_CURSOR_NOT_FOUND: Cursor was not found.
 * @MONGO_REPLY_QUERY_FAILURE: Query failed, error document provided.
 * @MONGO_REPLY_SHARD_CONFIG_STALE: Shard configuration is stale.
 * @MONGO_REPLY_AWAIT_CAPABLE: Wait for data to be returned until timeout
 *    has passed. Used with %MONGO_QUERY_TAILABLE_CURSOR.
 *
 * #MongoReplyFlags contains flags supplied by the Mongo server in reply
 * to a request.
 */
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

/**
 * MongoReply:
 * @ref_count: The reference count of the structure.
 * @flags: Flags for the reply.
 * @cursor_id: The cursor_id for the reply.
 * @starting_from: The offset of the first result document.
 * @n_returned: Number of documents returned.
 * @documents: (array length=n_returned): Array of documents returned.
 *
 * #MongoReply contains the reply from the mongo server. It is not
 * designed to be used by external applications unless you know exactly
 * why you need it. Try to use the higher level API when possible.
 */
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

/**
 * MongoProtocolClass:
 * @parent_class: The parent #GObjectClass.
 *
 */
struct _MongoProtocolClass
{
   GObjectClass parent_class;
};

MongoReply *mongo_reply_ref                    (MongoReply *reply);
void        mongo_reply_unref                  (MongoReply *reply);
GType       mongo_reply_get_type               (void) G_GNUC_CONST;
GQuark      mongo_protocol_error_quark         (void) G_GNUC_CONST;
GType       mongo_protocol_get_type            (void) G_GNUC_CONST;
GIOStream  *mongo_protocol_get_io_stream       (MongoProtocol        *protocol);
void        mongo_protocol_fail                (MongoProtocol        *protocol,
                                                const GError         *error);
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
GType       mongo_query_flags_get_type         (void) G_GNUC_CONST;

G_END_DECLS

#endif /* MONGO_PROTOCOL_H */
