#include "./interrupt.h"
#include "../lib/stdint.h"
#include "./global.h"
#include "./io.h"
#include "../lib/kernel/print.h"

#define PIC_M_CTRL 0x20 // 这里用的可编程中断控制器是8259A,主片的控制端口是0x20
#define PIC_M_DATA 0x21 // 主片的数据端口是0x21
#define PIC_S_CTRL 0xa0 // 从片的控制端口是0xa0
#define PIC_S_DATA 0xa1 // 从片的数据端口是0xa1

#define IDT_DESC_CNT 0x81 // 目前总共支持的中断数

#define EFLAGS_IF 0x00000200                                                          // 中断标志位IF,在EFLAGS寄存器中
#define GET_EFLAGS_IF(EFLAGS_VAR) asm volatile("pushfl ; popl %0" : "=g"(EFLAGS_VAR)) // 获取中断标志位IF

extern uint32_t syscall_handler(void); // 系统调用中断处理函数

/*中断门描述符结构体*/
struct gate_desc
{
    uint16_t func_offset_low_word;
    uint16_t selector;
    uint8_t dcount; // 此项为双字计数字段，是门描述符中的第4字节。此项固定值，不用考虑
    uint8_t attribute;
    uint16_t func_offset_high_word;
};

static struct gate_desc idt[IDT_DESC_CNT]; // idt是中断描述符表,本质上就是个中断门描述符数组
char *intr_name[IDT_DESC_CNT];             // 用于保存异常的名字

/********     定义中断处理程序数组     ********
 * 在kernel.S中定义的intrXXentry只是中断处理程序的入口,
 * 最终调用的是ide_table中的处理程序*/
intr_handler idt_table[IDT_DESC_CNT];
/********************************************/

extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 声明引用定义在kernel.S中的中断处理函数入口数组

/* 初始化可编程中断控制器8259A */
static void pic_init(void)
{

    /* 初始化主片 */
    outb(PIC_M_CTRL, 0x11); // ICW1: 边沿触发,级联8259, 需要ICW4.
    outb(PIC_M_DATA, 0x20); // ICW2: 起始中断向量号为0x20,也就是IR[0-7] 为 0x20 ~ 0x27.
    outb(PIC_M_DATA, 0x04); // ICW3: IR2接从片.
    outb(PIC_M_DATA, 0x01); // ICW4: 8086模式, 正常EOI

    /* 初始化从片 */
    outb(PIC_S_CTRL, 0x11); // ICW1: 边沿触发,级联8259, 需要ICW4.
    outb(PIC_S_DATA, 0x28); // ICW2: 起始中断向量号为0x28,也就是IR[8-15] 为 0x28 ~ 0x2F.
    outb(PIC_S_DATA, 0x02); // ICW3: 设置从片连接到主片的IR2引脚
    outb(PIC_S_DATA, 0x01); // ICW4: 8086模式, 正常EOI
    //主片打开时钟中断IRQ0、键盘中断IRQ1、级联从片的IRQ3
    outb(PIC_M_DATA, 0xf8);
    //从盘打开IRQ14,接受硬盘中断
    outb(PIC_S_DATA, 0xbf);

    put_str("  pic_init done\n");
}

/* 创建中断门描述符 */
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function)
{
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/*初始化中断描述符表*/
static void idt_desc_init(void)
{
    int i, last_index = IDT_DESC_CNT - 1;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    // 以下是单独的0x80系统调用中断处理函数的初始化，特权级是3用户级
    make_idt_desc(&idt[last_index], IDT_DESC_ATTR_DPL3, syscall_handler);
    put_str("  idt_desc_init done\n");
}

/* 通用的中断处理函数,一般用在异常出现时的处理 */
static void general_intr_handler(uint8_t vec_nr)
{
    if (vec_nr == 0x27 || vec_nr == 0x2f)
    {           // 0x2f是从片8259A上的最后一个irq引脚，保留
        return; // IRQ7和IRQ15会产生伪中断(spurious interrupt),无须处理。
    }
    // put_str("test general_intr_handler\n");
    /* 将光标置为0，从屏幕左上角清出一片打印异常信息的区域，方便阅读 */
    set_cursor(0);      // 设置光标位置为0
    int cursor_pos = 0; // 光标位置
    while (cursor_pos++ < 320)
    {                  // 清除屏幕
        put_char(' '); // 打印空格
    }

    set_cursor(0);                                                           // 设置光标位置为0
    put_str("!!!!!!            excetion message begin            !!!!!!\n"); // 打印异常信息
    set_cursor(88);
    put_str(intr_name[vec_nr]); // 打印异常名称
    if (vec_nr == 14)
    {                             // 如果是页面错误异常
        int page_fault_vaddr = 0; // 页面错误地址
        // cr2寄存器存放造成page_fault的地址
        asm volatile("movl %%cr2, %0" : "=r"(page_fault_vaddr)); // 获取页面错误地址
        put_str("\npage fault addr is ");                        // 打印页面错误地址
        put_int(page_fault_vaddr);                               // 打印页面错误地址
    }
    put_str("!!!!!!            excetion message end              !!!!!!\n"); // 打印异常信息结束

    // 目前处在关中断状态，不再调度进程，下面的死循环不再被中断覆盖
    while (1)
        ; // 进入死循环
}

/* 完成一般中断处理函数注册及异常名称注册 */
static void exception_init(void)
{ // 完成一般中断处理函数注册及异常名称注册
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {

        /* idt_table数组中的函数是在进入中断后根据中断向量号调用的,
         * 见kernel/kernel.S的call [idt_table + %1*4] */
        idt_table[i] = general_intr_handler; // 默认为general_intr_handler。以后会由register_handler来注册具体处理函数。
        intr_name[i] = "unknown";            // 先统一赋值为unknown
    }
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/*完成有关中断的所有初始化工作*/
void idt_init()
{
    put_str("idt_init start\n");
    idt_desc_init();  // 初始化中断描述符表
    exception_init(); // 异常名初始化并注册通常的中断处理函数
    pic_init();       // 初始化8259A
    /* 加载idt */
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m"(idt_operand));
    put_str("idt_init done\n");
}

/* 在中断处理程序数组第vector_no个元素中注册安装中断处理程序function */
void register_handler(uint8_t vector_no, intr_handler function)
{
    idt_table[vector_no] = function; // 注册中断处理函数
}

/* 开启中断并返回开启中断之前的状态 */
enum intr_status intr_enable(void)
{
    if (intr_get_status() == INTR_ON)
    {
        return INTR_ON; // 如果当前中断已经打开,则直接返回
    }
    else
    {
        asm volatile("sti" : : : "memory"); // 开中断
        return INTR_OFF;                    // 返回关中断
    }
}

/* 关闭中断并返回在关闭中断之前的状态 */
enum intr_status intr_disable(void)
{
    if (intr_get_status() == INTR_OFF)
    {
        return INTR_OFF; // 如果当前中断已经关闭,则直接返回
    }
    else
    {
        asm volatile("cli" : : : "memory"); // 关中断
        return INTR_ON;                     // 返回开中断
    }
}

/* 将中断状态设置为status */
enum intr_status intr_set_status(enum intr_status status)
{
    return (status == INTR_ON) ? intr_enable() : intr_disable();
}

/* 获取当前中断状态 */
enum intr_status intr_get_status(void)
{
    uint32_t eflags = 0;
    GET_EFLAGS_IF(eflags);
    return (eflags & EFLAGS_IF) ? INTR_ON : INTR_OFF;
}