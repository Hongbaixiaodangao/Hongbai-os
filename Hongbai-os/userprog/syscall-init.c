// 内核调用本文件
#include "./syscall-init.h"
#include "../lib/stdint.h"
#include "../lib/user/syscall.h"
#include "../thread/thread.h"
#include "../lib/kernel/print.h"
#include "../device/console.h"
#include "../lib/string.h"

#define syscall_nr 32 // 最大支持的子功能个数
typedef void *syscall;
syscall syscall_table[syscall_nr];

/*返回当前任务的pid*/
uint32_t sys_getpid(void)
{
    return running_thread()->pid;
}

/*打印字符串str，返回strlen*/
uint32_t sys_wirte(char *str)
{
    console_put_str(str);
    return strlen(str);
}

/*void *sys_malloc(uint32_t size)
{
    return sys_malloc(size);
}

void sys_free(void *ptr)
{
    sys_free(ptr);
}*/

/*初始化系统调用*/
void syscall_init(void)
{
    put_str("syscall_init start\n");
    syscall_table[SYS_GETPID] = sys_getpid;
    syscall_table[SYS_WRITE] = sys_wirte;
    syscall_table[SYS_MALLOC] = sys_malloc;
    syscall_table[SYS_FREE] = sys_free;
    put_str("syscall_init done\n");
}