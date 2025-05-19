
#include "ide.h"
#include "../lib/stdio.h"
#include "../kernel/debug.h"
#include "../kernel/global.h"
#include "../thread/sync.h"
#include "../kernel/io.h"
#include "timer.h"
#include "../kernel/interrupt.h"
#include "../lib/string.h"

/*主IDE通道寄存器端口的基址是0x1F0，从通道基址是0x170*/
#define reg_data(channel) (channel->port_base + 0)           // 读写数据，每次传输两字节
#define reg_error(channel) (channel->port_base + 1)          // 读取硬盘错误信息
#define reg_sect_cnt(channel) (channel->port_base + 2)       // 扇区计数
#define reg_lba_l(channel) (channel->port_base + 3)          // 起始扇区LBA低八位
#define reg_lba_m(channel) (channel->port_base + 4)          // 中八位
#define reg_lba_h(channel) (channel->port_base + 5)          // 高八位
#define reg_dev(channel) (channel->port_base + 6)            // 驱动器/磁头寄存器
#define reg_status(channel) (channel->port_base + 7)         // 状态寄存器
#define reg_cmd(channel) (reg_status(channel))               // 命令寄存器，和状态寄存器共用0x1F7端口
#define reg_alt_status(channel) (channel->port_base + 0x206) // 备用状态寄存器
#define reg_ctl(channel) (reg_alt_status(channel))           // 设备控制寄存器，和前者共用0x3F6端口

/*记录reg_alt_status备用状态寄存器的一些关键位*/
#define BIT_ALT_STAT_BSY 0x80  // 1000 0000,表示硬盘忙
#define BIT_ALT_STAT_DRDY 0x40 // 0100 0000,表示驱动器准备好了
#define BIT_ALT_STAT_DRQ 0x8   // 0000 1000,表示数据传输准备好了

/*记录device驱动器/磁头寄存器的一些关键位*/
#define BIT_DEV_MBS 0xa0 // 1010 0000，设置必须的保留位
#define BIT_DEV_LBA 0x40 // 0100 0000，启用LBA模式
#define BIT_DEV_DEV 0x10 // 0001 0000，如果使用此位，则选择了从盘

/*一些硬盘操作指令的指令码*/
#define CMD_IDENTIFY 0xec     // identify识别硬盘指令
#define CMD_READ_SECTOR 0x20  // 读扇区指令
#define CMD_WRITE_SECTOR 0x30 // 写扇区指令

// 定义可读写的最大扇区数，用于调试
#define max_lba ((80 * 1024 * 1024 / 512) - 1) // 只支持80MB硬盘

uint8_t channel_cnt;            // 通道数
struct ide_channel channels[2]; // 一个主板最多有两个通道

int32_t ext_lba_base = 0;   // 总扩展分区LBA基址
uint8_t p_no = 0, l_no = 0; // 主分区和逻辑分区的下标
struct list partition_list; // 分区队列

/*分区表项结构体*/
struct partition_table_entry
{
    uint8_t bootable;   // 是否可引导
    uint8_t start_head; // 起始磁头号
    uint8_t start_sec;  // 起始扇区号
    uint8_t start_chs;  // 起始柱面号
    uint8_t fs_type;    // 分区类型
    uint8_t end_head;   // 结束磁头号
    uint8_t end_sec;    // 结束扇区号
    uint8_t end_chs;    // 结束柱面号

    uint32_t start_lba;    // 本分区起始扇区LBA地址
    uint32_t sec_cnt;      // 本分区扇区数
} __attribute__((packed)); // 此关键字保证这个结构是16字节大小

/*引导扇区mbr/ebr结构体*/
struct boot_sector
{
    uint8_t other[446]; // 446字节的引导代码
    struct partition_table_entry partition_table[4];
    uint16_t signature; // 结束标志0x55 0xaa
} __attribute__((packed));

/*选择要读写的硬盘*/
static void select_disk(struct disk *hd)
{
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no == 1) // 如果是从盘
    {
        reg_device |= BIT_DEV_DEV;
    }
    // 通过汇编写入通道寄存器
    outb(reg_dev(hd->my_channel), reg_device);
}

