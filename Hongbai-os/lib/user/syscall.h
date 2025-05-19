//用户进程调用本文件
#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H
#include "../stdint.h"

enum SYSCALL_NR
{
    SYS_GETPID,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE
};
uint32_t getpid(void);     // 获取任务pid
uint32_t write(char *str); // 打印字符串并返回字符串长度
void *malloc(uint32_t size);
void free(void *ptr);

#endif