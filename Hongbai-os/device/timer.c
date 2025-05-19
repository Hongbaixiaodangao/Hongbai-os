// 时钟中断相关
#include "./timer.h"
#include "../kernel/io.h"
#include "../lib/kernel/print.h"
#include "../kernel/interrupt.h"
#include "../thread/thread.h"
#include "../kernel/debug.h"

#define IRQ0_FREQUENCY 100      // 时钟中断频率
#define INPUT_FREQUENCY 1193180 // 8254PIT的输入时钟频率
// 计数器初始值
#define COUNTER0_VALUE INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT 0X40     // Counter0的数据端口，通过该端口写入计数器初始值
#define COUNTER0_NO 0          // 选择Counter0
#define COUNTER_MODE 2         // 选择模式2,进行周期中断
#define READ_WRITE_LATCH 3     // 表示先写低字节，再写高字节
#define PIT_COUNTROL_PORT 0x43 // 控制字寄存器端口
// 计算每多少毫秒发生1次中断
#define mil_second_per_intr (1000 / IRQ0_FREQUENCY)

uint32_t ticks; // ticks是内核自中断开启以来的滴答数，一个tick就是一次时钟中断

/* 配置8524PIT */
void frequency_set(uint8_t counter_port, uint8_t counter_no, uint8_t rwl, uint8_t counter_mode, uint16_t counter_value)
{
    outb(PIT_COUNTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    outb(counter_port, (uint8_t)counter_value);
    outb(counter_port, (uint8_t)counter_value >> 8);
    return;
}

/* 时钟的中断处理函数 */
void intr_timer_handler(void)
{
    struct task_struct *cur_thread = running_thread();
    ASSERT(cur_thread->stack_magic == 0x20250325); // 检测栈溢出
    cur_thread->elapsed_ticks++;                   // 线程运行的时间加1
    // 每次时钟中断，ticks加1
    ticks++;
    if (cur_thread->ticks == 0)
    {               // 如果当前线程的时间片用完
        schedule(); // 调度其他线程
    }
    else
    {
        cur_thread->ticks--; // 否则，当前线程的时间片减1
    }
    return;
}

/* 初始化PIT8253 */
void timer_init(void)
{
    put_str("timer_init start\n");
    frequency_set(COUNTER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler); // 注册时钟中断处理函数
    put_str("timer_init done\n");
    return;
}

/*以tick为单位的sleep，让当前线程休眠sleep_ticks次中断
 *每次中断全局变量ticks++，计算某次中断后ticks和开始休眠时的start_tick的差值，
 *决定是继续休眠还是结束休眠*/
static void ticks_to_sleep(uint32_t sleep_ticks)
{
    uint32_t start_tick = ticks;
    while (ticks - start_tick < sleep_ticks) // 如果差值没达到目标，就一直休眠
    {
        thread_yield();
    }
}

/*以毫秒ms为单位的休眠*/
void mtime_sleep(uint32_t m_second)
{
    uint32_t sleep_ticks = DIV_ROUND_UP(m_second, mil_second_per_intr);
    ASSERT(sleep_ticks > 0);
    ticks_to_sleep(sleep_ticks);
}