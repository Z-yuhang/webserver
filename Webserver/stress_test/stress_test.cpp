#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <iostream>

using namespace std;

// 向服务器发送请求
static const char* request = "GET http://localhost/readme.html HTTP/1.1\r\nConnection: keep-alive\r\n\r\nxxxxxxxxxxxx";

int setnonblocking(int fd ) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epoll_fd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLOUT | EPOLLET | EPOLLERR;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 向服务器写数据，长度为len
bool write_nbytes(int sockfd, const char* buffer, int len) {
    int bytes_write = 0;
    printf("write out %d bytes to socket %d\n", len, sockfd);
    while(true) {
        bytes_write = send(sockfd, buffer, len, 0);
        if(bytes_write == -1) {
            return false;
        }
        else if(bytes_write == 0) {
            return false;
        }
        len -= bytes_write;
        buffer += bytes_write;
        if(len <= 0) {
            return true;
        }
    }
}

// 从服务器读取数据
bool read_bytes(int sockfd, char* buffer, int len) {
    int bytes_read = 0;
    memset(buffer, '\0', len);
    bytes_read = recv(sockfd, buffer, len, 0);
    if(bytes_read == -1 || bytes_read == 0) {
        return false;
    }
    printf("read in %d bytes from socket %d with content: %s\n", bytes_read, sockfd, buffer);
    return true;
}

// 像服务器发送num个连接请求
void start_conn(int epollfd, int num, const char* ip, int port) {
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    for(int i = 0; i < num; i++) {
        sleep(1);
        int sockfd = socket(PF_INET, SOCK_STREAM, 0);
        cout << "creat 1 sock" << endl;
        if(sockfd < 0) {
            continue;
        }
        if(connect(sockfd, (struct sockaddr*)&address, sizeof(address)) == 0) {
            cout << "build connection " << i << endl;
            addfd(epollfd, sockfd);
        }
    }
}

// 关闭连接
void close_conn(int epollfd, int sockfd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0);
    close(sockfd);
}

int main() {
    int epollfd = epoll_create(100);
    start_conn(epollfd, 20, "192.168.3.223", 12345);
    epoll_event events[10000];
    char buffer[2048];

    while(true) {
        int nums = epoll_wait(epollfd, events, 10000, 2000);
        for(int i = 0; i < nums; i++) {
            int sockfd = events[i].data.fd;
            if(events[i].events & EPOLLIN) {             // 可读事件
                if(!read_bytes(sockfd, buffer, 2048)) {
                    close_conn(epollfd, sockfd);
                }
                struct epoll_event event;
                event.events = EPOLLOUT | EPOLLET | EPOLLERR;
                event.data.fd = sockfd;
                epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
            }
            else if(events[i].events & EPOLLOUT) {      // 可写事件
                if(!write_nbytes(sockfd, request, strlen(request))) {
                    close_conn(epollfd, sockfd);
                }
                struct epoll_event event;
                event.events = EPOLLIN | EPOLLET | EPOLLERR;
                event.data.fd = sockfd;
                epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
            }
            else if(events[i].events & EPOLLERR) {
                close_conn(epollfd, sockfd);
            }
        }
    }
    return 0;
}