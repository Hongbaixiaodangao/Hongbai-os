
#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H
#include "../lib/stdint.h"
#include "../thread/thread.h"
#include "../thread/sync.h"
//定义缓冲区大小
#define bufsize 64

/*环形队列*/
struct ioqueue{
    struct lock lock;
    //生产者，缓冲区不满时向其中存入数据，满的时候在缓冲区上睡眠
    struct task_struct* producer;
    //消费者，缓冲区不空时向其中取出数据，空的时候在缓冲区上睡眠
    struct task_struct* consumer;

    char buf[bufsize];  //缓冲区大小
    int32_t head;       //队首，数据写入节点
    int32_t tail;       //队尾，数据读出节点
};
void ioqueue_init(struct ioqueue* ioq);
bool ioq_full(struct ioqueue* ioq);
bool ioq_empty(struct ioqueue* ioq);
char ioq_getchar(struct ioqueue* ioq);
void ioq_putchar(struct ioqueue* ioq,char byte);
#endif