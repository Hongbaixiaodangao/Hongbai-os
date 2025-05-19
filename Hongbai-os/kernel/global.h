// 这个文件定义了段选择子的属性和IDT描述符的属性
#ifndef __KERNEL_GLOBAL_H
#define __KERNEL_GLOBAL_H
#include "../lib/stdint.h"
/*----------GDT段描述符属性----------*/
#define DESC_G_4K 1  // 粒度为4k
#define DESC_D_32 1  // 操作数为32位
#define DESC_L 0     // 64位模式
#define DESC_AVL 0   // 软件位，置0
#define DESC_P 1     // 段是否有效
#define DESC_DPL_0 0 // 4个特权级
#define DESC_DPL_1 1
#define DESC_DPL_2 2
#define DESC_DPL_3 3
/*S说明：代码段和数据段是存储段，tss和门描述符属于系统段
 *S为1时表示存储段，S为0时表示系统段*/
#define DESC_S_CODE 1
#define DESC_S_DATA 1
#define DESC_S_SYS 0

#define DESC_TYPE_CODE 8 // 1000
#define DESC_TYPE_DATA 2 // 0010
#define DESC_TYPE_TSS 9  // 1001

/*----------段选择子属性----------*/
#define RPL0 0 // 段特权级
#define RPL1 1
#define RPL2 2
#define RPL3 3

#define TI_GDT 0
#define TI_LDT 1

#define SELECTOR_K_CODE ((1 << 3) + (TI_GDT << 2) + RPL0) // 内核代码段
#define SELECTOR_K_DATA ((2 << 3) + (TI_GDT << 2) + RPL0) // 内核数据段
#define SELECTOR_K_STACK SELECTOR_K_DATA                  // 内核栈段,和数据段用一个内存段
#define SELECTOR_K_GS ((3 << 3) + (TI_GDT << 2) + RPL0)   // 显存段
                                                          // GDT四号位置是tss
#define SELECTOR_U_CODE ((5 << 3) + (TI_GDT << 2) + RPL3) // 用户代码段
#define SELECTOR_U_DATA ((6 << 3) + (TI_GDT << 2) + RPL3) // 用户数据段
#define SELECTOR_U_STACK SELECTOR_U_DATA                  // 用户栈段

/*----------64位GDT描述符----------*/
// 把上面定义的GDT位属性连接起来，构建64位的GDT描述符
#define GDT_ATTR_HIGH \
    ((DESC_G_4K << 7) + (DESC_D_32 << 6) + (DESC_L << 5) + (DESC_AVL << 4))
#define GDT_CODE_ATTR_LOW_DPL3 \
    ((DESC_P << 7) + (DESC_DPL_3 << 5) + (DESC_S_CODE << 4) + DESC_TYPE_CODE)
#define GDT_DATA_ATTR_LOW_DPL3 \
    ((DESC_P << 7) + (DESC_DPL_3 << 5) + (DESC_S_DATA << 4) + DESC_TYPE_DATA)

/*----------TSS描述符属性----------*/
// 专门定义一下TSS属性
#define TSS_DESC_D 0
#define TSS_ATTR_HIGH \
    ((DESC_G_4K << 7) + (TSS_DESC_D << 6) + (DESC_L << 5) + (DESC_AVL << 4) + 0x0)
#define TSS_ATTR_LOW \
    ((DESC_P << 7) + (DESC_DPL_0 << 5) + (DESC_S_SYS << 4) + DESC_TYPE_TSS)
#define SELECTOR_TSS ((4 << 3) + (TI_GDT << 2) + RPL0)

/*----------定义GDT描述符的结构----------*/
struct gdt_desc
{
    uint16_t limit_low_word;                    // 低16位段界限
    uint16_t base_low_word;                     // 低16位段基址
    uint8_t base_mid_byte;                      // 中8位段基址
    uint8_t attr_low_byte;                      // 8位段属性
    uint8_t limit_high_byte_and_attr_high_byte; // 4位段界限+4位段属性
    uint8_t base_hight_byte;                    // 高8位段基址
};

/*----------IDT中断描述符表属性----------*/
#define IDT_DESC_P 1
#define IDT_DESC_DPL0 0
#define IDT_DESC_DPL1 1
#define IDT_DESC_DPL2 2
#define IDT_DESC_DPL3 3
#define IDT_DESC_32_TYPE 0xE // 32位的门
#define IDT_DESC_16_TYPE 0x6 // 16位的门,不会用到,定义它只为和 32 位门区分

#define IDT_DESC_ATTR_DPL0 ((IDT_DESC_P << 7) + (IDT_DESC_DPL0 << 5) + IDT_DESC_32_TYPE)
#define IDT_DESC_ATTR_DPL3 ((IDT_DESC_P << 7) + (IDT_DESC_DPL3 << 5) + IDT_DESC_32_TYPE)

/*----------用户进程相关属性----------*/
#define EFLAGS_MBS (1 << 1)                           // 此项必须要设置
#define EFLAGS_IF_1 (1 << 9)                          // if位1,屏蔽中断信号，关中断
#define EFLAGS_IF_0 0                                 // 开中断
#define EFLAGS_IOPL_3 (3 << 12)                       // 测试用，允许用户进程跳过系统调用直接操作硬件
#define EFLAGS_IOPL_0 (0 << 12)                       // 默认状态

/*杂项*/
#define DIV_ROUND_UP(X, STEP) ((X + STEP - 1) / STEP) // 实现除法后向上取整
#define PG_SIZE 4096

#endif
