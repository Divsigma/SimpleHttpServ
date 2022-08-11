#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

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

// class: processpool and parent process
template<typename T>
class ProcessPool {
public:

    // singletone
    static ProcessPool<T>* create(int listenfd, int num_process = 5) {
        if (!instance_) {
            instance_ = new ProcessPool<T>(listenfd, num_process);
        }
        return instance_;
    }

    virtual ~ProcessPool() {
        delete[] proc_;
    }

    void run();

private:
    ProcessPool(int listenfd, int num_process);
    void RunParent(int pa_pool_id);
    void RunChild(int ch_pool_id);

private:
    int num_proc_;
    Process* proc_;

    int stop_;

    static ProcessPool<T>* instance_;

    static const int k_max_num_process = 10;
    static const int k_max_num_epoll_event = 1000;
    static const int k_max_clientfd = 65536;
    static const int k_max_accept_n = 100;

};

static int SetNonBlocking(int fd) {
    int old_opt = fcntl(fd, F_GETFL);
    int new_opt = old_opt | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_opt);
    return old_opt;
}

// NOTES: static member must be initialized explicitly
//        else 'undefined reference to xxxx' may occurs
template<typename T>
ProcessPool<T>* ProcessPool<T>::instance_ = NULL;

// processpool constructor
template<typename T>
ProcessPool<T>::ProcessPool(int listenfd, int num_process)
 : num_proc_(num_process), 
   stop_(0)
{
    assert(num_proc_ >= 0 && num_proc_ <= k_max_num_process);
    
    // leave one for parent process
    proc_ = new Process[num_proc_ + 1];

    // create children process
    for (int i = 1; i < num_proc_ + 1; i++) {

        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, proc_[i].fa_pipefd_) == 0);

        int pid = fork();
        assert(pid >= 0);
        proc_[i].pid_ = pid;
        if (!pid) {
            // child process
            SetNonBlocking(proc_[i].fa_pipefd_[Process::kChRfdIdx]);
            close(proc_[i].fa_pipefd_[Process::kPaWfdIdx]);
            proc_[i].pid_ = pid;
            proc_[i].listenfd_ = listenfd;
            proc_[i].pool_id_ = i;
            return;
        } else {
            // parent process
            SetNonBlocking(proc_[i].fa_pipefd_[Process::kPaWfdIdx]);
            close(proc_[i].fa_pipefd_[Process::kChRfdIdx]);
            proc_[i].pid_ = pid;
            continue;
        }
    }

    // create parent process
    proc_[0].pid_ = getpid();
    proc_[0].listenfd_ = listenfd;
    proc_[0].pool_id_ = 0;

}

template<typename T>
void
ProcessPool<T>::run() {
    if (proc_[0].pool_id_ != -1) {
        // run parent
        RunParent(0);
    } else {
        // get children pool_id_ from map,
        // and run child
        int ch_pool_id = -1;
        for (int i = 0; i < num_proc_ + 1; ++i)
            if (proc_[i].pool_id_ != -1) {
                ch_pool_id = i;
                break;
            }
        printf("[run] ch_pool_id = %d\n", ch_pool_id);
        RunChild(ch_pool_id);
    }
}

