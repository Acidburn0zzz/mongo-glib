#include <mongo-glib/mongo-glib.h>
#include <mongo-glib/mongo-debug.h>

static gboolean
compare_buffers (const gchar *a,
                 const gchar *b,
                 gsize        length)
{
   guint8 c1;
   guint8 c2;
   guint i;

   for (i = 0; i < length; i++) {
      c1 = a[i];
      c2 = b[i];
      if (c1 != c2) {
         g_error("Expected 0x%02x at offset %d got 0x%02x.", c1, i, c2);
      }
   }

   return TRUE;
}

static void
test_MongoOutputStream_write_message (void)
{
   MongoOutputStream *output;
   MongoWriteConcern *concern;
   GOutputStream *memory;
   MongoMessage *message;
   MongoBson *q;
   gboolean r;
   GError *error = NULL;
   gchar *capture;
   gsize length;
   gchar *capture2;
   gsize length2;
   guint i;

   r = g_file_get_contents("tests/capture/100queries.dat", &capture, &length, NULL);
   g_assert(r);

   concern = mongo_write_concern_new_unsafe();

   memory = g_memory_output_stream_new(NULL, 0, g_realloc, g_free);
   output = g_object_new(MONGO_TYPE_OUTPUT_STREAM,
                         "base-stream", memory,
                         "next-request-id", 0,
                         NULL);
   q = mongo_bson_new_empty();

   for (i = 0; i < 100; i++) {
      message = g_object_new(MONGO_TYPE_MESSAGE_QUERY,
                             "collection", "test.documents",
                             "query", q,
                             "request-id", i,
                             "response-to", i,
                             NULL);
      r = mongo_output_stream_write_message(output, message, concern, NULL, &error);
      g_assert_no_error(error);
      g_assert(r);
      g_object_unref(message);
   }

   capture2 = g_memory_output_stream_get_data(G_MEMORY_OUTPUT_STREAM(memory));
   length2 = g_memory_output_stream_get_data_size(G_MEMORY_OUTPUT_STREAM(memory));

   DUMP_BYTES(capture, capture, length);
   DUMP_BYTES(capture2, capture2, length2);

   g_assert_cmpint(length, ==, length2);
   g_assert(compare_buffers(capture, capture2, length));

   g_object_unref(memory);
   g_object_unref(output);
   mongo_bson_unref(q);
   g_free(capture);
   mongo_write_concern_free(concern);
}

gint
main (gint   argc,
      gchar *argv[])
{
   g_type_init();
   g_test_init(&argc, &argv, NULL);
   g_test_add_func("/MongoOutputStream/write_message",
                   test_MongoOutputStream_write_message);
   return g_test_run();
}
