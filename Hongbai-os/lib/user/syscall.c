#include "./syscall.h"

/*从上到下，分别是0、1、2、3参数的系统调用，结构基本一致
 *eax是子程序号，剩下三个存在ebx、ecx、edx中*/

/*({ ... })是gcc扩展
 *将一组语句封装为一个表达式，返回最后一个语句的值*/
#define _syscall0(NUMBER) ({ \
    int retval;              \
    asm volatile(            \
        "int $0x80"          \
        : "=a"(retval)       \
        : "a"(NUMBER)        \
        : "memory");         \
    retval;                  \
})

#define _syscall1(NUMBER, ARG1) ({ \
    int retval;                    \
    asm volatile(                  \
        "int $0x80"                \
        : "=a"(retval)             \
        : "a"(NUMBER), "b"(ARG1)   \
        : "memory");               \
    retval;                        \
})

#define _syscall2(NUMBER, ARG1, ARG2) ({    \
    int retval;                             \
    asm volatile(                           \
        "int $0x80"                         \
        : "=a"(retval)                      \
        : "a"(NUMBER), "b"(ARG1), "c"(ARG2) \
        : "memory");                        \
    retval;                                 \
})

#define _syscall3(NUMBER, ARG1, ARG2, ARG3) ({         \
    int retval;                                        \
    asm volatile(                                      \
        "int $0x80"                                    \
        : "=a"(retval)                                 \
        : "a"(NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3) \
        : "memory");                                   \
    retval;                                            \
})

/*返回当前任务的pid*/
uint32_t getpid()
{
    return _syscall0(SYS_GETPID);
}

/*打印字符串str，返回strlen*/
uint32_t write(char *str)
{
    return _syscall1(SYS_WRITE, str);
}

void *malloc(uint32_t size)
{
    return (void *)_syscall1(SYS_MALLOC, size);
}

void free(void *ptr)
{
    return (void *)_syscall1(SYS_FREE, ptr);
}