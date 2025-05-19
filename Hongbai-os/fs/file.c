#include "file.h"
#include "inode.h"            //对应inode前向声明
#include "fs.h"               //cur_part全局变量
#include "dir.h"              //dir/dir_entry
#include "../device/ide.h"    //对应partition前向声明
#include "../thread/thread.h" //pcb
#include "../lib/stdio.h"     //printk
#include "../lib/string.h"
#include "../lib/stdint.h"

/*文件表，前三个成员预留给标准输入、标准输出、标准错误*/
struct file file_table[MAX_FILE_OPEN];

/*从文件表file_table中获取一个空闲位，成功返回下标，失败返回-1*/
int32_t get_free_slot_in_global(void)
{
    uint32_t fd_idx = 3; // 跳过0、1、2
    while (fd_idx < MAX_FILE_OPEN)
    {
        if (file_table[fd_idx].fd_inode == NULL)
        {
            break; // i结点==NULL说明这个文件结构没被使用，存在空闲
        }
        fd_idx++;
    }
    if (fd_idx == MAX_FILE_OPEN)
    {
        // 超出最大打开文件数限制
        printk("exceed max open files\n");
        return -1;
    }
    return fd_idx;
}

/*将全局描述符下标安装到进程或线程自己的文件描述符数组fd_table中
 *成功返回下标，失败返回-1*/
int32_t pcb_fd_install(int32_t global_fd_idx)
{
    struct task_struct *cur = running_thread();
    uint8_t local_fd_idx = 3;
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC)
    {
        // 我们在线程初始化的时候，用-1表示线程文件表空位，所以-1对应可以使用
        if (cur->fd_table[local_fd_idx] == -1)
        {
            cur->fd_table[local_fd_idx] = global_fd_idx;
            break;
        }
        local_fd_idx++;
    }
    if (local_fd_idx == MAX_FILES_OPEN_PER_PROC)
    {
        // 超出单个进程最大打开文件数限制
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}

/*分配一个i节点，返回i结点号*/
int32_t inode_bitmap_alloc(struct partition *part)
{
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1)
    {
        return -1; // 申请失败
    }
    // 申请成功，设置位图并返回编号
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

/*分配1个扇区，返回扇区lba地址*/
int32_t block_bitmap_alloc(struct partition *part)
{
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1)
    {
        return -1; // 申请失败
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    // 返回扇区lba号
    return (part->sb->data_start_lba + bit_idx);
}

/*内存中某个bitmap第bit_idx位修改后，将对应的修改同步到硬盘*/
void bitmap_sync(struct partition *part, uint32_t bit_idx, uint8_t btmp)
{
    // 一个位图的一个位进行了修改，说明512字节的一个扇区被分配或者回收，需要更新硬盘位图
    // 位图结构总是整扇区大小的，一次至少更新512字节
    uint32_t off_sec = bit_idx / 4096; // 找到位图对应的硬盘上的扇区偏移量
    uint32_t off_size = off_sec * 512; // 对应的字节偏移量
    uint32_t sec_lba = 0;
    uint8_t *bitmap_off;
    if (btmp == INODE_BITMAP)
    {
        sec_lba = part->sb->inode_bitmap_lba + off_sec;
        bitmap_off = part->inode_bitmap.btmp_bits + off_size;
    }
    else if (btmp == BLOCK_BITMAP)
    {
        sec_lba = part->sb->block_bitmap_lba + off_sec;
        bitmap_off = part->block_bitmap.btmp_bits + off_size;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

/*创建文件，成功返回文件描述符，否则返回-1*/
int32_t file_create(struct dir *parent_dir, char *filename, uint8_t flag)
{
    (void)flag; /****避免警告****/
    /*创建后续的公共缓冲区*/
    void *io_buf = sys_malloc(1024);
    if (io_buf == NULL)
    {
        printk("sys_malloc for io_buf failed\n");
        return -1;
    }
    uint8_t rollback_step = 0; // 用于操作失败时状态回滚

    /*为新文件分配inode*/
    int i_no = inode_bitmap_alloc(cur_part);
    if (i_no == -1)
    {
        printk("inode_bitmap_alloc for i_no failed\n");
        return -1;
    }
    struct inode *new_file_inode = (struct inode *)sys_malloc(sizeof(struct inode));
    if (new_file_inode == NULL)
    {
        printk("sys_malloc for inode failed\n");
        rollback_step = 1;
        goto rollback;
    }
    inode_init(i_no, new_file_inode);

    /*处理文件结构和文件表*/
    int fd_idx = get_free_slot_in_global(); // 在文件表中找到空位
    if (fd_idx == -1)
    {
        printk("get_free_slot_in_global for fd_idx failed\n");
        rollback_step = 2;
        goto rollback;
    }
    file_table[fd_idx].fd_flag = FT_REGULAR;
    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_inode->write_deny = false;

    /*文件目录项相关*/
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(filename, i_no, FT_REGULAR, &new_dir_entry);

    /*同步内存数据到硬盘*/
    // 1.在父目录下安装目录项
    if (sync_dir_entry(parent_dir, &new_dir_entry, io_buf) == false)
    {
        printk("sync dir_entry to disk failed\n");
        rollback_step = 3;
        goto rollback;
    }
    
    // 2.同步父目录inode
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, parent_dir->inode, io_buf);
    // 3.同步自身inode
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, new_file_inode, io_buf);
    // 4.同步inode_bitmap
    bitmap_sync(cur_part, i_no, INODE_BITMAP);
    // 5.将inode加入到open_inode链表
    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;

    sys_free(io_buf);
    return pcb_fd_install(fd_idx);

/*错误回滚*/
rollback:
    switch (rollback_step) // 注意前两个分支没有break，因为都要执行
    {
    case 3:
        memset(&file_table[fd_idx], 0, sizeof(struct file)); // 清空文件表
    case 2:
        sys_free(new_file_inode);
    case 1:
        bitmap_set(&cur_part->inode_bitmap, i_no, 0);
        break;
    }
    sys_free(io_buf);
    return -1;
}