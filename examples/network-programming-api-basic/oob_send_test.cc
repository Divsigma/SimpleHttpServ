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

void SendAndPrint(int server_sockfd, const void *buf, size_t len, int flags) {

  int ret;

  ret = send(server_sockfd, buf, len, flags);
  if (ret == -1) {
    printf("[ERROR] errno is %d\n", errno);
  } else {
    printf("[Sent] %d bytes of data: %s\n", ret, (char *)buf);
  }
}

int main(int argc, char *argv[]) {

  if (argc < 3) {
    printf("usage: %s <server_ip_address> -p <service_port>\n",
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
        printf("usage: %s <server_ip_address> -p <service_port>\n",
               basename(argv[0]));
        exit(EXIT_FAILURE);
    }
  }
  printf("parsed\n ip: %s\n port: %d\n", ip, port);

  // Create new sockaddr
  struct sockaddr_in server_sockaddr;
  server_sockaddr.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &server_sockaddr.sin_addr);
  server_sockaddr.sin_port = htons(port);

  // Create new socket
  int server_sockfd = socket(PF_INET, SOCK_STREAM, 0);
  assert(server_sockfd >= 0);

  // Connect & Send OOB
  int ret = connect(server_sockfd, 
                    (struct sockaddr *)&server_sockaddr,
                    sizeof(server_sockaddr));
  if (ret == -1) {
    printf("[failed] errno is %d\n", errno);
  } else {
    char normal_data[]  = "1234";
    char oob_data[]     = "abc";
    SendAndPrint(server_sockfd, normal_data, strlen(normal_data),     0);
    SendAndPrint(server_sockfd, oob_data,    strlen(oob_data),  MSG_OOB);
    SendAndPrint(server_sockfd, normal_data, strlen(normal_data),     0);
  }

  close(server_sockfd);

  return 0;

}
