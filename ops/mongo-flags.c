/* mongo-flags.c
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

#include "mongo-flags.h"

GType
mongo_query_flags_get_type (void)
{
   static gsize initialized;
   static GType type_id;
   static const GFlagsValue values[] = {
      { MONGO_QUERY_NONE, "MONGO_QUERY_NONE", "NONE" },
      { MONGO_QUERY_TAILABLE_CURSOR, "MONGO_QUERY_TAILABLE_CURSOR", "TAILABLE_CURSOR" },
      { MONGO_QUERY_SLAVE_OK, "MONGO_QUERY_SLAVE_OK", "SLAVE_OK" },
      { MONGO_QUERY_OPLOG_REPLAY, "MONGO_QUERY_OPLOG_REPLAY", "OPLOG_REPLAY" },
      { MONGO_QUERY_NO_CURSOR_TIMEOUT, "MONGO_QUERY_NO_CURSOR_TIMEOUT", "NO_CURSOR_TIMEOUT" },
      { MONGO_QUERY_AWAIT_DATA, "MONGO_QUERY_AWAIT_DATA", "AWAIT_DATA" },
      { MONGO_QUERY_EXHAUST, "MONGO_QUERY_EXHAUST", "EXHAUST" },
      { MONGO_QUERY_PARTIAL, "MONGO_QUERY_PARTIAL", "PARTIAL" },
      { 0 }
   };

   if (g_once_init_enter(&initialized)) {
      type_id = g_flags_register_static("MongoQueryFlags", values);
      g_once_init_leave(&initialized, TRUE);
   }

   return type_id;
}

GType
mongo_reply_flags_get_type (void)
{
   static gsize initialized;
   static GType type_id;
   static const GFlagsValue values[] = {
      { MONGO_REPLY_NONE, "MONGO_REPLY_NONE", "NONE" },
      { MONGO_REPLY_CURSOR_NOT_FOUND, "MONGO_REPLY_CURSOR_NOT_FOUND", "CURSOR_NOT_FOUND" },
      { MONGO_REPLY_QUERY_FAILURE, "MONGO_REPLY_QUERY_FAILURE", "QUERY_FAILURE" },
      { MONGO_REPLY_SHARD_CONFIG_STALE, "MONGO_REPLY_SHARD_CONFIG_STALE", "SHARD_CONFIG_STALE" },
      { MONGO_REPLY_AWAIT_CAPABLE, "MONGO_REPLY_AWAIT_CAPABLE", "AWAIT_CAPABLE" },
      { 0 }
   };

   if (g_once_init_enter(&initialized)) {
      type_id = g_flags_register_static("MongoReplyFlags", values);
      g_once_init_leave(&initialized, TRUE);
   }

   return type_id;
}