template<typename T>
void
ProcessPool<T>::RunParent(int pa_pool_id) {

    Process &pa_proc = proc_[pa_pool_id];

    pa_proc.SetupEpoll();

    // ipc with children is set to m_sub_process[].m_pipefd[wfd]

    // listen to the CREATED services socket
    //EpollAddFd(m_epollfd, m_listenfd);
    TestEpollAddFdLT(
        pa_proc.epollfd_, 
        pa_proc.listenfd_);
    // NOTES:
    //  m_listenfd should be set as nonblocking and accept in a while(),
    //  to be used in EPOLLET mode 
    //  (check out `man epoll` for suggested use of edge-trigger !)
    //SetNonBlocking(m_listenfd);

    // sio
    pa_proc.SetupSigPipe();

    epoll_event ev[k_max_num_epoll_event];
    int num = 0;
    int ret = -1;

    int sub_process_idx = pa_pool_id;

    while (!stop_) {
        printf("[parent] new epoll_wait\n");
        num = epoll_wait(pa_proc.epollfd_, ev, k_max_num_epoll_event, -1);
        if (num < 0 && errno != EINTR) {
            printf("[parent][ERROR] epoll failure, errno = %d\n", errno);
            break;
        }

        printf("[parent] epoll succeed: num = %d, errno = %d\n", num, errno);
        for (int i = 0; i < num; i++) {
            int ev_fd = ev[i].data.fd;
            uint32_t ev_event = ev[i].events;
            if (ev_fd == pa_proc.listenfd_) {
                // select a child in Round Robin style
                int sel = sub_process_idx;
                do {
                    if (proc_[sel].pid_ != -1 && sel != pa_pool_id) {
                        break;
                    }
                    sel = (sel + 1) % (num_proc_ + 1);
                } while (sel != sub_process_idx);
                // TODO: exit parent if all sub_process exit

                // update Round Robin sub process pointer
                sub_process_idx = (sel + 1) % (num_proc_ + 1);
                //assert( sel != pa_pool_id );

                // send a flag to the selected sub process
                // NOTE: accept(m_listenfd) in the selected sub process
                char has_new_conn[2] = "1";
                send(proc_[sel].fa_pipefd_[Process::kPaWfdIdx], 
                     has_new_conn, sizeof(has_new_conn), 0);
                printf("[parent] notify proc-%d\n", sel);
            } else if (ev_fd == sig_pipefd[kSigRfdIdx] && (ev_event & EPOLLIN)) {
                // process signal
                char sigs[10];
                ret = recv(ev_fd, sigs, sizeof(sigs), 0);
                printf("[parent] recv ret = %d\n", ret);
                if (ret <= 0) {
                    continue;
                } else {
                    for (int i = 0; i < ret; i++) {
                        switch (sigs[i]) {
                            case SIGCHLD: {
                                printf("[parent] got SIGCHLD\n");
                                break;
                            }
                            case SIGINT:
                            case SIGTERM: {
                                printf("[parent] got SIGTERM | SIGINT\n");
                                printf("[parent] kill all children\n");
                                for (int idx = 0; idx < num_proc_ + 1; ++idx) {
                                    if (idx == pa_pool_id)
                                        continue;
                                    int pid = proc_[idx].pid_;
                                    if (pid != -1) {
                                        kill(pid, SIGTERM);
                                    }
                                }
                                stop_ = 1;
                                break;
                            } 
                            default:
                                break;
                        }
                    }
                }

            } 
        }
//        sleep(3);
//        for (int i = 0; i < m_num_process; i++) {
//            char msg[10];
//            sprintf(msg, "pa -> %d", i);
//            send(m_sub_process[i].m_pipefd[k_parent_wfd_idx], msg, sizeof(msg), 0);
//            printf("[parent] sent \"%s\" to %d\n", msg, i);
//        }
    }

    close(pa_proc.epollfd_);
}

