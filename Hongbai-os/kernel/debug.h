// 这个头文件定义了内核调试相关的宏和函数
// 包括断言和错误处理
#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H
void panic_spin(char *filename, int line, const char *func, const char *condition);
/* __VA_ARGS__代表若干个参数，对应前面的... */
#define PANIC(...) panic_spin(__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifdef NDEBUG
#define ASSERT(condition) ((void)0)
#else
#define ASSERT(condition)                                     \
    if (condition)                                            \
    {                                                         \
    }                                                         \
    else                                                      \
    {                                                         \
        /*关于#，学名字符串化宏，相当于宏定义了""字符串标号*/ \
        PANIC(#condition);                                    \
    }
#endif /*结束__NDEBUG*/
#endif /*结束__KERNEL_DEBUG_H*/