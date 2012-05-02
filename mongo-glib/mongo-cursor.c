/* mongo-cursor.c
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

#include <glib/gi18n.h>

#include "mongo-cursor.h"

G_DEFINE_TYPE(MongoCursor, mongo_cursor, G_TYPE_OBJECT)

struct _MongoCursorPrivate
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
mongo_cursor_finalize (GObject *object)
{
   G_OBJECT_CLASS(mongo_cursor_parent_class)->finalize(object);
}

static void
mongo_cursor_class_init (MongoCursorClass *klass)
{
   GObjectClass *object_class;

   object_class = G_OBJECT_CLASS(klass);
   object_class->finalize = mongo_cursor_finalize;
   g_type_class_add_private(object_class, sizeof(MongoCursorPrivate));
}

static void
mongo_cursor_init (MongoCursor *cursor)
{
   cursor->priv =
      G_TYPE_INSTANCE_GET_PRIVATE(cursor,
                                  MONGO_TYPE_CURSOR,
                                  MongoCursorPrivate);
}
