#ifndef __USERPROG_SYSCALL_INIT_H
#define __USERPROG_SYSCALL_INIT_H
#include "../lib/stdint.h"

uint32_t sys_getpid(void);
void syscall_init(void);
// 以下两个函数声明，实现在memory.c
void *sys_malloc(uint32_t size);
void sys_free(void *ptr);
// 缺少了write/print的声明
#endif