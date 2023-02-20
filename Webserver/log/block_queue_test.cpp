#include "block_queue.h"
#include<unistd.h>
#include<stdio.h>
//测试阻塞队列
block_queue<int> b_que(4);

void* pop_thread(void* arg) {
    printf("start pop1\n");
    int num = 0;
    while(1) {
        int temp = 20;
        sleep(1);
        if(b_que.pop(temp, 100)) {            // 超时处理
            printf("pop1: %d\n", temp);
        }
        printf("pop1运行次数: %d\n", ++num);
    }
    return NULL;
}

void* pop_thread_t(void* arg) {
    printf("start pop2\n");
    int num = 0;
    while(1) {
        int temp = 20;
        sleep(1);
        if(b_que.pop(temp, 100)) {           // 超时处理
            printf("pop2: %d\n", temp);
        }
        printf("pop2运行次数: %d\n", ++num);
    }
    return NULL;
}

int main() {
    //创建线程
    printf("开始\n");
    pthread_t pthread_out, pthread_out_t;
    int ret = 0;

    ret = pthread_create(&pthread_out, NULL, pop_thread, NULL);
    if(ret != 0) {
        printf("pop1线程创建失败\n");
    }
    else {
        printf("pop1线程创建成功\n");
    }
    ret = pthread_create(&pthread_out_t, NULL, pop_thread_t, NULL);
    if(ret != 0) {
        printf("pop2线程创建失败\n");
    }
    else {
        printf("pop2线程创建成功\n");
    }
    sleep(1);
    b_que.push(40);
    b_que.push(30);
    sleep(4);          // 延时4s
    //pthread_cancel(pthread_out);    //不主动取消线程，会进入阻塞状态，导致终端卡住
    //pthread_cancel(pthread_out_t);
    return 0;
}