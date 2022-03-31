#ifndef CLIENT_LIST_TIMER
#define CLIENT_LIST_TIMER

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <assert.h>

// Pre-declaration
class ClientTimer;

// max size of clients' receive buffer
static const int kBufSizeRecv = 32;

// data structure for each client
struct ClientData {
  sockaddr_in client_sockaddr;
  char *buf_to_write;
  char recv_buf[kBufSizeRecv];
  int pollfds_idx;
  ClientTimer *timer;
};

class ClientTimer {
 public:
  ClientTimer() : prev_(nullptr), next_(nullptr) {};
 
 public:
  ClientTimer *prev_;
  ClientTimer *next_;
  ClientData *client_data_;
  time_t t_expire_;
  // expire handler for poll(), pass index of ClientData* in pollfds
  void (*poll_expire_handler_) (ClientData *);
};

// A timer implemented with doubly linked list 
// for each ClientData (e.q. for each client)
class TimerSortedList {
 public:
  TimerSortedList() : head_(nullptr), tail_(nullptr) {};
  ~TimerSortedList();
  void AddTimer(ClientTimer *new_timer);
  void DelTimer(ClientTimer *del_timer);
  void ChgTimer(ClientTimer *old_timer, time_t t_reset);
  void TickOnce(time_t t_current);
  void OutputList();
 
 private:
  bool SoftDelTimer(ClientTimer *del_timer);
  ClientTimer *head_;
  ClientTimer *tail_;
  
};

TimerSortedList::~TimerSortedList() {
  // release `cur` if not null
  // updated head is stored in `head_`
  ClientTimer *cur = head_;
  while (cur) {
    head_ = cur->next_;
    // release if not null
    delete cur;
    cur = head_;
  }

}

void TimerSortedList::AddTimer(ClientTimer *new_timer) {

  ClientTimer *cur = head_;
  if (!cur) {
    head_ = new_timer;
    tail_ = new_timer;
    printf("add as head\n");
    return;
  }

  ClientTimer *prev = cur->prev_;
  while (cur) {
    printf("in while\n");
    if (cur->t_expire_ < new_timer->t_expire_) {
      prev = cur;
      cur = cur->next_;
    } else {
      printf("try to insert\n");
      if (prev) {
        prev->next_ = new_timer;
      } else {
        // Should not re-enter loop
        head_ = new_timer;
      }
      new_timer->prev_ = prev;

      new_timer->next_ = cur;
      cur->prev_ = new_timer;
      return;
    }
  }
  printf("out of while\n");

  assert(prev);
  printf("add to tail\n");
  prev->next_ = new_timer;
  new_timer->prev_ = prev;
  new_timer->next_ = cur;
  
  return;

}

bool TimerSortedList::SoftDelTimer(ClientTimer *del_timer) {

  ClientTimer *cur = head_;
  while (cur && cur != del_timer) {
    cur = cur->next_;
  }
  if (!cur) {
    printf("(SoftDelTimer) timer not found\n");
    return false;
  } else {
    ClientTimer *curprev = cur->prev_;
    ClientTimer *curnext = cur->next_;

    if (curprev) {
      curprev->next_ = curnext;
    } else {
      head_ = curnext;
    }
    
    if (curnext) {
      curnext->prev_ = curprev;
    } else {
      tail_ = curprev;
    }

    printf("(SoftDelTimer) done\n");
    return true;
  }

}

void TimerSortedList::DelTimer(ClientTimer *del_timer) {
  if (SoftDelTimer(del_timer)) {
    delete del_timer;
    printf("(DelTimer) done\n");
  }
}

void TimerSortedList::ChgTimer(ClientTimer *old_timer, time_t t_reset) {
  if (SoftDelTimer(old_timer)) {
    old_timer->t_expire_ = t_reset;
    old_timer->prev_ = nullptr;
    old_timer->next_ = nullptr;
    AddTimer(old_timer);
  }
}

void TimerSortedList::OutputList() {

  ClientTimer *cur = head_;
  while (cur) {
    printf("t_expire_ = %lu\n", cur->t_expire_);
    cur = cur->next_;
  }

}

void TimerSortedList::TickOnce(time_t t_current) {

  ClientTimer *cur = head_;
  while (cur && cur->t_expire_ < t_current) {
    printf("(TickOnce) del t_expire_=%lu\n", cur->t_expire_);
    head_ = cur->next_;
    if (head_) {
      head_->prev_ = nullptr;
    }

    // callback for poll, to remove client from poll 
    cur->poll_expire_handler_(cur->client_data_);

    delete cur;
    cur = head_;
  }

}

#endif
