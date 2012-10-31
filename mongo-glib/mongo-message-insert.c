/* mongo-message-insert.c
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

#include "mongo-message-insert.h"

G_DEFINE_TYPE(MongoMessageInsert, mongo_message_insert, MONGO_TYPE_MESSAGE)

struct _MongoMessageInsertPrivate
{
   gpointer dummy;
};

enum
{
   PROP_0,
   LAST_PROP
};

//static GParamSpec *gParamSpecs[LAST_PROP];

static void
mongo_message_insert_finalize (GObject *object)
{
   G_OBJECT_CLASS(mongo_message_insert_parent_class)->finalize(object);
}

static void
mongo_message_insert_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
   //MongoMessageInsert *insert = MONGO_MESSAGE_INSERT(object);

   switch (prop_id) {
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_message_insert_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
   //MongoMessageInsert *insert = MONGO_MESSAGE_INSERT(object);

   switch (prop_id) {
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_message_insert_class_init (MongoMessageInsertClass *klass)
{
   GObjectClass *object_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_message_insert_finalize;
   object_class->get_property = mongo_message_insert_get_property;
   object_class->set_property = mongo_message_insert_set_property;
   g_type_class_add_private(object_class, sizeof(MongoMessageInsertPrivate));
}

static void
mongo_message_insert_init (MongoMessageInsert *insert)
{
   insert->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(insert,
                                  MONGO_TYPE_MESSAGE_INSERT,
                                  MongoMessageInsertPrivate);
}
