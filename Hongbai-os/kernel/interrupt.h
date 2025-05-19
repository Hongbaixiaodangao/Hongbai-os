// 这个头文件声明了中断枚举体和中断处理函数
#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H

#include "../lib/stdint.h"
typedef void *intr_handler; // 中断处理函数类型

/* 定义中断的两种状态
 * 关于枚举体enum，第一项默认值0,后面每项默认+1
 * 开中断的意思是cpu可以相应中断，否则cpu忽视中断请求
 * 进入中断处理程序之前，cpu进入关中断模式！ */
enum intr_status
{
    INTR_OFF, // 关中断
    INTR_ON   // 开中断
};

void register_handler(uint8_t vector_no, intr_handler function); // 在中断处理程序数组第vector_no个元素中注册安装中断处理程序function
void idt_init(void);                                             // 完成有关中断的所有初始化工作

enum intr_status intr_enable(void);                        // 打开中断
enum intr_status intr_disable(void);                       // 关闭中断
enum intr_status intr_set_status(enum intr_status status); // 设置中断状态
enum intr_status intr_get_status(void);                    // 获取中断状态
#endif
