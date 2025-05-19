// 这个头文件定义了位图的结构体和函数
// 位图是一个用来表示某些资源是否被占用的结构体
#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H
#include "./stdint.h"

#define BITMAP_MASK 1

struct bitmap //0代表不可以使用，1代表可以使用
{
    uint32_t btmp_bytes_len; // 位图的长度
    /*关于位图指针，遍历位图时，整体以字节为单位，细节上以位为单位，所以位图指针还是从单字节类型*/
    uint8_t *btmp_bits; // 位图的起始字节指针
};

void bitmap_init(struct bitmap *btmp);                                // 初始化位图为0
bool bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx);         // 测试位图的某一位是0还是1
int bitmap_scan(struct bitmap *btmp, uint32_t cnt);                   // 申请连续的cnt个位
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value); // 设置位图的某一位为0或1
#endif