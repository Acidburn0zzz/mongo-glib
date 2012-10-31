/* mongo-insert.h
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

#ifndef MONGO_INSERT_H
#define MONGO_INSERT_H

#include "mongo-message.h"

G_BEGIN_DECLS

#define MONGO_TYPE_INSERT            (mongo_insert_get_type())
#define MONGO_INSERT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_INSERT, MongoInsert))
#define MONGO_INSERT_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MONGO_TYPE_INSERT, MongoInsert const))
#define MONGO_INSERT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MONGO_TYPE_INSERT, MongoInsertClass))
#define MONGO_IS_INSERT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MONGO_TYPE_INSERT))
#define MONGO_IS_INSERT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MONGO_TYPE_INSERT))
#define MONGO_INSERT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MONGO_TYPE_INSERT, MongoInsertClass))

typedef struct _MongoInsert        MongoInsert;
typedef struct _MongoInsertClass   MongoInsertClass;
typedef struct _MongoInsertPrivate MongoInsertPrivate;

struct _MongoInsert
{
   MongoMessage parent;

   /*< private >*/
   MongoInsertPrivate *priv;
};

struct _MongoInsertClass
{
   MongoMessageClass parent_class;
};

GType mongo_insert_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* MONGO_INSERT_H */
