#ifndef REQSTATMACH_H
#define REQSTATMACH_H

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

#include "ProcessPool.h"

// HTTP Header Status Line
enum {
  STATUS_OK = 0,
  STATUS_INTERNAL_ERROR,
};

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

class ReqStatMach {
public:
    ReqStatMach() : m_sockfd(-1) {}
    ~ReqStatMach() {}

    void init(int epollfd, int sockfd, const struct sockaddr_in);
    int is_init(int expected_sockfd);
    void process();

    // getter
    RESULT_CODE get_result();

private:
    // functions for receiving and parsing http request
    void parse_batch();
    LINE_STAT check_one_line();
    RESULT_CODE parse_requesturi(const char *line);
    RESULT_CODE parse_headers(const char *line);
    void respond_http();

    // getter and setter
    void set_result(RESULT_CODE res);
    CHECK_STAT get_check_stat();
    void set_check_stat(CHECK_STAT stat);

private:
    // HTTP Response status line
    static const char *k_status_line[2];
    // HTTP Header Buffer Size
    static const int k_headerbuf_size = 1024;
    // HTTP Data Buffer Size
    static const int k_databuf_size = 1024;

    static const int k_recvbuf_size = k_headerbuf_size + k_databuf_size;

    int m_epollfd;
    int m_sockfd;
    struct sockaddr_in m_addr;
    
    int m_read_idx;
    int m_checked_idx;
    int m_linest_idx;
    LINE_STAT m_lastline_stat;
    CHECK_STAT m_check_stat;
    RESULT_CODE m_result;   

    char recvbuf[k_recvbuf_size];

};
const char *ReqStatMach::k_status_line[2] = {
      [STATUS_OK]             = "200 OK",
      [STATUS_INTERNAL_ERROR] = "500 Internal server error"
};

RESULT_CODE ReqStatMach::get_result() { return m_result; }
void ReqStatMach::set_result(RESULT_CODE res) { m_result = res; }
CHECK_STAT ReqStatMach::get_check_stat() { return m_check_stat; }
void ReqStatMach::set_check_stat(CHECK_STAT stat) { m_check_stat = stat; }

void ReqStatMach::init(int epollfd, int sockfd, const struct sockaddr_in addr)
{
    m_epollfd = epollfd;
    m_sockfd = sockfd;
    m_addr = addr;

    m_read_idx = 0;
    m_checked_idx = 0;
    m_linest_idx = 0;
    m_lastline_stat = LINE_STAT_OK;
    m_check_stat = CHECK_STAT_REQLINE;
    m_result = RES_NO_REQUEST;

    memset(recvbuf, 0, sizeof(recvbuf));

    printf("client inited\n");
    char ip[16];
    printf("[init] addr = %s:%d \n", 
           inet_ntop(AF_INET, &(this->m_addr).sin_addr, ip, INET_ADDRSTRLEN), 
           ntohs((this->m_addr).sin_port));
    printf("--------\n");
}

int ReqStatMach::is_init(int expected_sockfd) {
    return m_sockfd == expected_sockfd;
}

void ReqStatMach::process() {
    int bytes_read = 0;

    printf("----[[Try recv and parse]]----\n");
    while (this->get_result() == RES_NO_REQUEST) {
      // 1. Receive a new batch of data in NON-BLOCKING way
      int bytes_read = recv(this->m_sockfd,
                            this->recvbuf + this->m_read_idx,
                            this->k_recvbuf_size - (this->m_read_idx), 0);
      if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          printf("[ReqStatMach] no data to process, errno = %d\n", errno);
        } else {
          this->set_result(RES_INTERNAL_ERROR);
          printf("[ReqStatMach] [ERROR] bytes_read = %d, errno = %d\n", bytes_read, errno);
        }
        break;
      } else if (bytes_read == 0) {
        printf("[ReqStatMach] done receiving\n");
        this->set_result(RES_DONE_RECV);
        break;
      } else {
        char ip[16];
        printf("[ReqStatMach] recv from (%s:%d): \n'''\n%s\n'''\n", 
               inet_ntop(AF_INET, &(this->m_addr).sin_addr, ip, INET_ADDRSTRLEN), 
               ntohs((this->m_addr).sin_port),
               this->recvbuf + this->m_read_idx);
        // 2. Parse this batch 
        this->m_read_idx += bytes_read;
        this->parse_batch();
      }
    }
    printf("----[[Done once recv and parse]]----\n");

    printf("----[[Try respond http]]----\n");
    RESULT_CODE res = this->get_result();
    switch (res) {
        case RES_NO_REQUEST:
          printf("[~] should continue receiving\n----------------\n");
          break;
        case RES_GOT_REQUEST:
          printf("[~] got request\n----------------\n");
          // Respond 'Hello' on any Request
          this->respond_http();
          EpollDelFd(this->m_epollfd, this->m_sockfd);
          // close(this->m_sockfd);
          printf("----[[Done respond http]]----\n========================\n");
          break;
        case RES_DONE_RECV:
          printf("[~] done receiving\n----------------\n");
          //this->respond_http();
          EpollDelFd(this->m_epollfd, this->m_sockfd);
          // close(this->m_sockfd);
          //printf("----[[Done respond http]]----\n========================\n");
          // close(this->m_sockfd);
          break;
        default:
          EpollDelFd(this->m_epollfd, this->m_sockfd);
          // close(this->m_sockfd);
          printf("[x] result = %d\n----------------\n",
                 res);
          break;
    }

    this->m_sockfd = -1;

}

