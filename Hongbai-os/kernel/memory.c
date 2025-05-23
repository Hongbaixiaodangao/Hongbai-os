// 实现memery.h中的函数
#include "memory.h"
#include "debug.h"
#include "../lib/kernel/print.h"
#include "../lib/string.h"
#include "../thread/thread.h"
#include "../thread/sync.h"
#include "global.h"
#include "interrupt.h"

#define PAGE_SIZE 4096 // 定义页面大小为4KB
// 关于位图地址，安排在0xc009a000，我的整个系统支持4个4kb位图，共管理512MB的内存
#define MEM_BITMAP_BASE 0xc009a000 // 内核内存池位图基地址
#define K_HEAP_START 0xc0100000    // 内核堆起始地址

#define PDE_INDEX(addr) ((addr & 0xffc00000) >> 22) // 获取页目录项索引
#define PTE_INDEX(addr) ((addr & 0x003ff000) >> 12) // 获取页表项索引

// 内存池结构体
struct pool
{
    struct bitmap pool_bitmap; // 内存池位图
    struct lock lock;          // 创建用户进程会用到，让用户进程申请内存的行为互斥
    uint32_t phy_addr_start;   // 物理内存池起始地址
    uint32_t pool_size;        // 内存池大小
};
struct pool kernel_pool, user_pool; // 内核内存池和用户内存池
struct virtual_addr kernel_vaddr;   // 用来给内核分配虚拟地址

// arena结构体
struct arena
{
    struct mem_block_desc *desc; // 此arena关联的mem_block_desc
    // 如果large为true，cnt代表arena拥有的页数
    // 否则代表空闲的mem_block数
    bool large;
    uint32_t cnt;
};
struct mem_block_desc k_block_descs[DESC_CNT]; // 内核内存块描述符数组

/* 在pf表示的虚拟内存池中申请pg_cnt个虚拟页
 * 成功则返回虚拟页的起始地址，失败则返回NULL */
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt)
{
    int bit_idx_start = -1;   // 位图索引
    uint32_t vaddr_start = 0; // 虚拟地址
    uint32_t cnt = 0;         // 计数器
    if (pf == PF_KERNEL)
    { // 内核内存池
        // 扫描位图，找到空余的pg_cnt个位，对应相等的页
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)
        { // 没有找到合适的位置
            return NULL;
        }
        // 找到后把位图中pg_cnt个位置设置为1，表示这些页已分配
        while (cnt < pg_cnt)
        {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt, 1); // 设置位图
            cnt++;
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE; // 计算虚拟地址
    }
    else
    { // 用户内存池
        /*整体和内核内存池申请过程一致，进行扫描-设置-更新三个过程
         *因为我们是通过线程建立用户进程，需要先获取目前线程状态*/
        struct task_struct *cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)
        {
            return NULL;
        }
        while (cnt < pg_cnt)
        {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt, 1);
            cnt++;
        }
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
        // 这是在用户内存，最大页起点就是分界线0xc0000000-一个页大小
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void *)vaddr_start; // 返回虚拟地址
}

/* 得到虚拟地址vaddr对应的pte的指针 */
uint32_t *pte_ptr(uint32_t vaddr)
{
    uint32_t *pte = (uint32_t *)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_INDEX(vaddr) * 4); // 计算页表项地址
    return pte;                                                                                     // 返回页表项地址
}

/* 得到虚拟地址vaddr对应的pde的指针 */
uint32_t *pde_ptr(uint32_t vaddr)
{
    uint32_t *pde = (uint32_t *)(0xfffff000 + PDE_INDEX(vaddr) * 4); // 计算页目录项地址
    return pde;                                                      // 返回页目录项地址
}

/* 在m_pool 指向的物理内存池中申请一个物理页，成功返回页物理地址，失败返回NULL */
static void *palloc(struct pool *m_pool)
{
    /* 扫描或设置位图要保证原子操作 */
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1); // 扫描位图
    if (bit_idx == -1)
    { // 没有找到合适的地址
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);                         // 设置位图
    uint32_t page_phyaddr = m_pool->phy_addr_start + bit_idx * PG_SIZE; // 计算物理地址
    return (void *)page_phyaddr;                                          // 返回物理地址
}

