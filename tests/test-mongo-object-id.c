#include "test-helper.h"

#include <mongo-glib/mongo-glib.h>

static void
test1 (void)
{
   MongoObjectId *oid1;
   MongoObjectId *oid2;

   oid1 = mongo_object_id_new();
   oid2 = mongo_object_id_new();

   g_assert(!mongo_object_id_equal(oid1, oid2));

   mongo_object_id_free(oid1);
   mongo_object_id_free(oid2);
}

static void
test2 (void)
{
   MongoObjectId *oid1;
   MongoObjectId *oid2;

   oid1 = mongo_object_id_new();
   oid2 = mongo_object_id_copy(oid1);

   g_assert(mongo_object_id_equal(oid1, oid2));

   mongo_object_id_free(oid1);
   mongo_object_id_free(oid2);
}

static void
test3 (void)
{
   MongoObjectId *oid1;

   oid1 = mongo_object_id_new();
   g_assert(oid1);
   mongo_clear_object_id(&oid1);
   g_assert(!oid1);
}

static void
test4 (void)
{
   MongoObjectId *oid1;
   MongoObjectId *oid2;
   gchar *str;
   gint ret;

   oid1 = mongo_object_id_new();
   str = mongo_object_id_to_string(oid1);
   oid2 = mongo_object_id_new_from_string(str);

   ret = mongo_object_id_equal(oid1, oid2);
   g_assert_cmpint(ret, ==, TRUE);

   g_free(str);
   mongo_object_id_free(oid1);
   mongo_object_id_free(oid2);
}

gint
main (gint   argc,
      gchar *argv[])
{
   g_test_init(&argc, &argv, NULL);
   g_type_init();

   g_test_add_func("/MongoObjectId/new", test1);
   g_test_add_func("/MongoObjectId/new_from_string", test4);
   g_test_add_func("/MongoObjectId/copy", test2);
   g_test_add_func("/MongoObjectId/clear", test3);

   return g_test_run();
}
