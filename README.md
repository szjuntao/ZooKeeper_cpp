ZooKeeper_server
================

This is a naive implementation of ZooKeeper Architecture using C++, providing remote cloud storage feature across multiple server nodes. 

## Features

* Implemented two-phase commit protocol to ensure consensus and correctness of each transaction.
* Concurrent accesses to the same resource are ordered by their arrival time. Racing condition is thus avoided. 
* Applied leader election algorithm for crash recovery.

## Current Limitation

* I am still organizing the crash-recovery code, will be done in a day or two.
* Does not support transfering message of more than 8KB in length, fixing soon.
* All files are storing in heap.

## Command Interface

By connecting to Leader / any Follower node with netcat, following commands are supported for accessing / modifying the storage.

* ```create <filename>``` - create a new file.
* ```append <filename> <text content>``` - append to an existing file.
* ```read <filename>``` - read a file.
* ```delete <filename>``` - delete a file. 

## Compile

```
make
```

## Sample Usage

The sample usage configuration files start two nodes in the same mathine (One Leader and One Follower). Server port is used for communication among ZooKeeper servers and Client port is used to accept connections from clients.

After compile, first, in one terminal:

```
./ZooKeeper sample_config1.txt LEADER 0
```

In another terminal:

```
./ZooKeeper sample_config2.txt FOLLOWER 1
``` 


