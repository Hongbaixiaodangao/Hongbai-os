// 声明了虚拟将地址池结构体
#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H
#include "../lib/stdint.h"
#include "../lib/kernel/bitmap.h"
#include "../lib/kernel/list.h"

/* 内存池标记 */
enum pool_flags
{
    PF_KERNEL = 1, // 内核内存池
    PF_USER = 2,   // 用户内存池
};

#define PG_P_1 1         // 页表项的存在
#define PG_P_0 0         // 页表项的不存在
#define PG_RW_R 0        // 可读可执行
#define PG_RW_W 2        // 可读可写可执行
#define PG_US_S 0        // 内核特权级
#define PG_US_U (1 << 2) // 用户特权级
// 虚拟地址结构体，内部有一个位图结构体，还有一个虚拟地址起始位置
struct virtual_addr
{
    struct bitmap vaddr_bitmap; // 虚拟地址位图
    uint32_t vaddr_start;       // 虚拟地址起始位置
};

/*内存块*/
struct mem_block
{
    struct list_elem free_elem;
};

/*内存块描述符*/
struct mem_block_desc
{
    uint32_t block_size;      // 小内存块大小
    uint32_t block_per_arena; // 每个arena拥有的小内存块数量
    struct list free_list;    // 目前空闲的小内存块链表
};

#define DESC_CNT 7 // 总共有7种mem_block_desc

extern struct pool kernel_pool;                         // 内核内存池
extern struct pool user_pool;                           // 用户内存池
uint32_t *pte_ptr(uint32_t vaddr);                      /* 得到虚拟地址vaddr对应的pte的指针 */
uint32_t *pde_ptr(uint32_t vaddr);                      /* 得到虚拟地址vaddr对应的pde的指针 */
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt); /* 分配pg_cnt个页空间，成功则返回起始虚拟地址，失败时返回NULL */
void *get_kernel_pages(uint32_t pg_cnt);                /* 从内核物理内存池中申请pg_cnt页内存 */
void *get_user_page(uint32_t pg_cnt);
void *get_a_page(enum pool_flags pf, uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void mem_pool_init(uint32_t all_mem); // 内存池初始化
void block_init(struct mem_block_desc *desc_array);
void mem_init(void); // 内存管理初始化

void *sys_malloc(uint32_t size);
void pfree(uint32_t pg_phy_addr);
void mfree_page(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt);
void sys_free(void *ptr);
#endif
