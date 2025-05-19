#include "inode.h"
#include "../device/ide.h"       //partition结构体
#include "../kernel/debug.h"     //ASSERT哨兵
#include "../kernel/interrupt.h" //中断状态枚举体和相关函数
#include "../kernel/memory.h"    //sys_molloc函数
#include "../thread/thread.h"    //pcb结构体
#include "../lib/string.h"       //memset,memcpy函数
#include "../lib/stdint.h"
#include "../lib/kernel/list.h" //list_elem结构体

// 已经编译过一次，没有编译错误了

/*用来存储inode位置的结构体*/
struct inode_position
{
    bool two_sec;      // 此inode是否跨区
    uint32_t sec_lba;  // 此inode起始扇区号
    uint32_t off_size; // 此inode在扇区内的字节偏移量
};

/*获取inode所在的扇区和扇区内的偏移量*/
static void inode_locate(struct partition *part, uint32_t inode_no, struct inode_position *inode_pos)
{
    ASSERT(inode_no < 4096);

    uint32_t inode_table_lba = part->sb->inode_table_lba;
    uint32_t inode_size = sizeof(struct inode);
    uint32_t off_size = inode_no * inode_size; // 第no号inode据inode数组起始位置的字节偏移量
    uint32_t off_sec = off_size / 512;         // 字节偏移量对应的扇区偏移量
    uint32_t off_size_in_sec = off_size % 512; // 待查找的inode在此扇区的起始地址

    // 判断此inode是否跨区
    uint32_t left_in_sec = 512 - off_size_in_sec; // 本扇区剩余存储空间
    // 剩余空间不够一个inode
    if (left_in_sec < inode_size)
    {
        inode_pos->two_sec = true;
    }
    else
    {
        inode_pos->two_sec = false;
    }
    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

/*将内存中的inode写入到硬盘分区part*/
void inode_sync(struct partition *part, struct inode *inode, void *io_buf)
{
    uint8_t inode_no = inode->i_no;
    struct inode_position inode_pos;
    // 调用上面的函数获取inode所在的扇区和偏移量，保存到inode_pos
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));

    // 以下三个成员只在内存中有意义，写入硬盘时清理掉即可
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = false; // 可读可写
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    // 以下，先把原有inode读取出来，更新后再写入
    char *inode_buf = (char *)io_buf;
    if (inode_pos.two_sec)
    {
        // 如果跨扇区了，需要读两个扇区
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        /*inode_buf + inode_pos.off_sizes是在缓冲区内的偏移地址
         *用于在512/1024字节的缓冲区内定位到实际indoe位置*/
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

/*根据i节点号返回i节点指针*/
struct inode *inode_open(struct partition *part, uint32_t inode_no)
{
    // 先在每个分区中存在的，打开的i节点链表中寻找i节点，此链表是为了提速创建的缓冲区
    struct list_elem *elem = part->open_inodes.head.next;
    struct inode *inode_found;
    while (elem != &part->open_inodes.tail)
    {
        inode_found = elem2entry(struct inode, inode_tag, elem);
        if (inode_found->i_no == inode_no) // 如果成功找到，inode打开次数+1,返回indoe地址
        {
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    /*目前在链表中没有找到，于是从硬盘中读入inode并加入链表*/
    struct inode_position inode_pos;
    // 调用locate函数，获知no对应的inode的信息
    inode_locate(part, inode_no, &inode_pos);

    /*为了让进程新创建的inode被共享，需要将inode放在内核区
     *需要临时将pcb的pgdir设置为NULL*/
    struct task_struct *cur = running_thread();
    uint32_t *cur_pagedir_bak = cur->pgdir; // 临时记录
    cur->pgdir = NULL;
    inode_found = (struct inode *)sys_malloc(sizeof(struct inode));
    cur->pgdir = cur_pagedir_bak;

    char *inode_buf;
    if (inode_pos.two_sec == true)
    {
        // 跨扇区读两个扇区
        inode_buf = (char *)sys_malloc(1024);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        inode_buf = (char *)sys_malloc(512);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));
    // 加入队列方便后续使用
    list_push(&part->open_inodes, &inode_found->inode_tag);
    // 队列里没有，说明这是第一次被打开，打开次数设置为1
    inode_found->i_open_cnts = 1;
    sys_free(inode_buf);
    return inode_found;
}

/*关闭indoe或减少inode打开数*/
void inode_close(struct inode *inode)
{
    enum intr_status old_status = intr_disable(); // 关inode应为原子操作
    if (--inode->i_open_cnts == 0)
    {
        list_remove(&inode->inode_tag);
        // 内存中新的inode开辟在内核空间，移除时也需要确保回收内核空间
        struct task_struct *cur = running_thread();
        uint32_t *cur_pagedir_bak = cur->pgdir; // 临时记录
        cur->pgdir = NULL;
        sys_free(inode);
        cur->pgdir = cur_pagedir_bak;
    }
    intr_set_status(old_status);
}

/*初始化new_inode*/
void inode_init(uint32_t inode_no, struct inode *new_inode)
{
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;
    // 初始化块索引数组i_sector
    uint8_t sec_idx = 0;
    while (sec_idx < 13)
    {
        new_inode->i_sectors[sec_idx] = 0;
        sec_idx++;
    }
}