/* 页表中添加虚拟地址_vaddr与物理地址_page_phyaddr的映射 */
static void page_table_add(void *_vaddr, void *_page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr;               // 虚拟地址
    uint32_t page_phyaddr = (uint32_t)_page_phyaddr; // 物理地址
    uint32_t *pde = pde_ptr(vaddr);                  // 获取页目录项指针
    uint32_t *pte = pte_ptr(vaddr);                  // 获取页表项指针

    if (*pde & 0x00000001)
    { // 页目录项存在
        ASSERT(!(*pte & 0x00000001));
        if (!(*pte & 0x00000001))
        {                                                     // 页表项不存在
            *pte = page_phyaddr | PG_US_U | PG_RW_W | PG_P_1; // 设置页表项
        }
        else
        { // 页表项存在
            PANIC("page_table_add: pte repeat");
            *pte = page_phyaddr | PG_US_U | PG_RW_W | PG_P_1; // 设置页表项
        }
    }
    else
    {                                                               // 页目录项不存在
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);      // 分配一个物理页作为页表
        *pde = pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1;            // 设置页目录项
        memset((void *)((uint32_t)pte & 0xfffff000), 0, PG_SIZE); // 清空页表
        ASSERT(!(*pte & 0x00000001));
        *pte = page_phyaddr | PG_US_U | PG_RW_W | PG_P_1; // 设置页表项
    }
}

/* 分配pg_cnt个页空间，成功则返回起始虚拟地址，失败时返回NULL */
/* 上面三个函数的合成 */
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    ASSERT(pg_cnt > 0 && pg_cnt < 3840); // 检查页数

    void *vaddr_start = vaddr_get(pf, pg_cnt); // 获取虚拟地址
    if (vaddr_start == NULL)
    { // 没有找到合适的地址
        return NULL;
    }
    uint32_t vaddr = (uint32_t)vaddr_start;                             // 虚拟地址起始位置
    uint32_t cnt = pg_cnt;                                              // 计数器
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool; // 选择内存池
    while (cnt-- > 0)                                                   // cnt-- 后是否 > 0
    {                                                                   // 分配物理页
        void *page_phyaddr = palloc(mem_pool);                          // 分配物理页
        if (page_phyaddr == NULL)
        { // 失败时要将曾经已申请的虚拟地址和物理页全部回滚，在将来完成内存回收时再补充
            return NULL;
        }
        page_table_add((void *)vaddr, page_phyaddr); // 添加映射关系
        vaddr += PG_SIZE;                          // 移动到下一个页
    }
    return vaddr_start; // 返回虚拟地址
}

/* 从内核物理内存池中申请pg_cnt页内存，成功则返回其虚拟地址，失败则返回NULL */
void *get_kernel_pages(uint32_t pg_cnt)
{
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt); // 申请内存
    if (vaddr != NULL)                            // 申请成功
    {
        memset(vaddr, 0, pg_cnt * PG_SIZE); // 把这部分内存上的内容清理干净，准备让申请的东西使用
    }
    return vaddr; // 返回虚拟地址
}

