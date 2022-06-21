#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
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
    void epoll_addfd(int epollfd, int fd);

    void setup_sig_pipe();

    void run_parent();
    void run_child();

private:
    int m_listenfd;

    int m_epollfd;

    int m_num_process;
    int m_sub_idx;
    process* m_sub_process;

    static processpool<T>* m_instance;

    static const int k_parent_wfd_idx = 1;
    static const int k_child_rfd_idx = 0;
    static const int k_max_num_process = 10;
    static const int k_max_num_epoll_event = 1000;

};

// NOTES: static member must be initialized explicitly
//        else 'undefined reference to xxxx' may occurs
template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;

// processpool constructor
template<typename T>
processpool<T>::processpool(int listenfd, int num_process)
 : m_listenfd(listenfd), m_num_process(num_process), m_sub_idx(-1), m_epollfd(-1)
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
    return 0;
}

template<typename T>
void processpool<T>::epoll_addfd(int epollfd, int fd) {

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);

}

void sio_handler(int signum) {

    char msg = static_cast<char>(signum);
    send(sig_pipefd[k_sig_wfd_idx], &msg, 1, 0);

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

    add_sigaction(SIGTERM, sio_handler);
    add_sigaction(SIGINT, sio_handler);
    add_sigaction(SIGCHLD, sio_handler);

    epoll_addfd(m_epollfd, sig_pipefd[k_sig_rfd_idx]);

}

template<typename T>
void processpool<T>::run_parent() {
    while (1) {
        sleep(3);
        for (int i = 0; i < m_num_process; i++) {
            char msg[10];
            sprintf(msg, "pa -> %d", i);
            send(m_sub_process[i].m_pipefd[k_parent_wfd_idx], msg, sizeof(msg), 0);
            printf("[parent] sent \"%s\" to %d\n", msg, i);
        }
//        sleep(2);
//        printf("[parent]\n");
    }
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

    while (1) {
        num = epoll_wait(m_epollfd, ev, k_max_num_epoll_event, -1);
        if (num < 0 && errno != EINTR) {
            printf("[ERROR] epoll failure\n");
            break;
        }

        for (int i = 0; i < num; i++) {
            int ev_fd = ev[i].data.fd;
            uint32_t ev_event = ev[i].events;
            if (ev_fd == ipc_pipefd && (ev_event & EPOLLIN)) {
                char msg[10];
                ret = recv(ev_fd, msg, sizeof(msg), 0);
                if ((ret < 0 && errno != EAGAIN) || !ret) {
                    // non-blocking way
                    printf("[proc-%d] *\n", m_sub_idx);
                    continue;
                } else {
                    printf("[proc-%d] got msg: \"%s\"\n", m_sub_idx, msg);
                }

            } else if (ev_fd == sig_pipefd[k_sig_rfd_idx] && (ev_event & EPOLLIN)) {
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
                                // TODO: stop current process
                                break;
                            } 
                            default:
                                break;
                        }
                    }
                }
                
            } else if (ev_event & EPOLLIN) {
                // TODO: process client
                assert(0);

            } else {
                continue;
            }
        }

//        sleep(1);
//        printf("[%d]\n", m_sub_idx);
    }
}

int main() {
    printf("hello in vscode\n");

    int listenfd = 0;
    processpool<int>* pool = processpool<int>::create(listenfd, 3);

    pool->run();

    return 0;
}