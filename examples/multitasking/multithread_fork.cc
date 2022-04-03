#include <cstdio>
#include <pthread.h>
#include <sys/types.h>
#include <wait.h>
#include <unistd.h>

pthread_mutex_t lock;

void* work(void *arg) {

  pthread_t tid = pthread_self();
  printf("[thread-%ld] trying to acquire lock\n", tid);
  pthread_mutex_lock(&lock);
  printf("[thread-%ld] acquired lock\n", tid);
  sleep(2);
  pthread_mutex_unlock(&lock);
  printf("[thread-%ld] released lock\n", tid);

  return NULL;

}

int main (int argc, char *argv[]) {

  pthread_mutex_init(&lock, NULL);

  pthread_t tid;

  pthread_create(&tid, NULL, &work, NULL);

  sleep(1);

  int pid = fork();
  if (pid < 0) {
    pthread_join(tid, NULL);
    pthread_mutex_destroy(&lock);
    return 1;
  } else if (pid == 0) {
    printf("[process-%d] trying to acquire lock\n", getpid());
    pthread_mutex_lock(&lock);
    printf("[process-%d] acquired lock\n", getpid());
    sleep(2);
    pthread_mutex_unlock(&lock);
    printf("[process-%d] released lock\n", getpid());
  } else {
    printf("[main] waiting ...\n");
    wait(NULL);
    printf("[main] done ...\n");
  }

  pthread_join(tid, NULL);
  pthread_mutex_destroy(&lock);
  
  return 0;

}
