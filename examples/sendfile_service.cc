#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>

// Signal for detecting SIGTERM
static bool kStop = false;

static void HandleTERM(int sig) {
  kStop = true;
}

int main(int argc, char *argv[]) {

  if (argc < 4) {
    printf("usage: %s <ip_address> -p <port> -f <filename>\n",
           basename(argv[0]));
    return 1;
  }

  // Register signal handler
  signal(SIGTERM, HandleTERM);

  // Parse *argv[]
  int opt;

  const char *ip = argv[1];
  int port;
  char *filename;

  while ((opt = getopt(argc, argv, "p:f:")) != -1) {
    switch (opt) {
      case 'p':
        port = atoi(optarg);
        //printf("p: %s (int: %d)\n", (char*)optarg, port);
        break;
      case 'f':
       filename = (char*)optarg;
       break;
      default:
        printf("usage: %s <ip_address> -p <port> -f <filename>\n",
              basename(argv[0]));
        exit(EXIT_FAILURE);
    }
  }
  printf("parsed\n ip: %s\n port: %d\n filename: %s\n", ip, port, filename);

  // Before creating socket to transfer
  // verify and open the file first
  int filefd = open(filename, O_RDONLY);
  struct stat filestat;
  if (filefd == -1) {
    printf("[FAILED] file errno is %d\n", errno);
    exit(EXIT_FAILURE);
  } 
  if (fstat(filefd, &filestat) == -1) {
    close(filefd);
    printf("[FAILED] file errno is %d\n", errno);
    exit(EXIT_FAILURE);
  }

  // Create new sockaddr
  struct sockaddr_in my_sockaddr;
  my_sockaddr.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &my_sockaddr.sin_addr);
  my_sockaddr.sin_port = htons(port);

  // Create new socket
  int my_sockfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(my_sockfd >= 0);

  // Bind & Listen
  int ret;
  ret = bind(my_sockfd, (struct sockaddr *)&my_sockaddr, sizeof(my_sockaddr));
  assert(ret != -1);
  ret = listen(my_sockfd, 5);
  assert(ret != -1);

  // Accept & Send File
  struct sockaddr_in peer_sockaddr;
  socklen_t peer_sockaddr_len = sizeof(peer_sockaddr);
  int peer_sockfd = accept(my_sockfd, 
                           (struct sockaddr *)&peer_sockaddr,
                           &peer_sockaddr_len);
  if (peer_sockfd < 0) {
    printf("[FAILED] socket errno is %d\n", errno);
  } else {

    if (sendfile(peer_sockfd, filefd, NULL, filestat.st_size) == -1) {
      printf("[FAILED] sendfile errno is %d\n", errno);
    } else {
      char peer_ip[INET_ADDRSTRLEN];
      printf("[Sent] file: %s to %s\n", 
             filename, 
             inet_ntop(AF_INET, &peer_sockaddr.sin_addr, peer_ip, INET_ADDRSTRLEN));
      close(filefd);
    }

    close(peer_sockfd);
  }

  close(my_sockfd);

  return 0;

}
