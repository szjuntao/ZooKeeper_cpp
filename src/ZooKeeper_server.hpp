#ifndef ZOOKEEPER_SERVER_HPP
#define ZOOKEEPER_SERVER_HPP

#include <cstdio>         /* standard C i/o facilities */
#include <cstdlib>        /* needed for atoi() */
#include <cstring>        /* c-string Library */
#include <cstdint>
#include <cassert>

#include <unistd.h>       /* defines STDIN_FILENO, system calls,etc */
#include <sys/socket.h> 
#include <sys/types.h>
#include <arpa/inet.h> 
#include <netdb.h> 
#include <netinet/in.h>
#include <pthread.h>

#include <exception>
#include <fstream>
#include <iostream>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <regex>

#include "task.hpp"
#include "util.hpp"

#define STATUS_NULL      0
#define STATUS_REQUEST   1
#define STATUS_PROPOSAL  2
#define STATUS_COMMIT    3
#define STATUS_EXECUTION 4
#define CMD_APPEND       5
#define CMD_CREATE       6
#define CMD_DELETE       7

class ZooKeeper_server {

public:

  bool read_line(std::fstream &fs, std::string &str) {

    bool state = true;

    // skip empty line(len == 0) and line begin with '#'
    while((state = std::getline(fs, str))) {

      if (str.length() == 0) continue;
      if (str.length() > 0 && str[0] == '#') continue;
      break;
    }
    return state;
  }

  bool parse_config(char* filename) { 

    std::fstream fs;
    fs.open(filename, std::fstream::in);
    if (!fs.is_open()) return false;

    std::string line;
    size_t pos;
    
    try {

      read_line(fs, line);
      pos = line.find("server_port=");
      if (pos != 0)
        throw std::invalid_argument("configuration file: corrupt format.");
      server_port = stoi(line.substr(12));
      
      read_line(fs, line);
      pos = line.find("client_port=");
      if (pos != 0)
        throw std::invalid_argument("configuration file: corrupt format.");
      client_port = stoi(line.substr(12));

      while (read_line(fs, line)) {

        if (line.length() < 2) break; 
        pos = line.find(" ");
        int32_t zxid = stoi(line.substr(0, pos));
        std::string IP_addr = line.substr(++pos);

        all_other_nodes.insert( { zxid, move(IP_addr) } );
      }
      
    } catch (std::exception &e) {

      std::cerr << e.what() << std::endl;
      fs.close();
      return false;
    }

    printf("%d %d\n", server_port, client_port);
    
    fs.close();
    return true;
  }

  bool set_role(char* role) {

    if (strcmp(role, "LEADER") == 0) leader_fd = -1;
    else if (strcmp(role, "FOLLOWER") == 0 || strcmp(role, "RECOVER") == 0)
      leader_fd = 0;
    else return false;

    return true;
  }

  bool set_zxid(char* id) {

    try {

      _zxid = std::stoi(id);
      if (all_other_nodes.find(_zxid) == all_other_nodes.end())
        throw std::logic_error("zxid input is not registered.");
      all_other_nodes.erase(_zxid);
      
    } catch (std::exception &e) {

      std::cerr << e.what() << std::endl;
      return false;
    }
    
    return true;
  }

  pthread_t init_follower() {

    // find who is leader
    auto itr = all_other_nodes.begin();
    
    // N.B. when find_leader return true, connection to leader node
    // has been set up and file descriptor number is assigned to leader_fd
    for(; itr != all_other_nodes.end(); itr++)
      if (find_leader(itr->second)) break;

    if (itr == all_other_nodes.end()) {
      fprintf(stderr, "Leader not found.\n");
      exit(-1);
    }

    pthread_t tid;
    if ( pthread_create(&tid, NULL,
                        ZooKeeper_server::pthread_leader_connector,
                        this) < 0 ) {

      fprintf(stderr, "Error: init_follower() -> pthread_create()\n");
      exit(-1);
    }
    
    return tid;
  }

  static void* pthread_leader_connector(void* arg) {
    
    ZooKeeper_server *_this = static_cast<ZooKeeper_server *>(arg);
    _this->leader_connector();
    return nullptr;
  }

