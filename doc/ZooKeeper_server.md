Members in class ZooKeeper_server
=================================

## Server initialization
------------------------

```C++
bool parse_config(char* filename)
```

Open, read and parse configuration file. Set ```server_port```, the port number that used to communicate among other ZooKeeper and ```client_port``` which is used to accept clients' connections. Information of all other server nodes is parsed and stored in ```all_other_nodes```

* input value: c-style string specifying the filename.(Obtain from argv[1]) 

* return value:
  * true if parsing complete
  * false if text file format is corrupted

Shared data structure accessed: ```all_other_nodes```

Member function(s) called: ```read_line```

Allocation of dymamic memory: NO 
Spawning thread(s): NO

```C++
bool read_line(std::fstream &fs, std::string &str)
```

Warpper function of std::getline. Skip comment line (begin with '#') and empty line.

* return value:
  * true if read a line
  * false if EOF or other errors

```C++
bool set_role(char* role);
```

Set the role of calling server. Can only be 'LEADER' or 'FOLLOWER'.
Setting leader_fd to -1 for Leader Node, 0 for Follower(proper leader_fd will be set later). 

* input value: c-style string specifying the role. (Obtain from argv[2])
* return value:
  * true - setting complete
  * false - illegal input

```C++
bool set_zxid(char* id);
```

Set zxid of running server, storing in ```_zxid```. This zxid must also appear in the configuration text file.
* input value: c-style string specifying the zxid. (Obtain from argv[3]. ```[0-9]+```)
* return value:
  * true - if success
  * false - if parsing error or given zxid is not shown up in configuration file.

```C++
pthread_t init_follower()
```

Initialize the server as a follower. It first tries to find and connect to Leader node from ```all_other_nodes```. Then it spawns a thread to handle communication with Leader node with function ```pthread_leader_connector```.

* return value: 
  * if success - the pthread number of the new created thread.
  * if fail - exit the whole program with value -1.

N.B. This is NOT a self detach thread. Main thread is blocked after calling ```init_follower()```.

```C++
static void* pthread_leader_connector(void* arg)
```

Warpper function of ```leader_connector()```.
* input: ```ZooKeeper_server *this```, cast into ```void*``` by ```pthread_create()```.

```C++
void leader_connector()
```

Handles communication from/to Leader server. It first sends its ```_zxid``` to leader. After being acknowledged by leader, the real data exchange starts. There are three types of instructions sent from Leader. The correctness of format of the instructions sent are verified by corresponding regular expressions.

1. ```<proposal>_the_actual_command_</proposal>``` - A proposal. The follower judges and chooses to vote commit(yes) or deny(no) for this proposal in the format:

  * ```<commit>_the_same_command_</commit>```
  * ```<reject>_the_same_command_</reject>``` (NOT AVAILABLE YET)

2. ```<execution>_the_actual_command_</exection>``` - A previous command is approved by the majority of nodes. This command will be executed in this follower (and all other nodes). The client will get a ACK msg indicating the success.

3. ```<quorum>_the_actual_command_</quorum>``` - A veto from Leader about the previous request sent from this follower. The reason for veto is the number of alive machines does not met the quorum for making a decision. This follower will forward this error message back to client who sent the request.

###~To be continue...


 



