#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

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
#include <sys/uio.h>
#include <sys/sendfile.h>
#include <string>
#include <iostream>
#include <map>

#include "../lock/locker.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"
#include "../SQL/sql_connection_pool.h"

#define Threshold_sf 100000             // sendfile发送次数的阈值

using namespace std;

// 定义http响应的状态信息
// const char* ok_200_title = "OK";
// const char* error_400_title = "Bad Request";
// const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
// const char* error_403_title = "Forbidden";
// const char* error_403_form = "You do not have permission to get file from this server.\n";
// const char* error_404_title = "Not Found";
// const char* error_404_form = "The requested file was not found on this server.\n";
// const char* error_500_title = "Internal Error";
// const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 定义根目录
// const char* doc_root = "./var/www/html";

class http_conn
{
public:
    static const int FILENAME_LEN = 200;          // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;     // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;    // 写缓冲区大小
    // http请求方法
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };  
    /* 解析客户请求时，主状态机状态列表:
    CHECK_STATE_REQUESTLINE: 表示正在分析请求行
    CHECK_STATE_HEADER: 表示正在分析头部字段
    CHECK_STATE_CONTENT: 表示正在分析消息体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};   
    /* 服务器处理HTTP请求的可能结果:
    NO_REQUEST: 表示请求不完整，需要读取客户数据；
    GET_REQUEST: 表示获得一个完整的客户请求；
    BAD_REQUEST: 表示客户请求有语法错误；
    NO_RESOURCE: 表示没有找到客户端所要的资源；
    FORBIDDEN_REQUEST: 表示客户对资源没有足够的访问权限；
    FILE_REQUEST: 表示已经获取到文件；
    INTERNAL_ERROR: 表示服务器内部错误；
    CLOSED_CONNECTION: 表示客户端已经关闭连接了；
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    /* 解析客户请求时，从状态机状态列表，即行读取状态:
    LINE_OK: 表示读取出完整的一行
    LINE_BAD: 表示行出错
    LINE_OPEN: 表示行数据不完整
    */
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn(){}
    ~http_conn(){}

public:
    void init(int sockfd, const sockaddr_in& addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname);    // 初始化新接受的连接
    void close_conn( bool real_close = true );           // 关闭连接
    void process();                                      // 处理客户请求
    bool read();                                         // 非阻塞读操作
    bool write();                                        // 非阻塞写操作，使用writev发送数据
    bool m_write();                                      // 使用sendfile发送文件
    sockaddr_in *get_address() {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);

private:
    void init();                            // 初始化连接
    HTTP_CODE process_read();               // 解析HTTP请求
    bool process_write( HTTP_CODE ret );    // 填充HTTP应答

    // 解析HTTP请求函数组
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    // 填充HTTP应答函数组
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;         // socket的所有事件都被注册到这个静态的epoll中
    static int m_user_count;      // 统计用户的数量

private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[ READ_BUFFER_SIZE ];        // 读缓冲区
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;                           // 当前解析的行的起始位置
    char m_write_buf[ WRITE_BUFFER_SIZE ];      // 写缓冲区
    int m_write_idx;                            // 写缓冲区中待发送的字节数
    
    int bytes_to_send;                          // 要发送的字节数
    int bytes_have_send;                        // 已经发送的字节数


    CHECK_STATE m_check_state;    // 主状态机当前的状态
    METHOD m_method;              // http请求方法

    char m_real_file[ FILENAME_LEN ];    // 客户请求的目标文件的完整路径
    char* m_url;                         // 客户请求的目标文件的文件名
    char* m_version;                     // HTTP版本
    char* m_host;                        // 主机名
    int m_content_length;                // HTTP请求的消息长度
    bool m_linger;                       // HTTP请求是否要求保持连接

    char* m_file_address;                // 客户请求的目标文件被mmap到内存中的起始位置
    int m_filefd;                        // 目标文件描述符
    struct stat m_file_stat;             // 目标文件的状态
    struct iovec m_iv[2];                // writev写操作,头部 + 响应内容
    int m_iv_count;                      // 被写的内存块的数量

public:
    int timer_flag;                      // Reactor模式中定时器定时器标志位
    int improv;                          // Reactor模式中工作线程读写完成标志位
    int m_state;                         // 读为0，写为1
    int cgi;                             // 是否采用post传输方式
    char *doc_root;                      // 根目录
    char *m_string;                      // post方式，存储请求头数据

    int m_TRIGMode;
    int m_close_log;

    MYSQL *mysql;
    map<string, string> m_users;
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
