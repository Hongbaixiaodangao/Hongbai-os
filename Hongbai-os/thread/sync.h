
#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "../lib/stdint.h"
#include "../lib/kernel/list.h"
#include "./thread.h"

struct semaphore
{                        // 信号量结构体，包含value、waiters两个成员
    uint8_t value;       // 信号量的值，是1时允许申请锁，0说明锁被占用
    struct list waiters; // 用来记录在此信号量上等待的线程
};

struct lock
{                               // 锁结构体，包括semaphore、holder、holder_repeat_nr三个成员
    struct semaphore semaphore; // 实现锁的结构是二元信号量
    struct task_struct *holder; // 锁目前的持有者
    uint32_t holder_repeat_nr;  // 锁的持有者重复申请锁的次数
};
void sema_init(struct semaphore *psema, uint8_t value);
void lock_init(struct lock *plock);
void sema_down(struct semaphore *psema);
void sema_up(struct semaphore *psema);
void lock_acquire(struct lock *plock);
void lock_release(struct lock *plock);
#endif