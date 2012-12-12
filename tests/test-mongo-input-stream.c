#include <mongo-glib/mongo-glib.h>

static void
test_MongoInputStream_read_message (void)
{
   MongoInputStream *stream;
   GFileInputStream *input;
   MongoMessage *message;
   GError *error = NULL;
   GFile *file;
   guint i;

   file = g_file_new_for_path("tests/capture/capture.recv");
   g_assert(file);

   input = g_file_read(file, NULL, &error);
   g_assert(input);

   stream = mongo_input_stream_new(G_INPUT_STREAM(input));
   g_assert(stream);

   for (i = 0; i < 100; i++) {
      message = mongo_input_stream_read_message(stream, NULL, &error);
      g_assert_no_error(error);
      g_assert(message);
      g_assert(MONGO_IS_MESSAGE_REPLY(message));
      g_object_unref(message);
   }

   message = mongo_input_stream_read_message(stream, NULL, &error);
   g_assert(!message);
   g_assert(error);
   g_clear_error(&error);

   g_object_unref(stream);
   g_object_unref(input);
   g_object_unref(file);
}

gint
main (gint   argc,
      gchar *argv[])
{
   g_type_init();
   g_test_init(&argc, &argv, NULL);
   g_test_add_func("/MongoInputStream/read_message",
                   test_MongoInputStream_read_message);
   return g_test_run();
}
