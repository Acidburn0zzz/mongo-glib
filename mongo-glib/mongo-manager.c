/* mongo-manager.c
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

#include "mongo-manager.h"

struct _MongoManager
{
   volatile gint ref_count;
   GPtrArray *seeds;
   GPtrArray *hosts;
   guint delay;
};

MongoManager *
mongo_manager_new (void)
{
   MongoManager *mgr;

   mgr = g_slice_new0(MongoManager);
   mgr->ref_count = 1;
   mgr->hosts = g_ptr_array_new_with_free_func(g_free);
   mgr->seeds = g_ptr_array_new_with_free_func(g_free);

   return mgr;
}

void
mongo_manager_add_host (MongoManager *manager,
                        const gchar  *host)
{
   g_return_if_fail(manager);
   g_return_if_fail(host);
   g_ptr_array_add(manager->hosts, g_strdup(host));
}

void
mongo_manager_add_seed (MongoManager *manager,
                        const gchar  *seed)
{
   g_return_if_fail(manager);
   g_return_if_fail(seed);
   g_ptr_array_add(manager->seeds, g_strdup(seed));
}

void
mongo_manager_clear_hosts (MongoManager *manager)
{
   g_return_if_fail(manager);
   g_ptr_array_remove_range(manager->hosts, 0, manager->hosts->len);
}

/**
 * mongo_manager_get_hosts:
 * @manager: (in): A #MongoManager.
 *
 * Retrieves the array of hosts.
 *
 * Returns: (transfer full): An array of hosts.
 */
gchar **
mongo_manager_get_hosts (MongoManager *manager)
{
   gchar **ret;
   guint i;

   g_return_val_if_fail(manager, NULL);

   ret = g_new0(gchar *, manager->hosts->len + 1);
   for (i = 0; i < manager->hosts->len; i++) {
      ret[i] = g_strdup(g_ptr_array_index(manager->hosts, i));
   }

   return ret;
}

/**
 * mongo_manager_get_seeds:
 * @manager: (in): A #MongoManager.
 *
 * Retrieves the array of seeds.
 *
 * Returns: (transfer full): An array of seeds.
 */
gchar **
mongo_manager_get_seeds (MongoManager *manager)
{
   gchar **ret;
   guint i;

   g_return_val_if_fail(manager, NULL);

   ret = g_new0(gchar *, manager->seeds->len + 1);
   for (i = 0; i < manager->seeds->len; i++) {
      ret[i] = g_strdup(g_ptr_array_index(manager->seeds, i));
   }

   return ret;
}

void
mongo_manager_remove_host (MongoManager *manager,
                           const gchar  *host)
{
   guint i;

   g_return_if_fail(manager);
   g_return_if_fail(host);

   for (i = 0; i < manager->hosts->len; i++) {
      if (!g_strcmp0(manager->hosts->pdata[i], host)) {
         g_ptr_array_remove_index(manager->hosts, i);
         break;
      }
   }
}

void
mongo_manager_remove_seed (MongoManager *manager,
                           const gchar  *seed)
{
   guint i;

   g_return_if_fail(manager);
   g_return_if_fail(seed);

   for (i = 0; i < manager->seeds->len; i++) {
      if (!g_strcmp0(manager->seeds->pdata[i], seed)) {
         g_ptr_array_remove_index(manager->seeds, i);
         break;
      }
   }
}

void
mongo_manager_reset_delay (MongoManager *manager)
{
   g_return_if_fail(manager);
   manager->delay = 0;
}

static void
mongo_manager_dispose (MongoManager *manager)
{
   g_assert(manager);
   g_ptr_array_unref(manager->hosts);
   g_ptr_array_unref(manager->seeds);
}

MongoManager *
mongo_manager_ref (MongoManager *manager)
{
   g_return_val_if_fail(manager, NULL);
   g_return_val_if_fail(manager->ref_count > 0, NULL);
   g_atomic_int_inc(&manager->ref_count);
   return manager;
}

void
mongo_manager_unref (MongoManager *manager)
{
   g_return_if_fail(manager);
   g_return_if_fail(manager->ref_count > 0);
   if (g_atomic_int_dec_and_test(&manager->ref_count)) {
      mongo_manager_dispose(manager);
      g_slice_free(MongoManager, manager);
   }
}

GType
mongo_manager_get_type (void)
{
   static gsize initialized;
   static GType type_id;

   if (g_once_init_enter(&initialized)) {
      type_id = g_boxed_type_register_static(
            "MongoManager",
            (GBoxedCopyFunc)mongo_manager_ref,
            (GBoxedFreeFunc)mongo_manager_unref);
      g_once_init_leave(&initialized, TRUE);
   }

   return type_id;
}
