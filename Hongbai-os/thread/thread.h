
#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "../lib/stdint.h"
#include "../lib/kernel/list.h"
#include "../lib/kernel/bitmap.h"
#include "../kernel/memory.h"

#define MAX_FILES_OPEN_PER_PROC 8 // 每个进程最大能同时打开的文件数

/* 自定义通用函数类型，用来承载线程中函数的类型 */
typedef void thread_func(void *);
typedef int16_t pid_t;

/* 进程、线程的状态 */
enum thread_status
{
    TASK_RUNNING, // 运行中
    TASK_READY,   // 就绪
    TASK_BLOCKED, // 阻塞
    TASK_WAITING, // 等待
    TASK_HANGING, // 挂起
    TASK_DIED     // 死亡
};

/***********   中断栈intr_stack   ***********
 * 此结构用于中断发生时保护程序（线程或进程）的上下文环境:
 * 进程或线程被外部中断或软中断打断时，会按照此结构压入上下文
 * 寄存器，intr_exit中的出栈操作是此结构的逆操作
 * 此栈在线程自己的内核栈中位置固定，所在页的最顶端
 ********************************************/
/*进入中断后，在kernel.S中的中断入口程序“intr%1entry”所执行的上下
文保护的一系列压栈操作都是压入了此结构中。*/
struct intr_stack
{
    uint32_t vec_no; // 中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;
    // 虽然pushad把esp也压入，但esp是不断变化的，所以会被popad忽略
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    /* 以下由cpu从低特权级到高特权级时压入 */
    uint32_t err_code; // 错误码
    // eip是指向无参数无返回值的函数的指针
    void (*eip)(void);
    uint32_t cs;
    uint32_t eflags;
    void *esp;
    uint32_t ss;
};

/***********  线程栈thread_stack  ***********
 * 线程自己的栈，用于存储线程中待执行的函数
 * 此结构在线程自己的内核栈中位置不固定，
 * 仅用在switch_to(任务切换)时保存线程环境。
 * 实际位置取决于实际运行情况。
 ******************************************/
struct thread_stack
{
    /*：关于下面四个寄存器，在被调函数运行完之后，这4个寄存器的
    值必须和运行前一样，它必须在自己的栈中存储这些寄存器的值。*/
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    /* 线程第一次执行时，eip指向待调用的函数。其他
    时候，eip指向switch_to后新任务的返回地址 */
    void (*eip)(thread_func *func, void *func_arg);

    /* 以下函数，仅供线程第一次被调度到cpu时使用 */
    void(*unused_retaddr); // 未使用的返回地址，目前仅起到占位作用
    thread_func *function; // kernel_thread内核线程要执行的函数名
    void *func_arg;        // kernel_thread内核线程要执行的函数的参数
};

/* 线程或进程的pcb程序控制块 */
struct task_struct
{
    uint32_t *self_kstack;     // 线程自己的栈的栈顶指针
    pid_t pid;                 // 线程的pid，系统调用部分对它进行操作
    enum thread_status status; // 线程的状态
    uint8_t priority;          // 线程的优先级
    uint8_t ticks;             // 线程的时间片，在处理器上运行的时间滴答数
    uint32_t elapsed_ticks;    // 线程的运行时间，也就是这个线程已经执行了多久
    char name[16];             // 线程的名字

    int32_t fd_table[MAX_FILES_OPEN_PER_PROC]; // 文件描述符数组

    struct list_elem general_tag;  // 用于线程在一般队列中的节点
    struct list_elem all_list_tag; // 用于线程在thread_all_list队列中的节点

    uint32_t *pgdir;                              // 如果是进程，这是进程的页表结构中页目录表的虚拟地址，线程则置为NULL
    struct virtual_addr userprog_vaddr;           // 用户进程的虚拟地址，后续转化为物理地址后存入cr3寄存器
    struct mem_block_desc u_block_desc[DESC_CNT]; // 进程内存块描述符数组，用于用户进程的堆内存管理
    uint32_t stack_magic;                         // 线程栈的魔数，边界标记，用来检测栈溢出
};

struct task_struct *running_thread(void);                                                     // 获取当前线程的pcb指针
void kernel_thread(thread_func *function, void *func_arg);                                    // 内核线程的入口函数
void thread_create(struct task_struct *pthread, thread_func function, void *func_arg);        // 初始化线程栈
void init_thread(struct task_struct *pthread, char *name, int prio);                          // 初始化线程基本信息
struct task_struct *thread_start(char *name, int prio, thread_func function, void *func_arg); // 创建一个优先级为prio的线程，线程名是name，执行的函数是function(func_arg)
void make_main_thread(void);                                                                  // 创建主线程
void schedule(void);                                                                          // 线程调度函数
void thread_init(void);                                                                       // 线程初始化函数
void thread_block(enum thread_status stat);                                                   // 线程阻塞函数
void thread_unblock(struct task_struct *pthread);                                             // 线程解除阻塞函数
void ready_list_len(void);
void all_list_len(void);
void thread_yield(void);

struct task_struct *main_thread; // 主线程pcb
struct list thread_ready_list;   // 就绪线程队列
struct list thread_all_list;     // 所有线程队列
#endif