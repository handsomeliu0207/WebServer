#include "heap_timer.h"

void time_heap::add_timer(std::shared_ptr<heap_timer> timer) {
    if (!timer) {
        return;
    }
    heap.push(timer);
}
void time_heap::del_timer(std::shared_ptr<heap_timer> timer) {
    if (!timer) {
        return;
    }
    timer->m_adjust_times = -1;
}
void time_heap::adjust_timer(std::shared_ptr<heap_timer> timer) {
    add_timer(timer);
    timer->m_adjust_times++;
}
void time_heap::tick() {
    if (heap.empty()) {
        //printf("no client connection\n");
        return;
    }
    std::shared_ptr<heap_timer> tmp;
    time_t cur = time(NULL);
    while (!heap.empty()) {
        tmp = heap.top();
        if (!tmp) {
            break;
        }
        if (tmp->m_adjust_times == -1) {
            heap.pop();
            continue;
        }
        if (tmp->m_adjust_times > 0) {
            heap.pop();
            tmp->m_adjust_times--;
            continue;
        }
        if (tmp->expire > cur) {
            Utils::m_TIMESLOT = tmp->expire - cur;
            break;
        }
        tmp->cb_func(tmp->user_data);
        heap.pop();
    }
}

int Utils::m_TIMESLOT = 0;
void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig) {
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    m_time_heap.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;