#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>

using namespace std;

void signal_handler(int signal_num) {      // 定时处理函数，SIGALRM信号为(int)14
    cout << "signal = " << signal_num << endl;
    alarm(2);                       // 2秒后给当前进程发SIGALRM信号，触发SIGALRM绑定的函数
}

int main() {
    signal(SIGALRM, signal_handler);
    alarm(2);
    int num = 0;
    while(1) {
        sleep(1);
        printf("计时:%d\n", ++num);
    }
    return 0;
}