LINE_STAT ReqStatMach::check_one_line() {

  int *checked_idx  = &(this->m_checked_idx);
  int *read_idx     = &(this->m_read_idx);
  char *buf         = this->recvbuf;
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
        if (this->m_lastline_stat == LINE_STAT_WAITING_N) {
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

RESULT_CODE ReqStatMach::parse_requesturi(const char *line) {

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

RESULT_CODE ReqStatMach::parse_headers(const char *line) {
  if (line == nullptr) {
    printf("[ERROR] ParseHeaders: line is null\n");
    return RES_INTERNAL_ERROR;
  } else if (line[0] == '\0') {
    // NOTES: for now, just accept request-uri and headers
    return RES_GOT_REQUEST;
  } else {
    printf("[WARNING] ParseHeaders: all headers are not parsed now\n"
           "      --> %s\n", line);
  }

  return RES_NO_REQUEST;
}

void ReqStatMach::parse_batch() {
  
  LINE_STAT *line_stat = &(this->m_lastline_stat);

  while ( ((*line_stat) = this->check_one_line()) == LINE_STAT_OK ) {
    printf("parse_batch(): (in while) line_stat = %d\n", *line_stat);
    char *cur_line = this->recvbuf + this->m_linest_idx;
    this->m_linest_idx = this->m_checked_idx;

    CHECK_STAT check_stat = this->get_check_stat();
    switch (check_stat) {
      case CHECK_STAT_REQLINE: {
        RESULT_CODE res = parse_requesturi(cur_line);
        this->set_result(res);
        // Transfer check_stat
        if (res == RES_NO_REQUEST) {
          printf("[finish parse_requesturi()]\n");
          this->set_check_stat(CHECK_STAT_HEADER);
        }
        break;
      }
      case CHECK_STAT_HEADER: {
        RESULT_CODE res = parse_headers(cur_line);
        this->set_result(res);
        printf("[done once parse_headers()]\n");
        // NOTES: check_stat is transfered in return value of parse_headers()
        break;
      }
      case CHECK_STAT_ENTITY: {
        assert(0);
        this->set_result(RES_INTERNAL_ERROR);
        printf("[WARNING] parse_batch(): "
               "undefined check_stat == CHECK_STAT_ENTITY\n");
        break;
      }
      default: {
        this->set_result(RES_INTERNAL_ERROR);
        printf("[ERROR] parse_batch(): undefined check_stat value\n");
        break;
      }
    }

    RESULT_CODE result = this->get_result();
    switch (result) {
      case RES_NO_REQUEST:
        continue;
      default:
        // Whether GOT_REQUEST or ERROR, return
        printf("[break from parse_batch() checking line] RETURNED result = %d\n",
               result);
        return;
    }

  }
  printf("parse_batch(): (out of checking line) line_stat = %d\n", *line_stat);

  switch (*line_stat) {
    case LINE_STAT_OPEN:
      return;
    case LINE_STAT_WAITING_N:
      return;
    default:
      this->set_result(RES_BAD_REQUEST);
      return;
  }
  
  printf("[finish parse_batch()] RETURN result = %d\n",
         this->get_result());
        
}

void ReqStatMach::respond_http() {
  int peer_sockfd = this->m_sockfd;
  struct sockaddr_in peer_sockaddr = this->m_addr;

      char peer_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &peer_sockaddr.sin_addr, peer_ip, INET_ADDRSTRLEN);
      printf("[respond_http] peer socket %s:%u\n", 
             peer_ip, ntohs(peer_sockaddr.sin_port));

      int headbuf_used_len = 0;
      int databuf_used_len = 0;
      char head_buf[k_headerbuf_size];
      char data_buf[k_databuf_size];

      int ret = 0;
      // Add data
      ret = snprintf(data_buf, 
                     k_databuf_size,
                     "%s", "Hello from Tom HTTP Server\n");
      databuf_used_len += ret;
      
      // Add header
      // Add protocol version and status line
      ret = snprintf(head_buf, 
                     k_headerbuf_size, 
                     "%s %s\r\n", "HTTP/1.1", k_status_line[STATUS_OK]);
      headbuf_used_len += ret;
      // Add Content-Length
      ret = snprintf(head_buf + headbuf_used_len,
                     k_headerbuf_size - headbuf_used_len,
                     "Content-Length: %d\r\n", databuf_used_len);
      headbuf_used_len += ret;
      // Add tail
      ret = snprintf(head_buf + headbuf_used_len, 
                     k_headerbuf_size - headbuf_used_len,
                     "%s", "\r\n");
      headbuf_used_len += ret;

      // Gather-write header and data to socket
      int iovcnt = 2;
      struct iovec iov[iovcnt];
      iov[0].iov_base = head_buf;
      iov[0].iov_len  = headbuf_used_len;
      iov[1].iov_base = data_buf;
      iov[1].iov_len  = databuf_used_len;

      int opt = fcntl(peer_sockfd, F_GETFL);
      printf("writev peer_sockfd, opt=%d, opt&O_NONBLOCK=%d\n", opt, opt & O_NONBLOCK);

      ret = writev(peer_sockfd, iov, iovcnt);

      printf("[respond_http] writev ret = %d errno = %d\n", ret, errno);

}

#endif
