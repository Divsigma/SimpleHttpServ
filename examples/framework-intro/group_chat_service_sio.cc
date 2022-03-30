#define _GNU_SOURCE 1

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <libgen.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <sys/uio.h>
#include <poll.h>
#include <fcntl.h>

// 0 for listened socket
// 1 for signal IO
static const int kStIdxClient = 2;
// maximum count of clients
static const int kMaxnClient = 2;
// max size of clients' receive buffer
static const int kBufSizeRecv = 32;
// max size of buckets map from 
// file descriptors to struct ClientData
static const int kMaxnFD = 65535;

struct ClientData {
  sockaddr_in client_sockaddr;
  char *buf_to_write;
  char recv_buf[kBufSizeRecv];
} kClient[kMaxnFD];

int kCntClient = 0;
struct pollfd kFDS[kStIdxClient + kMaxnClient];

int kSigPipeFD[2];

// To mark whether SIGTERM is received
bool kStop = false;

void PrintErrno(const char *tag) {
  printf("[FAILED] %s: errno is %d\n",
         tag, errno);
}
void PrintErrnoAndExist(const char *tag) {
  printf("[FAILED] %s: errno is %d\n",
         tag, errno);
  exit(EXIT_FAILURE);
}

void GenSigIO(int sig) {
  //printf("handing %d\n", sig);
  char msg = static_cast<char>(sig);
  send(kSigPipeFD[1], &msg, 1, 0);
  //printf("sig %d sent\n", sig);
}

void AddSignal(int sig) {
  printf("SIG = %d\n", sig);

  struct sigaction act;

  act.sa_flags = 0;
  //act.sa_flags |= (SA_RESTART | (~SA_SIGINFO));
  //act.sa_flags |= SA_RESTART;
  act.sa_handler = GenSigIO;
  sigfillset(&act.sa_mask);
  printf("%d %d\n", SA_RESTART, SA_SIGINFO);

  if (sigaction(sig, &act, nullptr)) {
    PrintErrnoAndExist("sigaction");
  }
  //signal(sig, HandleURG);
}

void PrintUsage(char *command) {
  printf("Usage: %s <ip> -p <port> [-f <filename>]\n",
         command);
}

int CreateSocketAndListen(char *ip, int port, int backlog) {

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

  ret = listen(my_sockfd, backlog);
  if (ret == -1)
    PrintErrnoAndExist("listen");

  return my_sockfd;

}

void ReplaceAndClosePollfd(struct pollfd *dst, struct pollfd *src) {
  close(dst->fd);
  kClient[dst->fd] = kClient[src->fd];
  *(dst) = *(src);
}

void HandlePollError(struct pollfd *pfd) {


}

void SetNonBlocking(int fd) {

  int old_options = fcntl(fd, F_GETFL);
  int new_options = old_options | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_options);

}

