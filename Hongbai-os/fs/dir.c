
#include "dir.h"
#include "inode.h"            // 前向声明
#include "file.h"             //block_bitmap_alloc函数
#include "../device/ide.h"    // partition结构体
#include "../kernel/memory.h" // sys_malloc函数
#include "../kernel/debug.h"  //ASSERT哨兵
#include "../lib/stdio.h"     //printk函数
#include "../lib/string.h"    //strcmp函数

struct dir root_dir; // 根目录

/*打开根目录，即初始化*/
void open_root_dir(struct partition *part)
{
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0; // 偏移地址为0
}

/*在part分区打开i结点编号inode_no的目录，并返回目录指针*/
struct dir *dir_open(struct partition *part, uint32_t inode_no)
{
    struct dir *pdir = (struct dir *)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

/*在part分区内的pdir目录内寻找名为name的文件或目录
 *找到后返回true，并把目录项存在dir_e，否则返回false*/
bool search_dir_entry(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dir_e)
{
    /*对于我们的inode，只有13个指针，前12个是直接块指针，最后一个指针指向一级间接块索引表
     *索引表页也是一个块，512字节大小，内部包含128个4字节指针，再次指向128个块
     *当硬盘被虚拟抽象到内存后，LBA号也就抽象成了指针*/
    uint32_t block_cnt = 12 + 128; // 12个直接块+128个一级间接块指针
    // all_block保存此inode所有块指针，32位系统中，一个指针大小为4字节
    uint32_t *all_block = (uint32_t *)sys_malloc((12 + 128) * 4);
    if (all_block == NULL)
    {
        printk("search_dir_entry: sys_malloc for all_block failed");
        return false;
    }
    uint32_t block_idx = 0;

    // 先处理12个直接块
    while (block_idx < 12)
    {
        all_block[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx++;
    }
    block_idx = 0;
    // 如果使用了一级间接块表，处理一级间接块
    if (pdir->inode->i_sectors[12] != 0)
    {
        // 因为all_block的类型是uint32_t *，所以后面的+12其实是+12*4,目的是跳过12个直接块
        ide_read(part->my_disk, pdir->inode->i_sectors[12], all_block + 12, 1);
    }

    uint8_t *buf = (uint8_t *)sys_malloc(SECTOR_SIZE);
    struct dir_entry *p_de = (struct dir_entry *)buf; // 指向目录项的指针

    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size; // 计算一个扇区包含多少目录项

    while (block_idx < block_cnt) // 遍历这个目录拥有的所有的块
    {
        // 地址为0说明块内无数据，寻找下一个块
        if (all_block[block_idx] == 0)
        {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk, all_block[block_idx], buf, 1);

        // 现在我们读取了一个块的数据到buf中(也就是pd_e中)，接下来遍历这个扇区内所有的目录项
        uint32_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entry_cnt)
        {
            // 取反的原因涉及到strcmp函数的返回值
            if (!strcmp(p_de->filename, name))
            {
                // 找到了相关文件或目录，复制到指定内存dir_e
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);
                sys_free(all_block);
                return true;
            }
            dir_entry_idx++; // 进入下一个目录项
            p_de++;
        }
        block_idx++;                    // 此扇区已遍历完，进入下一个扇区
        p_de = (struct dir_entry *)buf; // 指向新扇区的buf
        memset(buf, 0, SECTOR_SIZE);    // 将buf清零
    }
    sys_free(buf);
    sys_free(all_block);
    return false;
}

/*关闭目录*/
void dir_close(struct dir *dir)
{
    /******************** 根目录不能被关闭 ********************
     *1 根目录自打开后就不应该关闭，否则还需要再次open_root_dir();
     *2 root_dir 所在的内存是低端1MB之内，并非在堆中，free会出问题 */
    if (dir == &root_dir)
    {
        return;
    }
    // 对于一般目录，关闭目录就是关闭目录文件inode，然后释放dir所占内存
    inode_close(dir->inode);
    sys_free(dir);
}

/*在内存中初始化目录项p_de*/
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, struct dir_entry *p_de)
{
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);
    memcpy(p_de->filename, filename, strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}

/*将目录项p_de写入父目录parent_dir中，io_buf由主调函数提供*/
bool sync_dir_entry(struct dir *parent_dir, struct dir_entry *p_de, void *io_buf)
{
    struct inode *dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

    ASSERT(dir_size % dir_entry_size == 0); // 目录大小是目录项大小的整数倍

    uint32_t dir_entry_per_sec = (512 / dir_entry_size); // 每扇区目录项数
    int32_t block_lba = -1;                              // 数据块lba

    uint8_t block_idx = 0;
    uint32_t all_block[140] = {0};
    // 处理直接块
    while (block_idx < 12)
    {
        all_block[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    struct dir_entry *dir_e = (struct dir_entry *)io_buf; // dir_e用来在io_buf中遍历目录项
    int32_t block_bitmap_idx = -1;                        // 数据块位图索引

    /*开始遍历扇区寻找空目录项，如果已有扇区没有空目录项，在文件大小范围内，申请新扇区*/
    while (block_idx < 140)
    {
        block_bitmap_idx = -1;
        if (all_block[block_idx] == 0) // 此块未使用
        {
            // 申请此块，获得lba
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) // 失败
            {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                return false;
            }
            // 数据块位图索引=当前lba-数据块起始lba
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);

            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            block_bitmap_idx = -1;

            // 先定位到空目录项所在扇区
            if (block_idx < 12) // 直接块
            {
                dir_inode->i_sectors[block_idx] = all_block[block_idx] = block_lba;
            }
            else if (block_idx == 12) // 还未分配一级间接块表地址
            {
                dir_inode->i_sectors[12] = block_lba; // 将上面获取的lba作为一级间接块表地址
                block_lba = -1;
                block_lba = block_bitmap_alloc(cur_part); // 分配第0个间接块地址
                if (block_lba == -1)                      // 分配失败
                {
                    block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0); // 将相应块位图设置为未使用
                    dir_inode->i_sectors[12] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed\n");
                    return false;
                }
                // 同步block_bitmap
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                all_block[12] = block_lba;
                // 写入硬盘
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_block + 12, 1);
            }
            else // 还有未分配的间接块
            {
                all_block[block_idx] = block_lba;
                // 我们更新硬盘中的一级间接表，让间接表多一个指向新扇区的指针
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_block + 12, 1);
            }

            // 将新目录项p_de写入新分配的间接块
            memset(io_buf, 0, 512);
            memcpy(io_buf, p_de, dir_entry_size);
            // 区别dir_inode->i_sectors[12]和all_block
            // 前者是inode内一级块表，通过它索引到all_block后128项
            ide_write(cur_part->my_disk, all_block[block_idx], io_buf, 1);
            dir_inode->i_size += dir_entry_size;
            return true;
        }

        /*对应此块未使用的情况，如果此块已被使用，将块读入内存
         *然后寻找块内有没有空目录项*/
        ide_read(cur_part->my_disk, all_block[block_idx], io_buf, 1);
        uint8_t dir_entry_idx = 0; // 用于按目录项遍历块
        while (dir_entry_idx < dir_entry_per_sec)
        {
            if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN)
            {
                // FT_UNKNOWN代表未使用或已删除，总之就是空白
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
                ide_write(cur_part->my_disk, all_block[block_idx], io_buf, 1);
                dir_inode->i_size += dir_entry_size;
                return true;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    printk("directory is full!\n");
    return false;
}