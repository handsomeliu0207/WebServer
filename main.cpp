#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <assert.h>
#include "./locker/locker.h"
#include "./threadpool/threadpool.h"
#include <signal.h>
#include "./http/http_conn.h"
#include "./timer/heap_timer.h"

#define MAX_FD 65536  //最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000 //监听的最大事件数量
#define TIMESLOT 5

static int pipefd[2];
static time_heap timer_heap;
static int epollfd = 0;
client_data *users_timer = new client_data[MAX_FD];
// 定时回调函数
void cb_func(client_data *user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    locker userlocker;
    userlocker.lock();
    http_conn::m_user_count--;
    userlocker.unlock();
    printf("cb_func, user_count:%d\n", http_conn::m_user_count);
}

//信号处理函数
void sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//定时处理函数
void timer_handler() {
    timer_heap.tick();
    alarm(TIMESLOT);
}
//添加信号捕捉
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);

//从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);

//修改文件描述符
extern void modfd(int epollfd, int fd, int ev);
extern int setnonblocking(int fd);

int main(int argc, char* argv[]) {
    
    if (argc <= 1) {
        printf("按照如下格式运行： %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);

    //对SIGPIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池，初始化线程池
    threadpool<http_conn>* pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch(...) {
        exit(-1);
    }

    //创建一个数组用于保存所有的客户端信息
    http_conn *users = new http_conn[MAX_FD];

    //创建监听的套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr *)&address, sizeof(address));

    //监听
    listen(listenfd, 5);

    //创建epoll对象， 事件数组， 添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 将监听的文件添加到epoll对象中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    //创建管道
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler);
    addsig(SIGTERM, sig_handler);
    bool stop_server = false;
    bool timeout = false;
    alarm(TIMESLOT);

    while (!stop_server) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((num < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }

        // 循环遍历事件数组
        for (int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                // 有客户端连接进来
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 
                if (http_conn::m_user_count >= MAX_FD) {
                    //目前连接数满了
                    //给客户端写一个信息： 服务器内部正忙
                    close(connfd);
                    continue;
                }
                // 将新的客户数据初始化，放到数组中
                users[connfd].init(connfd, client_address);

                printf("客户端数目：%d\n, 客户端信息: 地址：%d 端口：%d\n", http_conn::m_user_count, client_address.sin_addr.s_addr,client_address.sin_port);

                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                std::shared_ptr<heap_timer> timer = std::make_shared<heap_timer>();
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_heap.add_timer(timer);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //对方异常断开或者错误等事件
                //users[sockfd].close_conn();
                cb_func(&users_timer[sockfd]);
                if (users_timer[sockfd].timer) {
                    timer_heap.del_timer(users_timer[sockfd].timer);
                }
            }
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                            break;
                        }
                        }
                    }
                }
            }
            else if (events[i].events & EPOLLIN)
            {
                std::shared_ptr<heap_timer> timer = users_timer[sockfd].timer;
                if (users[sockfd].read())
                {
                    //一次性把所有数据都读完

                    pool->append(users + sockfd); 
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf("adjust timer once\n");
                        timer_heap.adjust_timer(timer);
                    }
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                if (!users[sockfd].write()) { 
                    //一次性写完所有数据
                    //users[sockfd].close_conn();
                cb_func(&users_timer[sockfd]);
                if (users_timer[sockfd].timer) {
                    timer_heap.del_timer(users_timer[sockfd].timer);
                }
                }
            }
        }
        if (timeout) {
            timer_handler();
            timeout = false;
        }
    }

    close(pipefd[0]);
    close(pipefd[1]);
    close(epollfd);
    close(listenfd);
    delete[] users;
    delete[] users_timer;
    delete pool;

    return 0;
}