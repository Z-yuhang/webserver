#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

/*
阻塞队列:
1. 当队列中没有元素时，对这个队列的弹出操作将会被阻塞，直到有元素被插入时才会被唤醒;
2. 当队列已满时，对这个队列的插入操作就会被阻塞，直到有元素被弹出后才会被唤醒;
*/

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"

template <class T>
class block_queue {
public:
    block_queue(int max_size) {
        if(max_size <= 0) {
            exit(-1);                // 退出进程，给父进程返回-1
        }
        // 创建循环数组
        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~block_queue() {
        m_mutex.lock();             
        if(m_array != NULL) {
            delete [] m_array;
            m_array = NULL;
        }
        m_mutex.unlock();
    }

    bool isfull() {
        m_mutex.lock();
        if(m_size == m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty() {
        m_mutex.lock();
        if(m_size == 0) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队首元素
    bool front(T& value) {
        m_mutex.lock();
        if(m_size > 0) {
            value = m_array[m_front];
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队尾元素
    bool back(T& value) {
        m_mutex.lock();
        if(m_size > 0) {
            value = m_array[m_back];
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    // 返回队列大小
    int size() {
        int temp = 0;
        m_mutex.lock();
        temp = m_size;
        m_mutex.unlock();
        
        return temp;
    }

    // 返回队列中允许的最大元素个数
    int max_size()
    {
        int temp = 0;
        m_mutex.lock();
        temp = m_max_size;
        m_mutex.unlock();
        
        return temp;
    }

    // 往队列中添加元素
    bool push(const T& value) {
        m_mutex.lock();
        if(m_size >= m_max_size) {
            m_cond.broadcast();         // 以广播的方式唤醒所有使用队列的线程
            m_mutex.unlock();
            return false;
        }
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = value;
        m_size++;
        
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    // 出队，如果队列中没有元素，就会等待条件变量
    bool pop(T& value) {
        m_mutex.lock();
        while(m_size <= 0) {
            if(!m_cond.wait(m_mutex.get())) {      // 等待目标变量
                m_mutex.unlock();                  // 内部解锁
                return false;
            }
        }
        m_front = (m_front + 1) % m_max_size;
        value = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    // 出队，增加超时处理
    // //在pthread_cond_wait基础上增加了等待的时间，在指定时间内能抢到互斥锁即可
    bool pop(T& value, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t)) {   
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0) {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        value = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;         // 互斥锁，每次操作队列前都要加锁
    cond m_cond;            // 条件变量
    T* m_array;             // 队列
    int m_size;             // 当前元素的数量
    int m_max_size;         // 容量
    int m_front;            // 队头
    int m_back;             // 队尾
};

#endif
