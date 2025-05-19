#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H

#include "../lib/stdint.h"
struct super_block
{
    uint32_t magic;         // 标识文件系统类型
    uint32_t sec_cnt;       // 本分区扇区数
    uint32_t inode_cnt;     // 本分区inode数
    uint32_t part_lba_base; // 分区起始lba

    uint32_t block_bitmap_lba;   // 块位图起始lba
    uint32_t block_bitmap_sects; // 块位图占用扇区数

    uint32_t inode_bitmap_lba;   // indoe位图起始lba
    uint32_t inode_bitmap_sects; // inode位图占用扇区数

    uint32_t inode_table_lba;   // inode表起始lba
    uint32_t inode_table_sects; // inode表占用扇区数

    uint32_t data_start_lba; // 数据块起始lba
    uint32_t root_inode_no;  // 根目录i结点号
    uint32_t dir_entry_size; // 目录项大小

    uint8_t pad[460]; // 占位，凑足512字节大小
} __attribute__((packed));
#endif
