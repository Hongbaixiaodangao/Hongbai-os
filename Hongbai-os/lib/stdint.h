// 这个头文件定义了常见变量类型，bool型，空指针，指针类型
#ifndef _LIB_STDINT_H
#define _LIB_STDINT_H

typedef signed char int8_t;
typedef signed short int int16_t;
typedef signed int int32_t;
typedef signed long long int int64_t;

typedef unsigned char uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long int uint64_t;

typedef int32_t intptr_t;
typedef uint32_t uintptr_t;
typedef int32_t ssize_t;
typedef uint32_t size_t;

#define NULL ((void *)0) // 空指针
// 实现除法后向上取整,定义在stdint.h
#define DIV_ROUND_UP(X, STEP) ((X + STEP - 1) / STEP) 
#define PG_SIZE 4096

#ifndef __cplusplus
#define bool _Bool // 布尔类型，_Bool是标准关键字，不依赖任何头文件
#define true 1
#define false 0
#endif // bool

#endif // _LIB_STDINT_H
