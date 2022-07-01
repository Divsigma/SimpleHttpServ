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
static const int kHeaderBufSize = 1024;
// HTTP Data Buffer Size
static const int kDataBufSize = 1024;
// HTTP Request Buffer Size
static const int kReqBufSize = kHeaderBufSize + kDataBufSize;

// HTTP Message Line Parser State Machine
enum LINE_STAT {
  LINE_STAT_OK  = 0,
  LINE_STAT_OPEN,
  LINE_STAT_WAITING_N,
  LINE_STAT_BAD
};

// HTTP Request Parser State Machine
enum CHECK_STAT {
  CHECK_STAT_REQLINE  = 0,
  CHECK_STAT_HEADER,
  CHECK_STAT_ENTITY
};
enum RESULT_CODE {
  RES_NO_REQUEST  = 0,
  RES_GOT_REQUEST,
  RES_DONE_RECV,
  RES_BAD_REQUEST,
  RES_INTERNAL_ERROR
};
typedef struct ReqStatMach ReqStatMach;
struct ReqStatMach {
  int req_sockfd;
  int read_idx;
  int checked_idx;
  int linest_idx;
  char recv_buf[kReqBufSize];
  LINE_STAT lastline_stat;
  CHECK_STAT check_stat;
  RESULT_CODE result;
};

// To mark whether SIGTERM is received
static bool kStop = false;

void RecvAndParse(ReqStatMach *req_parser);
void RespondHelloHTTP(int peer_sockfd, struct sockaddr_in peer_sockaddr);

static void HandleTERM(int sig) {
  kStop = true;
  printf("GOT SIGTERM\n");
  exit(0);
}

void PrintErrno(const char *tag) {
  printf("[FAILED] %s: errno is %d\n", tag, errno);
}
void PrintErrnoAndExist(const char *tag) {
  printf("[FAILED] %s: errno is %d\n", tag, errno);
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

    //sleep(5);

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
      
      printf("========================\n"
             "----[[Accepted New Socket]]----\n");
//      close(peer_sockfd);

      ReqStatMach req_parser;
      req_parser.req_sockfd = peer_sockfd;
      req_parser.read_idx = 0;
      req_parser.checked_idx = 0;
      req_parser.linest_idx = 0;
      memset(req_parser.recv_buf, 0, sizeof(req_parser.recv_buf));
      req_parser.lastline_stat = LINE_STAT_OK;
      req_parser.check_stat = CHECK_STAT_REQLINE;
      req_parser.result = RES_NO_REQUEST;

      printf("----[[RecvAndParse]]----\n");
      while (req_parser.result == RES_NO_REQUEST) {
        RecvAndParse(&req_parser);
      }
      printf("----[[Done RecvAndParse]]----\n");

      printf("----[[Response]]----\n");
      switch (req_parser.result) {
        case RES_GOT_REQUEST:
          printf("[~] got request\n----------------\n");
          // Respond 'Hello' on any Request
          RespondHelloHTTP(peer_sockfd, peer_sockaddr);
          break;
        case RES_DONE_RECV:
          printf("[~] done receiving\n----------------\n");
          break;
        default:
          printf("[x] req_parser.result = %d\n----------------\n",
                 req_parser.result);
          break;
      }
      printf("----[[Done Response]]----\n========================\n");

      close(peer_sockfd);

    }

  }

  close(my_sockfd);

  printf(">>>> CLOSE <<<<\n");

  return 0;

}

LINE_STAT CheckOneLine(ReqStatMach *req_parser) {

  int *checked_idx  = &(req_parser->checked_idx);
  int *read_idx     = &(req_parser->read_idx);
  char *buf         = req_parser->recv_buf;
  for ( ; (*checked_idx) < (*read_idx); (*checked_idx)++) {
    char c = buf[*checked_idx];

    switch (c) {
      case '\r': {
        if ((*checked_idx) + 1 == (*read_idx)) {
          return LINE_STAT_WAITING_N;
        } else if (buf[(*checked_idx) + 1] == '\n') {
          buf[(*checked_idx)++] = '\0';
          buf[(*checked_idx)++] = '\0';
          return LINE_STAT_OK;
        }
        break;
      }
      case '\n': {
        // the only valid case is that the previous
        // buf contains '\r' as the last byte
        if (req_parser->lastline_stat == LINE_STAT_WAITING_N) {
          buf[(*checked_idx)-1] = '\0';
          buf[(*checked_idx)++] = '\0';
          printf("waiting \\n here!!!!!!!!!\n");
          return LINE_STAT_OK;
        } else {
          return LINE_STAT_BAD;
        }
        break;
      }
    }

  }

  return LINE_STAT_OPEN;

}

