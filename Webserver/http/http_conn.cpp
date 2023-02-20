#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;

// 将文件描述符fd设为非阻塞
int setnoblocking(int fd) {
    int old_option = fcntl(fd, F_GETFD);         // 获取旧的状态标志
    int new_option = old_option | O_NONBLOCK;    // 将标志设置为非阻塞
    fcntl( fd, F_SETFL, new_option );            
    return old_option;                           // 返回旧的状态标志
}

// 将文件描述符fd的事件添加到epollfd中
void addfd(int epollfd, int fd, bool one_shot, int TRIGmode) {
    epoll_event event;
    event.data.fd = fd;
    if(TRIGmode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;     // 读事件、ET模式、对方关闭TCP连接
    else
        event.events = EPOLLIN | EPOLLRDHUP;
    // 针对connfd，开启EPOLLONESHOT，每个socket在任意时刻都只被一个线程处理 
    if(one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);     // 添加事件
    setnoblocking(fd);
}

// 在epolled中删除fd
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改fd的注册事件
void modfd( int epollfd, int fd, int ev, int TRIGmode) {
    epoll_event event;
    event.data.fd = fd;
    if(TRIGmode == 1)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 对静态成员变量的初始化(在类外初始化)
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 初始化新接受的连接
void http_conn::init(int sockfd, const sockaddr_in& addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname) {     
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
} 

// 初始化变量
void http_conn::init() {   
    mysql = NULL;              
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_write_idx = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_state = 0;
    improv = 0;
    timer_flag = 0;
    cgi = 0;

    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;
    m_content_length = 0;
    m_linger = false;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN );
}

// 关闭连接
void http_conn::close_conn( bool real_close) {
    if(real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_user_count--;
        m_sockfd = -1;
    }
}

// 解析http请求:从状态机
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for(; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r') {
            if((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;                         // 读取到完整一行
            }
            return LINE_BAD;
        }
        else if(temp == '\n') {
            if((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
            {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;                        // 读取到完整一行        
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析http请求:请求行
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    m_url = strpbrk(text, " \t");     // 查询text中第一个空格或第一个tab键
    if(!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0'; 
    char* method = text;
    if(strcasecmp(method, "GET") == 0) {   // 字符串比较
        m_method = GET;
    }
    else if(strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    }
    else {
        return BAD_REQUEST;
    }
    m_url += strspn(m_url, " \t");           // 防止中间有多个空格出现
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");   // 防止中间有多个空格出现
    if(strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }    
    // 检测m_url是否合法
    if (strncasecmp(m_url, "http://", 7) == 0 ) {
        m_url += 7;
        m_url = strchr( m_url, '/' );        // 在m_url中寻找第一次出现 / 的位置，根的位置
    }
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    if(strlen(m_url) == 1) {                //当url为/时，显示判断界面
        strcat(m_url, "judge.html");
    }
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求:头部字段
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    if(text[0] == '\0') {              // 遇到空行说明头部字段分析完毕
        if(m_content_length != 0) {    // 如果有消息体
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;            // http请求分析已经结束
    }
    // Connection字段
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    // Connect-Length字段
    else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    // Host字段
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        LOG_INFO("oop!unknow header: %s", text);
        std::cout << "oop! unknow header" << text << std::endl;
    }

    return NO_REQUEST;
}

// 解析http请求:消息体
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        m_string = text;                         // POST请求中为输入的用户名和密码
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    // 从buffer中取出所有完整的行
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        // cout << "goto while" << endl;
        text = get_line();
        m_start_line = m_checked_idx;      // 下一行的起始位置
        LOG_INFO("%s", text);
        std::cout << "get one http line: " << text << std::endl;
        
        // 主状态机状态
        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}              

// 确认http请求后，寻找文件所在的位置
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/');         // 搜索字符

    // 处理cgi
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        char flag = m_url[1];                   //根据标志判断是登录事件还是注册事件
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // 用户名密码的格式：user=123&password=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        // 通过m_url定位‘/’所在位置，根据‘/’后的第一个字符判断是登录事件还是注册事件
        // 注册事件
        if(*(p + 1) == '3') {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 判断map中能否找到重复的用户名
            if (users.find(name) == users.end()) {
                m_lock.lock();                             // 操作数据库，需要加锁
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();
                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        // 登录事件
        else if(*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // 页面跳转：通过m_url定位‘/’所在位置，根据‘/’后的第一个字符，使用分支语句实现页面跳转
    if (*(p + 1) == '0') {                                             // 注册页面
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '1') {                                        // 登录页面
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '5') {                                        // 图片页面
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '6') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);         // 视频页面
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '7') {                                        // 关注页面
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else {                                                             // url实际请求页面
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    if(stat(m_real_file, &m_file_stat) < 0) {       // 获取文件信息，保存到m_file_stat中
        cout << m_real_file << endl;
        return NO_RESOURCE;
    }
    if (!( m_file_stat.st_mode & S_IROTH)) {        // 文件是否可读  
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR( m_file_stat.st_mode)){             // 文件是否为目录文件 
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);           // 打开文件，只读
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);   // 将文件内容映射到内存(进程的地址空间)
    close(fd);
    return FILE_REQUEST;
}


// 在内存中取消文件的映射
void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = NULL;
    }
}

// 非阻塞读操作，循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    // LT读数据
    if (0 == m_TRIGMode) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;
        if (bytes_read <= 0) {
            return false;
        }
        return true;
    }
    //ET读数据
    else {
        while (true) {                 // 循环读取数据
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 非阻塞写操作
bool http_conn::write() {
    int temp = 0;

    if(bytes_to_send == 0) {             // 没有要发送的数据
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);           
        init();
        return true;
    }

    // 发送数据
    while(true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);     
        if(temp < 0) {                    // 写失败
            if(errno == EAGAIN) {           // TCP缓存没有空间:等待下一轮EPOLLOUT事件(数据可写)
                cout << "TCP缓存没有空间" << endl;
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); 
                return true;         
            }
            unmap();
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        
        // 解决大文件传输失败的问题
        if(bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_to_send <= 0) {    
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            if(m_linger) {                       // 保持连接
                init();
                // modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); 
                return true;
            }
            else {
                // modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
                return false;
            }
        }
    }
}

// 以sendfile方式传输文件，在此之前要取消文件的映射
bool http_conn::m_write() {
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    int temp = 0;

    if(bytes_to_send == 0) {             // 没有要发送的数据
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);           
        init();
        return true;
    }

    while(true) {                       // 发送http响应头
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= 1) {
            if(errno == EAGAIN) {           // TCP缓存没有空间:等待下一轮EPOLLOUT事件(数据可写)
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); 
                return true;         
            }
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if(bytes_to_send <= 0) {        // http响应头发送成功
            break;    
        }
    }
    // 发送文件
    m_filefd = open(m_real_file, O_RDONLY);
    cout << "传输的字节数: " << m_file_stat.st_size << endl;

    // sendfile(m_sockfd, m_filefd, NULL, m_file_stat.st_size);

    int num = 0;                          // sendfile发送次数的阈值
    while(true) {  
        int temp = sendfile(m_sockfd, m_filefd, NULL, m_file_stat.st_size);
        num++;
        if(num > Threshold_sf) {  
            break;
        }
    }
    
    close(m_filefd);
    if(m_linger) {
        init();
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return true;
    }
    else {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return false;
    }
}

// 往缓冲区内写入待发送的数据
bool http_conn::add_response(const char* format, ... ) {        // 可变参数函数
    if(m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);    // arg_list为可变参数 
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - m_write_idx - 1, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - m_write_idx - 1)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);              // 停止使用可变参数
    LOG_INFO("request:%s", m_write_buf);
    // cout << "缓冲区写入的数据: " << m_write_idx << endl;    
    return true;
}

