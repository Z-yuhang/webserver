#ifndef WEBSEVER_H
#define WEBSEVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include "../http/http_conn.h"
#include "../threadpool/threadpool.h"

const int MAX_FD = 65536;              //最大文件描述符
const int MAX_EVENT_NUMBER = 10000;    //最大事件数
const int TIMESLOT = 5;                //最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

    // 初始化
    void init(int port, string user, string passWord, string databaseName, int log_write, int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model);
    // 创建线程池
    void creatThreadpool();
    // 初始化数据库
    void sql_pool();
    // 初始化日志系统
    void log_write();
    // 设置epoll的触发模式
    void trig_mode();
    // 开启epoll监听
    void eventListen();
    //初始化定时器
    void timer(int connfd, struct sockaddr_in client_address);
    //调整定时器
    void adjust_timer(util_timer *timer);
    //删除定时器
    void deal_timer(util_timer *timer, int sockfd);
    //http 处理用户数据
    bool dealclinetdata();
    //处理定时器信号,set the timeout ture
    bool dealwithsignal(bool& timeout, bool& stop_server);
    //处理客户连接上接收到的数据
    void dealwithread(int sockfd);
    //写操作
    void dealwithwrite(int sockfd);
    //事件回环（即服务器主线程）
    void eventLoop();

public:
    //基础
    int m_port;
    char *m_root;                  
    int m_log_write;               // 异步日志或同步日志
    int m_close_log;               // 开启或关闭日志
    int m_actormodel;              // Reactor或Proactor

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];
    int m_listenfd;
    int m_OPT_LINGER;             // 是否优雅关闭连接
    int m_TRIGMode;               // 触发模式：Listen + conn
    int m_LISTENTrigmode;         // Listen触发模式
    int m_CONNTrigmode;           // connect触发模式     

    //定时器相关
    client_data *users_timer;     // 连接资源，每个连接的客户端绑定一个定时器
    Utils utils;            

    //数据库相关
    connection_pool *m_connPool;
    string m_user;                 //登陆数据库用户名
    string m_passWord;             //登陆数据库密码
    string m_databaseName;         //使用数据库名
    int m_sql_num;
};

#endif