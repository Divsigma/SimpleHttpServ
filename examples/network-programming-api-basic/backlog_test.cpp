#include <cstdio>
#include <libgen.h>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Signal for detecting SIGTERM
static bool kStop = false;

static void HandleTERM(int sig) {
  kStop = true;
}

int main(int argc, char *argv[]) {

  if (argc < 4) {
    printf( "usage: %s <ip_address> -p <port> -b <backlog>\n",
            basename(argv[0]) );
    return 1;
  }

  // Register signal handler
  signal(SIGTERM, HandleTERM);

  // Parse *argv[]
  int opt;
  const char *ip = argv[1];
  int port, backlog;
  while ((opt = getopt(argc, argv, "p:b:")) != -1) {
    switch (opt) {
      case 'p':
        port = atoi(optarg);
        //printf("p: %s (int: %d)\n", (char*)optarg, port);
        break;
      case 'b':
        backlog = atoi(optarg);
        //printf("b: %s (int: %d)\n", (char*)optarg, backlog);
        break;
      default:
        printf( "usage: %s <ip_address> -p <port> -b <backlog>\n",
                basename(argv[0]) );
        exit(EXIT_FAILURE);
    }
  }
  printf("parsed\n ip: %s\n port: %d\n backlog: %d\n", ip, port, backlog);

  // Start new socket and listen (with backlog)
  int sock = socket( AF_INET, SOCK_STREAM, 0);
  assert( sock >= 0 );

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  
  inet_pton(AF_INET, ip, &addr.sin_addr);  // If not set, socket listen on 0.0.0.0:port

  addr.sin_port = htons(port);             // The actual listened port is in the form of
                                           // network short
                                           // e.g. 12345(host) == 14640(network short)

  int ret;
  ret = bind(sock, (struct sockaddr *)&addr, sizeof(sockaddr_in));
  assert(ret != -1);

  ret = listen(sock, backlog);
  assert(ret != -1);

  while (!kStop) {
    sleep(30);  // bigger for test: accepting ESTABLISHED but stopped client 
                //                  (execute `telnet <ip> <port> &`)
                //                  or CLOSE_WAIT
                //                  (execute `telnet <ip> <port>` then close)
    struct sockaddr_in client;
    socklen_t client_addrlen = sizeof(sockaddr_in);
    int client_sock = accept(sock, (struct sockaddr *)&client, &client_addrlen);
    if (client_sock < 0) {
      printf("connection error\n");
    } else {
      char client_ip[INET_ADDRSTRLEN];
      printf("[connected] ip: %s port: %d\n", inet_ntop(AF_INET, &client.sin_addr, client_ip, INET_ADDRSTRLEN), 
                                              ntohs(client.sin_port));
      sleep(5);
      close(client_sock);
    }
  }

  close(sock);
  return 0;

}
