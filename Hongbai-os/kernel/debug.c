// 这个文件实现debug.h中声明的函数
#include "./debug.h"
#include "../lib/kernel/print.h"
#include "./interrupt.h"

/* 打印相关信息并悬停程序 */
void panic_spin(char *filename, int line, const char *func, const char *condition)
{
    intr_disable(); // 关闭中断
    put_str("\n\n\n!!!kernel panic!!!\n");
    put_str("filename:");
    put_str((char *)filename);
    put_str("\n");

    put_str("line:0x");
    put_int(line);
    put_str("\n");

    put_str("function:");
    put_str((char *)func);
    put_str("\n");

    put_str("condition:");
    put_str((char *)condition);
    put_str("\n");
    while (1);
}