/* 从用户内存池申请pg_cnt页内存 */
void *get_user_page(uint32_t pg_cnt)
{
    lock_acquire(&user_pool.lock); // 保证互斥
    void *vaddr = malloc_page(PF_USER, pg_cnt);
    if (vaddr != NULL)
    {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    lock_release(&user_pool.lock);
    return vaddr;
}

/* 将虚拟地址vaddr和内存池物理地址关联 */
/* 仅支持一页*/
void *get_a_page(enum pool_flags pf, uint32_t vaddr)
{
    struct pool *mem_pool = (pf == PF_KERNEL) ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);

    struct task_struct *cur = running_thread();
    int32_t bit_idx = -1;
    if (cur->pgdir != NULL && pf == PF_USER)
    {
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else if (cur->pgdir == NULL && pf == PF_KERNEL)
    {
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else
    {
        PANIC("get_a_page:not allow kernel alloc userspace or user alloc kernelspace by get_a_page");
    }

    void *page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL)
    {
        return NULL;
    }
    page_table_add((void *)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}

/*得到虚拟地址映射到的物理地址*/
uint32_t addr_v2p(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);

    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

/* 初始化内存池 */
void mem_pool_init(uint32_t all_mem)
{
    put_str("  mem_pool_init start\n");

    uint32_t page_table_size = 256 * PG_SIZE;     // 计算页表使用的内存的大小
    uint32_t used_mem = page_table_size + 0x100000; // 计算已用内存
    uint32_t free_mem = all_mem - used_mem;         // 计算总可用内存

    uint16_t all_free_pages = free_mem / PG_SIZE;                // 计算总可用页数
    uint16_t kernel_free_pages = all_free_pages / 2;               // 内核可用页数
    uint16_t user_free_pages = all_free_pages - kernel_free_pages; // 用户可用页数

    uint32_t kbm_len = kernel_free_pages / 8; // 内核位图长度
    uint32_t ubm_len = user_free_pages / 8;   // 用户位图长度

    uint32_t kp_start = used_mem;                                 // 内核内存池起始地址
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // 用户内存池起始地址

    kernel_pool.phy_addr_start = kp_start; // 设置内核内存池起始地址
    user_pool.phy_addr_start = up_start;   // 设置用户内存池起始地址

    kernel_pool.pool_size = kernel_free_pages * PG_SIZE; // 设置内核内存池大小
    user_pool.pool_size = user_free_pages * PG_SIZE;     // 设置用户内存池大小

    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_len; // 设置内核位图长度
    user_pool.pool_bitmap.btmp_bytes_len = ubm_len;   // 设置用户位图长度

    kernel_pool.pool_bitmap.btmp_bits = (void *)MEM_BITMAP_BASE;           // 设置内核位图地址
    user_pool.pool_bitmap.btmp_bits = (void *)(MEM_BITMAP_BASE + kbm_len); // 设置用户位图地址

    /* 输出内存池信息 */
    put_str("    all_free_pages: ");
    put_int(all_free_pages);
    put_char('\n'); // 总内存512mb，使用了2mb，可用页 510mb/4kb=127000
    put_str("    kernel_free_pages: ");
    put_int(kernel_free_pages);
    put_char('\n');
    put_str("    user_free_pages: ");
    put_int(user_free_pages);
    put_char('\n');

    put_str("    kernel_pool_bitmap_start: ");
    put_int((int)kernel_pool.pool_bitmap.btmp_bits);
    put_char('\n');
    put_str("    kernel_pool_phy_addr_start: ");
    put_int(kernel_pool.phy_addr_start);
    put_char('\n');

    put_str("    user_pool_bitmap_start: ");
    put_int((int)user_pool.pool_bitmap.btmp_bits);
    put_char('\n');
    put_str("    user_pool_phy_addr_start: ");
    put_int(user_pool.phy_addr_start);
    put_char('\n');

    /* 将池内位图置0 */
    bitmap_init(&kernel_pool.pool_bitmap); // 初始化内核位图
    bitmap_init(&user_pool.pool_bitmap);   // 初始化用户位图

    /* 初始化内核虚拟地址的位图 */
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_len; // 设置内核虚拟地址位图长度
    kernel_vaddr.vaddr_bitmap.btmp_bits =
        (void *)(MEM_BITMAP_BASE + kbm_len + ubm_len); // 设置内核虚拟地址位图地址
    kernel_vaddr.vaddr_start = K_HEAP_START;           // 设置内核虚拟地址起始位置
    bitmap_init(&kernel_vaddr.vaddr_bitmap);           // 初始化内核虚拟地址位图
    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);
    put_str("  mem_pool_init done\n");
}

/*初始化mem_block_desc，为malloc作准备*/
void block_init(struct mem_block_desc *desc_array)
{
    uint16_t desc_index;      // 内存块描述符数组索引
    uint16_t block_size = 16; // 目前每个小块的大小
    for (desc_index = 0; desc_index < DESC_CNT; desc_index++)
    {
        desc_array[desc_index].block_size = block_size;
        desc_array[desc_index].block_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[desc_index].free_list);
        block_size *= 2;
    }
}

