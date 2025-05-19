#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H

#include "../lib/stdint.h"
#include "../lib/kernel/list.h"
#include "../thread/sync.h"
#include "../fs/super_block.h"

/*分区结构*/
struct partition
{
    uint32_t start_lba;         // 起始扇区
    uint32_t sec_cnt;           // 扇区数
    struct disk *my_disk;       // 分区所属的硬盘
    struct list_elem part_tag;  // 对列标记
    char name[8];               // 分区名称
    struct super_block *sb;     // 超级块
    struct bitmap block_bitmap; // 块的位图
    struct bitmap inode_bitmap; // i节点位图
    struct list open_inodes;    // i节点队列
};

/*硬盘结构*/
struct disk
{
    char name[8];                    // 硬盘名称
    struct ide_channel *my_channel;  // 硬盘使用的ide通道
    uint8_t dev_no;                  // 本硬盘是主盘还是从盘，主0从1
    struct partition prim_parts[4];  // 主分区，上限为4
    struct partition logic_parts[8]; // 逻辑分区，理论上无上限，我设置为只支持8个
};

/*ata通道结构*/
struct ide_channel
{
    char name[8];               // ata通道名称
    uint16_t port_base;         // 本通道起始端口号
    uint8_t irq_no;             // 本通道使用的中断号
    struct lock lock;           // 通道锁
    bool expecting_intr;        // 用来表示是否等待硬盘中断
    struct semaphore disk_done; // 用来阻塞、唤醒驱动程序
    struct disk devices[2];     // 一个通道上可以连接两个硬盘
};

void ide_read(struct disk *hd, uint32_t lba, void *buf, uint8_t sec_cnt);  /*从硬盘sec_cnt个扇区读取数据到内存buf的全过程*/
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint8_t sec_cnt); /*从内存buf写入sec_cnt个扇区数据到硬盘的全过程*/
void intr_hd_handler(uint8_t irq_no);                                      /*硬盘中断处理程序*/
void ide_init(void);                                                       /*硬盘驱动程序初始化*/
/*以下三个外部变量，实现在ide.c，会在fs.c中再次使用*/
extern uint8_t channel_cnt;
extern struct ide_channel channels[2];
extern struct list partition_list;
#endif
