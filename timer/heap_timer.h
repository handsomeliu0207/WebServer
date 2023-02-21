#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include <queue>
#include <vector>
#include <fcntl.h>
#include <sys/epoll.h>

class heap_timer;

struct client_data
{
    struct sockaddr_in address;
    int sockfd;
    heap_timer *timer;
};

//定时器类
class heap_timer {
public:
    heap_timer():m_adjust_times(0) {}

    time_t expire; //定时器生效绝对时间
    void (*cb_func)(client_data *); // 定时器回调函数
    client_data *user_data;

    //void set_used(bool use) { used = use; }
    //bool is_used() { return used; }
    int m_adjust_times;
private:
    //bool used;
    
};

//时间堆类
class time_heap {
public:
    void add_timer(heap_timer *timer);
    void del_timer(heap_timer *timer);
    void adjust_timer(heap_timer *timer);
    void tick();

    class timer_cmp {
    public:
        bool operator() (const heap_timer* a, const heap_timer* b) {
            return a->expire > b->expire;
        }
    };
private:
    std::priority_queue<heap_timer*, std::vector<heap_timer*>, timer_cmp> heap;
};




#endif