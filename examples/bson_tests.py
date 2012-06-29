#!/usr/bin/env python

def runTimed(f, *args, **kwargs):
    from datetime import datetime
    start = datetime.now()
    f(*args, **kwargs)
    end = datetime.now()
    return (end - start).total_seconds()

def test1():
    from gi.repository import Mongo

    bson = Mongo.Bson()
    for i in xrange(10000):
        bson.append_int(str(i), i)

def test2():
    import bson as _bson
    seq = ((str(i), i) for i in xrange(10000))
    bson = _bson.BSON.encode(_bson.SON(seq))

def main():
    import sys

    n_runs = 30

    glibtotal = 0
    bsontotal = 0

    for i in range(n_runs):
        t1 = runTimed(test1)
        glibtotal += t1
        print 'Mongo.Bson:', t1
        t2 = runTimed(test2)
        bsontotal += t2
        print '      bson:', t2
        print '   Speedup:', t2 / t1
        print

    print '====='
    print 'GLib Average:', glibtotal / float(n_runs)
    print 'BSON Average:', bsontotal / float(n_runs)

    sys.exit(0)

if __name__ == '__main__':
    main()
