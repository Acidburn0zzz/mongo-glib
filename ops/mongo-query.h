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
#include "mongo-flags.h"
#include "mongo-message.h"

G_BEGIN_DECLS

#define MONGO_TYPE_QUERY            (mongo_query_get_type())
#define MONGO_QUERY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_QUERY, MongoQuery))
#define MONGO_QUERY_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_QUERY, MongoQuery const))
#define MONGO_QUERY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MONGO_TYPE_QUERY, MongoQueryClass))
#define MONGO_IS_QUERY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONGO_TYPE_QUERY))
#define MONGO_IS_QUERY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MONGO_TYPE_QUERY))
#define MONGO_QUERY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MONGO_TYPE_QUERY, MongoQueryClass))

typedef struct _MongoQuery        MongoQuery;
typedef struct _MongoQueryClass   MongoQueryClass;
typedef struct _MongoQueryPrivate MongoQueryPrivate;

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
