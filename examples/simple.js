#!/usr/bin/env gjs

const Mongo = imports.gi.Mongo;
const MainLoop = imports.mainloop;

let connection = new Mongo.Connection.new_from_uri('mongodb://127.0.0.1')
let database = connection.get_database('dbtest1');
let collection = database.get_collection('dbcollection1');

collection.count_async(null, null, function(collection, result, data) {
  let success, count = collection.count_finish(result);
  log('*** Found ' + count + ' documents ***');
  MainLoop.quit('');
});

MainLoop.run('');
