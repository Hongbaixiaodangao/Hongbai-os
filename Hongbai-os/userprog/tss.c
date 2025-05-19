

#include "./tss.h"
#include "../lib/stdint.h"
#include "../kernel/global.h"
#include "../lib/kernel/print.h"
#include "../lib/string.h"

#define PG_SIZE 4096             // 标准页大小
#define GDT_BASE_ADDR 0xc0000903 // gdt基地址，可以用info gdt查看
/*TSS结构，由程序员定义，CPU维护*/
struct tss
{
    uint32_t backlink; // 上一个TSS的指针

    uint32_t *esp0; // 特权级栈
    uint32_t ss0;
    uint32_t *esp1;
    uint32_t ss1;
    uint32_t *esp2;
    uint32_t ss2;
    uint32_t cr3;

    uint32_t (*eip)(void); // 各种寄存器
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;

    uint32_t ldt; // ldt
    uint32_t trace;
    uint32_t io_base; // io位图
};
static struct tss tss;

/*更新tss中esp0字段的值为 pthread的0级栈*/
void update_tss_esp(struct task_struct *pthread)
{
    tss.esp0 = (uint32_t *)((uint32_t)pthread + PG_SIZE);
}

/*创建gdt描述符*/
static struct gdt_desc make_gdt_desc(uint32_t *desc_addr, uint32_t limit,
                                     uint8_t attr_low, uint8_t attr_high)
{
    uint32_t desc_base = (uint32_t)desc_addr;
    struct gdt_desc desc;
    desc.limit_low_word = limit & 0x0000ffff;
    desc.base_low_word = desc_base & 0x0000ffff;
    desc.base_mid_byte = ((desc_base & 0x00ff0000) >> 16);
    desc.attr_low_byte = (uint8_t)attr_low;
    desc.limit_high_byte_and_attr_high_byte =
        (((limit & 0x00ff0000) >> 16) + (uint8_t)attr_high);
    desc.base_hight_byte = ((desc_base & 0xff000000) >> 24);
    return desc;
}

/*在gdt中创建tss并重新加载gdt*/
void tss_init()
{
    put_str("tss_init start\n");
    uint32_t tss_size = sizeof(tss);
    memset(&tss, 0, tss_size);
    tss.ss0 = SELECTOR_K_STACK; // 0特权级栈就是内核栈，将选择子赋给ss0字段
    tss.io_base = tss_size;
    // 注册TSS
    *((struct gdt_desc *)(GDT_BASE_ADDR + 0x20)) =
        make_gdt_desc((uint32_t *)&tss, tss_size - 1, TSS_ATTR_LOW, TSS_ATTR_HIGH);
    // 注册用户级代码段和数据段
    *((struct gdt_desc *)(GDT_BASE_ADDR + 0x28)) =
        make_gdt_desc((uint32_t *)0, 0xfffff, GDT_CODE_ATTR_LOW_DPL3, GDT_ATTR_HIGH);
    *((struct gdt_desc *)(GDT_BASE_ADDR + 0x30)) =
        make_gdt_desc((uint32_t *)0, 0xfffff, GDT_DATA_ATTR_LOW_DPL3, GDT_ATTR_HIGH);
    /*构建GDT的操作数，用于LGDT指令，传递信息到GDTR寄存器
     *结构：|16位GDT限长 (Limit)|32位 GDT基地址(Base Address)|16位保留（通常为0）|
     *Limit类似数组下标，需要-1*/
    uint64_t gdt_operand =
        ((8 * 7 - 1) | ((uint64_t)(uint32_t)GDT_BASE_ADDR << 16));
    // 将GDT信息和TSS信息用lgdt和ltr指令分别写入GDTR和TR寄存器
    asm volatile("lgdt %0" : : "m"(gdt_operand));
    asm volatile("ltr %w0" : : "r"(SELECTOR_TSS));
    put_str("tss_init done\n");
}