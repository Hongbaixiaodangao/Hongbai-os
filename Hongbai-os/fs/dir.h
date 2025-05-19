#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "../lib/stdint.h"
#include "fs.h" //提供enum file_types
struct inode;   // inode前向声明

#define MAX_FILE_NAME_LEN 16 // 最大文件名长度

/*目录结构体*/
struct dir
{
    struct inode *inode;  // 目录作为文件，也有它的inode
    uint32_t dir_pos;     // 记录在此目录下的偏移
    uint8_t dir_buf[512]; // 目录的数据缓冲区
};

/*目录项结构体*/
struct dir_entry
{
    char filename[MAX_FILE_NAME_LEN]; // 普通文件或目录名称
    uint32_t i_no;                    // 目录项描述的文件对应的i结点编号
    enum file_types f_type;           // 文件类型，1对应普通文件，2对应目录文件
};

/*打开根目录*/
void open_root_dir(struct partition *part);
/*在part分区打开i结点编号inode_no的目录，并返回目录指针*/
struct dir *dir_open(struct partition *part, uint32_t inode_no);
/*在part分区内的pdir目录内寻找名为name的文件或目录
 *找到后返回true，并把目录项存在dir_e，否则返回false*/
bool search_dir_entry(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dir_e);
/*关闭目录*/
void dir_close(struct dir *dir);
/*在内存中初始化目录项p_de*/
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, struct dir_entry *p_de);
/*将目录项p_de写入父目录parent_dir中，io_buf由主调函数提供*/
bool sync_dir_entry(struct dir *parent_dir, struct dir_entry *p_de, void *io_buf);

extern struct dir root_dir;
#endif