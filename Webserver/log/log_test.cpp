#include "log.h"
#include <unistd.h>

// 同步模式
void syn_mode() {
    int m_close_log = 0;
    log::get_instance()->init("./test_log", 0, 60);
    LOG_DEBUG("%s", "debug test");
    LOG_INFO("%d, %s\n", 22, "abc");
}

// 异步模式
int m_close_log = 0;
static int count = 0;
locker mutex;

void* log_info(void *arg) {
    while (1) {
        usleep(1000);                    // 微秒级
        mutex.lock();
        LOG_INFO("INFO: %d", ++count);
        mutex.unlock();
    }
}

void* log_warn(void *arg) {
    while (1) {
        usleep(1000);
        mutex.lock();
        LOG_WARN("WARN: %d", ++count);
        mutex.unlock();
    }
}

// 异步模式
void async_mode() {
    log::get_instance()->init("./log_info", 0, 60, 800, 20);
    pthread_t info, warn;
    pthread_create(&info, NULL, log_info, NULL);
    pthread_create(&warn, NULL, log_warn, NULL);
    sleep(1);                                       // 以秒为单位
    pthread_cancel(info);
    pthread_cancel(warn);
}

int main() {
    // syn_mode();                                     // 同步模式
    async_mode();                                   // 异步模式
    return 0;
}