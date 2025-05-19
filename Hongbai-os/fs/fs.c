#include "fs.h"
#include "inode.h"
#include "dir.h"
#include "super_block.h"
#include "file.h"
#include "../lib/stdint.h"
#include "../lib/kernel/list.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "../device/ide.h" //partition
#include "../kernel/debug.h"

struct partition *cur_part; // 记录默认情况下操作的分区

/* 在分区链表中找到名为part_name的分区，并将其指针赋值给cur_part */
static bool mount_partition(struct list_elem *pelem, int arg)
{
    char *part_name = (char *)arg;
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    if (!strcmp(part->name, part_name))
    {
        cur_part = part;
        struct disk *hd = cur_part->my_disk;
        // 创建用来保存超级块的缓冲区
        struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);
        // 在内存创建cur_part的超级块
        cur_part->sb = (struct super_block *)sys_malloc(sizeof(struct super_block));
        if (cur_part->sb == NULL)
        {
            PANIC("alloc memory failed!");
        }
        /*读入超级块到缓冲区*/
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);
        /*把缓冲区超级块数据复制到cur_part的sb中*/
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));
        /*为什么要先读入缓冲区，再把缓冲区数据复制到相应的变量中？
         *缓冲区大小就是1扇区512字节，和硬盘读取标准对齐，而实际超级块结构体小于512字节
         *如果直接读入实际结构体，会导致硬盘读写很慢。*/

        /*将分区的块位图写入内存*/
        // 开辟内存空间给位图指针
        cur_part->block_bitmap.btmp_bits = (uint8_t *)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
        if (cur_part->block_bitmap.btmp_bits == NULL)
        {
            PANIC("alloc memory failed!");
        }
        // 设置位图长度
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
        // sb_buf->block_bitmap_sects等价于cur_part->sb->block_bitmap_sects
        // 给位图指针赋值
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.btmp_bits, sb_buf->block_bitmap_sects);

        /*将分区的inode位图写入内存*/
        cur_part->inode_bitmap.btmp_bits = (uint8_t *)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (cur_part->inode_bitmap.btmp_bits == NULL)
        {
            PANIC("alloc memory failed!");
        }
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.btmp_bits, sb_buf->inode_bitmap_sects);

        list_init(&cur_part->open_inodes);
        printk("mount %s done!\n", part->name);
        /*返回true是为了配合定义在list.c的list_traversal函数，和本函数功能无关
         *返回true时list_traversal停止对链表的遍历*/
        return true;
    }
    return false;
}

