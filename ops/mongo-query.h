/* mongo-query.h
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

#ifndef MONGO_QUERY_H
#define MONGO_QUERY_H

#include "mongo-bson.h"
#include "mongo-message.h"

G_BEGIN_DECLS

#define MONGO_TYPE_QUERY            (mongo_query_get_type())
#define MONGO_TYPE_QUERY_FLAGS      (mongo_query_flags_get_type())
#define MONGO_QUERY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_QUERY, MongoQuery))
#define MONGO_QUERY_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_QUERY, MongoQuery const))
#define MONGO_QUERY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MONGO_TYPE_QUERY, MongoQueryClass))
#define MONGO_IS_QUERY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONGO_TYPE_QUERY))
#define MONGO_IS_QUERY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MONGO_TYPE_QUERY))
#define MONGO_QUERY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MONGO_TYPE_QUERY, MongoQueryClass))

typedef struct _MongoQuery        MongoQuery;
typedef struct _MongoQueryClass   MongoQueryClass;
typedef struct _MongoQueryPrivate MongoQueryPrivate;

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
typedef enum
{
   MONGO_QUERY_NONE              = 0,
   MONGO_QUERY_TAILABLE_CURSOR   = 1 << 1,
   MONGO_QUERY_SLAVE_OK          = 1 << 2,
   MONGO_QUERY_OPLOG_REPLAY      = 1 << 3,
   MONGO_QUERY_NO_CURSOR_TIMEOUT = 1 << 4,
   MONGO_QUERY_AWAIT_DATA        = 1 << 5,
   MONGO_QUERY_EXHAUST           = 1 << 6,
   MONGO_QUERY_PARTIAL           = 1 << 7,
} MongoQueryFlags;

struct _MongoQuery
{
   MongoMessage parent;

   /*< private >*/
   MongoQueryPrivate *priv;
};

struct _MongoQueryClass
{
   MongoMessageClass parent_class;
};

GType            mongo_query_flags_get_type (void) G_GNUC_CONST;
const gchar     *mongo_query_get_collection (MongoQuery      *query);
MongoQueryFlags  mongo_query_get_flags      (MongoQuery      *query);
guint            mongo_query_get_limit      (MongoQuery      *query);
const MongoBson *mongo_query_get_query      (MongoQuery      *query);
const MongoBson *mongo_query_get_selector   (MongoQuery      *query);
guint            mongo_query_get_skip       (MongoQuery      *query);
GType            mongo_query_get_type       (void) G_GNUC_CONST;
void             mongo_query_set_collection (MongoQuery      *query,
                                             const gchar     *collection);
void             mongo_query_set_flags      (MongoQuery      *query,
                                             MongoQueryFlags  flags);
void             mongo_query_set_limit      (MongoQuery      *query,
                                             guint            limit);
void             mongo_query_set_query      (MongoQuery      *query,
                                             const MongoBson *bson);
void             mongo_query_set_selector   (MongoQuery      *query,
                                             const MongoBson *bson);
void             mongo_query_set_skip       (MongoQuery      *query,
                                             guint            skip);
void             mongo_query_take_query     (MongoQuery      *query,
                                             MongoBson       *bson);
void             mongo_query_take_selector  (MongoQuery      *query,
                                             MongoBson       *bson);

G_END_DECLS

#endif /* MONGO_QUERY_H */
