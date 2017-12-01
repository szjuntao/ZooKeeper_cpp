Members in class ZooKeeper_server
=================================

## Server initialization
------------------------

```C++
bool parse_config(char* filename)
```

Open, read and parse configuration file. 

* input value: c-style string specifying the filename.(Obtain from argv[1]) 

* return value:
  * true if parsing complete
  * false if text file format is corrupted

Shared data structure accessed: all_other_nodes

Member function(s) called: rd_line
Member variable(s) used: client_port, server_port

Allocation of dymamic memory: NO 
Spawning thread(s): NO

```C++
bool rd_line(std::fstream &fs, std::string &str)
```

Warpper function of std::getline. Skip comment line (begin with '#') and empty line.

* return value:
  * true if read a line
  * false if EOF

```C++
bool set_role(char* role);
```

Set the role of calling server. Can only be 'LEADER', 'FOLLOWER' or 'RECOVER'.
Setting leader_fd to -1 for Leader Node, 0 for Follower.

* input value: c-style string specifying the role. (Obtain from argv[2])
* return value:
  * true - setting complete
  * false - illegal input

```C++
bool set_zxid(char* id);
```

set


