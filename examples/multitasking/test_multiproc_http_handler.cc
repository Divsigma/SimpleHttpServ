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

#include "processpool.h"
#include "nonblock_http_handler.h"

int main(int argc, char *argv[]) {
    printf("hello in vscode\n");

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &addr.sin_addr);
    addr.sin_port = htons(atoi(argv[2]));

    ret = bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == -1) {
        printf("[bind] errno = %d\n", errno);
        return 0;
    }

    ret = listen(listenfd, 5);
    assert(ret != -1);

    processpool<ReqStatMach>* pool = processpool<ReqStatMach>::create(listenfd, 2);

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