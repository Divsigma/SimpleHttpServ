#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Buffer Size of Receive Buffer
const size_t kBufferSize = 1023;
// Signal for detecting SIGTERM
static bool kStop = false;

static void HandleTERM(int sig) {
  kStop = true;
}

void CleanReceiveAndPrint(int peer_sockfd, void *buf, size_t len, int flags) {

  int ret;

  memset(buf, 0, sizeof(buf));
  ret = recv(peer_sockfd, buf, len, flags);
  if (ret == -1) {
    printf("[ERROR] errno: %d\n", errno);
  } else {
    printf("[Got] %d bytes of data: %s\n", ret, (char *)buf);
  }
}

int main(int argc, char *argv[]) {

  if (argc < 3) {
    printf("usage: %s <ip_address> -p <port>\n",
           basename(argv[0]));
    return 1;
  }

  // Register signal handler
  signal(SIGTERM, HandleTERM);

  // Parse *argv[]
  int opt;
  const char *ip = argv[1];
  int port, backlog;
  while ((opt = getopt(argc, argv, "p:")) != -1) {
    switch (opt) {
      case 'p':
        port = atoi(optarg);
        //printf("p: %s (int: %d)\n", (char*)optarg, port);
        break;
      default:
        printf("usage: %s <ip_address> -p <port>\n",
               basename(argv[0]));
        exit(EXIT_FAILURE);
    }
  }
  printf("parsed\n ip: %s\n port: %d\n", ip, port);

  // Create new sockaddr
  struct sockaddr_in my_sockaddr;
  my_sockaddr.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &my_sockaddr.sin_addr);
  my_sockaddr.sin_port = htons(port);

  // Create new socket
  int my_sockfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(my_sockfd >= 0);
  //int flag_oob_inline = 0;
  //setsockopt(my_sockfd, SOL_SOCKET, SO_OOBINLINE, &flag_oob_inline, sizeof(flag_oob_inline));

  // Bind & Listen
  int ret;
  ret = bind(my_sockfd, (struct sockaddr *)&my_sockaddr, sizeof(my_sockaddr));
  assert(ret != -1);
  ret = listen(my_sockfd, 5);
  assert(ret != -1);

  // Accept & Receive OOB
  struct sockaddr_in peer_sockaddr;
  socklen_t peer_sockaddr_len = sizeof(peer_sockaddr);
  int peer_sockfd = accept(my_sockfd, 
                           (struct sockaddr *)&peer_sockaddr,
                           &peer_sockaddr_len);
  if (peer_sockfd < 0) {
    printf("[failed] errno is %d\n", errno);
  } else {
    char recv_buf[kBufferSize + 1];

    CleanReceiveAndPrint(peer_sockfd, recv_buf, kBufferSize,        0);
    CleanReceiveAndPrint(peer_sockfd, recv_buf, kBufferSize,  MSG_OOB);
    CleanReceiveAndPrint(peer_sockfd, recv_buf, kBufferSize,        0);

    close(peer_sockfd);
  }

  close(my_sockfd);

  return 0;

}