  void leader_connector() {

    char recv_buf[buf_size];
    int32_t nbytes;

    // send leader the identity, i.e. zxid
    std::string zxid_str = std::to_string(_zxid);
    nbytes = write(leader_fd, zxid_str.c_str(), zxid_str.length());

    int32_t zxid_len = zxid_str.length();
    assert(nbytes == zxid_len);

    // if zxid is recognized, "ACK\n" should be received
    nbytes = read(leader_fd, recv_buf, buf_size);
    assert( strncmp(recv_buf, ack.c_str(), ack.length()) == 0 );

    while (1) {

      nbytes = read(leader_fd, recv_buf, buf_size-1);
      
      if (nbytes < 0) { 
        fprintf(stderr, "Error - read(): ");
        perror(0);
        continue;
      }

      // nbytes == 0 leader lost connection
      if (nbytes == 0) {
        printf("Lost connection with leader.\n");
        break;
      }

      recv_buf[nbytes] = '\0';
      /*
      if ( !strncmp(recv_buf, "SYNC", 4) ) {

        pthread_mutex_lock(&fs_lock);
        file_system.clear();
        pthread_mutex_unlock(&fs_lock);
        continue;
        }*/

      printf("Leader's msg: %s\n", recv_buf);

      std::string recv_cmd = recv_buf;

      if (std::regex_match(recv_cmd, verify_proposal)) {

        recv_cmd.erase(recv_cmd.begin(), recv_cmd.begin()+10);
        recv_cmd.erase(recv_cmd.end()-11, recv_cmd.end());

        // compose commit msg(agree)
        std::string commit;
        commit.reserve(18 + recv_cmd.length());
        commit = "<commit>";
        commit += recv_cmd;
        commit += "</commit>";
        
        // find the job in queue change status
        // or add this job into work queue w/ STATUS_PROPOSAL
        pthread_mutex_lock(&wq_lock);

        auto itr = working_queue.begin();
        for(;itr != working_queue.end(); itr++){

          if( itr->cmd == recv_cmd ) {
            itr->status = STATUS_PROPOSAL;
            break;
          }
        }

        if (itr == working_queue.end()) {

          task a_task(STATUS_PROPOSAL, -1, -1, std::move(recv_cmd));
          working_queue.push_back(std::move(a_task));
        }

        pthread_mutex_unlock(&wq_lock);

        // send the commit msg back to leader
        nbytes = write(leader_fd, commit.c_str(), commit.length());

      } else if (std::regex_match(recv_cmd, verify_execution)) {

        recv_cmd.erase(recv_cmd.begin(), recv_cmd.begin()+11);
        recv_cmd.erase(recv_cmd.end()-12, recv_cmd.end());

        // find the job in queue
        int32_t fd = -1;
        pthread_mutex_lock(&wq_lock);

        auto itr = working_queue.begin();
        for(; itr != working_queue.end(); itr++){

          if( itr->cmd == recv_cmd ) {
            fd = itr->fd;
            working_queue.erase(itr);      // remove the job
            break;
          }
        }
        local_exec_cmd(recv_cmd, fd);      // execute locally

        pthread_mutex_unlock(&wq_lock);

        write(leader_fd, ack.c_str(), ack.length());

      } else if (std::regex_match(recv_cmd, verify_quorum)) {

        recv_cmd.erase(recv_cmd.begin(), recv_cmd.begin()+8);
        recv_cmd.erase(recv_cmd.end()-9, recv_cmd.end());

        // find the job in queue
        int32_t fd = -1;
        pthread_mutex_lock(&wq_lock);

        auto itr = working_queue.begin();
        for(; itr != working_queue.end(); itr++){

          if( itr->cmd == recv_cmd ) {
            fd = itr->fd;
            working_queue.erase(itr);
            write(fd, quorum_not_met.c_str(), quorum_not_met.length());
            break;
          }
        }
        pthread_mutex_unlock(&wq_lock);
        write(leader_fd, ack.c_str(), ack.length());
      }
    }
  }

