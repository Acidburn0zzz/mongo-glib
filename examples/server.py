from gi.repository import Mongo
from gi.repository import GLib

COLLECTIONS = {}
COMMANDS = {}

def getlasterror(server, client, message):
    b = Mongo.Bson.new_empty()
    b.append_int('n', 0)
    b.append_null('err')
    b.append_int('ok', 1)
    return b

def whatsmyuri(server, client, message):
    b = Mongo.Bson.new_empty()
    b.append_string('you', client.get_uri())
    return b

def replSetGetStatus(server, client, message):
    b = Mongo.Bson.new_empty()
    b.append_string('errmsg', 'not running with --replSet')
    b.append_int('ok', 0)
    return b

def handleGetmore(server, client, message):
    reply = Mongo.MessageReply()
    message.set_reply(reply)
    return True

def handleInsert(server, client, message):
    documents = message.get_documents()
    if documents:
        collection = message.props.collection
        if collection not in COLLECTIONS:
            COLLECTIONS[collection] = []
        # XXX: Investigate why ref'ing is broken, work around below.
        for d in documents:
            COLLECTIONS[collection].append(d.dup())

def handleQuery(server, client, message):
    print 'COLLECTIONS'
    print '==========='
    for k,v in COLLECTIONS.iteritems():
        print k
        for i in v:
            print ' ', i.to_string(False)
    if message.is_command():
        bson = COMMANDS[message.get_command_name()](server, client, message)
        message.set_reply_bson(0, bson);
        return True
    reply = Mongo.MessageReply(response_to=message.get_request_id())
    docs = COLLECTIONS.get(message.props.collection, [])
    skip = message.props.skip
    limit = message.props.limit
    if skip or limit:
        docs = docs[skip:limit or -1]
    print 'DOCS', docs
    reply.set_documents(docs)
    message.set_reply(reply)
    print "RETURNING DOCS", reply.get_documents()
    return True

COMMANDS['getlasterror'] = getlasterror
COMMANDS['whatsmyuri'] = whatsmyuri
COMMANDS['replSetGetStatus'] = replSetGetStatus

Server = Mongo.Server()
Server.add_inet_port(5201, None)
Server.connect('request-insert', handleInsert)
Server.connect('request-getmore', handleGetmore)
Server.connect('request-query', handleQuery)

GLib.MainLoop().run()
