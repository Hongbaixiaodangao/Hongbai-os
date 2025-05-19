// 内核的入口函数
#include "init.h"
#include "interrupt.h"
#include "../lib/stdio.h"
#include "../thread/thread.h"
#include "../userprog/process.h"
#include "../lib/user/syscall.h"
#include "../userprog/syscall-init.h"
// 本章测试头文件
#include "../fs/fs.h"

void k_thread_a(void);
void k_thread_b(void);
void u_prog_a(void);
void u_prog_b(void);
int main(void)
{
    init_all(); // 初始化所有模块
    printk("HongBai's OS kernel\n");
    intr_enable();
    sys_open("/file1",O_CREAT);
    sys_open("/hongbai",O_CREAT);
    // process_execute(u_prog_a, "user_prog_a");
    // process_execute(u_prog_b, "user_prog_b");
    // thread_start("k_thread_a", 31, k_thread_a, "argA: ");
    // thread_start("k_thread_b", 31, k_thread_b, "argB: ");
    while (1)
    {
    };
}

void k_thread_a(void)
{
    printk("This is kernel_thread_a");
    while (1)
    {
    };
}

void k_thread_b(void)
{
    printk("This is kernel_thread_b");
    while (1)
    {
    };
}

void u_prog_a(void)
{
    printf("This is user_prog_a");
    while (1)
    {
    };
}

void u_prog_b(void)
{
    printf("This is user_prog_b");
    while (1)
    {
    };
}