static void partition_format(struct disk *hd, struct partition *part)
{
    uint32_t boot_sector_sects = 1; // 根目录扇区
    uint32_t super_block_sects = 1; // 超级块扇区
    // inode位图所占扇区
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    // inode表所占扇区
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) * MAX_FILES_PER_PART)), SECTOR_SIZE);
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;

    // 块位图所占扇区
    uint32_t block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    // 将超级块初始化
    struct super_block sb;
    sb.magic = 0x20250325;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    sb.block_bitmap_lba = sb.part_lba_base + 2;
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);

    printk("%s info:\n"
           "  magic:              0x%x\n"
           "  part_lba_base:      0x%x\n"
           "  all_sectors:        0x%x\n"
           "  inode_cnt:          0x%x\n"
           "  block_bitmap_lba:   0x%x\n"
           "  block_bitmap_sects: 0x%x\n"
           "  inode_bitmap_lba:   0x%x\n"
           "  inode_bitmap_sects: 0x%x\n"
           "  inode_table_lba:    0x%x\n"
           "  inode_table_sects:  0x%x\n"
           "  data_start_lba:     0x%x\n",
           part->name,
           sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt,
           sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba,
           sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects,
           sb.data_start_lba);

    // 1.将超级块写入本分区1扇区
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("  super_blcok_lba:    0x%x\n", part->start_lba + 1);
    // 开辟一块缓冲区，大小为三个属性中最大的
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects) ? sb.block_bitmap_sects : sb.inode_bitmap_sects;
    buf_size = (buf_size >= sb.inode_table_sects) ? buf_size : sb.inode_table_sects;
    buf_size *= SECTOR_SIZE;
    uint8_t *buf = (uint8_t *)sys_malloc(buf_size);

    // 2.将块位图初始化并写入sb.block_bitmap_lba
    buf[0] |= 0x01; // 0号块留给根目录
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
    // last_size是保存位图的最后一个扇区中，多余出的位
    uint32_t last_size = (SECTOR_SIZE - block_bitmap_last_byte % SECTOR_SIZE);
    // 先将超出实际块数的部分设置为已占用1
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);
    // 在将有效位重新设置为未占用0
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit)
    {
        // 通过取反+左移，实现逐位清零
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    // 3.将inode位图初始化并写入sb.inode_bitmap_lba
    // 清空缓冲区
    memset(buf, 0, buf_size);
    buf[0] |= 0x1;
    /*inode_table中有4096个inode，正好一个扇区，
     *inode_bitmap扇区没有多余无效位，不需要进一步处理*/
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    // 4 将inode数组初始化并写入sb.inode_table_lba
    // 初始化了第一个indoe
    memset(buf, 0, buf_size);
    struct inode *i = (struct inode *)buf;
    i->i_size = sb.dir_entry_size * 2; // 留出..和.目录
    i->i_no = 0;
    i->i_sectors[0] = sb.data_start_lba;
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    // 5.将根目录写入sb.data_start_lba
    memset(buf, 0, buf_size);
    struct dir_entry *p_de = (struct dir_entry *)buf;

    // 初始化当前目录.
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    // 初始化父目录..
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;

    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("  root_dir_lba:       0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}

/*在磁盘上搜索文件系统，若没有则格式化分区创建文件系统*/
void filesys_init()
{
    uint8_t channel_no = 0, dev_no, part_idx = 0;
    // 开辟超级块缓冲区
    struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);
    if (sb_buf == NULL)
    {
        PANIC("alloc memory failed!");
    }
    printk("searching filesystem......\n");
    while (channel_no < channel_cnt) // channel_cnt声明在ide.h，实现在ide.c
    {
        dev_no = 0;
        while (dev_no < 2) // 一个通道可以挂载2个设备
        {
            if (dev_no == 0)
            {
                dev_no++;
                continue;
            }
            struct disk *hd = &channels[channel_no].devices[dev_no];
            struct partition *part = hd->prim_parts; // 初始指向4个主分区

            while (part_idx < 12) // 4主分区+8逻辑分区
            {
                if (part_idx == 4)
                {
                    part = hd->logic_parts; // 开始处理逻辑分区
                }
                if (part->sec_cnt != 0)
                {
                    memset(sb_buf, 0, SECTOR_SIZE);
                    // 读取超级块，根据魔数判断是否存在文件系统
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
                    // 魔数匹配，说明存在我的文件系统
                    if (sb_buf->magic == 0x20250325)
                    {
                        printk("    %s has file system\n", part->name);
                    }
                    // 不匹配，认为不存在文件系统，于是创建我的操作系统
                    else
                    {
                        // 提示正在进行初始化
                        printk("formatting %s's partition %s......\n", hd->name, part->name);
                        // 调用函数创建每个分区的文件系统
                        partition_format(hd, part);
                    }
                }
                part_idx++;
                part++; // 进入下一分区
            }
            dev_no++; // 进入下一磁盘
        }
        channel_no++; // 进入下一通道
    }
    sys_free(sb_buf);

    /*确定默认操作分区*/
    char default_part[8] = "sdb1";
    /*挂载分区*/
    list_traversal(&partition_list, mount_partition, (int)default_part);
    /*打开当前分区的根目录*/
    open_root_dir(cur_part);
    /*初始化文件表*/
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN)
    {
        file_table[fd_idx].fd_inode = NULL;
        fd_idx++;
    }
}

/*将最上层路径名解析出来，在name_store保存当前路径名，然后返回解析后的子路径*/
static char *path_paser(char *pathname, char *name_store)
{
    if (pathname[0] == '/') // 跳过前面所有的/
    {
        while (*pathname == '/')
        {
            pathname++;
        }
    }
    // 开始一般的路径解析，提取最上层路径名，即从字符开始到第一个/停止
    while (*pathname != '/' && *pathname != 0) // 0是空字符ascii码
    {
        // 我不喜欢写自增，因为自增容易带来阅读障碍
        *name_store = *pathname;
        name_store++;
        pathname++;
    }
    if (pathname[0] == 0)
    {
        return NULL;
    }
    return pathname;
}

