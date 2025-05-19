
#include "./thread.h"
#include "../lib/string.h"
#include "../kernel/memory.h"
#include "../kernel/interrupt.h"
#include "../kernel/debug.h"
#include "../lib/kernel/print.h"
#include "../userprog/process.h"
#include "./sync.h"

#define PG_SIZE 4096

extern void switch_to(struct task_struct *cur, struct task_struct *next); // 任务切换函数

struct lock pid_lock;            // 分配pid锁，此锁用来在分配pid时实现互斥，避免为不同的任务分配重复的pid
struct task_struct *idle_thread; // idle线程

/*设置系统空闲时运行的线程*/
static void idle(void *arg)
{
    (void)arg; // 显式忽略未使用的参数，相当于UNUSED
    while (1)
    {
        thread_block(TASK_BLOCKED); // 初始为阻塞状态
        // 被唤醒后执行下面的代码
        asm volatile("sti; hlt" : : : "memory");
    }
}

/* 获取当前线程的pcb指针 */
struct task_struct *running_thread(void)
{
    uint32_t esp;
    asm volatile("movl %%esp, %0" : "=g"(esp)); // 获取当前线程的栈顶地址
    /* 取得esp整数部分 */
    return (struct task_struct *)(esp & 0xfffff000); // 返回pcb指针
}

/* 由kernel_thread执行function(func_arg) */
void kernel_thread(thread_func *function, void *func_arg)
{
    intr_enable(); // 开中断,避免后面的时钟中断被屏蔽，而无法调度其他线程
    function(func_arg);
}

/* 分配pid */
static pid_t allocate_pid(void)
{
    static pid_t next_pid = 0; // 用static延长生命周期，确保pid唯一
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}

/* 初始化线程栈thread_stack */
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg)
{
    /* 先预留中断栈空间 */
    pthread->self_kstack -= sizeof(struct intr_stack);
    /* 再预留线程栈空间 */
    pthread->self_kstack -= sizeof(struct thread_stack);
    /* 填充线程栈 */
    struct thread_stack *kthread_stack = (struct thread_stack *)pthread->self_kstack;

    kthread_stack->eip = kernel_thread; // eip指向kernel_thread
    kthread_stack->function = function; // 函数指针
    kthread_stack->func_arg = func_arg; // 函数参数

    kthread_stack->ebp = kthread_stack->ebx =
        kthread_stack->esi = kthread_stack->edi = 0; // ebp, ebx, edi, esi寄存器值
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct *pthread, char *name, int prio)
{
    memset(pthread, 0, sizeof(*pthread)); // 清空线程pcb
    pthread->pid = allocate_pid();        // 获取唯一的pid
    strcpy(pthread->name, name);          // 线程名字
    if (pthread == main_thread)           // 线程状态
        pthread->status = TASK_RUNNING;
    else
        pthread->status = TASK_READY;

    /* self_kstack 是线程自己在内核态下使用的栈顶地址 */
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE); // 线程内核栈

    /*初始化文件描述符数组*/
    // 预留标准输入输出
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    // 剩下的设置为-1,-1表示空位，free_slot
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC)
    {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }

    pthread->priority = prio;          // 线程优先级
    pthread->ticks = prio;             // 线程时间片
    pthread->elapsed_ticks = 0;        // 线程运行时间
    pthread->pgdir = NULL;             // 线程页表
    pthread->stack_magic = 0x20250325; // 线程栈的魔数，边界标记，用来检测栈溢出
}

/* 创建一个优先级为prio的线程，线程名是name，执行的函数是function(func_arg) */
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg)
{
    struct task_struct *thread = get_kernel_pages(1); // 分配1页的内存空间给pcb
    init_thread(thread, name, prio);                  // 初始化线程基本信息
    thread_create(thread, function, func_arg);        // 初始化线程栈

    /* 确保线程不在就绪线程队列 */
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag); // 将线程加入就绪队列
    /* 确保线程不在所有线程队列 */
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag); // 将线程加入所有线程队列
    return thread;
}

