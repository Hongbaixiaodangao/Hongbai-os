
#include "./stdio.h"
#include "./stdint.h"
#include "./string.h"
#include "../kernel/debug.h"
#include "./user/syscall.h"
#include "../device/console.h"

#define va_start(ap, v) ap = (va_list) & v // ap指向第一个固定参数v
#define va_arg(ap, t) *((t *)(ap += 4))    // ap依次指向下一个参数，通过解除引用返回其值
#define va_end(ap) ap = NULL

/*将整型转化为字符ascii*/
/*三个参数依次是带转化数值，转化后字符保存的缓冲区，转化进制*/
static void itoa(uint32_t value, char **buf_ptr_addr, uint8_t base)
{
    uint32_t m = value % base; // 余数
    uint32_t i = value / base; // 倍数
    if (i)
    {
        itoa(i, buf_ptr_addr, base);
    }
    if (m < 10)
    {
        // 第一次解引用后是缓冲区地址，++提供下一个字符的位置
        // 第二次解引用后是char，赋值为对应的字符
        *((*buf_ptr_addr)++) = m + '0';
    }
    else
    {
        *((*buf_ptr_addr)++) = m - 10 + 'A';
    }
}

/*将参数ap按照格式format输出到字符串str，并返回替换后str长度*/
uint32_t vsprintf(char *str, const char *format, va_list ap)
{
    char *buf_ptr = str;
    const char *index_ptr = format;

    char index_char = *index_ptr;
    int32_t arg_int;
    char *arg_str;
    while (index_char) // 没有到达末尾就一直处理
    {
        if (index_char != '%') // 没有遇到%，直接复制即可
        {
            *buf_ptr = index_char;
            buf_ptr++;

            index_ptr++;
            index_char = *index_ptr;
            continue;
        }
        // 以下为遇到%后的处理过程
        // 先跳过%
        index_ptr++;
        index_char = *index_ptr;
        // 然后判断占位符是哪种类型
        // %x，后面的参数是16进制unsigned int
        if (index_char == 'x')
        {
            // 获得第一个参数，并且ap指向下一个参数
            arg_int = va_arg(ap, int);
            // 将无符号整型转化为字符，并放到str后面
            itoa(arg_int, &buf_ptr, 16);
            // 跳过x，并且准备好进行后面的处理
            index_ptr++;
            index_char = *index_ptr;
        }
        // %d，后面的参数是int
        else if (index_char == 'd')
        {
            arg_int = va_arg(ap, int);
            // 负数需要进行补码操作转化为正数，然后额外输出一个-
            if (arg_int < 0)
            {
                arg_int = 0 - arg_int;
                *buf_ptr = '-';
                buf_ptr++;
            }
            itoa(arg_int, &buf_ptr, 10);
            index_ptr++;
            index_char = *index_ptr;
        }
        // %c,后面的参数是char
        else if (index_char == 'c')
        {
            *buf_ptr = va_arg(ap, char);
            buf_ptr++;
            index_ptr++;
            index_char = *index_ptr;
        }
        // %s,后面的参数是string(char*)
        else if (index_char == 's')
        {
            arg_str = va_arg(ap, char *);
            strcpy(buf_ptr, arg_str);
            buf_ptr += strlen(arg_str);
            index_ptr++;
            index_char = *index_ptr;
        }
        else
        {
            PANIC("Undefined placeholder");
        }
    }
    return strlen(str);
}

/*格式化输出字符串format，即printf*/
/*包含可变参数*/
uint32_t printf(const char *format, ...)
{
    va_list args; // 可变参数列表
    va_start(args, format);
    char buf[1024] = {0}; // 最终拼接后字符串储存位置
    vsprintf(buf, format, args);
    va_end(args);
    return write(buf);
}

/*内核专用print*/
void printk(const char *format, ...)
{
    va_list args; // 可变参数列表
    va_start(args, format);
    char buf[1024] = {0}; // 最终拼接后字符串储存位置
    vsprintf(buf, format, args);
    va_end(args);
    console_put_str(buf);
}

/*向指定的缓冲区格式化输出*/
uint32_t sprintf(char *buf, const char *format, ...)
{
    va_list args;
    uint32_t retval;
    va_start(args, format);
    retval = vsprintf(buf, format, args);
    va_end(args);
    return retval;
}