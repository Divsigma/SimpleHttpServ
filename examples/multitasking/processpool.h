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

// class: sub process
class process {
public:
    process(): m_pid(-1) {}
    void sio_handler(int signum);
    void add_sig_to_pipe(int signum);

public:
    pid_t m_pid;
    // to communicate with parent process
    int m_pipefd[2];
};

// signal sources with I/O
int sig_pipefd[2];
static const int k_sig_rfd_idx = 0;
static const int k_sig_wfd_idx = 1;

// class: processpool and parent process
template<typename T>
class processpool {
public:

    // singletone
    static processpool<T>* create(int listenfd, int num_process = 5) {
        if (!m_instance) {
            m_instance = new processpool<T>(listenfd, num_process);
        }
        return m_instance;
    }

    virtual ~processpool() {
        delete[] m_sub_process;
    }

    void run();

private:
    processpool(int listenfd, int num_process);

    void setup_epoll();

    void setup_sig_pipe();

    void run_parent();
    void run_child();

private:
    // service socket (can be static ?)
    int m_listenfd;

    int m_epollfd;

    int m_num_process;
    int m_sub_idx;
    process* m_sub_process;

    int m_stop;

    static processpool<T>* m_instance;

    static const int k_parent_wfd_idx = 1;
    static const int k_child_rfd_idx = 0;
    static const int k_max_num_process = 10;
    static const int k_max_num_epoll_event = 1000;
    static const int k_max_clientfd = 65536;

};

// NOTES: static member must be initialized explicitly
//        else 'undefined reference to xxxx' may occurs
template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;

// processpool constructor
template<typename T>
processpool<T>::processpool(int listenfd, int num_process)
 : m_listenfd(listenfd), 
   m_num_process(num_process), 
   m_sub_idx(-1), 
   m_epollfd(-1),
   m_stop(0)
{
    assert(m_num_process >= 0 && m_num_process <= k_max_num_process);

    m_sub_process = new process[m_num_process];

    // create children process
    for (int i = 0; i < m_num_process; i++) {

        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd) == 0);

        int pid = fork();
        assert(pid >= 0);
        m_sub_process[i].m_pid = pid;
        if (!pid) {
            // child
            close(m_sub_process[i].m_pipefd[k_parent_wfd_idx]);
            //close(m_sub_process[i].m_pipefd[1 - k_child_rfd_idx]);
            m_sub_idx = i;
            break;
        } else {
            // parent
            close(m_sub_process[i].m_pipefd[k_child_rfd_idx]);
            //close(m_sub_process[i].m_pipefd[1 - k_parent_wfd_idx]);
            continue;
        }
    }

}

template<typename T>
void processpool<T>::run() {
    if (m_sub_idx == -1) {
        // parent
        run_parent();
    } else {
        // child
        run_child();
    }
}

template<typename T>
void processpool<T>::setup_epoll() {
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);
}

static int setnonblocking(int fd) {
    int old_opt = fcntl(fd, F_GETFL);
    int new_opt = old_opt | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_opt);
    return old_opt;
}

void epoll_addfd(int epollfd, int fd) {

    //setnonblocking(fd);

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);

}

void epoll_delfd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void sio_handler(int signum) {
    int save_errno = errno;
    char msg = static_cast<char>(signum);
    send(sig_pipefd[k_sig_wfd_idx], &msg, 1, 0);
    errno = save_errno;
}

void add_sigaction(int signum, void (*func)(int)) {

    struct sigaction act;
    act.sa_flags = 0;
    act.sa_handler = func;
    sigfillset(&act.sa_mask);

    assert(sigaction(signum, &act, nullptr) != -1);   

}

template<typename T>
void processpool<T>::setup_sig_pipe() {
    assert(m_epollfd != -1);
    
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sig_pipefd) != -1);

    /*
    int opt0 = fcntl(sig_pipefd[0], F_GETFL);
    int opt1 = fcntl(sig_pipefd[1], F_GETFL);
    printf("opt0 %d --> %d\n", opt0, opt0 | O_NONBLOCK);
    printf("opt1 %d --> %d\n", opt1, opt1 | O_NONBLOCK);
    */

    add_sigaction(SIGTERM, sio_handler);
    add_sigaction(SIGINT, sio_handler);
    add_sigaction(SIGCHLD, sio_handler);

    epoll_addfd(m_epollfd, sig_pipefd[k_sig_rfd_idx]);

}