/* 将kernel中的main函数完善成主线程 */
void make_main_thread(void)
{
    /* 因为main线程早已运行，
     * 咱们在loader.S中进入内核时的mov esp,0xc0009f00，
     * 就是为其预留pcb的，因此pcb地址为0xc0009e00，
     * 不需要通过get_kernel_page另分配一页*/
    main_thread = running_thread();                                   // 获取主线程pcb
    init_thread(main_thread, "main", 31);                             // 初始化主线程基本信息
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag)); // 确保主线程不在就绪队列
    list_append(&thread_all_list, &main_thread->all_list_tag);        // 将主线程加入就绪队列
}

/* 实现任务调度 */
void schedule(void)
{
    ASSERT(intr_get_status() == INTR_OFF);      // 确保中断关闭
    struct task_struct *cur = running_thread(); // 获取当前线程pcb

    if (cur->status == TASK_RUNNING)
    {                                                              // 如果当前线程是运行状态
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag)); // 确保当前线程不在就绪队列
        list_append(&thread_ready_list, &cur->general_tag);        // 将当前线程加入就绪队列
        cur->status = TASK_READY;                                  // 设置当前线程状态为就绪
        cur->ticks = cur->priority;                                // 重置时间片
    }
    else
    {
    }
    if (list_empty(&thread_ready_list))
    {
        thread_unblock(idle_thread); // 如果就绪队列为空，唤醒idle线程
    }
    struct task_struct *thread_tag = (struct task_struct *)list_pop(&thread_ready_list); // 从就绪队列中取出一个线程节点
    struct task_struct *next = (struct task_struct *)((uint32_t)thread_tag & 0xfffff000);

    next->status = TASK_RUNNING; // 设置下一个线程状态为运行
    process_activate(next);      // 激活任务页表
    switch_to(cur, next);        // 任务切换
}

/*当前线程将自己阻塞，目前的状态变为stat
 *stat的取值是blocked、waiting、hanging*/
void thread_block(enum thread_status stat)
{
    // 先检验stat是否处于这三种状态
    ASSERT(stat == TASK_BLOCKED || stat == TASK_WAITING || stat == TASK_HANGING);
    enum intr_status old_status = intr_disable();

    struct task_struct *cur_thread = running_thread();
    cur_thread->status = stat; // 修改目前正在运行的线程的pcb的状态

    schedule(); // 目前已经不再是运行态，不会重置时间片

    intr_set_status(old_status); // schedule-swich后，才会执行
}

/*锁的拥有者，将名为pthread的线程解除阻塞*/
void thread_unblock(struct task_struct *pthread)
{
    enum intr_status old_status = intr_disable();
    ASSERT((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING));
    if (pthread->status != TASK_READY)
    {
        // 当前线程不应该在就绪队列里
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        // 如果线程因为某些特殊原因还在就绪队列里
        if (elem_find(&thread_ready_list, &pthread->general_tag))
        {
            PANIC("thread_unblock:block thread in ready list");
        }
        // 正常情况下，即不在就绪队列里
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}

/*主动让出CPU，切换其他线程运行*/
void thread_yield(void)
{
    struct task_struct *cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

/* 初始化线程环境 */
void thread_init(void)
{
    put_str("thread_init start\n");
    list_init(&thread_ready_list); // 初始化就绪线程队列
    list_init(&thread_all_list);   // 初始化所有线程队列
    lock_init(&pid_lock);          // 初始化pid锁
    make_main_thread();            // 创建主线程
    // 创建idle线程
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init done\n");
}

void ready_list_len()
{ // 测试就绪队列是否为空，输出就绪队列的元素数
    put_int(list_len(&thread_ready_list));
    put_str("\n");
}

void all_list_len()
{
    put_int(list_len(&thread_all_list));
    put_str("\n");
}
