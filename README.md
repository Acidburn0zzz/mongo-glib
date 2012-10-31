# Mongo-GLib

## Introduction

Mongo-GLib is a library written in C to communicate with the MongoDB database
in an asynchronous fashion. Even though the library is written in C, it can be
used from higher level libraries such as Python and JavaScript through the use
of GObject Introspection.

The library supports both being a client and a server of the MongoDB wire
protocol. The MongoConnection structure works a lot like the connection
objects used in other MongoDB drivers, except asynchronously. You provide
a callback for most operations that will be executed upon completion of the
operation. Using MongoServer, you can implement the server side of the
MongoDB protocol allowing you to do interesting things like storing data
in systems other than MongoDB such as Redis.

## What it supports

  * BSON
    * Support for most BSON types.
    * Iteration of BSON documents including recursion without allocations.
  * Connection
    * Client support for MongoDB protocol.
    * Don't worry about connecting, it happens automatically and asynchronously
      without any intervention.
    * Support to auto-reconnect to a new master.
    * Support for execution commmands.
    * Automatically performs a getlasterr command to check status on operations
      that modify the database.
  * Server
    * Implement your own MongoDB server.
    * Handle requests as they come in, query external systems and provide a
      reply asynchronously.

## What it doesn't support

  * GridFS (patches welcome).
  * Sort support on queries.
  * Index creation.
  * Probably more ...

