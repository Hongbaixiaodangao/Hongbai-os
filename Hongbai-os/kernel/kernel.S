;实现中断处理程序数组
[bits 32]
%define ERROR_CODE nop			 ; 若在相关的异常中cpu已经自动压入了错误码,为保持栈中格式统一,这里不做操作.
%define ZERO push 0			 ; 若在相关的异常中cpu没有压入错误码,为了统一栈中格式,就手工压入一个0

extern put_str;
extern idt_table;

section .data
global intr_entry_table
intr_entry_table:

%macro VECTOR 2
section .text
intr%1entry:			  ; 每个中断处理程序都要压入中断向量号,所以一个中断类型一个中断处理程序，自己知道自己的中断向量号是多少

	%2						 ; 中断若有错误码会压在eip后面 
; 以下是保存上下文环境
	push ds
	push es
	push fs
	push gs
	pushad					 ; PUSHAD指令压入32位寄存器,其入栈顺序是: EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI

	; 如果是从片上进入的中断,除了往从片上发送EOI外,还要往主片上发送EOI 
	mov al,0x20						 ; 中断结束命令EOI
	out 0xa0,al						 ; 向从片发送
	out 0x20,al						 ; 向主片发送

	push %1					; 不管idt_table中的目标程序是否需要参数,都一律压入中断向量号,调试时很方便
	call [idt_table + %1*4]		 ; 调用idt_table中的C版本中断处理函数
	jmp intr_exit
	  
section .data
	dd	 intr%1entry	 ; 存储各个中断入口程序的地址，形成intr_entry_table数组
%endmacro

section .text
global intr_exit
intr_exit:			
; 以下是恢复上下文环境
	add esp, 4					  ; 跳过中断号
	popad
	pop gs
	pop fs
	pop es
	pop ds
	add esp, 4					  ; 跳过error_code
	iretd


VECTOR 0x0 ,ZERO
VECTOR 0X1 ,ZERO
VECTOR 0X2 ,ZERO
VECTOR 0x3 ,ZERO
VECTOR 0X4 ,ZERO
VECTOR 0X5 ,ZERO
VECTOR 0x6 ,ZERO
VECTOR 0X7 ,ZERO
VECTOR 0X8 ,ERROR_CODE
VECTOR 0x9 ,ZERO
VECTOR 0XA ,ERROR_CODE
VECTOR 0XB ,ERROR_CODE
VECTOR 0XC ,ERROR_CODE
VECTOR 0XD ,ERROR_CODE
VECTOR 0XE ,ERROR_CODE
VECTOR 0XF ,ZERO

VECTOR 0X10 ,ZERO
VECTOR 0X11 ,ERROR_CODE
VECTOR 0x12 ,ZERO
VECTOR 0X13 ,ZERO
VECTOR 0X14 ,ZERO
VECTOR 0x15 ,ZERO
VECTOR 0X16 ,ZERO
VECTOR 0X17 ,ZERO
VECTOR 0X18 ,ZERO
VECTOR 0X19 ,ZERO
VECTOR 0X1A ,ZERO
VECTOR 0X1B ,ZERO
VECTOR 0X1C ,ZERO
VECTOR 0X1D ,ZERO
VECTOR 0X1E ,ERROR_CODE	;处理器自动推错误码
VECTOR 0X1F ,ZERO

VECTOR 0X20 ,ZERO	;时钟中断
VECTOR 0x21 ,ZERO	;键盘中断
VECTOR 0x22 ,ZERO	;级联
VECTOR 0x23 ,ZERO	;串口2入口
VECTOR 0x24 ,ZERO	;串口1入口
VECTOR 0x25 ,ZERO	;并口2入口
VECTOR 0x26 ,ZERO	;软盘入口
VECTOR 0x27 ,ZERO	;并口1入口
VECTOR 0x28 ,ZERO	;实时时钟
VECTOR 0x29 ,ZERO	;重定向
VECTOR 0x2a ,ZERO	;保留
VECTOR 0x2b ,ZERO	;保留	
VECTOR 0x2c ,ZERO	;ps/2鼠标
VECTOR 0x2d ,ZERO	;fpu浮点数异常
VECTOR 0x2e ,ZERO	;硬盘
VECTOR 0x2f ,ZERO	;保留

;;;;;;;;;;;;;;;; 0x80中断 ;;;;;;;;;;;;;;;;
[bits 32]
extern syscall_table
section .text
global syscall_handler
syscall_handler:
    ;1.保存上下文环境
    ;因为我们要复用上面的intr_exit
    ;所以要按上面intr_entry_table的格式压入数据，让栈中格式统一
    push 0
    push ds
	push es
	push fs
	push gs
	pushad
    push 0x80
    ;2.为系统调用子功能传入参数
    ;不管我们需要几个参数，一律压入三个
    ;编译器会根据我们的函数声明正确处理
    push EDX
    push ECX
    push EBX
    ;3.调用子功能处理函数
    call [syscall_table+EAX*4]
    add esp ,12 ;跨过ebx等三个参数
    ;4.保存eax中的返回值
    ;eax内有call后的返回值，我们把它保存到内存中内核栈中eax变量的位置
    ;8=0x80+pushad后七个，这样esp+4*8就指向目前内存内核栈uint32_t eax变量，然后把eax寄存器中的数据存入内存
    mov [esp+4*8],EAX
    jmp intr_exit
