#include "ZooKeeper_server.hpp"

int main(int argc, char** argv) {

  if (argc != 4) {

    fprintf(stderr, "Incorrent arguments.\n");
    printf("./ZooKeeper <config.txt> <LEADER | FOLLOWER | RECOVER> <Zxid of this machine(unsigned integer)>\n");
    return EXIT_FAILURE;
  }

  ZooKeeper_server server;

  if (!server.parse_config(argv[1])) {

    fprintf(stderr, "Failed to parse configuration txt.\n");
    return EXIT_FAILURE;
  }

  if (!server.set_role(argv[2])) {

    fprintf(stderr, "argv[2]: Invalid argument.\n");
    return EXIT_FAILURE;    
  }

  if (!server.set_zxid(argv[3])) {

    fprintf(stderr, "argv[3]: Fail to set zxid.\n");
    return EXIT_FAILURE;    
  }

  pthread_t tid;
  if (server.leader_fd == -1) tid = server.init_leader();
  else tid = server.init_follower();

  server.init_client_listener();
  
  // main thread is blocked here 
  // the block is broken only when current server (LEADER/FOLLOWER) Crashed 
  pthread_join(tid, NULL);

  // TODO crash recovery code here~

  return 0;
}