template<typename T>
void
ProcessPool<T>::RunChild(int ch_pool_id) {

    Process &ch_proc = proc_[ch_pool_id];

    ch_proc.SetupEpoll();

    // NOTES: see NOTES in RunParent()
    SetNonBlocking(ch_proc.listenfd_);

    // ipc with parent
    int ipc_pipefd = ch_proc.fa_pipefd_[Process::kChRfdIdx];
    EpollAddFd(ch_proc.epollfd_, ipc_pipefd);
    // sio
    ch_proc.SetupSigPipe();

    epoll_event ev[k_max_num_epoll_event];
    int num = 0;
    int ret = -1;

    T* clients = new T[k_max_clientfd];
    assert(clients);

    while (!stop_) {
        printf("[proc-%d] new epoll_wait\n", ch_pool_id);
        num = epoll_wait(ch_proc.epollfd_, ev, k_max_num_epoll_event, -1);
        if (num < 0 && errno != EINTR) {
            printf("[proc-%d][ERROR] epoll failure\n", ch_pool_id);
            break;
        }

        printf("[proc-%d] epoll succeed: num = %d, errno = %d\n", ch_pool_id, num, errno);
        for (int i = 0; i < num; i++) {
            int ev_fd = ev[i].data.fd;
            uint32_t ev_event = ev[i].events;
            if (ev_fd == ipc_pipefd && (ev_event & EPOLLIN)) {
                // retrive notify from parent process
                // and accept new client from service socket
                char msg[10];
                memset(msg, 0, sizeof(msg));
                ret = recv(ev_fd, msg, sizeof(msg), 0);
                if ((ret < 0 && errno != EAGAIN) || !ret) {
                    // non-blocking way
                    printf("[proc-%d] *\n", ch_pool_id);
                    continue;
                } else {
                    printf("[proc-%d] (ret = %d errno = %d) got msg: \"%s\"\n", ch_pool_id, ret, errno, msg);

                    // (a) accept a client from the CREATED service socket

                    // NOTES: should accept() listened socket in a while()
                    //        when listened fd is added to epoll in EPOLLET
                    int max_accept_n = k_max_accept_n;
                    while (max_accept_n --) {
                        struct sockaddr_in client_addr;
                        socklen_t client_addrlen = sizeof(client_addr);
                        int client_sockfd = accept(ch_proc.listenfd_, 
                                                   (struct sockaddr *)&client_addr,
                                                   &client_addrlen);
                        if (client_sockfd < 0) {
                            printf("[proc-%d] [accept] errno = %d\n", ch_pool_id, errno);
                            break;
                        }
                        if (!clients[client_sockfd].is_init(-1)) {
                            printf("[proc-%d] accept to quick !\n", ch_pool_id);
                            close(client_sockfd);
                            break;
                        }
                    
                        char ip[16];
                        printf("[RunChild()] client_sockfd = %d, addr = %s:%d \n", 
                               client_sockfd,
                               inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN),
                               ntohs(client_addr.sin_port));
                        // TODO: (b) init for new client
                        clients[client_sockfd].init(
                            ch_proc.epollfd_,
                            client_sockfd,
                            client_addr
                        );

                        // TODO: (c) add accepted client_sockfd to epoll
                        EpollAddFd(ch_proc.epollfd_, client_sockfd);

                        int client_opt;
                        client_opt = fcntl(client_sockfd, F_GETFL);
                        printf("client_opt %d (before SetNonBlocking())\n", client_opt);

                        SetNonBlocking(client_sockfd);

                        client_opt = fcntl(client_sockfd, F_GETFL);
                        printf("client_opt %d (after SetNonBlocking())\n", client_opt);
                    }

                }

            } else if (ev_fd == sig_pipefd[kSigRfdIdx] && (ev_event & EPOLLIN)) {
                // process signal
                char sigs[10];
                ret = recv(ev_fd, sigs, sizeof(sigs), 0);
                if (ret <= 0) {
                    continue;
                } else {
                    for (int i = 0; i < ret; i++) {
                        switch (sigs[i]) {
                            case SIGCHLD: {
                                printf("[proc-%d] got SIGCHLD\n", ch_pool_id);
                                break;
                            }
                            case SIGINT:
                            case SIGTERM: {
                                printf("[proc-%d] got SIGTERM | SIGINT\n", ch_pool_id);
                                stop_ = 1;
                                break;
                            } 
                            default:
                                break;
                        }
                    }
                }
                
            } else if (ev_event & EPOLLIN) {
                // TODO: process client
                clients[ev_fd].process();
                //assert(0);

            } else {
                continue;
            }
        }

//        sleep(1);
//        printf("[%d]\n", ch_pool_id);
    }

    for (int fd = 0; fd < k_max_clientfd; fd++) {
        if (clients[fd].is_init(fd)) {
            close(fd);
            printf("[proc-%d] close socket: %d\n", ch_pool_id, fd);
        }
    }
    close(ch_proc.epollfd_);
}


#endif