/*返回路径深度*/
int32_t path_depth_cnt(char *pathname)
{
    ASSERT(pathname != NULL);
    char *p = pathname;           // 用于保存每次path_paser返回的子路径
    char name[MAX_FILE_NAME_LEN]; // 用于保存每次path_paser返回的原文件名
    uint32_t depth = 0;
    p = path_paser(p, name);
    while (name[0] != 0) // 只要存在当前文件
    {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (p != 0) // 只要存在子路径
        {
            p = path_paser(p, name);
        }
    }
    return depth;
}

/*搜索文件pathname，若找到返回inode号，否则返回-1*/
static int search_file(const char *pathname, struct path_search_record *search_record)
{
    /*如果查找的是根目录，直接返回根目录信息*/
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/.."))
    {
        search_record->file_type = FT_DIRECTORY;
        search_record->parent_dir = &root_dir;
        search_record->searched_path[0] = 0;
        return 0;
    }

    uint32_t path_len = strlen(pathname);
    ASSERT(path_len < MAX_PATH_LEN);
    char *sub_path = (char *)pathname;
    struct dir *parent_dir = &root_dir;
    struct dir_entry dir_e; // 保存查到的目录项

    /*逐级解析路径*/
    char name[MAX_FILE_NAME_LEN] = {0};
    search_record->parent_dir = parent_dir;
    search_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0; // 父目录的inode号

    sub_path = path_paser(sub_path, name);
    while (name[0])
    {
        ASSERT(strlen(search_record->searched_path) < MAX_PATH_LEN);

        /*记录已经存在的路径*/
        strcat(search_record->searched_path, "/");
        strcat(search_record->searched_path, name);

        // 如果成功找到了当前本级文件名的目录项
        if (search_dir_entry(cur_part, parent_dir, name, &dir_e))
        {
            memset(name, 0, MAX_FILE_NAME_LEN);
            if (sub_path) // 如果还有子目录项
            {
                sub_path = path_paser(sub_path, name);
            }

            if (dir_e.f_type == FT_DIRECTORY) // 目录文件，需要进一步查找
            {
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part, dir_e.i_no);
                search_record->parent_dir = parent_dir;
                continue;
            }
            else if (dir_e.f_type == FT_REGULAR) // 普通文件，查找到了终点
            {
                search_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        }
        else // 没有找到当前文件名的目录项
        {
            return -1;
        }
    }
    /*至此，已经遍历完完整目录，并且最后一个文件是目录文件*/
    dir_close(search_record->parent_dir);
    // 更新查找记录
    search_record->parent_dir = dir_open(cur_part, parent_inode_no);
    search_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}

/*创建或打开文件，成功后返回文件描述符，否则返回-1*/
int32_t sys_open(const char *pathname, uint8_t flags)
{
    if (pathname[strlen(pathname) - 1] == '/') // 我们目前不处理目录
    {
        printk("can't open a directory %s\n", pathname);
        return -1;
    }
    ASSERT(flags <= 7);
    int32_t fd = -1;

    struct path_search_record search_record;
    memset(&search_record, 0, sizeof(struct path_search_record));
    uint32_t pathname_depth = path_depth_cnt((char *)pathname);

    int i_no = search_file(pathname, &search_record);
    bool found = (i_no != -1) ? true : false;

    if (search_record.file_type == FT_DIRECTORY)
    {
        printk("can't open a direcotry with open(), use opendir() to instead\n");
        dir_close(search_record.parent_dir);
        return -1;
    }
    uint32_t search_depth = path_depth_cnt(search_record.searched_path);

    // 判断整个路径是否都被访问到
    if (search_depth != pathname_depth)
    {
        // 某个路径不存在
        printk("cannot access %s: Not a directory, subpath %s is't exist\n",
               pathname,
               search_record.searched_path);
        dir_close(search_record.parent_dir);
        return -1;
    }
    if (!found && !(flags & O_CREAT))
    {
        // 没找到最后一个文件，并且也不是要创建新文件
        printk("in path %s,file %s is't exist\n",
               search_record.searched_path,
               (strrchr(search_record.searched_path, '/') + 1));
        dir_close(search_record.parent_dir);
        return -1;
    }
    else if (found && (flags & O_CREAT))
    {
        // 待创建的文件已经存在
        printk("%s has already exist!\n", pathname);
        dir_close(search_record.parent_dir);
        return -1;
    }
    switch (flags & O_CREAT)
    {
    case O_CREAT: // 开始创建文件
        printk("creating file\n");
        fd = file_create(search_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
        dir_close(search_record.parent_dir);

        // 其余是打开文件
    }
    // 返回任务pcb->fd_table下标
    return fd;
}