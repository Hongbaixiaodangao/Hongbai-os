// 这个头文件定义了打印函数的原型
// 包括打印单个字符、打印字符串和打印16进制整数的函数
#ifndef __LIB_KERNEL_PRINT_H
#define __LIB_KERNEL_PRINT_H
#include "./stdint.h"
extern void put_char(uint8_t char_asci);
extern void put_str(char *message);
extern void put_int(uint32_t number);
extern void set_cursor(uint32_t position);
#endif
