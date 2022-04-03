#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

void* SigHandler(void *sig_set) {

  int sig = 0;
  pthread_t tid = pthread_self();
  while (1) {
    if (sigwait((sigset_t *)sig_set, &sig) == 0) {
      printf("[thread-%ld] Got SIGCHLD\n", tid);
    } else {
      printf("[ERROR] sigwait: errno is %d\n", errno);
    }
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  pthread_t sig_tid_0;
  pthread_t sig_tid_1;

  sigset_t sig_set;
  sigemptyset(&sig_set);
  sigaddset(&sig_set, SIGCHLD);
  sigaddset(&sig_set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &sig_set, NULL);

  int ret;
  ret = pthread_create(&sig_tid_0, NULL, &SigHandler, (void *)&sig_set);
  if (ret) {
    printf("[ERROR] pthread_create 0: errno is %d\n", errno);
    exit(EXIT_FAILURE);
  }

  ret = pthread_create(&sig_tid_1, NULL, &SigHandler, (void *)&sig_set);
  if (ret) {
    printf("[ERROR] pthread_create 1: errno is %d\n", errno);
    exit(EXIT_FAILURE);
  }

//  main thread is also able to receive BLOCKED signal
//  int sig;
//  while (1) {
//    if (sigwait(&sig_set, &sig) == 0) {
//      printf("[main] Got %d\n", sig);
//    } else {
//      printf("[ERROR] main sigwait: errno is %d\n", errno);
//    }
//  }


  pthread_join(sig_tid_0, NULL);
  pthread_join(sig_tid_1, NULL);

  return 0;
}
