# TODO

 * Make unit tests use MongoServer subclass for mocking replies.
 * Fuzz messages and test decoding and encoding.
 * Change MongoProtocol to use MongoMessage subclasses for encoding.
 * Sort support.
 * Index creation.

MongoClient

 * queue requests while we connect.
 * handle auto connecting.
 * reconnect on failure.
 * tests for short reads/writes.

