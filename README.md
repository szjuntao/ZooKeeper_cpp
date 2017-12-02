ZooKeeper_server
================

This is a cloud file storage application in C++ running across multiple server nodes. The architecture of this application is a simplefied / naive replica of Apache ZooKeeper. 

## Features

* A two-phase commit protocol is implemented to ensure consensus and correctness of each transaction among server nodes.
* Concurrent accesses to the same resource are ordered by their request arrival time. Racing condition is thus avoided. 
* The failure of a single node / minority of nodes will not affect the service of the whole network. A minority is defined as less than half of the whole number of nodes.

## Current Limitation

* I am still re-constructing the synchronization and crash-recovery code, will be done in a couple of days.
* Does not support transfering message of more than 8KB in length, fixing soon.
* All files are storing in heap.

## Command Interface

By connecting to Leader / any Follower node with netcat, following commands are supported for accessing / modifying the storage.

* ```create <filename>``` - create a new file.
* ```append <filename> <text content>``` - append to an existing file.
* ```read <filename>``` - read a file.
* ```delete <filename>``` - delete a file. 

N.B. later versions will require user to specify size of <text content> such as

```append a_file.txt 11 hello_world```

## Compile

```
make
```

## Usage

```./ZooKeeper <configuration file name> <LEADER/FOLLOWER> <zxid of this node>```

### Configuration File Format

```server_port=``` specifies the port number used to communicate among servers.
```client_port=``` specifies the port number used to accept client connections.

The remaining part is a list of servers in the ZooKeeper network. The length of the list sets the size of the ZooKeeper network and is the key in the proposal voting process for checking Quorum. 

* Please make sure ```<zxid of this node>``` appears on the list. 
* For all machines on the same ZooKeeper network, please make sure ```server_port=``` and list of servers consistent to one another. ```client_port=``` can be different.

### Sample Usage

The sample usage configuration files start two nodes in the same mathine (One Leader and One Follower). Server port is used for communication among ZooKeeper servers and Client port is used to accept connections from clients.

After compilation, first, in one terminal:

```
./ZooKeeper sample_config1.txt LEADER 0
```

to initiate a leader node with zxid 0. 

In another terminal:

```
./ZooKeeper sample_config2.txt FOLLOWER 1
``` 

to initiate a follower node with zxid 1.

Now we can use netcat to connect to either of these server and input command.

```netcat 127.0.0.1 8120``` -> connecting to follower
```netcat 127.0.0.1 8121``` -> connecting to leader
