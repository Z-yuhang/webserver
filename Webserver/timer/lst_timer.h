#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#include "../log/log.h"
#include "../http/http_conn.h"

#define BUFFER_SIZE 64

//前向声明: 基于双向链表实现的定时器
class util_timer;

//连接资源，每个连接的客户端绑定一个定时器
struct client_data {
    sockaddr_in address;    //连接地址
    int sockfd; 
    util_timer *timer;      //指向连接对应的定时器
    char buffer[BUFFER_SIZE];
};

// 定时器类(节点): 基于双向链表实现的定时器
class util_timer {
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;               // 超过时间
    //回调函数:从内核事件表删除事件，关闭文件描述符，释放连接资源
    //定义函数指针，使用时指向要使用的函数
    void (*cb_func)(client_data*);
    client_data* user_data;      // 连接的客户端   
    util_timer *prev;            //前向定时器
    util_timer *next;            //后继定时器
};


// 定时器容器类: 升序排列
class timerList {
public:
    timerList();
    ~timerList();

    void add_timer(util_timer* timer);        // 添加定时器 
    void del_timer(util_timer* timer);        // 删除定时器
    void adjust_timer(util_timer* timer);     // 调整定时器，任务发生变化时，调整定时器在链表中的位置
    void tick();                              // 定时器任务处理函数

private:
    util_timer* head;                          // 头节点
    util_timer* tail;                          // 尾节点
    // 被公有成员add_timer和adjust_time调用，用于调整链表的内部结构
    void add_timer(util_timer* timer, util_timer* lst_timer);   
};


// 设置定时器
class Utils {
public:
    Utils() {};
    ~Utils() {};
    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
//使用管道通知主循环执行定时器链表的任务
    //逻辑顺序，设置信号后，触发时调用信号处理函数，信号处理函数通过管道将sig发送到主循环
    //主循环通过管道接收sig，得知有定时器超时，再调用定时器处理任务函数timer_handler()处理，并且再此设定ALARM信号触发，形成循环
    static int *u_pipefd;
    timerList m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

//定时器回调函数
void cb_func(client_data *user_data);

#endif