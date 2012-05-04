#!/usr/bin/env gjs

const Mongo = imports.gi.Mongo;
const MainLoop = imports.mainloop;

let client = new Mongo.Client();

client.add_seed("localhost", 27017);
client.connect_async(null, function(client, result, data) {
	client.connect_finish(result);
	col = client.get_database("dbtest1").get_collection("dbcollection1");
	col.count_async(null, null, function(col, result, data) {
		count = col.count_finish(result)[1];
		log('*** Found ' + count + ' Documents ***');
		MainLoop.quit('');
	}, null);
}, null);

MainLoop.run('');