RESULT_CODE ParseRequestUri(const char *line) {

  // Parse method
  const char *method_end = strpbrk(line, " \t");
  if (!strncasecmp(line, "GET", (char *)method_end - (char *)line)) {
    printf("[Method] GET\n");
  } else {
    printf("[Unsupport Method]\n");
    return RES_BAD_REQUEST;
  }

  // Parse URI
  line = (char *)method_end + 1;
  const char *uri_end = strpbrk(line, " \t");
  if ((char *)uri_end > (char *)line) {
    printf("[URI] %.*s\n", (int)((char *)uri_end - (char *)line), line);
  } else {
    printf("[Parse URI error] %s\n", line);
    return RES_BAD_REQUEST;
  }

  // Parse HTTP Version
  line = (char *)uri_end + 1;
  if (!strncasecmp(line, "HTTP/1.1", 8)) {
    printf("[Version] %s\n", line);
  } else if (!strncasecmp(line, "HTTP/1.0", 8)) {
    printf("[Version] %s\n", line);
  } else {
    printf("[Unsupport Version]\n");
    return RES_BAD_REQUEST;
  }
  
  return RES_NO_REQUEST;
}

RESULT_CODE ParseHeaders(const char *line) {
  if (line == nullptr) {
    printf("[ERROR] ParseHeaders: line is null\n");
    return RES_INTERNAL_ERROR;
  } else if (line[0] == '\0') {
    return RES_GOT_REQUEST;
  } else {
    printf("[WARNING] ParseHeaders: all headers are not parsed now\n"
           "      --> %s\n", line);
  }

  return RES_NO_REQUEST;
}

void ParseBatch(ReqStatMach *req_parser) {
  
  LINE_STAT *line_stat = &(req_parser->lastline_stat);

  while ( ((*line_stat) = CheckOneLine(req_parser)) == LINE_STAT_OK ) {
    printf("ParseBatch: (in while) line_stat = %d\n", *line_stat);
    char *cur_line = req_parser->recv_buf + req_parser->linest_idx;
    req_parser->linest_idx = req_parser->checked_idx;
    // Test: CheckOneLine()
    //printf("[Line]%s<--\n", cur_line);
    //if (strcmp(cur_line, "") <= 0) {
    //  req_parser->result = RES_DONE_RECV;
    //}
    switch (req_parser->check_stat) {
      case CHECK_STAT_REQLINE: {
        req_parser->result = ParseRequestUri(cur_line);
        // Transfer check_stat
        if (req_parser->result == RES_NO_REQUEST) {
          printf("[Done ParseRequestUri]\n");
          req_parser->check_stat = CHECK_STAT_HEADER;
        }
        break;
      }
      case CHECK_STAT_HEADER: {
        req_parser->result = ParseHeaders(cur_line);
        printf("[Done ParseHeaders]\n");
        // check_stat is transfered in return value of
        // ParseHeader()
        break;
      }
      case CHECK_STAT_ENTITY: {
        assert(0);
        req_parser->result = RES_INTERNAL_ERROR;
        printf("[WARNING] ParseBatch: "
               "undefined check_stat == CHECK_STAT_ENTITY\n");
        break;
      }
      default: {
        req_parser->result = RES_INTERNAL_ERROR;
        printf("[ERROR] ParseBatch: undefined check_stat value\n");
        break;
      }
    }

    switch (req_parser->result) {
      case RES_NO_REQUEST:
        continue;
      default:
        // Whether GOT_REQUEST or ERROR, return
        printf("[Done ParseBatch] RETURN req_parse->result = %d\n",
               req_parser->result);
        return;
    }

  }
  printf("ParseBatch: (out while) line_stat = %d\n", *line_stat);

  switch (*line_stat) {
    case LINE_STAT_OPEN:
      return;
    case LINE_STAT_WAITING_N:
      return;
    default:
      req_parser->result = RES_BAD_REQUEST;
      return;
  }
  
  printf("[Done ParseBatch] RETURN req_parse->result = %d\n",
         req_parser->result);
        
}

void RecvAndParse(ReqStatMach *req_parser) {
  printf("'''\n");

  // Receive a new batch of data in BLOCKING way
  int read_size = recv(req_parser->req_sockfd,
                       req_parser->recv_buf + req_parser->read_idx,
                       kReqBufSize - (req_parser->read_idx), 0);
  if (read_size == -1) {
    PrintErrno("RecvAndParse.recv");
    req_parser->result = RES_INTERNAL_ERROR;
    return;
  } else if (!read_size) {
    printf("no more data to (or can be) recv\n");
    req_parser->result = RES_DONE_RECV;
    //req_parser->result = RES_BAD_REQUEST;
    return;
  }
  // Parse this batch 
  req_parser->read_idx += read_size;

  printf("%s\n", req_parser->recv_buf);
  printf("read_idx=%d checked_idx=%d\n",
         req_parser->read_idx, req_parser->checked_idx);
  printf("'''\n");

  ParseBatch(req_parser);

}

void RespondHelloHTTP(int peer_sockfd, struct sockaddr_in peer_sockaddr) {

      char peer_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &peer_sockaddr.sin_addr, peer_ip, INET_ADDRSTRLEN);
      printf("[Connected] peer socket %s:%u\n", 
             peer_ip, ntohs(peer_sockaddr.sin_port));

      int headbuf_used_len = 0;
      int databuf_used_len = 0;
      char head_buf[kHeaderBufSize];
      char data_buf[kDataBufSize];

      int ret = 0;
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

}