int main(int argc, char *argv[]) {

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


  // Create listen socket
  // Blocked listening & accepting ?
  int my_sockfd = CreateSocketAndListen(ip, port, kMaxnClient);

  // Create buffer for every client
  // struct ClientData is allocated staticlly

  // Create signal socketpair
  int pipe_ret = socketpair(AF_UNIX, SOCK_STREAM, 0, kSigPipeFD);
  if (pipe_ret == -1)
    PrintErrnoAndExist("socketpair");

  // Prepare pollfd for 
  // a) listened socket 
  // b) signal socketpair
  // c) client sockets
  kFDS[0].fd = my_sockfd;
  kFDS[0].events = POLLIN | POLLERR;
  kFDS[0].revents = 0;

  kFDS[1].fd = kSigPipeFD[0];
  kFDS[1].events = POLLIN | POLLERR;
  kFDS[1].revents = 0;

  for (int i = kStIdxClient; i < kStIdxClient + kMaxnClient; i++) {
    // ignore fds[i]
    kFDS[i].fd = -1;
    kFDS[i].events = 0;
  }

  // Add signal IO
  //SetNonBlocking(kSigPipeFD[1]);
  AddSignal(SIGURG);
  AddSignal(SIGTERM);


  // Keep poll() while waiting for SIGTERM
  while (!kStop) {

    // poll() BLOCKS until one of the following case:
    //   1) events occurs; 2) error occurs; 3) timeout
    // (See `man poll` for details)
    printf("[INFO] waiting for poll ...\n");
    //printf("kFDS[1].revents = %d\n", kFDS[1].revents);
    int poll_ret = poll(kFDS, kStIdxClient + kCntClient, -1);
    if (poll_ret < 0 && errno != EINTR) {
      PrintErrno("poll");
      break;
    }
    printf("[INFO] CHANGED FD N: %d, kCntClient=%d\n", poll_ret, kCntClient);

    // Phase 0: Check signal pipe
    //printf("kFDS[1].revents (poll) = %d\n", kFDS[1].revents);
    if (kFDS[1].revents & POLLIN) {
      char sigpipe_buf[1025];
      memset(sigpipe_buf, '\0', sizeof(sigpipe_buf));
      int ret = recv(kFDS[1].fd, sigpipe_buf, sizeof(sigpipe_buf), 0);
      if (ret > 0) {
        //printf("[SIGPIPE] recv: %s\n", sigpipe_buf);
        //kStop = true;
        for (int si = 0; si < ret; si++) {
          switch (sigpipe_buf[si]) {
            case SIGURG:
              printf("[SIGPIPE] recv SIGURG\n");
              break;
            case SIGTERM:
              printf("[SIGPIPE] recv SIGTERM\n");
              kStop = true;
              break;
          }
        }
      }
    }
    if (kFDS[1].revents & POLLERR) {
      printf("[ERROR] POLLERR in signal IO: in fd %d\n", kFDS[1].fd);
    }

    // Phase 1: Check listened socket
    if (kFDS[0].revents & POLLIN) {
      // Accept peer socket and handle
      struct sockaddr_in peer_sockaddr;
      socklen_t peer_sockaddr_len = sizeof(peer_sockaddr);
      int peer_sockfd = accept(my_sockfd,
                               (struct sockaddr *)&peer_sockaddr,
                               &peer_sockaddr_len);
      if (peer_sockfd == -1) {
        // accept failure
        PrintErrno("accept");
      } else if (kCntClient >= kMaxnClient) {
        // too many clients
        printf("[WARNING] Too many clients\n");
        close(peer_sockfd);
      } else {
        // create buffer for new client
        assert(peer_sockfd < kMaxnFD);
        kClient[peer_sockfd].client_sockaddr =  peer_sockaddr;
        kClient[peer_sockfd].buf_to_write = nullptr;

        // peer_sockfd should be set to non-blocking
        //SetNonBlocking(peer_sockfd);

        // create pullfd for new client
        kCntClient ++;
        kFDS[kStIdxClient + kCntClient - 1].fd = peer_sockfd;
        kFDS[kStIdxClient + kCntClient - 1].events = POLLIN | POLLRDHUP | POLLERR;
        kFDS[kStIdxClient + kCntClient - 1].revents = 0;
        // log info
        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer_sockaddr.sin_addr, peer_ip, INET_ADDRSTRLEN);
        printf("[INFO] new client: %s:%u\n",
               peer_ip, ntohs(peer_sockaddr.sin_port));
        printf("[INFO] %d clients currently in group chat!\n",
               kCntClient);
      }
    }

    // Phase 1: Check listened socket
    if (kFDS[0].revents & POLLERR) {
      printf("[ERROR] POLLERR in listened socket: in fd %d\n", kFDS[0].fd);
    }

    // Phase 2: Check all possible client sockets
    for (int fds_i = kStIdxClient; fds_i < kStIdxClient + kCntClient; fds_i++) {
      printf("[INFO] checking kFDS[%d]\n", fds_i);
      struct pollfd *client_pollfd = &kFDS[fds_i];
      // For listened socket test
      //close(client_pollfd->fd);

      // Writing to client is now possible
      if ((client_pollfd->revents) & POLLOUT) {
        printf("[INFO] got something to printf\n");
        continue;
      }
      // Error
      if ((client_pollfd->revents) & POLLERR) {
        printf("[ERROR] POLLERR: in fd %d\n", client_pollfd->fd);
        ReplaceAndClosePollfd(client_pollfd, &kFDS[kStIdxClient + kCntClient - 1]);
        kCntClient --;
        fds_i --;
        continue;
      }
      // Client closed connection
      if ((client_pollfd->revents) & POLLRDHUP) {
        printf("[INFO] client fd %d closed\n", client_pollfd->fd);
        ReplaceAndClosePollfd(client_pollfd, &kFDS[kStIdxClient + kCntClient - 1]);
        kCntClient --;
        fds_i --;
        continue;
      }
      // Reading from client is now possible
      if ((client_pollfd->revents) & POLLIN) {
        int peer_sockfd = client_pollfd->fd;
        struct ClientData *peer_data = &kClient[peer_sockfd];
        memset(peer_data->recv_buf, '\0', sizeof(peer_data->recv_buf));
        
        // Blocked reading ?
        int ret = recv(peer_sockfd,
                       peer_data->recv_buf,
                       kBufSizeRecv - 1, 0);
        if (ret < 0) {
          PrintErrno("recv");
        } else if (ret > 0) {
          // Blocked writing ?
          for (int j = kStIdxClient; j < kStIdxClient + kCntClient; j++) {
            if (kFDS[j].fd != peer_sockfd) {
              send(kFDS[j].fd,
                   peer_data->recv_buf,
                   sizeof(peer_data->recv_buf), 0);
              printf("[INFO] done sending ...\n");
            }
          }
        } else if (ret == 0){
          printf("[INFO] recv 0 bytes\n");
        }
        continue;
      }
    }

  }

  close(my_sockfd);

  return 0;

}
