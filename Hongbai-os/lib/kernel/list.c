
#include "./list.h"
#include "../../kernel/interrupt.h"
#include "../../kernel/debug.h"
/* 初始化双向链表 */
void list_init(struct list *plist)
{
    // 初始化链表头和尾
    plist->head.prev = NULL;
    plist->head.next = &plist->tail;
    plist->tail.prev = &plist->head;
    plist->tail.next = NULL;
}

/* 把链表元素elem插入在元素before之前 */
void list_insert_before(struct list_elem *before, struct list_elem *elem)
{
    // 关闭中断，保存旧状态
    enum intr_status old_status = intr_disable(); // 关闭中断
    // 先把elem的前驱和后继指针指向before的前驱和before
    elem->prev = before->prev;
    elem->next = before;
    // 再把before的前驱指针指向elem
    before->prev->next = elem;
    before->prev = elem;
    // 恢复旧状态
    intr_set_status(old_status);
}

/* 添加元素到列表队首，类似栈push操作 */
void list_push(struct list *plist, struct list_elem *elem)
{
    // 把elem插入到链表头部
    list_insert_before(plist->head.next, elem);
}

/* 追加元素到链表队尾，类似队列的先进先出操作 */
void list_append(struct list *plist, struct list_elem *elem)
{
    // 把elem插入到链表尾部
    list_insert_before(&plist->tail, elem);
}

/* 使元素pelem脱离链表 */
void list_remove(struct list_elem *pelem)
{
    // 关闭中断，保存旧状态
    enum intr_status old_status = intr_disable(); // 关闭中断
    // 把pelem的前驱和后继指针指向pelem的前驱和pelem
    pelem->prev->next = pelem->next;
    pelem->next->prev = pelem->prev;
    // 恢复旧状态
    intr_set_status(old_status);
}

/* 将链表第一个元素弹出并返回，类似栈的pop操作 */
struct list_elem *list_pop(struct list *plist)
{
    ASSERT(plist->head.next != &plist->tail); // 确保链表不为空
    // 获取链表头元素
    struct list_elem *elem = plist->head.next;
    // 删除链表头元素
    list_remove(elem);
    return elem;
}

/* 从链表中查找元素obj_elem，成功时返回true，失败时返回false */
bool elem_find(struct list *plist, struct list_elem *obj_elem)
{
    // 遍历链表
    struct list_elem *elem = plist->head.next;
    while (elem != &plist->tail)
    {
        if (elem == obj_elem)
        {
            return true; // 找到元素
        }
        elem = elem->next; // 继续遍历
    }
    return false; // 没有找到元素
}

/* 把列表plist中的每个元素elem和arg传给回调函数func，
 * arg 给func用来判断elem是否符合条件．
 * 本函数的功能是遍历列表内所有元素，逐个判断是否有符合条件的元素。
 * 找到符合条件的元素返回元素指针，否则返回NULL */
struct list_elem *list_traversal(struct list *plist, function func, int arg)
{
    struct list_elem *elem = plist->head.next;
    if (list_empty(plist))
    {
        return NULL; // 链表为空，返回NULL
    }
    // 遍历链表
    while (elem != &plist->tail)
    {
        // 调用回调函数
        if (func(elem, arg))
        {
            return elem; // 找到符合条件的元素
        }
        elem = elem->next; // 继续遍历
    }
    return NULL; // 没有找到符合条件的元素
}

/* 返回链表长度 */
uint32_t list_len(struct list *plist)
{
    uint32_t len = 0;
    struct list_elem *elem = plist->head.next;
    // 遍历链表
    while (elem != &plist->tail)
    {
        len++;             // 计数
        elem = elem->next; // 继续遍历
    }
    return len; // 返回长度
}

/* 判断链表是否为空，空时返回true，否则返回false */
bool list_empty(struct list *plist)
{
    return (plist->head.next == &plist->tail ? true : false);
}