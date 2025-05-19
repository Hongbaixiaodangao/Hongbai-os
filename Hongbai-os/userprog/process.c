
#include "./process.h"
#include "../kernel/global.h"
#include "../lib/stdint.h"
#include "../thread/thread.h"
#include "../kernel/debug.h"
#include "./tss.h"
#include "../device/console.h"
#include "../lib/string.h"
#include "../kernel/interrupt.h"
#include "../lib/user/syscall.h"

/*构建用户进程初始化上下文信息*/
void start_process(void *filename_)
{
    void *function = filename_;                 // 用户进程中，具体程序的入口
    struct task_struct *cur = running_thread(); // 获取进程的pcb
    // 关于以下两行代码，在《真象还原》的509、510页有非常清晰的解释
    cur->self_kstack += sizeof(struct thread_stack); // 留出中断栈空间
    struct intr_stack *proc_stack = (struct intr_stack *)cur->self_kstack;
    // 初始化8个通用寄存器
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    // 显存段初始化
    proc_stack->gs = 0;
    // 初始化ds，ds保存数据段基址，ss保存栈段基址，都指向用户数据段选择子
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    // 初始化cs:ip，cs保存代码段基址，指向用户代码段选择子
    proc_stack->eip = function;
    proc_stack->cs = SELECTOR_U_CODE;
    // 初始化elfgs
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    // 初始化ss:sp
    proc_stack->esp = (void *)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;
    // 通过内联汇编，欺骗cpu，让它进行一次中断返回，把proc_stack中的数据压入cpu
    asm volatile("movl %0,%%esp;jmp intr_exit" : : "g"(proc_stack) : "memory");
}

/*激活页表，pthread可能是用户进程或者内核线程，
 *如果是内核线程，重新加载内核页表，否则加载进程的页表*/
void page_dir_activate(struct task_struct *pthread)
{
    // 默认值就是内核线程的页目录表基址
    // 0x100000对应1mb，我们的页目录表就是从0x00100000开始的，定义在boot.int
    uint32_t page_dir_phy_addr = 0x00100000;
    if (pthread->pgdir != NULL)
    { // 说明pthread是一个用户进程，激活进程的二级页表结构
        page_dir_phy_addr = addr_v2p((uint32_t)pthread->pgdir);
    }
    // 通过内联汇编，更新cr3中页目录表的物理地址基址
    asm volatile("movl %0,%%cr3" : : "r"(page_dir_phy_addr) : "memory");
}

/*激活线程或进程的页表，更新tss中的esp0为 0特权级的栈*/
void process_activate(struct task_struct *pthread)
{
    ASSERT(pthread != NULL);
    // 激活页表
    page_dir_activate(pthread);
    // 如果是内核线程，没有特权级转变，不需要更新pcb里的esp0
    // 如果是用户进程，需要更新esp0
    if (pthread->pgdir != NULL)
    {
        update_tss_esp(pthread);
    }
}

/*创建页目录表，成功返回页目录的虚拟地址，失败返回-1*/
uint32_t *create_page_dir(void)
{
    /*我们通过线程创建进程，所以先在内核申请内存*/
    uint32_t *page_dir_vaddr = get_kernel_pages(1);
    if (page_dir_vaddr == NULL)
    {
        console_put_str("create_page_dir error:get_kernel_page failed!\n");
        return NULL;
    }
    /*1.复制页表*/
    // 0x300 = 768,一个项4个字节 0xfffff000是最后一个分页，它同时映射页目录表本身，关键词：递归页表
    memcpy((uint32_t *)((uint32_t)page_dir_vaddr + 0x300 * 4), (uint32_t *)(0xfffff000 + 0x300 * 4), 1024);
    /*2.更新页目录基址*/
    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

    return page_dir_vaddr;
}

/*创建用户进程虚拟地址位图*/
void create_user_vaddr_bitmap(struct task_struct *user_prog)
{
    // 我们为用户进程定的起始地址是USER_VADDR_START
    // 该值定义在 process.h 中，其值为 0x8048000，可以通过readelf查看
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bits = get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap); // 通过初始化填充0
}

/*通过线程创建用户进程*/
void process_execute(void *filename, char *name)
{
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread, name, default_prio);        // 初始化线程
    create_user_vaddr_bitmap(thread);               // 位图
    thread_create(thread, start_process, filename); // 线程结构体-具体功能(创建进程)-线程名
    thread->pgdir = create_page_dir();              // 页目录表
    block_init(thread->u_block_desc);               // 进程内存块描述符数组初始化

    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old_status);
}