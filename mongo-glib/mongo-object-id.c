/* mongo-object-id.c
 *
 * Copyright (C) 2011 Christian Hergert <christian@catch.com>
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

#include <string.h>

#include "mongo-object-id.h"

struct _MongoObjectId
{
   guint8 data[12];
};

MongoObjectId *
mongo_object_id_new_from_data (const guint8 *bytes)
{
   MongoObjectId *object_id;

   object_id = g_slice_new0(MongoObjectId);

   if (bytes) {
      memcpy(object_id, bytes, sizeof *object_id);
   }

   return object_id;
}

/**
 * mongo_object_id_to_string:
 * @object_id: (in): A #MongoObjectId.
 *
 * Converts @id into a hex string.
 *
 * Returns: (transfer full): The ObjectId as a string.
 */
gchar *
mongo_object_id_to_string (const MongoObjectId *object_id)
{
   GString *str;
   guint i;

   g_return_val_if_fail(object_id, NULL);

   str = g_string_sized_new(24);
   for (i = 0; i < sizeof object_id->data; i++) {
      g_string_append_printf(str, "%02x", object_id->data[i]);
   }

   return g_string_free(str, FALSE);
}

MongoObjectId *
mongo_object_id_copy (const MongoObjectId *object_id)
{
   MongoObjectId *copy;

   g_return_val_if_fail(object_id != NULL, NULL);

   copy = g_slice_new(MongoObjectId);
   memcpy(copy, object_id, sizeof *object_id);

   return copy;
}

gint
mongo_object_id_compare (const MongoObjectId *object_id,
                         const MongoObjectId *other)
{
   return memcmp(object_id, other, sizeof object_id->data);
}

void
mongo_object_id_free (MongoObjectId *object_id)
{
   if (object_id) {
      g_slice_free(MongoObjectId, object_id);
   }
}

void
mongo_clear_object_id (MongoObjectId **object_id)
{
   if (object_id && *object_id) {
      mongo_object_id_free(*object_id);
      *object_id = NULL;
   }
}

GType
mongo_object_id_get_type (void)
{
   static GType type_id = 0;
   static gsize initialized = FALSE;

   if (g_once_init_enter(&initialized)) {
      type_id = g_boxed_type_register_static(
         "MongoObjectId",
         (GBoxedCopyFunc)mongo_object_id_copy,
         (GBoxedFreeFunc)mongo_object_id_free);
      g_once_init_leave(&initialized, TRUE);
   }

   return type_id;
}
