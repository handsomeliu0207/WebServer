#include "heap_timer.h"


void time_heap::add_timer(heap_timer* timer) {
    if (!timer) {
        return;
    }
    heap.push(timer);
}
void time_heap::del_timer(heap_timer *timer) {
    if (!timer) {
        return;
    }
    timer->m_adjust_times = -1;
}
void time_heap::adjust_timer(heap_timer *timer) {
    add_timer(timer);
    timer->m_adjust_times++;
}
void time_heap::tick() {
    printf("tick\n");
    if (heap.empty()) {
        printf("no client connection\n");
        return;
    }
    heap_timer *tmp;
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
            break;
        }
        tmp->cb_func(tmp->user_data);
        heap.pop();
    }
    
}
