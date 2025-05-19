
#include "./ioqueue.h"
#include "../kernel/interrupt.h"
#include "../lib/stdint.h"
#include "../kernel/debug.h"

/*初始化io队列*/
void ioqueue_init(struct ioqueue *ioq)
{
    lock_init(&ioq->lock);
    ioq->producer = ioq->consumer = NULL;
    ioq->head = ioq->tail = 0;
}

/*返回pos在缓冲区的下一个位置的值*/
static int32_t next_pos(int32_t pos)
{
    return (pos + 1) % bufsize;
}

/*判断队列（缓冲区）是否已满*/
bool ioq_full(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return next_pos(ioq->head) == ioq->tail;
}

/*判断队列（缓冲区）是否为空*/
bool ioq_empty(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return ioq->head == ioq->tail;
}

/*使当前的消费者或者生产者在缓冲区上等待
 *关于下面两个二重指针，第一重是本身的指针，第二重是指向结构体的指针
 *所以第一次解引用后，他就是消费者或者生产者，类型是task_struct* */
static void ioq_wait(struct task_struct **waiter)
{
    ASSERT(waiter != NULL && *waiter == NULL);
    *waiter = running_thread();
    thread_block(TASK_BLOCKED);
}

/*唤醒waiter线程*/
static void wake_up(struct task_struct **waiter)
{
    ASSERT(*waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}

/*消费者从缓冲区读一个字节的字符
 *从队尾读取
 *判断是否为空->读取字符->唤醒生产者*/
char ioq_getchar(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    while (ioq_empty(ioq))
    {                             // 只要缓冲区为空，就要一直阻塞消费者线程
        lock_acquire(&ioq->lock); // 保证原子操作，因为涉及到了公共的指针资源
        ioq_wait(&ioq->consumer); // 取地址后对齐二重指针
        lock_release(&ioq->lock);
    }
    char byte = ioq->buf[ioq->tail];
    ioq->tail = next_pos(ioq->tail); // 不能直接ioq++，可能导致溢出，需要取模构成环形
    if (ioq->producer != NULL)
    {
        wake_up(&ioq->producer); // 为什么唤醒不需要保证是原子操作？
    }
    return byte;
}

/*生产者向缓冲区写入一个字节的字符
 *从队首写入
 *判断是否为满->写入字符->唤醒消费者*/
void ioq_putchar(struct ioqueue *ioq, char byte)
{
    ASSERT(intr_get_status() == INTR_OFF);
    while (ioq_full(ioq))
    {
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->producer);
        lock_release(&ioq->lock);
    }
    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);
    if (ioq->consumer != NULL)
    {
        wake_up(&ioq->consumer);
    }
}