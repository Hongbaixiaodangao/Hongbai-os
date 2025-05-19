#ifndef __DEVICE_TIME_H
#define __DEVICE_TIME_H
#include "../lib/stdint.h"

void frequency_set(uint8_t counter_port, uint8_t counter_no, uint8_t rwl, uint8_t counter_mode, uint16_t counter_value);
void intr_timer_handler(void);        // 定时器中断处理函数
void timer_init(void);                // 初始化PIT8253
void mtime_sleep(uint32_t m_second);

#endif
