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

    int op = random() % 3;
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
        int del_idx = random() % kMaxIdx;
        int retry_cnt = 0;
        while (!p[del_idx] && retry_cnt < 5) {
          del_idx = random() % kMaxIdx;
          retry_cnt ++;
        }
        // successfully select an ClientTimer;
        if (retry_cnt < 5) {
          printf("[INFO] [TO DEL] time=%lu\n", p[del_idx]->t_expire_);
          timer_list.DelTimer(p[del_idx]);
          p[del_idx] = nullptr;
          printf("[INFO] [DELED] \n");
        } else {
          printf("[FAILED] DEL cannot select within 5 retries\n");
        }

        break;
      }
      case 2: {
        int chg_idx = random() % kMaxIdx;
        int retry_cnt = 0;
        while (!p[chg_idx] && retry_cnt < 5) {
          chg_idx = random() % kMaxIdx;
          retry_cnt ++;
        }
        if (retry_cnt < 5) {
          time_t t_reset = (unsigned long int)(random());
          printf("[INFO] [TO CHG] time=%lu, reset to=%lu\n", p[chg_idx]->t_expire_, t_reset);
          timer_list.OutputList();
          timer_list.ChgTimer(p[chg_idx], t_reset);
          printf("[INFO] [CHGED] \n");
          timer_list.OutputList();
        } else {
          printf("[FAILED] CHG cannot select within 5 retries\n");
        }
        break;
      }
      default:
        assert(0);
        break;
    }
    printf("---------------\n");
  }

  timer_list.OutputList();

  return 0;

}