// 状态响应
bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_length) {
    // add_content_length(content_length);
    // add_linger();
    // add_blank_line();
    // return true;
    return add_content_length(content_length) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_length) {
    cout << "Content Length: " << content_length << endl;
    return add_response("Content-Length: %d\r\n", content_length);
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

// 填充HTTP应答:根据http请求的结果，确定返回客户端的内容
bool http_conn::process_write(http_conn::HTTP_CODE ret) {
    switch(ret) {
        case INTERNAL_ERROR:            // 内部错误
        {
            cout << "INTERNAL_ERROR" << endl;
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST:               // 客户端请求错误
        {
            cout << "BAD_REQUEST" << endl;
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE:              // 未找到资源
        {   
            cout << "NO_RESOURCE" << endl;
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:       // 文件禁止访问
        {
            cout << "FORBIDDEN_REQUEST" << endl;
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST:            // 文件已获取
        {
            cout << "FILE_REQUEST" << endl;
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else {                    // 文件为空
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) {
                    return false;
                }
            }
        }  
        default:
        {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
} 


// 处理客户的http请求
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {              // 请求不完整
        cout << "请求不完整，继续监听" << endl;
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);  // 继续监听可读事件
        return;
    }  
    bool write_ret = process_write(read_ret);
    if(write_ret == false) {
        cout << "断开连接" << endl;
        close_conn();                         // 断开连接
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);     // 监听可写事件   
}

// 将数据库中的用户名和密码载入到服务器的map中来，map中的key为用户名，value为密码
void http_conn::initmysql_result(connection_pool *connPool) {
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);    
    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    //从结果集中获取下一行，将数据库中的用户名和密码载入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}




