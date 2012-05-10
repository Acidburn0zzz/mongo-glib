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
#include <unistd.h>

#include "mongo-object-id.h"

struct _MongoObjectId
{
   guint8 data[12];
};

static guint8  gMachineId[3];
static gushort gPid;
static gint32  gIncrement;

static void
mongo_object_id_init (void)
{
   gchar hostname[HOST_NAME_MAX] = { 0 };
   char *md5;
   int ret;

   if (0 != (ret = gethostname(hostname, sizeof hostname - 1))) {
      g_error("Failed to get hostname, cannot generate MongoObjectId");
   }

   md5 = g_compute_checksum_for_string(G_CHECKSUM_MD5, hostname,
                                       sizeof hostname);
   memcpy(gMachineId, md5, sizeof gMachineId);
   g_free(md5);

   gPid = (gushort)getpid();
}

MongoObjectId *
mongo_object_id_new (void)
{
   static gsize initialized = FALSE;
   GTimeVal val = { 0 };
   guint32 t;
   guint8 bytes[12];
   gint32 inc;

   if (g_once_init_enter(&initialized)) {
      mongo_object_id_init();
      g_once_init_leave(&initialized, TRUE);
   }

   g_get_current_time(&val);
   t = GUINT32_TO_BE(val.tv_sec);
   inc = GUINT32_TO_BE(g_atomic_int_add(&gIncrement, 1));

   memcpy(&bytes[0], &t, sizeof t);
   memcpy(&bytes[4], &gMachineId, sizeof gMachineId);
   memcpy(&bytes[7], &gPid, sizeof gPid);
   bytes[9] = ((guint8 *)&inc)[1];
   bytes[10] = ((guint8 *)&inc)[2];
   bytes[11] = ((guint8 *)&inc)[3];

   return mongo_object_id_new_from_data(bytes);
}

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
   MongoObjectId *copy = NULL;

   if (object_id) {
      copy = g_slice_new(MongoObjectId);
      memcpy(copy, object_id, sizeof *object_id);
   }

   return copy;
}

gint
mongo_object_id_compare (const MongoObjectId *object_id,
                         const MongoObjectId *other)
{
   return memcmp(object_id, other, sizeof object_id->data);
}

gboolean
mongo_object_id_equal (const MongoObjectId *object_id,
                       const MongoObjectId *other)
{
   return !mongo_object_id_compare(object_id, other);
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
