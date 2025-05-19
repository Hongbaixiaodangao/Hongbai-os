
#ifndef __USERPROG_PROCESS_H
#define __USERPROG_PROCESS_H
#include "../thread/thread.h"

/*关于下面这个宏定义的取值，参考P511
 *我们模仿c程序的内存分布，用户进程虚拟地址从高到低分别是：
 *命令行参数和环境变量、栈、堆、bss、data、text*/
#define USER_STACK3_VADDR (0xc0000000 - 0x1000)
#define USER_VADDR_START 0x8048000 // 用户进程起始虚拟地址
#define default_prio 31 // 临时定义
extern void intr_exit(void); // 相关实现在kernel.s里，是中断返回程序

void start_process(void *filename_);
void page_dir_activate(struct task_struct *pthread);
void process_activate(struct task_struct *pthread);
uint32_t *create_page_dir(void);
void create_user_vaddr_bitmap(struct task_struct *user_prog);
void process_execute(void *filename, char *name);

#endif