  bool find_leader(const std::string &IP_addr) {

    /* socket: create the socket */
    int32_t sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {

      perror("Error - socket(): ");
      return false;
    }

    leader_fd = sock_fd;

    // get information need for connect() from IP_addr
    struct hostent *server_info = gethostbyname(IP_addr.c_str());
    if (server_info == nullptr) {

      fprintf(stderr, "Cannot find host: %s\n", IP_addr.c_str());
      close(leader_fd);
      return false;
    }

    /* build the server's Internet address */
    struct sockaddr_in server_addr;
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    bcopy((char *)server_info->h_addr,
          (char *)&server_addr.sin_addr.s_addr,
          server_info->h_length);
    server_addr.sin_port = htons(server_port);

    /* connect: create a connection with the server */
    if (connect(leader_fd, (struct sockaddr*)&server_addr,
                sizeof(server_addr)) < 0) {
      
      fprintf(stderr,"Error - connect(): ");
      perror(0);
      
      close(leader_fd);
      return false;
    }

    return true;
  }

  pthread_t init_leader() {

    std::pair<int32_t*, ZooKeeper_server*> *pr =
      new std::pair<int32_t*, ZooKeeper_server*>(&server_port, this);

    pthread_t tid;
    if ( pthread_create(&tid, NULL,
                        ZooKeeper_server::pthread_init_tcp_server,
                        pr) < 0 ) {

      fprintf(stderr, "Error: init_leader() -> pthread_create()\n");
      exit(-1);
    }
    return tid;
  }

  void init_client_listener() {

    std::pair<int32_t*, ZooKeeper_server*> *pr =
      new std::pair<int32_t*, ZooKeeper_server*>(&client_port, this);

    pthread_t tid;
    if ( pthread_create(&tid, NULL,
                        ZooKeeper_server::pthread_init_tcp_server,
                        pr) < 0 ) {

      fprintf(stderr, "Error: init_client_handler() -> pthread_create()\n");
      exit(-1);
    }
  }

  static void *pthread_init_tcp_server(void *arg) {

    std::pair<int32_t*, ZooKeeper_server*> *pr =
      static_cast<std::pair<int32_t*, ZooKeeper_server*>*>(arg);

    int32_t *port = pr->first;
    ZooKeeper_server *_this = static_cast<ZooKeeper_server*>(pr->second);
    
    delete pr;
    
    _this->init_tcp_server(*port);
    
    return nullptr;
  }

  bool init_tcp_server(int32_t port) {

    if (port == server_port)
      printf("Initiating tcp server for followers...\n");
    else if (port == client_port)
      printf("Initiating tcp server for clients...\n");
    else
      return false;

    // Create socket
    int32_t server_fd = socket(AF_INET , SOCK_STREAM , 0);
    if (server_fd == -1) {

      fprintf(stderr, "Error -  socket(): ");
      perror(0);
      exit(-1);
    }
    printf("Socket created. Server fd: %d\n", server_fd);

    struct sockaddr_in server;
    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons( port );
     
    // Bind
    if( bind(server_fd,(struct sockaddr *)&server , sizeof(server)) < 0 ) {

      fprintf(stderr, "Error - bind(): ");
      perror(0);
      exit(-1);
    }

    // Listen
    listen(server_fd , max_listen);

    printf("Listening on port %d, ", port);
    if (port == server_port)
      printf("waiting for incoming connections from followers.\n");
    else if (port == client_port)
      printf("waiting for incoming connections from clients.\n");      
    
    int32_t len = sizeof(struct sockaddr_in);

    while(1) {
      
      int32_t* sock = (int32_t*)malloc(sizeof(int));
      struct sockaddr_in client;
      
      *sock = accept(server_fd, (struct sockaddr*) &client,
                     (socklen_t*)&len);

      if (*sock < 0) {
        
        fprintf(stderr, "Error: accept()");
        perror(NULL);
        
        free(sock);
        continue;
      }

      if (port == server_port) {

        printf("Connection accepted. New follower has fd: %d\n", *sock);
        std::string follower_IP = inet_ntoa(client.sin_addr);
        printf("IP address: %s\n", follower_IP.c_str());

        if (!find_IP(follower_IP)) {

          printf("Error: follower's IP address is not identified.\n");
          printf("Closing connection...\n");
          close(*sock);
          free(sock);
          continue;
        }
        
        //sync_fs(*client_sock, follower_zxid); // TODO

        pthread_t ftid;
        
        std::pair<int32_t*, ZooKeeper_server*> *pr =
          new std::pair<int32_t*, ZooKeeper_server*>(sock, this);
        
        if( pthread_create( &ftid, NULL,
                            pthread_follower_handler, pr) < 0) {

          fprintf(stderr, "Error: pthread_create()\n");
          
          free(sock);
          delete pr;
          
          exit(-1);
        }
      
      } else if (port == client_port) {
        
        printf("Connection accepted. New client has fd: %d\n", *sock);

        pthread_mutex_lock(&cfd_lock);
        client_fds.insert(*sock);
        pthread_mutex_unlock(&cfd_lock);

        pthread_t ctid;

        std::pair<int32_t*, ZooKeeper_server*> *pr =
          new std::pair<int32_t*, ZooKeeper_server*>(sock, this);
        
        if( pthread_create( &ctid, NULL,
                            pthread_client_handler, pr) < 0) {

          fprintf(stderr, "Error: pthread_create()\n");
          
          free(sock);
          delete pr;
          
          exit(-1);
        }
      }
    } // end of while(1)
  }