/* 内存管理初始化入口 */
void mem_init(void)
{
    put_str("mem_init start\n");
    // 之前loader在开启分页时就获取了全部内存的大小，放到了0xb00中
    uint32_t mem_bytes_total = (*(uint32_t *)(0xb00));
    mem_pool_init(mem_bytes_total); // 初始化内存池
    block_init(k_block_descs);      // 初始化mem_block_desc数组
    put_str("mem_init done\n");
}

/*返回arena中第idx内存块的地址*/
/*参考arena示意图，还是很好理解的，某块地址=起始地址+元数据+若干个块*/
static struct mem_block *arena2block(struct arena *ar, uint32_t idx)
{
    return (struct mem_block *)((uint32_t)ar + sizeof(struct arena) + idx * ar->desc->block_size);
}

/*返回b内存块对应的arena起始地址*/
/*由于arnea占据一个完整的页，被4kb整除，所以块的前20位地址就是arnea的地址*/
static struct arena *block2arena(struct mem_block *b)
{
    return (struct arena *)((uint32_t)b & 0xfffff000);
}

/*在堆中申请size个字节的内存*/
void *sys_malloc(uint32_t size)
{
    enum pool_flags PF;
    struct pool *mem_pool;
    uint32_t pool_size;
    struct mem_block_desc *descs;
    struct task_struct *cur = running_thread();
    // 判断使用哪个内存池
    if (cur->pgdir == NULL)
    {
        PF = PF_KERNEL;
        mem_pool = &kernel_pool;
        pool_size = kernel_pool.pool_size;
        descs = k_block_descs; // 全局变量
    }
    else
    {
        PF = PF_USER;
        mem_pool = &user_pool;
        pool_size = user_pool.pool_size;
        descs = cur->u_block_desc;
    }
    // 如果申请的内存不在内存池容量范围内
    if (!(size > 0 && size < pool_size))
    {
        return NULL;
    }

    struct arena *a;
    struct mem_block *b;
    lock_acquire(&mem_pool->lock); // 保证互斥
    if (size > 1024)               // 需要整页分配
    {
        // 计算需要的页数，向上取整
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);

        a = malloc_page(PF, page_cnt);
        if (a != NULL) // 成功申请
        {
            memset(a, 0, page_cnt * PG_SIZE); // 清零以备使用
            a->desc = NULL;
            a->cnt = page_cnt;
            a->large = true;
            return (void *)(a + 1); // 返回剩余内存
        }
        else
        {
            lock_release(&mem_pool->lock);
            return NULL;
        }
    }
    else // 分配小块即可
    {
        uint8_t desc_idx;
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
        {
            if (size <= descs[desc_idx].block_size) // 寻找大小合适的块
            {
                break;
            }
        }
        if (list_empty(&descs[desc_idx].free_list)) // 如果对应的大小已经没有空余的块，就要创建新的arena
        {
            a = malloc_page(PF, 1);
            if (a == NULL)
            {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(a, 0, PG_SIZE);

            // 更新desc信息
            a->desc = &descs[desc_idx];
            a->cnt = descs[desc_idx].block_per_arena;
            a->large = false;

            // 将新的arena拆成小块放入队列
            uint32_t block_idx;
            enum intr_status old_status = intr_disable();
            for (block_idx = 0; block_idx < descs[desc_idx].block_per_arena; block_idx++)
            {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);
            }
            intr_set_status(old_status);
        }
        // 开始分配内存块
        b = (struct mem_block *)(list_pop(&descs[desc_idx].free_list));
        memset(b, descs[desc_idx].block_size, 0); // 清理一个小块
        a = block2arena(b);
        a->cnt--;
        lock_release(&mem_pool->lock);
        return (void *)b;
    }
}

