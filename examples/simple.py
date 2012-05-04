#!/usr/bin/env python

from gi.repository import Mongo
from gi.repository import GLib

mainLoop = GLib.MainLoop(None, False)

def count_cb(col, result, data):
    success, count = col.count_finish(result)
    print 'We received %d documents!' % count
    mainLoop.quit()

def connect_cb(client, result, data):
    client.connect_finish(result)
    db = client.get_database('dbtest1')
    col = db.get_collection('dbcollection1')
    col.count_async(None, None, count_cb, None)

client = Mongo.Client()
client.add_seed('localhost', 27017)
client.connect_async(None, connect_cb, None)

mainLoop.run()
