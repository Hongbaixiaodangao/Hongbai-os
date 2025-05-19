#ifndef __FS_FILE_H
#define __FS_FILE_H

#include "../lib/stdint.h"
struct inode;     // 前向声明，代替inode
struct partition; // 代替ide.h
struct dir;       // 代替dir.h

/*文件结构，每次打开文件就会创建一个此结构
 *记录偏移量，打开情况，inode指针等信息*/
struct file
{
    uint32_t fd_pos;        // 记录文件操作的偏移地址，最小是0,最大是size-1
    uint32_t fd_flag;       // 记录是否打开
    struct inode *fd_inode; // 记录此文件的inode指针
};

/*标准输入输出描述符*/
enum std_fd
{
    stdin_no,  // 0,标准输入
    stdout_no, // 1,标准输出
    stderr_no  // 2，标准错误
};

/*位图类型*/
enum bitmap_type
{
    INODE_BITMAP, // inode位图
    BLOCK_BITMAP  // 块位图
};

#define MAX_FILE_OPEN 32 // 系统可打开的最大文件数

/*从文件表file_table中获取一个空闲位，成功返回下标，失败返回-1*/
int32_t get_free_slot_in_global(void);
/*将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中
 *成功返回下标，失败返回-1*/
int32_t pcb_fd_install(int32_t global_fd_idx);
/*分配一个i节点，返回i结点号*/
int32_t inode_bitmap_alloc(struct partition *part);
/*分配1个扇区，返回扇区地址*/
int32_t block_bitmap_alloc(struct partition *part);
/*将内存中bitmap第bit_idx位所在的512个字节同步到硬盘*/
void bitmap_sync(struct partition *part, uint32_t bit_idx, uint8_t btmp);
/*创建文件，成功返回文件描述符，否则返回-1*/
int32_t file_create(struct dir *parent_dir, char *filename, uint8_t flag);

extern struct file file_table[MAX_FILE_OPEN];
#endif