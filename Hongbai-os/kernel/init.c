// 完成所有的初始化工作
#include "./init.h"
#include "../lib/stdint.h"
#include "../lib/kernel/print.h"
#include "./interrupt.h"
#include "../device/timer.h"
#include "./memory.h"
#include "../thread/thread.h"
#include "../device/console.h"
#include "../device/keyboard.h"
#include "../userprog/tss.h"
#include "../userprog/syscall-init.h"
#include "../device/ide.h"
#include "../fs/fs.h"

/*负责初始化所有模块 */
void init_all()
{
    put_str("init_all\n");
    idt_init();      // 中断初始化
    mem_init();      // 内存初始化
    timer_init();    // 定时器初始化
    thread_init();   // 线程初始化
    console_init();  // 控制台初始化，最好放到开中断之前
    keyboard_init(); // 键盘初始化
    tss_init();      // TSS和GDT初始化
    syscall_init();  // 系统调用初始化
    ide_init();      // 硬盘驱动初始化
    filesys_init();  // 文件系统初始化
}