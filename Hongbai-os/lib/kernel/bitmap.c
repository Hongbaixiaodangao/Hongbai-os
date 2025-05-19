// 实现位图相关操作函数
#include "./bitmap.h"
#include "../../kernel/debug.h"
#include "../string.h"
#include "./stdint.h"

/* 将位图bitmap初始化为0 */
void bitmap_init(struct bitmap *btmp)
{
    ASSERT(btmp != NULL);
    memset(btmp->btmp_bits, 0, btmp->btmp_bytes_len);
}

/* 判断bit_idx位是否是1,为1返回true，否则返回false */
bool bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx)
{
    ASSERT(btmp != NULL);
    ASSERT(bit_idx < btmp->btmp_bytes_len * 8);
    uint32_t byte_idx = bit_idx / 8; // 字节索引
    uint32_t bit_odd = bit_idx % 8;  // 位索引
    return (btmp->btmp_bits[byte_idx] & (BITMAP_MASK << bit_odd)) != 0;
}

/* 在位图中申请连续的cnt个位，若成功，返回起始下标，失败返回-1 */
int bitmap_scan(struct bitmap *btmp, uint32_t cnt)
{
    ASSERT(btmp != NULL);
    uint32_t idx_byte = 0; // 字节索引
    // 先找到对应的字节，如果这个字节的值是0xff，说明这个字节的所有位都被占用
    // 如果这个字节的值不是0xff，说明这个字节中有空闲位
    while ((idx_byte < btmp->btmp_bytes_len) && (btmp->btmp_bits[idx_byte] == 0xff))
    {
        idx_byte++;
    }
    ASSERT(idx_byte < btmp->btmp_bytes_len);
    // 如果idx_byte == btmp->btmp_bytes_len，说明没有找到空闲位
    if (idx_byte == btmp->btmp_bytes_len)
    {
        return -1; // 没有找到空闲位
    }
    // 如果idx_byte < btmp->btmp_bytes_len，说明找到了空闲位
    // 找到空闲位后，找到这个字节中第一个空闲位
    int idx_bit = 0;
    while ((idx_bit < 8) && ((btmp->btmp_bits[idx_byte] & (BITMAP_MASK << idx_bit)) != 0))
    {
        idx_bit++;
    }
    int bit_idx_start = idx_byte * 8 + idx_bit; // 起始位索引
    if (cnt == 1)
    {
        return bit_idx_start;
    }
    uint32_t bit_left = (btmp->btmp_bytes_len * 8 - bit_idx_start); // 剩余位数
    uint32_t next_bit = bit_idx_start + 1;
    uint32_t count = 1; // 计数器

    bit_idx_start = -1; // 初始化为-1
    while (bit_left-- > 0)
    {
        if (bitmap_scan_test(btmp, next_bit) == 0)
        {
            count++;
        }
        else
        {
            count = 0; // 重置计数器
        }
        if (count == cnt)
        {
            bit_idx_start = next_bit - cnt + 1; // 起始位索引
            break;
        }
        next_bit++;
    }
    return bit_idx_start; // 返回起始位索引
}

/* 将位图的bit_idx位设置为value */
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value)
{
    ASSERT(btmp != NULL);
    ASSERT(bit_idx < btmp->btmp_bytes_len * 8);
    ASSERT(value == 0 || value == 1); // ASSERT(value == 0 || value == 1); //value只能是0或1
    uint32_t byte_idx = bit_idx / 8;  // 字节索引
    uint32_t bit_odd = bit_idx % 8;   // 位索引
    if (value)
    {
        btmp->btmp_bits[byte_idx] |= (BITMAP_MASK << bit_odd); // 设置为1
    }
    else
    {
        btmp->btmp_bits[byte_idx] &= ~(BITMAP_MASK << bit_odd); // 设置为0
    }
}