/*将物理地址pg_phy_addr回收到物理内存池，实现单页回收*/
void pfree(uint32_t pg_phy_addr)
{
    struct pool *mem_pool;
    uint32_t bit_idx = 0;
    if (pg_phy_addr >= user_pool.phy_addr_start)
    {
        mem_pool = &user_pool;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
    }
    else
    {
        mem_pool = &kernel_pool;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}

/*去除页表中vaddr虚拟地址的映射，即vaddr对应的pte页表项设为0*/
static void page_table_pte_remove(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1; // 只清除P位，不影响其他位
    asm volatile("invlpg %0" : : "m"(vaddr) : "memory");
}

/*在虚拟地址池中释放vaddr起始的连续pg_cnt个内存页*/
static void vaddr_remove(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;
    if (pf == PF_KERNEL)
    {
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt)
        {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt, 0);
            cnt++;
        }
    }
    else
    {
        struct task_struct *cur = running_thread();
        bit_idx_start = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt)
        {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt, 0);
            cnt++;
        }
    }
}

/*释放以虚拟地址vaddr为起始的cnt个物理页框*/
void mfree_page(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t pg_phy_addr;
    uint32_t vaddr = (uint32_t)_vaddr, cnt = 0;
    ASSERT(pg_cnt >= 1 && (vaddr % PG_SIZE) == 0);
    pg_phy_addr = addr_v2p(vaddr);

    // 确保待释放的物理内存在1MB低端内存+1KB页目录表+1KB页表地址外
    // 同时还必须是整4KB地址
    ASSERT(pg_phy_addr >= 0x102000 && (pg_phy_addr % PG_SIZE) == 0);

    if (pg_phy_addr >= user_pool.phy_addr_start)
    {
        vaddr -= PG_SIZE; // 为了对齐
        while (cnt < pg_cnt)
        {
            vaddr += PG_SIZE; // 每次都能正确指向下一个页
            pg_phy_addr = addr_v2p(vaddr);
            // 确保待回收地址在用户物理地址池内，并且是整4KB地址
            //put_int(user_pool.phy_addr_start);
            //put_char(' ');
            //put_int(pg_phy_addr);
            ASSERT(pg_phy_addr >= user_pool.phy_addr_start && (pg_phy_addr % PG_SIZE) == 0);
            pfree(pg_phy_addr);
            page_table_pte_remove(vaddr);
            cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
    else
    {
        vaddr -= PG_SIZE;
        while (cnt < pg_cnt)
        {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            // 确保待回收地址只在内核物理地址池内，并且是整4KB地址
            ASSERT(pg_phy_addr >= kernel_pool.phy_addr_start &&
                   pg_phy_addr < user_pool.phy_addr_start &&
                   (pg_phy_addr % PG_SIZE) == 0);
            pfree(pg_phy_addr);
            page_table_pte_remove(vaddr);
            cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
}

/*回收ptr处的内存*/
void sys_free(void *ptr)
{
    ASSERT(ptr != NULL);
    if (ptr != NULL)
    {
        enum pool_flags pf;
        struct pool *mem_pool;
        /*判断是线程还是进程*/
        if (running_thread()->pgdir == NULL)
        {
            ASSERT((uint32_t)ptr >= K_HEAP_START);
            pf = PF_KERNEL;
            mem_pool = &kernel_pool;
        }
        else
        {
            pf = PF_USER;
            mem_pool = &user_pool;
        }

        lock_acquire(&mem_pool->lock);
        struct mem_block *b = ptr;
        struct arena *a = block2arena(b);
        ASSERT(a->large == 1 || a->large == 0);
        // 判断是整页还是小块
        if (a->desc == 0 || a->large == true)
        {
            mfree_page(pf, a, a->cnt);
        }
        else
        {
            // 先把目前这个小块放回空闲队列
            list_append(&a->desc->free_list, &b->free_elem);
            // 然后判断这个arena是否空闲，是的话释放整个arena
            if (++a->cnt == a->desc->block_per_arena)
            {
                uint32_t block_idx;
                for (block_idx = 0; block_idx < a->desc->block_per_arena; ++block_idx)
                {
                    struct mem_block *b = arena2block(a, block_idx);
                    ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                    list_remove(&b->free_elem);
                }
                mfree_page(pf, a, 1);
            }
        }
        lock_release(&mem_pool->lock);
    }
}