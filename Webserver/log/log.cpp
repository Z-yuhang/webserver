#include "log.h"

using namespace std;

log::log() {
    m_count = 0;
    m_is_async = false;
}

log::~log() {
    if(m_fp != NULL) {   // 关闭文件
        fclose(m_fp); 
    }
}

bool log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    // 如果设置了阻塞队列的长度，则设置为异步日志
    if(max_queue_size > 0) {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);     // 设置阻塞队列的长度
        // 创建异步线程写日志
        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);        // 线程等待被阻塞队列中的条件变量broadcast唤醒！ 
    }

    m_close_log = close_log;
    // 输出内容长度
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char *p = strrchr(file_name, '/');      // 寻找file_name中最后一次出现‘/’的位置
    char log_full_name[256] = {0};

    if(p == NULL) {
        // 新建文件file_name名称：当前日期——年.月.日
        snprintf(log_full_name, 300, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {
        // 日志名称log_name
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 300, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL) {
        return false;
    }

    return true;
}

// 在日志内写入消息
void log::write_log(int level, const char* format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch (level) {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    // 操作
    m_mutex.lock();
    m_count++;

    // 日期改变—不是同一天，或者行数达到最大，此时需要新建一个log文件
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
       
        if (m_today != my_tm.tm_mday) {          
            snprintf(new_log, 300, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else {
            snprintf(new_log, 300, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    // 写入日志内容
    va_list valst;
    va_start(valst, format);
    string log_str;

    m_mutex.lock();
    // 确定写入的时间、事件
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 确定写入日志内容
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    // 写入的一行日志的内容
    log_str = m_buf;            
    m_mutex.unlock();

    // 异步日志
    if(m_is_async && !m_log_queue->isfull()) {
        m_log_queue->push(log_str);
    }
    // 同步日志
    else {
        // 将日志内容写入文件
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valst);
}

void log::flush() {
    m_mutex.lock();
    fflush(m_fp);      //强制刷新写入流缓冲区
    m_mutex.unlock();
}

// 异步处理：从阻塞队列中取出字符串，写入文件
void* log::async_write_log() {
    string single_log;
    while(m_log_queue->pop(single_log)) {
        m_mutex.lock();
        fputs(single_log.c_str(), m_fp);
        m_mutex.unlock();
    }
    return nullptr;
}

log* log::get_instance() {
    static log instance;        
    return &instance;
}

