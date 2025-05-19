
#include "./sync.h"
#include "./thread.h"
#include "../kernel/interrupt.h"
#include "../kernel/debug.h"
/*初始化信号量psema*/
void sema_init(struct semaphore *psema, uint8_t value)
{
    psema->value = value;
    list_init(&psema->waiters);
}

/*初始化锁plock*/
void lock_init(struct lock *plock)
{
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_init(&plock->semaphore, 1);
}

/*信号量down操作（V操作）
1. 判断信号量是否大于0。
2. 若信号量大于0，则将信号量减1。
3. 若信号量等于0，当前线程将自己阻塞，以在此信号量上等待。*/
void sema_down(struct semaphore *psema)
{
    // 关中断保证操作的原子性
    enum intr_status old_status = intr_disable();
    // 使用while可以反复判断目前有没有锁，如果使用if只会判断一次，可能导致错误
    while (psema->value == 0)
    { // 此时锁被别人持有，其他的线程应该阻塞
        // 目前的线程还不应该在信号量的等待队列里
        ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag));
        // 如果因为意外出现在等待队列
        if (elem_find(&psema->waiters, &running_thread()->general_tag))
        {
            PANIC("sema_down: thread blocked has been in waiters list\n");
        }
        // 正常情况下
        list_append(&psema->waiters, &running_thread()->general_tag); // 把当前队列加入到等待队列
        thread_block(TASK_BLOCKED);                                   // 当前线程状态变为阻塞
    }

    // 当目前value==1，线程可以被唤醒并的获得锁时，会执行下面的代码
    psema->value--;
    ASSERT(psema->value == 0);
    intr_set_status(old_status);
}

/*信号量的up操作（p操作）
1. 将信号量的值加1。
2. 唤醒在此信号量上等待的线程。*/
void sema_up(struct semaphore *psema)
{
    // 关中断保证操作的原子性
    enum intr_status old_status = intr_disable();
    ASSERT(psema->value == 0);
    if (!list_empty(&psema->waiters))
    {
        struct task_struct *thread_blocked = (struct task_struct *)((uint32_t)list_pop(&psema->waiters) & (0xfffff000));
        thread_unblock(thread_blocked);
    }
    // 当value==0时执行下面的代码
    psema->value++;
    ASSERT(psema->value == 1);
    intr_set_status(old_status);
}

/*获取锁plock
 *如果目前的线程持有锁，将申请次数+1,避免死锁
 *如果目前线程没有锁，设置目前的信号量为0,然后申请锁*/
void lock_acquire(struct lock *plock)
{
    if (plock->holder == running_thread())
    {
        plock->holder_repeat_nr++;
    }
    else
    {
        sema_down(&plock->semaphore);
        plock->holder = running_thread();
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    }
}

/*释放锁plock
 *如果锁的申请次数大于1,次数减一
 *如果锁的申请次数等于1,将锁的持有者置空，然后信号量+1*/
void lock_release(struct lock *plock)
{
    ASSERT(plock->holder == running_thread());
    if (plock->holder_repeat_nr > 1)
    {
        plock->holder_repeat_nr--;
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1);
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);
}