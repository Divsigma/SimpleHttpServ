#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

// class: sub process
class process {
public:
    process(): m_pid(-1) {}

public:
    pid_t m_pid;
    // to communicate with parent process
    int m_pipefd[2];
};

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
    void run_parent();
    void run_child();

private:
    processpool(int listenfd, int num_process);

private:
    int m_listenfd;
    int m_num_process;
    int m_sub_idx;
    process* m_sub_process;

    static processpool<T>* m_instance;

    static const int k_max_num_process = 10;

};

// NOTES: static member must be initialized explicitly
//        else 'undefined reference to xxxx' may occurs
template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;

// processpool constructor
template<typename T>
processpool<T>::processpool(int listenfd, int num_process)
 : m_listenfd(listenfd), m_num_process(num_process), m_sub_idx(-1)
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
            close(m_sub_process[i].m_pipefd[1]);
            m_sub_idx = i;
            break;
        } else {
            // parent
            close(m_sub_process[i].m_pipefd[0]);
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
void processpool<T>::run_parent() {
    while (1) {
        sleep(2);
        printf("[parent]\n");
    }
}

template<typename T>
void processpool<T>::run_child() {
    while (1) {
        sleep(1);
        printf("[%d]\n", m_sub_idx);
    }
}

int main() {
    printf("hello in vscode\n");

    int listenfd = 0;
    processpool<int>* pool = processpool<int>::create(listenfd, 3);

    pool->run();

    return 0;
}