template<typename T>
void processpool<T>::run_parent() {

    setup_epoll();

    // ipc with children is set to m_sub_process[].m_pipefd[wfd]

    // listen to the CREATED services socket
    epoll_addfd(m_epollfd, m_listenfd);
    // sio
    setup_sig_pipe();

    epoll_event ev[k_max_num_epoll_event];
    int num = 0;
    int ret = -1;

    int sub_process_idx = 0;

    while (!m_stop) {
        printf("[parent] new epoll_wait\n");
        num = epoll_wait(m_epollfd, ev, k_max_num_epoll_event, -1);
        if (num < 0 && errno != EINTR) {
            printf("[parent][ERROR] epoll failure, errno = %d\n", errno);
            break;
        }

        printf("[parent] epoll succeed: num = %d, errno = %d\n", num, errno);
        for (int i = 0; i < num; i++) {
            int ev_fd = ev[i].data.fd;
            uint32_t ev_event = ev[i].events;
            if (ev_fd == m_listenfd) {
                // select a child in Round Robin style
                int sel = sub_process_idx;
                do {
                    if (m_sub_process[sel].m_pid != -1) {
                        break;
                    }
                    sel = (sel + 1) % m_num_process;
                } while (sel != sub_process_idx);
                // TODO: exit parent if all sub_process exit

                // update Round Robin sub process pointer
                sub_process_idx = (sel + 1) % m_num_process;

                // send a flag to the selected sub process
                // NOTE: accept(m_listenfd) in the selected sub process
                char has_new_conn = '1';
                send(m_sub_process[sel].m_pipefd[k_parent_wfd_idx], 
                     (char*)&has_new_conn, sizeof(has_new_conn), 0);
                printf("[parent] notify proc-%d\n", sel);
            } else if (ev_fd == sig_pipefd[k_sig_rfd_idx] && (ev_event & EPOLLIN)) {
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
                                for (int idx = 0; idx < m_num_process; idx ++) {
                                    int pid = m_sub_process[idx].m_pid;
                                    if (pid != -1) {
                                        kill(pid, SIGTERM);
                                    }
                                }
                                m_stop = 1;
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

    close(m_epollfd);
}

template<typename T>
void processpool<T>::run_child() {

    setup_epoll();

    // ipc with parent
    int ipc_pipefd = m_sub_process[m_sub_idx].m_pipefd[k_child_rfd_idx];
    epoll_addfd(m_epollfd, ipc_pipefd);
    // sio
    setup_sig_pipe();

    epoll_event ev[k_max_num_epoll_event];
    int num = 0;
    int ret = -1;

    T* clients = new T[k_max_clientfd];
    assert(clients);

    while (!m_stop) {
        printf("[proc-%d] new epoll_wait\n", m_sub_idx);
        num = epoll_wait(m_epollfd, ev, k_max_num_epoll_event, -1);
        if (num < 0 && errno != EINTR) {
            printf("[proc-%d][ERROR] epoll failure\n", m_sub_idx);
            break;
        }

        printf("[proc-%d] epoll succeed: num = %d, errno = %d\n", m_sub_idx, num, errno);
        for (int i = 0; i < num; i++) {
            int ev_fd = ev[i].data.fd;
            uint32_t ev_event = ev[i].events;
            if (ev_fd == ipc_pipefd && (ev_event & EPOLLIN)) {
                // retrive notify from parent process
                // and accept new client from service socket
                char msg[10];
                ret = recv(ev_fd, msg, sizeof(msg), 0);
                if ((ret < 0 && errno != EAGAIN) || !ret) {
                    // non-blocking way
                    printf("[proc-%d] *\n", m_sub_idx);
                    continue;
                } else {
                    printf("[proc-%d] (ret = %d errno = %d) got msg: \"%s\"\n", m_sub_idx, ret, errno, msg);
                    // (a) accept a client from the CREATED service socket
                    struct sockaddr_in client_addr;
                    socklen_t client_addrlen;
                    int client_sockfd = accept(m_listenfd, 
                                           (struct sockaddr *)&client_addr,
                                           &client_addrlen);
                    if (client_sockfd < 0) {
                        printf("[proc-%d] [accept] errno = %d\n", m_sub_idx, errno);
                        continue;
                    }
                    // TODO: (b) init for new client
                    clients[client_sockfd].init(m_epollfd, client_sockfd, client_addr);

                    // TODO: (c) add accepted client_sockfd to epoll
                    epoll_addfd(m_epollfd, client_sockfd);

                    int client_opt;
                    client_opt = fcntl(client_sockfd, F_GETFL);
                    printf("client_opt %d (before setnonblocking())\n", client_opt);

                    setnonblocking(client_sockfd);

                    client_opt = fcntl(client_sockfd, F_GETFL);
                    printf("client_opt %d (after setnonblocking())\n", client_opt);

                }

            } else if (ev_fd == sig_pipefd[k_sig_rfd_idx] && (ev_event & EPOLLIN)) {
                // process signal
                char sigs[10];
                ret = recv(ev_fd, sigs, sizeof(sigs), 0);
                if (ret <= 0) {
                    continue;
                } else {
                    for (int i = 0; i < ret; i++) {
                        switch (sigs[i]) {
                            case SIGCHLD: {
                                printf("[proc-%d] got SIGCHLD\n", m_sub_idx);
                                break;
                            }
                            case SIGINT:
                            case SIGTERM: {
                                printf("[proc-%d] got SIGTERM | SIGINT\n", m_sub_idx);
                                m_stop = 1;
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
//        printf("[%d]\n", m_sub_idx);
    }

    for (int fd = 0; fd < k_max_clientfd; fd++) {
        if (clients[fd].is_init(fd)) {
            close(fd);
            printf("[proc-%d] close socket: %d\n", m_sub_idx, fd);
        }
    }
    close(m_epollfd);
}

#endif