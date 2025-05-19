
#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "../lib/stdint.h"
#include "../lib/kernel/list.h" //list_elem结构体
struct partition;               // 前向声明

/*inode结构体*/
struct inode
{
    uint32_t i_no;        // 编号
    uint32_t i_size;      // 大小
    uint32_t i_open_cnts; // 记录此文件被打开次数
    bool write_deny;      // 写文件时检查此标识，确保不存在并行，false对应可读可写状态
    // 对于硬盘的指针，就是硬盘的lba地址，通过lba号索引到对应的扇区
    uint32_t i_sectors[13]; // 这个数组前12项是直接块指针，13项存储一级间接块索引表指针
    struct list_elem inode_tag;
};

/*将inode写入到分区part*/
void inode_sync(struct partition *part, struct inode *inode, void *io_buf);
/*根据i节点号返回i节点指针*/
struct inode *inode_open(struct partition *part, uint32_t inode_no);
/*关闭indoe或减少inode打开数*/
void inode_close(struct inode *inode);
/*初始化new_inode*/
void inode_init(uint32_t inode_no, struct inode *new_inode);
#endif