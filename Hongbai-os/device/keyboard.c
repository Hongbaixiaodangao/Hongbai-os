
#include "./keyboard.h"
#include "../lib/kernel/print.h"
#include "../kernel/interrupt.h"
#include "../kernel/io.h"
#include "../lib/stdint.h"
#include "./ioqueue.h"

#define KBD_BUF_PORT 0x60 // 键盘输入/输出缓冲区端口号

/*用转义字符定义部分控制字符
 *用8或16进制转义字符定义esc和delete*/
#define esc '\x1b'
#define backspace '\b'
#define tab '\t'
#define enter '\r'
#define delete '\x7f'

/*不可见字符定义为0
 *其实就是认为设置了这些案件的ascii码，方便使用后续的处理函数*/
#define char_invisible 0
#define ctrl_l_char char_invisible
#define ctrl_r_char char_invisible
#define shift_l_char char_invisible
#define shift_r_char char_invisible
#define alt_l_char char_invisible
#define alt_r_char char_invisible
#define caps_lock_char char_invisible

/*定义控制字符的通码和断码*/
#define shift_l_make 0x2a
#define shift_r_make 0x36
#define alt_l_make 0x38
#define alt_r_make 0xe038
#define alt_r_break 0xe0b8
#define ctrl_l_make 0x1d
#define ctrl_r_make 0xe01d
#define ctrl_r_break 0xe09d
#define caps_lock_make 0x3a

struct ioqueue kbd_buf; // 定义键盘缓冲区

/*定义逻辑变量，用来记录按键是否被按下
 *ext_scancode用来记录makecode是否以e0开头*/
static bool ctrl_status, shift_status, alt_status, caps_lock_status, ext_scancode;

/*以通码为索引，定义一个二维数组
 *第一列是未与shift组合的字符*/
static char keymap[][2] = {
    /*0x00*/ {'0', '0'},
    /*0x01*/ {esc, esc},
    /*0x02*/ {'1', '!'},
    /*0x03*/ {'2', '@'},
    /*0x04*/ {'3', '#'},
    /*0x05*/ {'4', '$'},
    /*0x06*/ {'5', '%'},
    /*0x07*/ {'6', '^'},
    /*0x08*/ {'7', '&'},
    /*0x09*/ {'8', '*'},
    /*0x0A*/ {'9', '('},
    /*0x0B*/ {'0', ')'},
    /*0x0C*/ {'-', '_'},
    /*0x0D*/ {'=', '+'},
    /*0x0E*/ {backspace, backspace},
    /*0x0F*/ {tab, tab},

    /*0x10*/ {'q', 'Q'},
    /*0x11*/ {'w', 'W'},
    /*0x12*/ {'e', 'E'},
    /*0x13*/ {'r', 'R'},
    /*0x14*/ {'t', 'T'},
    /*0x15*/ {'y', 'Y'},
    /*0x16*/ {'u', 'U'},
    /*0x17*/ {'i', 'I'},
    /*0x18*/ {'o', 'O'},
    /*0x19*/ {'p', 'P'},
    /*0x1A*/ {'[', '{'},
    /*0x1B*/ {']', '}'},
    /*0x1C*/ {enter, enter},
    /*0x1D*/ {ctrl_l_char, ctrl_l_char},
    /*0x1E*/ {'a', 'A'},
    /*0x1F*/ {'s', 'S'},

    /*0x20*/ {'d', 'D'},
    /*0x21*/ {'f', 'F'},
    /*0x22*/ {'g', 'G'},
    /*0x23*/ {'h', 'H'},
    /*0x24*/ {'j', 'J'},
    /*0x25*/ {'k', 'K'},
    /*0x26*/ {'l', 'L'},
    /*0x27*/ {';', ':'},
    /*0x28*/ {'\'', '"'},
    /*0x29*/ {'`', '~'},
    /*0x2A*/ {shift_l_char, shift_l_char},
    /*0x2B*/ {'\\', '|'},
    /*0x2C*/ {'z', 'Z'},
    /*0x2D*/ {'x', 'X'},
    /*0x2E*/ {'c', 'C'},
    /*0x2F*/ {'v', 'A'},

    /*0x30*/ {'b', 'B'},
    /*0x31*/ {'n', 'N'},
    /*0x32*/ {'m', 'M'},
    /*0x33*/ {',', '<'},
    /*0x34*/ {'.', '>'},
    /*0x35*/ {'/', '?'},
    /*0x36*/ {shift_r_char, shift_r_char},
    /*0x37*/ {'*', '*'},
    /*0x38*/ {alt_l_char, alt_l_char},
    /*0x39*/ {' ', ' '},
    /*0x3A*/ {caps_lock_char, caps_lock_char}

    /*其他按键暂时不处理*/
};

