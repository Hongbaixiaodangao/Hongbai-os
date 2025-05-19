#ifndef __FS_FS_H
#define __FS_FS_H

#include "../lib/stdint.h"
struct partition; // 前向声明，代替ide.h
struct dir;       // 代替dir.h

#define MAX_FILES_PER_PART 4096 // 每个扇区最大支持文件数
#define BITS_PER_SECTOR 4096    // 每扇区的位数
#define SECTOR_SIZE 512         // 每扇区的字节数
#define BLOCK_SIZE SECTOR_SIZE  // 块字节大小 我们设置为1个块==1个扇区
#define MAX_PATH_LEN 512        // 路径最大长度

/*文件类型枚举*/
enum file_types
{
    FT_UNKNOWN,  // 0，未知文件类型
    FT_REGULAR,  // 1，普通文件类型
    FT_DIRECTORY // 2，目录文件类型
};

/*打开文件的选项枚举*/
enum oflags
{
    O_RDONLY,   // 只读
    O_WRONLY,   // 只写
    O_RDWR,     // 读写
    O_CREAT = 4 // 创建
};

/*记录查找过程中的上级路径*/
struct path_search_record
{
    char searched_path[MAX_PATH_LEN]; // 父路径
    struct dir *parent_dir;           // 直接父目录
    enum file_types file_type;        // 找到的文件的类型
};

void filesys_init(void);                /*在磁盘上搜索文件系统，若没有则格式化分区创建文件系统*/
int32_t path_depth_cnt(char *pathname); /*返回路径深度*/
int32_t sys_open(const char *pathname, uint8_t flags);

extern struct partition *cur_part;
#endif