  static void *pthread_follower_handler(void* info_pair) {

    std::pair<int32_t*, ZooKeeper_server*> *pr =
      static_cast<std::pair<int32_t*, ZooKeeper_server*>*>(info_pair);

    int32_t sock = *(pr->first);
    ZooKeeper_server *_this = static_cast<ZooKeeper_server*>(pr->second);
    
    free(pr->first);
    delete pr;
    
    _this->follower_handler(sock);
    
    return nullptr;
  }

  void follower_handler(int32_t fd) {

    // this function should only be called by leader
    assert(leader_fd == -1);

    pthread_detach( pthread_self() );

    char recv_buf[buf_size];
    int32_t nbytes, zxid;

    // we need to get the zxid of incoming follower at the very beginning
    // zxid should be int32_t
    try {

      nbytes = read(fd, recv_buf, buf_size-1);
      recv_buf[nbytes] = '\0';

      if (nbytes < 1 || nbytes > 10) throw std::logic_error("read()");
      
      zxid = std::stoi(recv_buf);

      if (all_other_nodes.find(zxid) == all_other_nodes.end())
        throw std::logic_error("zxid is not registered.");

      pthread_mutex_lock( &aan_lock );
      all_active_nodes.insert( { fd, zxid } );
      pthread_mutex_unlock( &aan_lock );

      // send 'ACK' back to client
      write(fd, ack.c_str(), ack.length());
      
    } catch (std::exception &e) {

      std::cerr << e.what() << std::endl;
      close(fd);
      return;
    }
    
    while(1) {

      nbytes = read(fd, recv_buf, buf_size-1);

      if (nbytes < 0) { 

        fprintf(stderr, "Error - read(): ");
        perror(0);

      // connection is closed
      } else if (nbytes == 0) {

        // remove from active node
        pthread_mutex_lock(&aan_lock);
        zxid = (all_active_nodes.find(fd))->second;
        all_active_nodes.erase(fd);
        pthread_mutex_unlock(&aan_lock);
       
        printf("Follower w/ zxid %d closed connection.\n", zxid);
        printf("Removed follower w/ zxid %d from list of active nodes.\n",
              zxid);

        break;

      } else {

        recv_buf[nbytes] = '\0';

        // TODO: for now ignore ACK sent from followers
        if ( strncmp(recv_buf, ack.c_str(), ack.length()) == 0)
          continue;

        printf("msg from follower: %s\n", recv_buf);
   
        std::string recv_cmd = recv_buf;
      
        // either request or commit
        if (std::regex_match(recv_cmd, verify_request)) {

          recv_cmd.erase(recv_cmd.begin(), recv_cmd.begin()+9);
          recv_cmd.erase(recv_cmd.end()-10, recv_cmd.end());

          // check if quorum is reached
          pthread_mutex_lock(&aan_lock);
          size_t aan_size = all_active_nodes.size()+1;
          pthread_mutex_unlock(&aan_lock);

          if (aan_size <= (all_other_nodes.size()+1)/2 ) {

            printf("Quorum not met. Operation abort.\n");
            std::string qe;
            qe.reserve(18 + recv_cmd.size());
            qe = "<quorum>";
            qe += recv_cmd;
            qe += "</quorum>";
        
            write(fd, qe.c_str(), qe.length());
            continue;
          } 

          // create and push task to working_queue
          // ASSUME: leader always vote agree
          task a_task(STATUS_PROPOSAL, -1, 1, std::move(recv_cmd));

          // we still need a copy of the msg for broadcast
          recv_cmd = a_task.cmd;
          
          pthread_mutex_lock(&wq_lock);
          working_queue.push_back(std::move(a_task));
          pthread_mutex_unlock(&wq_lock);

          printf("Sending proposal...\n");
          int num_send = broadcast_cmd(recv_cmd, STATUS_PROPOSAL);
          printf("Has broadcasted the proposal to %d followers.\n",
                 num_send);

        } else if (std::regex_match(recv_cmd, verify_commit)) {

          recv_cmd.erase(recv_cmd.begin(), recv_cmd.begin()+8);
          recv_cmd.erase(recv_cmd.end()-9, recv_cmd.end());

          std::cout << recv_cmd << std::endl;

          // find the job in queue and increase vote count
          pthread_mutex_lock(&wq_lock);
          auto itr = working_queue.begin();
          for(; itr != working_queue.end(); itr++){

            if( itr->cmd == recv_cmd ) {
              
              itr->vote_count++;
              
              // reach quorum
              if ( itr->vote_count > (all_other_nodes.size()+1)/2) {

                printf("Quorum met for executing command: %s\n",
                       recv_cmd.c_str());
                int32_t fd = itr->fd;
                working_queue.erase(itr);
                broadcast_cmd(recv_cmd, STATUS_EXECUTION);
                local_exec_cmd(recv_cmd, fd);
              }
              break;
            }
          }
          pthread_mutex_unlock(&wq_lock);
        }
      }
    }
  }  
  
