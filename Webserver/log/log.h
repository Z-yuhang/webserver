#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <stdio.h>
#include <string>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "block_queue.h"

using namespace std;

class log {
public:
    log();
    virtual ~log();
    // 初始化: 日志文件名、是否关闭日志、日志缓冲区大小、最大行数、最长日志条队列
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    // 向日志写入消息
    void write_log(int level, const char* format, ...);
    // 刷新缓冲区
    void flush();
    // 建立日志(单例模式)
    static log* get_instance();

    // 异步写
    void* async_write_log();
    static void* flush_log_thread(void* args) {              // 回调函数，异步写线程
        return log::get_instance()->async_write_log();
    }

private:
    char dir_name[128];       // 路径名
    char log_name[128];       // log文件名
    int m_split_lines;        // 日志最大行数
    int m_log_buf_size;       // 日志缓冲区大小
    long long m_count;        // 日志行数记录
    int m_today;              // 因为按天分类,记录当前时间是哪一天
    FILE *m_fp;               // 打开log的文件指针
    char *m_buf;              // 缓冲区
    block_queue<string> *m_log_queue;   // 阻塞队列
    bool m_is_async;                    // 是否异步
    locker m_mutex;                     // 互斥锁
    int m_close_log;                    // 关闭日志
};

//使用宏定义，便于调用， 使用##__VA_ARGS__，支持format后面可有0到多个参数
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {log::get_instance()->write_log(0, format, ##__VA_ARGS__); log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {log::get_instance()->write_log(1, format, ##__VA_ARGS__); log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {log::get_instance()->write_log(2, format, ##__VA_ARGS__); log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {log::get_instance()->write_log(3, format, ##__VA_ARGS__); log::get_instance()->flush();}

#endif