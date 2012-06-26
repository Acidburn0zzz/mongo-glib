#include <mongo-glib/mongo-glib.h>

static void
test1 (void)
{
   MongoManager *mgr;
   gchar **items;

   mgr = mongo_manager_new();
   mongo_manager_add_seed(mgr, "localhost:27017");
   mongo_manager_add_host(mgr, "127.0.0.1:27017");

   items = mongo_manager_get_seeds(mgr);
   g_assert_cmpint(1, ==, g_strv_length(items));
   g_assert_cmpstr(items[0], ==, "localhost:27017");
   g_strfreev(items);

   items = mongo_manager_get_hosts(mgr);
   g_assert_cmpint(1, ==, g_strv_length(items));
   g_assert_cmpstr(items[0], ==, "127.0.0.1:27017");
   g_strfreev(items);

   mongo_manager_remove_seed(mgr, "localhost:27017");
   items = mongo_manager_get_seeds(mgr);
   g_assert_cmpint(0, ==, g_strv_length(items));
   g_strfreev(items);

   mongo_manager_remove_host(mgr, "127.0.0.1:27017");
   items = mongo_manager_get_hosts(mgr);
   g_assert_cmpint(0, ==, g_strv_length(items));
   g_strfreev(items);

   mongo_manager_unref(mgr);
}

gint
main (gint   argc,
      gchar *argv[])
{
   g_test_init(&argc, &argv, NULL);
   g_type_init();
   g_test_add_func("/MongoManager/basic", test1);
   return g_test_run();
}