  static void *pthread_client_handler(void* info_pair) {

    std::pair<int32_t*, ZooKeeper_server*> *pr =
      static_cast<std::pair<int32_t*, ZooKeeper_server*>*>(info_pair);

    int32_t sock = *(pr->first);
    ZooKeeper_server *_this = static_cast<ZooKeeper_server*>(pr->second);

    free(pr->first);
    delete pr;
    
    _this->client_handler(sock);
    return nullptr;
  }

  void client_handler(int32_t fd) {

    char recv_buf[buf_size];
    int nbytes;
    
    while(1) {

      nbytes = read(fd, recv_buf, buf_size-1);

      if (nbytes < 0) { 

        fprintf(stderr, "Error - read(): ");
        perror(0);

      } else if (nbytes == 0) {

        printf("Client closed connection.\n");

        pthread_mutex_lock(&cfd_lock);
        client_fds.erase(fd);
        pthread_mutex_unlock(&cfd_lock);
        close(fd);

        break;
        
      } else {
        
        // remove ending \n \r if any
        recv_buf[nbytes--] = '\0';
        for(; nbytes >= 0; nbytes--) {

          if (recv_buf[nbytes] == '\n' || recv_buf[nbytes] == '\r' )
            recv_buf[nbytes] = '\0';
          else break;
        } 
     
        printf("msg from client: %s\n", recv_buf);

        std::string cmd = recv_buf;
        if (std::regex_match(cmd, verify_create)) {

          exec_cmd(cmd, fd, CMD_CREATE);

        } else if (std::regex_match(cmd, verify_read)) {

          read_cmd(cmd, fd);    

        } else if (std::regex_match(cmd, verify_append)) {

          exec_cmd(cmd, fd, CMD_APPEND);

        } else if (std::regex_match(cmd, verify_delete)) {

          exec_cmd(cmd, fd, CMD_DELETE);

        } else {

          write(fd, invalid_input.c_str(), invalid_input.length());
        }
      }
    }
    pthread_detach( pthread_self() );
  }

  int32_t broadcast_cmd(const std::string& cmd, unsigned char type) {

    // invalid type of command
    assert(type == STATUS_PROPOSAL || type == STATUS_EXECUTION); 
 
    std::string str;

    if (type == STATUS_PROPOSAL) {

      str.reserve(22 + cmd.length());
      str += "<proposal>";
      str += cmd;
      str += "</proposal>";
      
    } else {

      str.reserve(24 + cmd.length());
      str += "<execution>";
      str += cmd;
      str += "</execution>";
    }
    
    int32_t success_count = 0, nbytes;

    pthread_mutex_lock(&aan_lock);

    auto itr = all_active_nodes.begin();
    for(;itr != all_active_nodes.end(); itr++) {

      nbytes = write(itr->first, str.c_str(), str.length());
      int32_t slen = str.length();
      if (nbytes == slen) success_count++;
    }

    pthread_mutex_unlock(&aan_lock);
    return success_count;
  }

