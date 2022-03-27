#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <sys/uio.h>

// HTTP Header Status Line
enum {
  STATUS_OK = 0,
  STATUS_INTERNAL_ERROR,
};
static const char *kStatusLine[] = {
  [STATUS_OK]             = "200 OK",
  [STATUS_INTERNAL_ERROR] = "500 Internal server error"
};

// HTTP Header Buffer Size
static int kHeaderBufSize = 1024;
// HTTP Data Buffer Size
static int kDataBufSize = 1024;
// To mark whether SIGTERM is received
static bool kStop = false;

static void HandleTERM(int sig) {
  kStop = true;
}

void PrintErrnoAndExist(const char *tag) {
  printf("[FAILED] %s: errno is %d",
         tag, errno);
  exit(EXIT_FAILURE);
}

void PrintUsage(char *command) {
  printf("Usage: %s <ip> -p <port> [-f <filename>]\n",
         command);
}

int main(int argc, char *argv[]) {

  // Register SIGTERM handler
  signal(SIGTERM, HandleTERM);

  // Parse parameters
  if (argc < 3) {
    PrintUsage(basename(argv[0]));
    exit(EXIT_FAILURE);
  }

  int op;

  char *ip = argv[1];
  int port;
  char *filename = basename(argv[0]);
  while ((op = getopt(argc, argv, "p:f:")) != -1) {
    switch (op) {
      case 'p':
        port = atoi(optarg);
        break;
      case 'f':
        filename = (char *)optarg;
        break;
      default:
        PrintUsage(basename(argv[0]));
        exit(EXIT_FAILURE);
    }
  }

  printf("Parsed: \n ip %s\n port %d\n filename %s\n",
         ip, port, filename);

  // Create socket and listen
  struct sockaddr_in my_sockaddr;
  my_sockaddr.sin_family = AF_INET;
  inet_pton(AF_INET, ip, &my_sockaddr.sin_addr);
  my_sockaddr.sin_port = htons(port);

  int my_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (my_sockfd == -1)
    PrintErrnoAndExist("socket");

  int ret;
  ret = bind(my_sockfd, (struct sockaddr *)&my_sockaddr, sizeof(my_sockaddr));
  if (ret == -1)
    PrintErrnoAndExist("bind");

  ret = listen(my_sockfd, 5);
  if (ret == -1)
    PrintErrnoAndExist("listen");

  // Keep waiting for SIGTERM
  while (!kStop) {

    sleep(1);

    // Accept peer socket and handle
    struct sockaddr_in peer_sockaddr;
    socklen_t peer_sockaddr_len = sizeof(peer_sockaddr);  // accept() read addr data
                                                          // into sockaddr up to 
                                                          // this length
    int peer_sockfd = accept(my_sockfd,
                             (struct sockaddr *)&peer_sockaddr,
                             &peer_sockaddr_len);
    if (peer_sockfd == -1) {
      PrintErrnoAndExist("accept");
    } else {
      char peer_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &peer_sockaddr.sin_addr, peer_ip, INET_ADDRSTRLEN);
      printf("[Connected] peer socket %s:%u\n", peer_ip, ntohs(peer_sockaddr.sin_port));

      int headbuf_used_len = 0;
      int databuf_used_len = 0;
      char head_buf[kHeaderBufSize];
      char data_buf[kDataBufSize];

      // Add data
      ret = snprintf(data_buf, 
                     kDataBufSize,
                     "%s", "Hello from Tom HTTP Server\n");
      databuf_used_len += ret;
      
      // Add header
      // Add protocol version and status line
      ret = snprintf(head_buf, 
                     kHeaderBufSize, 
                     "%s %s\r\n", "HTTP/1.1", kStatusLine[STATUS_OK]);
      headbuf_used_len += ret;
      // Add Content-Length
      ret = snprintf(head_buf + headbuf_used_len,
                     kHeaderBufSize - headbuf_used_len,
                     "Content-Length: %d\r\n", databuf_used_len);
      headbuf_used_len += ret;
      // Add tail
      ret = snprintf(head_buf + headbuf_used_len, 
                     kHeaderBufSize - headbuf_used_len,
                     "%s", "\r\n");
      headbuf_used_len += ret;

      // Gather-write header and data to socket
      int iovcnt = 2;
      struct iovec iov[iovcnt];
      iov[0].iov_base = head_buf;
      iov[0].iov_len  = headbuf_used_len;
      iov[1].iov_base = data_buf;
      iov[1].iov_len  = databuf_used_len;
      
      ret = writev(peer_sockfd, iov, iovcnt);
      

      close(peer_sockfd);

    }

  }

  close(my_sockfd);

  return 0;

}