/*向硬盘控制器写入起始扇区地址和要读写的扇区数*/
static void select_sector(struct disk *hd, uint32_t lba, uint8_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    struct ide_channel *channel = hd->my_channel;
    // 写入扇区数
    outb(reg_sect_cnt(channel), sec_cnt);
    // 写入0-23位LBA
    outb(reg_lba_l(channel), lba);
    outb(reg_lba_m(channel), lba >> 8);
    outb(reg_lba_h(channel), lba >> 16);
    // 还有24-27四位LBA要写在dev寄存器里
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

/*向通道channel发命令cmd*/
static void cmd_out(struct ide_channel *channel, uint8_t cmd)
{
    // 因为我们已经向通道发出了命令，所以将此位置为true，供硬盘中断处理程序使用
    channel->expecting_intr = true;
    outb(reg_cmd(channel), cmd);
}

/*从硬盘读出sec_cnt个扇区的数据到内存buf*/
static void read_from_sector(struct disk *hd, void *buf, uint8_t sec_cnt)
{
    // 先计算待读取字节数
    uint32_t byte = 0;
    if (sec_cnt == 0) // sec_cnt范围是0-255,如果传入256会变为0
    {
        byte = 256 * 512;
    }
    else
    {
        byte = sec_cnt * 512;
    }
    // 按字读取，1字=2字节
    insw(reg_data(hd->my_channel), buf, byte / 2);
}

/*从内存buf向硬盘写入sec_cnt个扇区的数据*/
static void write_to_sector(struct disk *hd, void *buf, uint8_t sec_cnt)
{
    uint32_t byte = 0;
    if (sec_cnt == 0) // sec_cnt范围是0-255,如果传入256会变为0
    {
        byte = 256 * 512;
    }
    else
    {
        byte = sec_cnt * 512;
    }
    outsw(reg_data(hd->my_channel), buf, byte / 2);
}

/*等待30秒，本质是优化了的自选锁*/
static bool busy_wait(struct disk *hd)
{
    struct ide_channel *channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;
    while (time_limit -= 10 >= 0)
    {
        // 如果硬盘不忙
        if (!(inb(reg_status(channel)) & BIT_ALT_STAT_BSY))
        {
            // 已经准备好数据传输
            return (inb(reg_status(channel)) & BIT_ALT_STAT_DRQ);
        }
        else
        {
            mtime_sleep(10); // 睡眠10ms，10ms后再次判断
        }
    }
    return false;
}

/*从硬盘sec_cnt个扇区读取数据到内存buf的全过程*/
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint8_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    lock_acquire(&hd->my_channel->lock); // 加锁表示通道已被占用

    // 1.选择要操作的硬盘
    select_disk(hd);

    uint32_t secs_op = 0;   // 本次要处理的扇区数，最大为256
    uint32_t secs_done = 0; // 已经处理的扇区数
    while (secs_done < sec_cnt)
    {
        if (sec_cnt - secs_done >= 256)
        {
            secs_op = 256;
        }
        else
        {
            secs_op = sec_cnt - secs_done;
        }
        // 2.写入待读取的扇区数和起始扇区号
        select_sector(hd, lba + secs_done, secs_op);
        // 3.写入读取命令到cmd寄存器
        cmd_out(hd->my_channel, CMD_READ_SECTOR);
        /*硬盘IO最慢的环节是硬盘内部处理环节，机械硬盘涉及到磁头移动等物理过程
         *此时，硬盘收到信号，开始在内部进行数据处理，我们可以让读取线程先阻塞自己
         *在读取完成后，通过中断处理程序唤醒线程，实现cpu的高效利用*/
        sema_down(&hd->my_channel->disk_done);

        /*此时，硬盘内部处理完成，程序被唤醒，继续接下来的环节*/
        // 4.检查硬盘状态是否可读
        if (!busy_wait(hd)) // 30秒内硬盘一直处于忙状态
        {
            char error[64];
            sprintf(error, "%s read sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }
        // 5.把读取出数据放到内存buf中
        read_from_sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);

        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

/*从内存buf写入sec_cnt个扇区数据到硬盘的全过程*/
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint8_t sec_cnt)
{
    // 写入和读取过程基本一致
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    lock_acquire(&hd->my_channel->lock);

    select_disk(hd); // 选取硬盘

    uint32_t secs_op = 0;
    uint32_t secs_done = 0;
    while (secs_done < sec_cnt)
    {
        if (sec_cnt - secs_done >= 256)
        {
            secs_op = 256;
        }
        else
        {
            secs_op = sec_cnt - secs_done;
        }
        select_sector(hd, lba + secs_done, secs_op); // 写入起始扇区号和扇区数
        cmd_out(hd->my_channel, CMD_WRITE_SECTOR);   // 写入写命令
        if (!busy_wait(hd))                          // 检验状态
        {
            char error[64];
            sprintf(error, "%s write sector %d failed!!!!!!\n", hd->name, lba);
            PANIC(error);
        }
        write_to_sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);
        sema_down(&hd->my_channel->disk_done); // 阻塞
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

/*硬盘中断处理程序*/
void intr_hd_handler(uint8_t irq_no)
{
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    // 0x2e对应14引脚，是主通道，0x2f对应从通道
    uint32_t no = irq_no - 0x2e;
    struct ide_channel *channel = &channels[no];
    ASSERT(channel->irq_no == irq_no);
    if (channel->expecting_intr == true)
    {
        channel->expecting_intr = false;
        sema_up(&channel->disk_done);
        // 修改状态，让硬盘可以执行新的读写
        inb(reg_status(channel));
    }
}

/*将dst中len个相邻字节交换位置后存入buf*/
static void swap_pairs_bytes(const char *dst, char *buf, uint32_t len)
{
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2)
    {
        buf[idx + 1] = *dst;
        dst++;
        buf[idx] = *dst;
        dst++;
    }
    buf[idx] = '\0';
}

/*获取硬盘参数信息*/
static void identify_disk(struct disk *hd)
{
    char id_info[512];
    select_disk(hd);
    cmd_out(hd->my_channel, CMD_IDENTIFY);
    sema_down(&hd->my_channel->disk_done);

    if (!busy_wait(hd))
    {
        char error[64];
        sprintf(error, "%s identify failed!!!!!!\n", hd->name);
        PANIC(error);
    }
    read_from_sector(hd, id_info, 1);

    char buf[64];
    // 硬盘序列号起始偏移量10个字，是长度20字节的的字符串
    uint8_t sn_start = 10 * 2, sn_len = 20;
    // 硬盘型号起始偏移量27个字，长度40字节
    uint8_t md_start = 27 * 2, md_len = 40;
    swap_pairs_bytes(&id_info[sn_start], buf, sn_len);
    printk("  disk %s info:\n", hd->name);
    printk("    SN: %s\n", buf); // 打印序列号
    memset(buf, 0, sizeof(buf));
    swap_pairs_bytes(&id_info[md_start], buf, md_len);
    printk("    MODULE: %s\n", buf); // 打印硬盘型号
    // 找到可供用户使用的扇区数这个参数
    uint32_t sector = *(uint32_t *)&id_info[60 * 2];
    // 打印可以使用的扇区数
    printk("    SECTORS: %d\n", sector);
    // 打印可以使用的内存容量
    printk("    CAPACITY: %dMB\n", sector * 512 / 1024 / 1024);
}

/*扫描硬盘hd中地址为ext_lba的扇区中的所有分区*/
static void partition_scan(struct disk *hd, uint32_t ext_lba)
{
    struct boot_sector *bs = sys_malloc(sizeof(struct boot_sector));
    ide_read(hd, ext_lba, bs, 1);
    uint8_t part_idx = 0;
    struct partition_table_entry *p = bs->partition_table;

    while ((part_idx++) < 4)
    {
        if (p->fs_type == 0x5) // 若为拓展分区
        {
            if (ext_lba_base != 0)
            {
                partition_scan(hd, p->start_lba + ext_lba_base); // 递归调用
            }
            else // 第一次读取引导块
            {
                ext_lba_base = p->start_lba;
                partition_scan(hd, p->start_lba);
            }
        }
        else if (p->fs_type != 0) // 其他有效分区类型
        {
            if (ext_lba == 0) // 说明没有扩展分区，全是主分区
            {
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
                hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
                hd->prim_parts[p_no].my_disk = hd;
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
                p_no++;
                ASSERT(p_no < 4);
            }
            else // 其他分区
            {
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;
                hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
                hd->logic_parts[l_no].my_disk = hd;
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5);
                l_no++;
                if (l_no >= 8)
                {
                    return;
                }
            }
        }
        p++;
    }
    sys_free(bs);
}

/*打印分区信息*/
static bool partition_info(struct list_elem *pelem, int arg)
{
    (void)arg; // 此参数未使用，消除警告
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    printk("    %s start_lba:0x%x, sec_cnt:0x%x\n", part->name, part->start_lba, part->sec_cnt);
    return false;
}

/*硬盘数据结构初始化*/
void ide_init()
{
    printk("ide_init start\n");
    // BIOS扫描获取的硬盘数保存在物理地址0x475上，低1MB虚拟地址等于物理地址
    uint8_t hd_cnt = *((uint8_t *)(0x475));
    ASSERT(hd_cnt > 0);

    channel_cnt = DIV_ROUND_UP(hd_cnt, 2); // 计算通道数，一个通道对应两个硬盘
    struct ide_channel *channel;
    uint8_t channel_no = 0, dev_no = 0;
    list_init(&partition_list);

    // 处理每个通道上的硬盘
    while (channel_no < channel_cnt)
    {
        channel = &channels[channel_no];
        sprintf(channel->name, "ide%d", channel_no); // 设置通道名
        if (channel_no == 0)                         // 主通道
        {
            channel->port_base = 0x1f0;
            channel->irq_no = 0x20 + 14; // 对应从片IRQ14引脚
        }
        else if (channel_no == 1) // 从通道
        {
            channel->port_base = 0x170;
            channel->irq_no = 0x20 + 15;
        }
        // 默认不需要等待硬盘中断
        channel->expecting_intr = false;
        lock_init(&channel->lock);
        sema_init(&channel->disk_done, 0);
        // 注册中断处理程序
        register_handler(channel->irq_no, intr_hd_handler);
        // 分别获取两个硬盘的参数
        while (dev_no < 2)
        {
            struct disk *hd = &channel->devices[dev_no];
            hd->my_channel = channel;
            hd->dev_no = dev_no;
            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);
            identify_disk(hd);
            if (dev_no != 0) // 只处理文件盘
            {
                partition_scan(hd, 0); // 扫描文件盘分区
            }
            p_no = 0, l_no = 0;
            dev_no++;
        }
        dev_no = 0;
        channel_no++;
    }
    printk("all partition info\n");
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("ide_init done\n");
}