  void read_cmd(const std::string& cmd, int32_t fd) {

    std::string filename = cmd.c_str()+5;
    printf("Reading file <%s>\n", filename.c_str());

    pthread_mutex_lock(&fs_lock);

    auto fs_itr = file_system.find(filename);

    if (fs_itr == file_system.end())
      write(fd, file_not_found.c_str(), file_not_found.length());
    else
      write(fd, fs_itr->second.c_str(), fs_itr->second.length());

    pthread_mutex_unlock(&fs_lock);
  }

  void local_exec_cmd(const std::string& cmd, int32_t fd) {

    std::string filename;
    std::string content;
    char cmd_cstr[buf_size];
    strcpy(cmd_cstr, cmd.c_str());

    if (std::regex_match(cmd, verify_create)) {

      filename = cmd.c_str()+7;
      pthread_mutex_lock(&fs_lock);
      file_system.insert( { filename, content } );
      pthread_mutex_unlock(&fs_lock);

    } else if (std::regex_match(cmd, verify_append)) {

      int end_pos = 7;
      while(cmd_cstr[end_pos]!= ' ') end_pos++;

      filename = std::string(cmd_cstr+7, end_pos-7);
      content = cmd_cstr+end_pos+1;
      printf("appending '%s' to file %s\n",
             content.c_str(), filename.c_str());
      content += '\n';

      pthread_mutex_lock(&fs_lock);

      auto itr = file_system.find(filename);
      if(itr != file_system.end())
        itr->second += content;
      
      pthread_mutex_unlock(&fs_lock);

    } else if (std::regex_match(cmd, verify_delete)) {

      filename = cmd.c_str()+7;

      pthread_mutex_lock(&fs_lock);
      file_system.erase(filename);
      pthread_mutex_unlock(&fs_lock);
    }

    // tell client the result - ACK = success
    if (fd != -1) write(fd, ack.c_str(), ack.length());

    /* TODO
    // write record to log
    pthread_mutex_lock(&log_lock);
    fputs(cmd.c_str(), system_log);
    fputs("\n", system_log);
    pthread_mutex_unlock(&log_lock);
    */
  }

  void exec_cmd(std::string& cmd,
                    int32_t fd, unsigned char type) {

    std::string filename;

    if (type == CMD_CREATE) {

      filename = cmd.c_str()+7;
      printf("Creating file <%s>\n", filename.c_str());
    }

    if (type == CMD_APPEND) {

      int end_pos = 7;
      char cmd_cstr[buf_size];
      strcpy(cmd_cstr, cmd.c_str());
      while(cmd_cstr[end_pos]!= ' ') end_pos++;

      filename = std::string(cmd_cstr+7, end_pos-7);
      std::string content = cmd_cstr+end_pos+1;
      printf("Appending '%s' to file %s\n",
             content.c_str(), filename.c_str());
    }

    if (type == CMD_DELETE) {

      filename = cmd.c_str()+7;
      printf("Deleting file <%s>\n", filename.c_str());
    }

    // check file exist or not
    pthread_mutex_lock(&fs_lock);
    auto itr = file_system.find(filename);
    pthread_mutex_unlock(&fs_lock);

    // error handling1
    if (itr == file_system.end()) {
      if (type == CMD_APPEND || type == CMD_DELETE) {
        write(fd, file_not_found.c_str(), file_not_found.length());
        return;
      }
    }

    // error handling2
    if (itr != file_system.end() && type == CMD_CREATE) {
      write(fd, file_already_exist.c_str(), file_already_exist.length());
      return;
    }

    // set STATUS_ and vote_count later
    task a_task(STATUS_NULL, fd, -1, move(cmd));

    // if this function called by leader node
    if (leader_fd == -1) { 

      // check if quorum is reached
      pthread_mutex_lock(&aan_lock);
      uint32_t aan_size = all_active_nodes.size();
      pthread_mutex_unlock(&aan_lock);

      if (aan_size < (all_other_nodes.size()+1)/2 ) {
        write(fd, quorum_not_met.c_str(), quorum_not_met.length());
        return;
      }

      // a corner case: the ZooKeeper system is consists of
      // 1 leader and 0 follower
      // execute directly and return (no need working queue)
      if (all_other_nodes.size() == 0) {
        local_exec_cmd(a_task.cmd, fd);
        return;
      }

      printf("Sending proposal...\n");
      int num_send = broadcast_cmd(a_task.cmd, STATUS_PROPOSAL);
      printf("Has broadcasted the proposal to %d followers.\n", num_send);

      a_task.status = STATUS_PROPOSAL;
      a_task.vote_count = 1;  // leader always agree its own proposal

    // or this function called by follower node
    } else {

      printf("Sending request to leader...\n");
      if (!send_request(a_task.cmd)) return;
      a_task.status = STATUS_REQUEST;
    }

    // push this job into working queue
    pthread_mutex_lock(&wq_lock);
    working_queue.push_back(std::move(a_task));
    pthread_mutex_unlock(&wq_lock);
  }

