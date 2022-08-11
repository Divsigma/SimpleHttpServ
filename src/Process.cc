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
#include "Process.h"

/* * * * * * * * * * * * * * * *
 * Signal IO
 * * * * * * * * * * * * * * * *
 */
// signal sources with I/O
int sig_pipefd[2];

void
Process::SetupSigPipe() {
    assert(epollfd_ != -1);
    
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sig_pipefd) != -1);

    /*
    int opt0 = fcntl(sig_pipefd[0], F_GETFL);
    int opt1 = fcntl(sig_pipefd[1], F_GETFL);
    printf("opt0 %d --> %d\n", opt0, opt0 | O_NONBLOCK);
    printf("opt1 %d --> %d\n", opt1, opt1 | O_NONBLOCK);
    */

    AddSigaction(SIGTERM, SioHandler);
    AddSigaction(SIGINT, SioHandler);
    AddSigaction(SIGCHLD, SioHandler);

    EpollAddFd(epollfd_, sig_pipefd[kSigRfdIdx]);

}

void SioHandler(int signum) {
    int save_errno = errno;
    char msg = static_cast<char>(signum);
    send(sig_pipefd[kSigWfdIdx], &msg, 1, 0);
    errno = save_errno;
}

void AddSigaction(int signum, void (*func)(int)) {

    struct sigaction act;
    act.sa_flags = 0;
    act.sa_handler = func;
    sigfillset(&act.sa_mask);

    assert(sigaction(signum, &act, nullptr) != -1);   

}

/* * * * * * * * * * * * * * * *
 * Epoll
 * * * * * * * * * * * * * * * *
 */
void
Process::SetupEpoll() {
    epollfd_ = epoll_create(5);
    assert(epollfd_ != -1);
}


void EpollAddFd(int epollfd, int fd) {

    //setnonblocking(fd);

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);

}

void EpollDelFd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    int ret = close(fd);
    printf("[epoll_delfd] ret = %d errno = %d\n", ret, errno);
}


void TestEpollAddFdLT(int epollfd, int fd) {

    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);

}