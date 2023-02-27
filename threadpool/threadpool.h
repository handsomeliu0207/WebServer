#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <cstdio>

#include "../locker/locker.h"

//线程池类，定义成模板类是为了代码复用，模板参数T是任务类
template<typename T>
class threadpool {
public:
    threadpool(int actor_model, int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    //添加任务
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    static void *worker(void *arg);
    void run();

private:
    //线程的数量
    int m_thread_number;

    //线程池数组，大小为m_thread_number
    pthread_t *m_threads;

    //请求队列中最多允许的，等待处理的请求数量
    int m_max_requests;

    //请求队列
    std::list<T *> m_workqueue;

    //互斥锁
    locker m_queuelocker;

    //信号量用来判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;

    //模型切换
    int m_actor_model;
};

template<typename T>
threadpool<T>::threadpool(int actor_model, int thread_number, int max_requests) : 
    m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), 
    m_stop(false), m_threads(NULL) {
    
    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    //创建thread_number个线程，并将它们设置为线程脱离
    for (int i = 0; i < thread_number; i++) {
        //printf("create the %dth thread\n", i);

        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }     
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request, int state) {
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void *  arg) {
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request) {
            continue;
        }
        //reactor
        if (1 == m_actor_model) {
            if (0 == request->m_state) {
                if (request->read()) {
                    request->improv = 1;    
                    request->process();
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            } else {
                if (request->write()) {
                    request->improv = 1;
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else { //proactor
            request->process();
        }
    }
}

#endif