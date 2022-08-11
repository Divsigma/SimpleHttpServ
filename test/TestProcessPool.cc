#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <errno.h>

#include "ProcessPool.h"

class client {
public:
    client() : m_sockfd(-1) {}
    ~client() {}
    void init(int epollfd, int sockfd, const struct sockaddr_in);
    int is_init(int expected_sockfd);
    void process();

private:
    static const int k_recvbuf_size = 32;

    int m_epollfd;
    int m_sockfd;
    struct sockaddr_in m_addr;
    
    char recvbuf[k_recvbuf_size];

};

void client::init(int epollfd, int sockfd, const struct sockaddr_in addr)
{
    m_epollfd = epollfd;
    m_sockfd = sockfd;
    m_addr = addr;
    memset(recvbuf, 0, sizeof(recvbuf));
    printf("client inited\n");
    printf("--------\n");
}

int client::is_init(int expected_sockfd) {
    return m_sockfd == expected_sockfd;
}

void client::process() {
    int bytes_read = 0;

    while (1) {
        bytes_read = recv(m_sockfd, recvbuf, k_recvbuf_size - 1, 0);
        if (bytes_read <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("[client] no data to process, errno = %d\n", errno);
            }
            break;
        } else {
            char ip[16];
            printf("[client] recv from (%s:%d): len = %ld\n'''\n%s\n'''\n", 
                   inet_ntop(AF_INET, &m_addr.sin_addr, ip, INET_ADDRSTRLEN), 
                   ntohs(m_addr.sin_port),
                   strlen(recvbuf),
                   recvbuf);
        }
    }

    printf("client processed\n");
    printf("--------\n");
}

int main() {
    printf("hello in vscode\n");

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.56.2", &addr.sin_addr);
    addr.sin_port = htons(11111);

    ret = bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1) {
        printf("[bind] errno = %d\n", errno);
        return 0;
    }

    ret = listen(listenfd, 5);
    assert(ret != -1);

    ProcessPool<client>* pool = ProcessPool<client>::create(listenfd, 3);

    pool->run();

    ret = close(listenfd);
    if (ret == -1) {
        printf("[close] errno = %d\n", errno);
        return 0;
    } else {
        printf("[closed] listenfd\n");
    }

    return 0;
}