  bool send_request(const std::string &cmd) {

    std::string request;
    request.reserve(20 + cmd.length());
    request = "<request>";
    request += cmd;
    request += "</request>";

    // this function should only be used for
    // follower sending request to leader
    assert(leader_fd != -1);
    
    int nbytes = write(leader_fd, request.c_str(), request.length());
    int32_t rlen = request.length();
    if (nbytes == rlen) return true;
    else return false;
  }
  
  int32_t find_zxid_by_IP(const std::string& ip) const {

    auto itr = all_other_nodes.begin();
    for(; itr != all_other_nodes.end(); itr++)
      if (itr->second == ip) return itr->first;

    return -1;
  }

  // find if the IP address from follower is valid
  // (registered in config.txt)
  bool find_IP(const std::string & ip) const {

    auto itr = all_other_nodes.begin();
    for (; itr != all_other_nodes.end(); itr++)
      if (itr->second == ip) return true;
    return false;
  }
  
public:  // information for tcp connection
  
  int32_t server_port;
  int32_t client_port;
  const int32_t max_listen = 1024;
  const int32_t buf_size = 8192;

  /* file descriptor number of leader node
     if this node is leader, then leader_fd is set to be -1;
  */
  int32_t leader_fd;
  int32_t _zxid;

  // regular expression for verifying users' incoming commands
  std::regex verify_create {"^create [^ ]+$"};
  std::regex verify_delete {"^delete [^ ]+$"};
  std::regex verify_read {"^read [^ ]+$"};
  std::regex verify_append {"^append [^ ]+ (.+)$"};
  
  // regular expression for verifying internal commands
  std::regex verify_request {"^<request>(.+)</request>$"};
  std::regex verify_proposal {"^<proposal>(.+)</proposal>$"};
  std::regex verify_commit {"^<commit>(.+)</commit>$"};
  std::regex verify_execution {"^<execution>(.+)</execution>$"};
  std::regex verify_quorum {"^<quorum>(.+)</quorum>$"};

  // error msgs;
  std::string invalid_input {"Error: invalid command.\n"};
  std::string file_not_found {"Error: file not found.\n"};
  std::string file_already_exist {"Error: file already exist.\n"};
  std::string quorum_not_met {"Error: system quorum not met.\n"};
  std::string ack {"ACK\n"};

  /* local file system */
  std::unordered_map<std::string, std::string> file_system;
  pthread_mutex_t fs_lock = PTHREAD_MUTEX_INITIALIZER;

  /* working queue */
  std::list<task> working_queue;
  pthread_mutex_t wq_lock = PTHREAD_MUTEX_INITIALIZER;
  
  /* key = zxid; val = IP address of all other nodes.
     Lock is not need since it will remain unchanged after 
     parsing the configuration txt file */
  std::unordered_map<int32_t, std::string> all_other_nodes;

  /* key = fd; val = zxid of all active nodes */
  std::unordered_map<int32_t, int32_t> all_active_nodes;
  pthread_mutex_t aan_lock = PTHREAD_MUTEX_INITIALIZER;

  /* clients fd */
  std::unordered_set<int32_t> client_fds;
  pthread_mutex_t cfd_lock = PTHREAD_MUTEX_INITIALIZER;

  /* log file */
  FILE *sys_log;
  pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
};

#endif


