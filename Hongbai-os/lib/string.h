// 我的os的c标准库字符串处理函数的声明
#ifndef __LIB_STRING_H
#define __LIB_STRING_H
#include "./stdint.h"

void memset(void *dst_, uint8_t value, uint32_t size);         // 将dst_起始的size个字节设置为value
void memcpy(void *dst_, const void *src_, uint32_t size);      // 将src_起始的size个字节拷贝到dst_
int32_t memcmp(const void *a_, const void *b_, uint32_t size); // 连续比较a_和b_两个地址开头的size个字节，若相等返回0，如果a_大于b_返回1,否则返回-1
char *strcpy(char *dst_, const char *src_);                    // 将字符串从src_复制到dst_
uint32_t strlen(const char *str_);                             // 返回字符串长度
int32_t strcmp(const char *a_, const char *b_);                // 比较两个字符串a和b_，如果a_大于b_返回1，相等返回0,否则返回-1
char *strchr(const char *string_, char ch_);                   // 在字符串string_中查找字符ch_，返回指向该字符的指针

char *strrchr(const char *str_, const uint8_t ch); /*查找str字符串最后一次出ch字符的位置*/
char *strcat(char *dsc_, const char *src_);        /*将src_字符串拼接到dsc_字符串末尾*/
int32_t strchrs(const char *str_, uint8_t ch);         /*统计str字符串中ch字符出现的频率*/

#endif //__LIB_STRING_H