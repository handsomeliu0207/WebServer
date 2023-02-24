#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <queue>

#include <time.h>
#include <memory>


class heap_timer;

struct client_data
{
    struct sockaddr_in address;
    int sockfd;
    std::shared_ptr<heap_timer> timer;
};

//定时器类
class heap_timer {
public:
    heap_timer():m_adjust_times(0) {}
    time_t expire;                  // 定时器生效绝对时间
    void (*cb_func)(client_data *); // 定时器回调函数
    client_data *user_data;

    //void set_used(bool use) { used = use; }
    //bool is_used() { return used; }
    int m_adjust_times;
};

//时间堆类
class time_heap {
public:
    time_heap() {}
    ~time_heap() {}
    
    void add_timer(std::shared_ptr<heap_timer> timer);
    void del_timer(std::shared_ptr<heap_timer> timer);
    void adjust_timer(std::shared_ptr<heap_timer> timer);
    void tick();

    class timer_cmp {
    public:
        bool operator() (std::shared_ptr<heap_timer> a, std::shared_ptr<heap_timer> b) {
            return a->expire > b->expire;
        }
    };
private:
    std::priority_queue<std::shared_ptr<heap_timer>, std::vector<std::shared_ptr<heap_timer>>, timer_cmp> heap;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    time_heap m_time_heap;
    static int u_epollfd;
    int m_TIMESLOT;
};

#endif