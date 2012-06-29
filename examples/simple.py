#!/usr/bin/env python

from gi.repository import Mongo
from gi.repository import GLib

def count_cb(col, result, mainLoop):
    success, count = col.count_finish(result)
    print 'Counted %d documents!' % count
    mainLoop.quit()

if __name__ == '__main__':
    mainLoop = GLib.MainLoop(None, False)
    conn = Mongo.Connection.new_from_uri('mongodb://127.0.0.1')
    db = conn.get_database('dbtest1')
    col = db.get_collection('dbcollection1')
    col.count_async(None, None, count_cb, mainLoop)
    mainLoop.run()
