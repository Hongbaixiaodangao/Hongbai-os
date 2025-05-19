;switch_to函数接受两个参数，第1个参数是当前线程cur
;第2个参数是下一个上处理器的线程next
;此函数的功能是保存cur线程的寄存器映像，将下一个线程next的寄存器映像装载到处理器。
[bits 32]
section .text
global switch_to
switch_to:
    ;-----以下是备份当前线程的上下文环境-----
    ;第一个位置是返回地址
    push esi
    push edi
    push ebx
    push ebp

    mov eax,[esp+20]    
    mov [eax],esp   ;获取cur，把esp保存到cur的结构体的self_kstack里

    ;-----以下是恢复下一个线程的环境-----
    mov eax,[esp+24]    
    mov esp,[eax]   ;获取next，把esp指向next的结构体的self_kstack

    pop ebp
    pop ebx
    pop edi
    pop esi
    ret             ;返回到next->eip