/*键盘的中断处理程序*/
static void intr_keyboard_handler(void)
{
    // 获取上次中断，这三个控制键是否被按下
    bool ctrl_down_last = ctrl_status;
    (void)ctrl_down_last; // 此参数未使用，消除编译警告
    bool shift_down_last = shift_status;
    bool caps_down_last = caps_lock_status;
    /*判断这次的扫描码是通码还是断码
     *总的来说，断码不需要太多的处理，通码需要的处理比较多*/
    bool break_code;
    /*获取本次的扫描码*/
    uint16_t scancode = inb(KBD_BUF_PORT);
    /*如果扫描码是0xe0,说明这个码和下一次中断的码是一组的
     *要记录下来状态*/
    if (scancode == 0xe0)
    {
        ext_scancode = true; // 修改全局变量
        return;
    }
    /*上一次如果是0xe0，这次要合并扫描码*/
    if (ext_scancode)
    {
        scancode = ((0xe000) | (scancode));
        ext_scancode = false; // 关闭标记
    }
    /*判断这次的码是通码还是断码
     *通过判断scancode的第8位是0还是1即可
     *1断0通*/
    break_code = ((0x0080 & scancode) != 0);

    /*如果是断码，我们不太关注非控制字符松开后的结果
     *主要关注三个控制键是否松开了*/
    if (break_code)
    {
        /*先获取这个断码对应的通码
         *通码和断码的区别就是第8位是0还是1,前7位一模一样
         *我们直接按位取出前7位，即可还原出通码*/
        uint16_t make_code = (scancode &= 0xff7f);
        if ((make_code == ctrl_l_make) || (make_code == ctrl_r_make))
        {
            ctrl_status = false;
        }
        else if ((make_code == shift_l_make) || (make_code == shift_r_make))
        {
            shift_status = false;
        }
        else if ((make_code == alt_l_make) || (make_code == alt_r_make))
        {
            alt_status = false;
        }
        return;
    }
    /*如果是通码*/
    else if ((scancode > 0x00 && scancode < 0x3B) || (scancode == alt_r_make) || (scancode == ctrl_r_make))
    {
        bool shift = false; // 这个变量指示通码是否和shift组合了
        /*如果是数字0-9或者是这些符号 ` [ ] \ ; ' , . /*/
        if ((scancode < 0x0e) || (scancode == 0x29) ||
            (scancode == 0x1a) || (scancode == 0x1b) ||
            (scancode == 0x2b) || (scancode == 0x27) ||
            (scancode == 0x28) || (scancode == 0x33) ||
            (scancode == 0x34) || (scancode == 0x35))
        {
            if (shift_down_last)
            { // 上次按下了shift
                shift = true;
            }
        }
        /*剩下的按键，也就是字母*/
        else
        {
            // 大写锁定的情况下按下shift，输出小写
            if (shift_down_last && caps_down_last)
            {
                shift = false;
            }
            // 如果大写锁定了或者只按下shift，输出大写
            else if (shift_down_last || caps_down_last)
            {
                shift = true;
            }
            // 默认输出小写
            else
            {
                shift = false;
            }
        }
        // 以下的处理，去除0xe0,断码转化为通码，获取索引
        uint8_t index = (scancode &= 0x00ff);
        // 获取按键对应的字符
        char cur_char = keymap[index][shift];

        /*如果这个字符的ascii非0,说明不是特殊字符，发给put_char输出即可*/
        if (cur_char)
        {
            // 如果缓冲区不满，将cur_char加入到缓冲区
            if (!ioq_full(&kbd_buf))
            {
                ioq_putchar(&kbd_buf, cur_char);
                // put_char(cur_char); //临时
            }
            return;
        }
        /*记录我们此次处理是否用到了4个控制键*/
        if ((scancode == ctrl_l_make) || (scancode == ctrl_r_make))
        {
            ctrl_status = true;
        }
        else if ((scancode == shift_l_make) || (scancode == shift_r_make))
        {
            shift_status = true;
        }
        else if ((scancode == alt_l_make) || (scancode == alt_r_make))
        {
            alt_status = true;
        }
        // 按下了大写锁定，大写锁定字符就要取反
        else if (scancode == caps_lock_make)
        {
            caps_lock_status = !caps_lock_status;
        }
    }
    /*未定义的字符*/
    else
    {
        put_str("Unknow key!\n");
    }
}

/*键盘驱动程序初始化*/
void keyboard_init(void)
{
    put_str("keyboard init start\n");
    ioqueue_init(&kbd_buf);                        // 初始化键盘缓冲区
    register_handler(0x21, intr_keyboard_handler); // 向中断处理入口数组的0x21项写入对应的键盘中断处理程序
    put_str("keyboard init done\n");
}