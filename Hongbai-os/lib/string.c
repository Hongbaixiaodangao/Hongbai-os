// 常见字符串操作函数的实现，相当于写我的os的c标准库函数
// 包括复制，获得长度，比较，查找等
#include "./string.h"
#include "../kernel/debug.h"

/* 将dst_起始的size个字节设置为value */
void memset(void *dst_, uint8_t value, uint32_t size)
{
    ASSERT(dst_ != NULL);
    uint8_t *dst = (uint8_t *)dst_;
    while (size-- > 0)
    {
        *dst = value;
        dst++;
    }
}

/* 将src_起始的size个字节拷贝到dst_ */
void memcpy(void *dst_, const void *src_, uint32_t size)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    uint8_t *dst = (uint8_t *)dst_;
    const uint8_t *src = (const uint8_t *)src_;
    while (size-- > 0)
    {
        *dst = *src;
        dst++;
        src++;
    }
}

/* 连续比较a_和b_两个地址开头的size个字节，若相等返回0，如果a_大于b_返回1,否则返回-1 */
int32_t memcmp(const void *a_, const void *b_, uint32_t size)
{
    ASSERT(a_ != NULL && b_ != NULL);
    const uint8_t *a = (const uint8_t *)a_;
    const uint8_t *b = (const uint8_t *)b_;
    while (size-- > 0)
    {
        if (*a != *b)
        {
            return (*a > *b) ? 1 : -1;
        }
        a++;
        b++;
    }
    return 0;
}

/* 将字符串从src_复制到dst_ */
char *strcpy(char *dst_, const char *src_)
{
    ASSERT(dst_ != NULL && src_ != NULL);
    char *ret = dst_;
    while ((*dst_++ = *src_++))
        ;
    return ret;
}

/* 返回字符串长度 */
uint32_t strlen(const char *str_)
{
    ASSERT(str_ != NULL);
    const char *s = str_;
    while (*s++)
        ;
    return (uint32_t)(s - str_ - 1);
}

/* 比较两个字符串a和b_，如果a_大于b_返回1，相等返回0,否则返回-1 */
int32_t strcmp(const char *a_, const char *b_)
{
    ASSERT(a_ != NULL && b_ != NULL);
    while (*a_ && (*a_ == *b_))
    {
        a_++;
        b_++;
    }
    return (*a_ > *b_) ? 1 : ((*a_ < *b_) ? -1 : 0);
}

/* 从左到右，查找str字符串中首次出现ch字符的地址 */
char *strchr(const char *str_, char ch)
{
    ASSERT(str_ != NULL);
    while (*str_)
    {
        if (*str_ == ch)
        {
            return (char *)str_;
        }
        str_++;
    }
    return NULL;
}

/*查找str字符串最后一次出ch字符的位置*/
char *strrchr(const char *str_, const uint8_t ch)
{
    ASSERT(str_ != NULL);
    char *last_char_ptr = NULL;
    while (*str_)
    {
        if (*str_ == ch)
        {
            last_char_ptr = (char *)str_;
        }
        str_++;
    }
    return last_char_ptr;
}

/*将src_字符串拼接到dsc_字符串末尾*/
char *strcat(char *dsc_, const char *src_)
{
    ASSERT(dsc_ != NULL && src_ != NULL);
    char *str = dsc_;
    while (*str)
    {
        str++;
    }
    while (*src_)
    {
        *str = *src_;
        str++;
        src_++;
    }
    return dsc_;
}

/*统计str字符串中ch字符出现的频率*/
int32_t strchrs(const char *str_, uint8_t ch)
{
    ASSERT(str_ != NULL);
    uint32_t ch_cnt = 0;
    while (*str_)
    {
        if (*str_ == ch)
        {
            ch_cnt++;
        }
        str_++;
    }
    return ch_cnt;
}