#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <assert.h>

#include "client_timer_list.h"

TimerSortedList timer_list;

const int kMaxIdx = 20;
int p_idx = 0;
int p_cnt = 0;
ClientTimer *p[kMaxIdx];

int main() {

  srandom(time(NULL));

  for (int i = 0; i < kMaxIdx; i++) {

    int op = random() % 2;
    switch (op) {
      case 0: {
        ClientTimer *new_timer = new ClientTimer;
        new_timer->t_expire_ = (unsigned long int)(random());
        bool flag = false;
        for (int j = 0; j < kMaxIdx; j++) {
          if (!p[j]) {
            p[j] = new_timer;
            flag = true;
            break;
          }
        }
        if (flag) {
          printf("[INFO] [TO ADD] time=%lu\n", new_timer->t_expire_);
          timer_list.AddTimer(new_timer);
          printf("[INFO] [ADDED] %d, time=%lu\n", i, new_timer->t_expire_);
        } else {
          printf("[FAILED] ADD container is full\n");
        }

        break;
      }
      case 1: {
        time_t t_current = (unsigned long int)(random());
        printf("[INFO] [TO EXPIRE] time<%lu\n", t_current);
        timer_list.OutputList();
        printf("[INFO] [EXPIRING] time<%lu\n", t_current);
        timer_list.TickOnce(t_current);
        printf("[INFO] [EXPIRED] \n");
        timer_list.OutputList();
        break;
      }
      default:
        assert(0);
        break;
    }
    printf("---------------\n");
  }

  return 0;

}
