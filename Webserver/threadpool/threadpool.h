#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../SQL/sql_connection_pool.h"

// 半同步/半反应堆线程池
template<typename T>   // T表示任务类
class threadpool {
public:
    // thread_number: 线程池中线程的数量
    // max_request: 请求队列中最多允许等待处理的请求的数量
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 10000);    
    ~threadpool();
    bool append_p(T* request);            // proactor模式下的请求入队
    bool append(T *request, int state);   // recactor模式下的入队

private:
    static void* worker(void* arg);    // 线程运行的函数，从请求队列中取出任务执行
    void run();              

private:
    int m_thread_number;           // 线程池中线程的数量
    int m_max_requests;            // 请求队列中最大请求数量     
    pthread_t* m_threads;          // 描述线程的数组，大小为m_thread_number
    std::list<T*> m_workqueue;     // 请求队列，是主线程和子线程共享的，使用时应该加互斥锁！！！
    locker m_queuelocker;          // 互斥锁
    sem m_queuestat;               // POSIX信号量: 是否有任务需要处理    
    bool m_stop;                   // 线程是否结束
    int m_actor_model;             // 模型切换（这个切换是指Reactor/Proactor）
    connection_pool *m_connPool;   //数据库
};

template<typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : 
        m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL), m_connPool(connPool)
{
    if((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();                              // 抛出异常，try能够捕获
    }

    m_threads = new pthread_t[m_thread_number];     
    if(!m_threads) {
        throw std::exception();  
    }

    for (int i = 0; i < thread_number; ++i ) {                // 建立线程池
        printf( "create the %dth thread\n", i );
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {     // 创建成功，返回0；创建失败，返回非0
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])) {      // 线程分离，与主控线程断开关系，线程结束后，自动释放占用的资源，不用单独对工作线程进行回收，成功返回0
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete [] m_threads;
    m_stop = true;
}

//proactor模式下的请求入队
template<typename T>
bool threadpool<T>::append_p(T* request) {
    m_queuelocker.lock();                       // 操作工作队列，要加锁，因为工作队列被所有线程共享
    if (m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();                         // 信号量 + 1，提醒有任务要处理
    return true;
}

//reactor模式下的请求入队
template<typename T>
bool threadpool<T>::append(T* request, int state) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    //读写事件
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}


template<typename T>
void* threadpool<T>::worker(void* arg) {             // 静态函数，工作线程调用，静态函数中使用类的动态成员
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();                    // 信号量等待
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();      // 从请求队列中取出请求
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request ) {
            continue;
        }

        // Proactor，读写操作由主线程完成，工作线程只负责处理请求
        if(m_actor_model == 0) {
            connectionRAII mysqlcon(&request->mysql, m_connPool);       // 从连接池中取一个数据库连接 
            request->process();                                         // 执行任务类中的任务函数
        }
        // Reactor，读写操作由工作线程完成
        else {
            if(request->m_state == 0) {                // 读操作
                if(request->read()) {                  // 缓冲区有数据可读
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else {                                    // 写操作
                if(request->write()) {
                    request->improv = 1;              // 缓冲区有数据可写  
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        
    }
}

#endif
