/* mongo-reply.h
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

#ifndef MONGO_REPLY_H
#define MONGO_REPLY_H

#include "mongo-bson.h"
#include "mongo-message.h"

G_BEGIN_DECLS

#define MONGO_TYPE_REPLY            (mongo_reply_get_type())
#define MONGO_TYPE_REPLY_FLAGS      (mongo_reply_flags_get_type())
#define MONGO_REPLY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_REPLY, MongoReply))
#define MONGO_REPLY_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_REPLY, MongoReply const))
#define MONGO_REPLY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MONGO_TYPE_REPLY, MongoReplyClass))
#define MONGO_IS_REPLY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONGO_TYPE_REPLY))
#define MONGO_IS_REPLY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MONGO_TYPE_REPLY))
#define MONGO_REPLY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MONGO_TYPE_REPLY, MongoReplyClass))

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
typedef enum
{
   MONGO_REPLY_NONE               = 0,
   MONGO_REPLY_CURSOR_NOT_FOUND   = 1 << 0,
   MONGO_REPLY_QUERY_FAILURE      = 1 << 1,
   MONGO_REPLY_SHARD_CONFIG_STALE = 1 << 2,
   MONGO_REPLY_AWAIT_CAPABLE      = 1 << 3,
} MongoReplyFlags;

typedef struct _MongoReply        MongoReply;
typedef struct _MongoReplyClass   MongoReplyClass;
typedef struct _MongoReplyPrivate MongoReplyPrivate;

struct _MongoReply
{
   MongoMessage parent;

   /*< private >*/
   MongoReplyPrivate *priv;
};

struct _MongoReplyClass
{
   MongoMessageClass parent_class;
};

GType            mongo_reply_flags_get_type (void) G_GNUC_CONST;
gsize            mongo_reply_get_count      (MongoReply       *reply);
guint64          mongo_reply_get_cursor_id  (MongoReply       *reply);
MongoBson      **mongo_reply_get_documents  (MongoReply       *reply,
                                             gsize            *count);
MongoReplyFlags  mongo_reply_get_flags      (MongoReply       *reply);
guint            mongo_reply_get_offset     (MongoReply       *reply);
GType            mongo_reply_get_type       (void) G_GNUC_CONST;
void             mongo_reply_set_cursor_id  (MongoReply       *reply,
                                             guint64           cursor_id);
void             mongo_reply_set_documents  (MongoReply       *reply,
                                             MongoBson       **documents,
                                             gsize             count);
void             mongo_reply_set_flags      (MongoReply       *reply,
                                             MongoReplyFlags   flags);
void             mongo_reply_set_offset     (MongoReply       *reply,
                                             guint             offset);

G_END_DECLS

#endif /* MONGO_REPLY_H */
