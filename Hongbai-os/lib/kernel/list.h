#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H
#include "./stdint.h"

#define offset(struct_type, member) (int)(&((struct_type *)0)->member) // 计算成员在结构体中的偏移量
#define elem2entry(struct_type, struct_member, elem_ptr) \
    (struct_type *)((int)elem_ptr - offset(struct_type, struct_member)) // 通过成员节点获取结构体指针
/* 定义链表成员节点的结构 */
struct list_elem
{
    struct list_elem *prev; // 前驱节点
    struct list_elem *next; // 后继节点
};

/* 定义链表结构 */
struct list
{
    struct list_elem head; // 链表头
    struct list_elem tail; // 链表尾
};
/* 自定义函数类型function，用于在list_traversal中做回调函数 */
typedef bool function(struct list_elem *elem, int arg);

void list_init(struct list *);                                                // 初始化链表
void list_insert_before(struct list_elem *before, struct list_elem *elem);    // 在before节点前插入elem节点
void list_push(struct list *plist, struct list_elem *elem);                   // 在链表头插入节点
void list_append(struct list *plist, struct list_elem *elem);                 // 在链表尾插入节点
void list_remove(struct list_elem *pelem);                                    // 删除节点
struct list_elem *list_pop(struct list *plist);                               // 删除链表头节点
struct list_elem *list_traversal(struct list *plist, function func, int arg); // 遍历链表并执行回调函数
bool list_empty(struct list *plist);                                          // 判断链表是否为空
bool elem_find(struct list *plist, struct list_elem *obj_elem);               // 查找节点是否在链表中
uint32_t list_len(struct list *plist);                                        // 获取链表长度

#endif