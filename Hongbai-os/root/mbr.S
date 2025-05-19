%include "boot.inc"
section MBR vstart=0x7c00
;--------------------以下部分完成初始化--------------------
	mov ax,cs
	mov ds,ax
	mov es,ax
	mov ss,ax
	mov fs,ax
	mov sp,0x7c00
	mov ax,0xb800
	mov gs,ax		;初始化，最后gs指向显存起点
;--------------------以下部分完成清屏--------------------
	mov ax,0x600
	mov bx,0x700
	mov cx,0
	mov dx,0x184f	
	int 0x10		;利用的bios的中断程序完成清屏
;--------------------以下部分进行硬盘读取工作的准备--------------------
	mov eax,LOADER_START_SECTOR
	mov bx,LOADER_BASE_ADDR
	mov cx,4		;扇区数
	call rd_disk_m_16	;进行读取硬盘操作

	jmp LOADER_BASE_ADDR	;写到的内存空间地址
;--------------------以下部分进行硬盘读取工作--------------------
rd_disk_m_16:
;以下部分备份关键数据
	mov esi,eax		;备份eax
	mov di,cx		;备份cx
;以下部分写入要读取的扇区数
	mov dx,0x1f2
	mov al,cl
	out dx,al
	mov eax,esi		;写入扇区数
;以下部分写入LBA编号和参数	
	mov dx,0x1f3
	out dx,al
	
	mov cl,0x8
	shr eax,cl
	mov dx,0x1f4
	out dx,al
	
	shr eax,cl
	mov dx,0x1f5
	out dx,al		;写入LBA低24位的地址
	
	shr eax,cl
	and al,0x0f		;00001111,作用是保留0-3位,4-7位变为0
	or al,0xe0		;01110000,作用是把al的7-4位设置为0111,即设置lba模式
	mov dx,0x1f6
	out dx,al		;向0x1F6,device寄存器写LBA最高4位数字和LBA参数
;以下部分写入要进行的硬盘操作的操作码	
	mov dx,0x1f7
	mov ax,0x20
	out dx,al		;向0x1F7，command寄存器写读入命令0x20
;以下部分检验硬盘状态
.not_ready:
	nop			;在这里设置了一定的时间
	in al,dx
	and al,0x88 		;10001000，第 4 位为 1 表示硬盘控制器已准备好数据传输，第 7 位为 1 表示硬盘忙
	cmp al,0x08		;比较，改变标志寄存器状态
	jne .not_ready		;jump_not_equal,检验硬盘状态，如果失败就继续检验
;以下部分从硬盘读入loader程序
	mov ax,di
	mov dx,256
	mul dx 			;计算读取的字数，mul的结果默认存在ax里，相当于ax=ax*256，即扇区数*512（字节数）/2（1字=2字节）
	mov cx,ax 		;cx寄存器存循环次数
	mov dx,0x1f0
.go_on_ready:
	in ax,dx
	mov [bx],ax
	add bx,2
	loop .go_on_ready	;从0x1F0，data寄存器读取数据

	ret
;--------------------以下部分填充0，0x55和0xaa，满足mbr格式要求--------------------
	times 510-($-$$) db 0
	db 0x55,0xaa
