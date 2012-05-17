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

/**
 * SECTION:mongo-object-id
 * @title: MongoObjectId
 * @short_description: ObjectId structure used by BSON and Mongo.
 *
 * #MongoObjectId represents an ObjectId as used by the BSON serialization
 * format and Mongo. It is a 12-byte structure designed in a way to allow
 * client side generation of unique object identifiers. The first 4 bytes
 * contain the UNIX timestamp since January 1, 1970. The following 3 bytes
 * contain the first 3 bytes of the hostname MD5 hashed. Following that are
 * 2 bytes containing the Process ID or Thread ID. The last 3 bytes contain
 * a continually incrementing counter.
 *
 * When creating a new #MongoObjectId using mongo_object_id_new(), all of
 * this data is created for you.
 *
 * To create a #MongoObjectId from an existing 12 bytes, use
 * mongo_object_id_new_from_data().
 *
 * To free an allocated #MongoObjectId, use mongo_object_id_free().
 */

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

/**
 * mongo_object_id_new:
 *
 * Create a new #MongoObjectId. The timestamp, PID, hostname, and counter
 * fields of the #MongoObjectId will be generated.
 *
 * Returns: (transfer full): A #MongoObjectId that should be freed with
 * mongo_object_id_free().
 */
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

/**
 * mongo_object_id_new_from_data:
 * @bytes: (array): The bytes containing the object id.
 *
 * Creates a new #MongoObjectId from an array of 12 bytes. @bytes
 * MUST contain 12-bytes.
 *
 * Returns: (transfer full): A newly allocated #MongoObjectId that should
 * be freed with mongo_object_id_free().
 */
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
 * Converts @object_id into a hex string.
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

/**
 * mongo_object_id_copy:
 * @object_id: (in): A #MongoObjectId.
 *
 * Creates a new #MongoObjectId that is a copy of @object_id.
 *
 * Returns: (transfer full): A #MongoObjectId that should be freed with
 * mongo_object_id_free().
 */
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

/**
 * mongo_object_id_compare:
 * @object_id: (in): The first #MongoObjectId.
 * @other: (in): The second #MongoObjectId.
 *
 * A qsort() style compare function that will return less than zero
 * if @object_id is less than @other, zero if they are the same, and
 * greater than one if @other is greater than @object_id.
 *
 * Returns: A qsort() style compare integer.
 */
gint
mongo_object_id_compare (const MongoObjectId *object_id,
                         const MongoObjectId *other)
{
   return memcmp(object_id, other, sizeof object_id->data);
}

/**
 * mongo_object_id_equal:
 * @object_id: (in): A #MongoObjectId.
 * @other: (in): A #MongoObjectId.
 *
 * Checks if @object_id and @other contain the same object id.
 *
 * Returns: %TRUE if @object_id and @other are equal.
 */
gboolean
mongo_object_id_equal (const MongoObjectId *object_id,
                       const MongoObjectId *other)
{
   return !mongo_object_id_compare(object_id, other);
}

/**
 * mongo_object_id_free:
 * @object_id: (in): A #MongoObjectId.
 *
 * Frees a #MongoObjectId.
 */
void
mongo_object_id_free (MongoObjectId *object_id)
{
   if (object_id) {
      g_slice_free(MongoObjectId, object_id);
   }
}

/**
 * mongo_clear_object_id:
 * @object_id: (out): A pointer to a #MongoObjectId.
 *
 * Clears the pointer to a #MongoObjectId by freeing the #MongoObjectId
 * and then setting the pointer to %NULL. If no #MongoObjectId was found,
 * then no operation is performed.
 */
void
mongo_clear_object_id (MongoObjectId **object_id)
{
   if (object_id && *object_id) {
      mongo_object_id_free(*object_id);
      *object_id = NULL;
   }
}

/**
 * mongo_object_id_get_type:
 *
 * Fetches the boxed #GType for #MongoObjectId.
 *
 * Returns: A #GType.
 */
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
