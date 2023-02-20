#include "lst_timer.h"

timerList::timerList() : head(NULL), tail(NULL) {}

timerList::~timerList() {
    util_timer* tmp = head;
    while(tmp) {
        head = head->next;
        delete tmp;
        tmp = head;
    }
}

void timerList::add_timer(util_timer* timer) {
    if(timer == NULL) {
        return;
    }
    if(head == NULL) {
        head = timer;
        tail = timer;
        return;
    }
    if(timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

void timerList::del_timer(util_timer* timer) {
    if(timer == NULL) {
        return;
    }
    if((timer == head) && (timer == tail)) {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if(timer == head) {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if(timer == tail) {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }

    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// 当某个定时任务发生变化时，调整定时器在链表中的位置(只考虑定时时间延长额情况)
void timerList::adjust_timer(util_timer* timer) {
    if(timer == NULL) {
        return;
    }
    util_timer* tmp = timer->next;
    if((tmp == NULL) || timer->expire < tmp->expire) {
        return;
    }

    if(timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else {
        timer->next->prev = timer->prev;
        timer->prev->next = timer->next;
        add_timer(timer, timer->next);
    }
}


void timerList::add_timer(util_timer* timer, util_timer* lst_timer) {
    if(timer == NULL) {
        return;
    }
    util_timer* prev = lst_timer;
    util_timer* tmp = prev->next;
    while(tmp) {
        if(timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    if(tmp == NULL) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

// SIGALRM信号每触发一次，就在信号处理函数中执行一次tick函数，处理链表上的到期任务
void timerList::tick() {
    if(head == NULL){
        return;
    }
    cout << "timer tick" << endl;

    time_t cur = time(NULL);      // 获取系统当前时间
    util_timer* tmp = head;
    while(tmp){
        if(cur < tmp->expire) {
            break;
        }
        tmp->cb_func(tmp->user_data);   // 调用任务回调函数
        head = tmp->next;
        if(head){
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}


void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 信号处理函数：仅仅通过管道发送信号值，不处理信号对应的逻辑，缩短异步执行时间，减少对主程序的影响
void Utils::sig_handler(int sig) {
     //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char*)&msg, 1, 0);         // 将信号值从管道的写端写入，传输的是字符类型
    errno = save_errno;
}

//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;         // 信号处理函数
    if (restart)
        sa.sa_flags |= SA_RESTART;   // 使被信号打断的系统调用自动重新发起
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
     m_timer_lst.tick();
    //最小的时间单位为5s
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = NULL;
int Utils::u_epollfd = 0;

//定时器回调函数: 从内核事件表删除事件，关闭文件描述符，释放连接资源
void cb_func(client_data *user_data) {
     //删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    //删除非活动连接在socket上的注册事件
    close(user_data->sockfd);
    //减少连接数
    http_conn::m_user_count--;
}
