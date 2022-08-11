#ifndef PROCESS_H
#define PROCESS_H

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

class Process {
public:
    Process(): pid_(-1), listenfd_(-1), epollfd_(-1), pool_id_(-1) {}
    void SetupEpoll();
    void SetupSigPipe();

public:
    pid_t pid_;
    int listenfd_;
    int epollfd_;
    // idx in process[] of processpool
    int pool_id_;
    // to communicate with parent process
    int fa_pipefd_[2];
    static const int kPaWfdIdx = 1;
    static const int kChRfdIdx = 0;

};

// signal sources with I/O
extern int sig_pipefd[2];
static const int kSigRfdIdx = 0;
static const int kSigWfdIdx = 1;

void SioHandler(int signum);
void AddSigaction(int signum, void (*func)(int));

void EpollAddFd(int epollfd, int fd);
void EpollDelFd(int epollfd, int fd);
void TestEpollAddFdLT(int epollfd, int fd);

#endif
