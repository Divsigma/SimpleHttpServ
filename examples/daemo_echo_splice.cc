#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Buffer size 
const size_t kBufferSize = PIPE_BUF;
// Signal for detecting SIGTERM
static bool kStop = false;

static void HandleTERM(int sig) {
  kStop = true;
}

// Daemonize current services
void Daemonize() {
  printf("Creating daemo ...\n");

  pid_t pid = fork();

  if (pid < 0) {
    printf("[FAILED] fork errno is %d\n", errno);
    exit(EXIT_FAILURE);
  } else if (pid > 0) {
    // For parent, exits successfully
    exit(EXIT_SUCCESS);
  } else {
    // For child, transform into daemon

    // Open a new session
    pid_t gid = setsid();
    if (gid < 0) {
      printf("[FAILED] setsid errno is %d\n", errno);
      exit(EXIT_FAILURE);
    }
    
    // Redirect stdio stream to /dev/null
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
    
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

  // Bind & Listen
  int ret;
  ret = bind(my_sockfd, (struct sockaddr *)&my_sockaddr, sizeof(my_sockaddr));
  assert(ret != -1);
  ret = listen(my_sockfd, 5);
  assert(ret != -1);

  // Accept peer socket
  struct sockaddr_in peer_sockaddr;
  socklen_t peer_sockaddr_len = sizeof(peer_sockaddr);
  int peer_sockfd = accept(my_sockfd, 
                           (struct sockaddr *)&peer_sockaddr,
                           &peer_sockaddr_len);
  if (peer_sockfd < 0) {
    printf("[FAILED] socket errno is %d\n", errno);
  } else {

    // Receive & Echo
    int pipefd[2];
    int ret;
    if (pipe(pipefd) == -1) {
      printf("[FAILED] pipe errno is %d\n", errno);
    } else {

      Daemonize();

      // Keep querying the pipe and peer_sockfd
      while (!kStop) {
        // Echo every 5 seconds
        sleep(5);
        // Splice socket data to pipe write end
        // (Pipe reads from peer socket)
        ret = splice(peer_sockfd, NULL, pipefd[1], NULL,
                     kBufferSize,
                     SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret != -1);
        // Splice pipe read end back to socket
        // (The peer socket reads from the same pipe)
        ret = splice(pipefd[0], NULL, peer_sockfd, NULL, 
                     kBufferSize,
                     SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret != -1);
      }

      // Close write first
      close(pipefd[1]);
      close(pipefd[0]);
     
    }

    close(peer_sockfd);
  }

  close(my_sockfd);

  return 0;

}
