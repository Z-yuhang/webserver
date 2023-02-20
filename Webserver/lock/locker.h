#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 封装POSIX信号量
class sem {
public:
    sem() {                                         // 创建并初始化信号量
        if(sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(int num) {
        if (sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~sem() {                                        // 销毁信号量
        sem_destroy(&m_sem);
    }

    bool wait() {                                   // 等待信号量
        return sem_wait(&m_sem) == 0;
    }

    bool post() {                                   // 增加信号量
        return sem_post(&m_sem) == 0;   
    }

private:
    sem_t m_sem;                                    // 被操作的信号量
};


// 封装互斥锁
class locker {
public:
    locker() {                                      // 创建并初始化互斥锁
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }

    ~locker() {                                     // 销毁互斥锁 
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock() {                                   // 加锁
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock() {                                 // 解锁
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t *get() {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

// 封装条件变量
class cond {
public:
    cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }

    ~cond() {
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t *m_mutex) {
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }

    bool timewait(pthread_mutex_t *m_mutex, struct timespec t) {
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
    }

    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast() {                 // 以广播的方式唤醒所有等待目标条件变量的线程
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;        // 目标条件变量
};

#endif
