/* mongo-update.c
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

#include "mongo-update.h"

G_DEFINE_TYPE(MongoUpdate, mongo_update, MONGO_TYPE_MESSAGE)

struct _MongoUpdatePrivate
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
mongo_update_finalize (GObject *object)
{
   G_OBJECT_CLASS(mongo_update_parent_class)->finalize(object);
}

static void
mongo_update_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
   //MongoUpdate *update = MONGO_UPDATE(object);

   switch (prop_id) {
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_update_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
   //MongoUpdate *update = MONGO_UPDATE(object);

   switch (prop_id) {
   default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
   }
}

static void
mongo_update_class_init (MongoUpdateClass *klass)
{
   GObjectClass *object_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_update_finalize;
   object_class->get_property = mongo_update_get_property;
   object_class->set_property = mongo_update_set_property;
   g_type_class_add_private(object_class, sizeof(MongoUpdatePrivate));
}

static void
mongo_update_init (MongoUpdate *update)
{
   update->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(update,
                                  MONGO_TYPE_UPDATE,
                                  MongoUpdatePrivate);
}
