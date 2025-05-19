%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR
	jmp loader_start
;-------------------- 数据段 --------------------------------

;这部分完成GDT和4个段描述符的构建
;编译后程序的地址是越来越高的，所以代码要先开辟低地址再开辟高地址。8字节64位是一个段描述符。dd命令开辟出一个4字节32位空间。
GDT_BASE:
	dd 0x00000000
	dd 0x00000000		;第0个段描述符无法使用，开辟出来空着
CODE_DESC:
	dd 0x0000ffff		;前四位是16位段基址，设置为0，后四位是16位段界限，设置为1。高位在前低位在后
	dd DESC_CODE_HIGH4	;提前在配置文件中写好的高32位
DATA_STACK_DESC:
	dd 0x0000ffff
	dd DESC_DATA_HIGH4	;数据和栈用相同的段描述符
VIDEO_DESC:
	dd 0x80000007		;这部分请看博客里的说明
	dd DESC_VIDEO_HIGH4
;计算gdt大小，同时预留出60个段描述符的空间 
;这部分为加载gdt做准备。dq定义4字8字节64位。times是nasm提供的伪指令，作用是循环。以后我们还要向gdt里添加别的表符，这里提前留出位置。
GDT_SIZE 	equ $-GDT_BASE
GDT_LIMIT 	equ GDT_SIZE-1
	times 59 dq 0
	times 5 db 0
;构建选择子
;选择子放在段寄存器里，16位大小，高13位是gdt的索引，第2位是ti位，指示索引是gdt的还是ldt的，0、1两位是特权级位。
SELECTOR_CODE 	equ (0x0001<<3)+TI_GDT+RPL0	;0001就是下标1，左移3位相当于*8，因为一个表项是8字节
SELECTOR_DATA 	equ (0x0002<<3)+TI_GDT+RPL0
SELECTOR_VIDEO 	equ (0x0003<<3)+TI_GDT+RPL0
;设置保存内存容量的标号
;关于这个标号的地址，loader.S的起始地址是0x900，这行前面有64个8字节的段描述符，所以这里是0x900+0x200=0xb00
	total_mem_bytes dd 0	;初始为0,最终变成总内存容量
;定义GDT指针
gdt_ptr:
	dw GDT_LIMIT		;前2字节是gdt的界限
	dd GDT_BASE		;后4字节是gdt的起始位置
;ards缓冲区地址和数量
	ards_buf times 244 db 0	;ards缓冲区，存放ards
	ards_nr dw 0		;用于记录ards结构体的数量
	
;-------------------- 实现三种获取内存容量的方法 --------------------------------

loader_start:
	mov dword [GDT_BASE],0		
	mov dword [GDT_BASE+4],0;显式清零
	mov sp,LOADER_BASE_ADDR	;先初始化了栈指针
	
;方法1：利用0xe820获取内存
;以下部分，通过0xe820获取所有的ards
	xor ebx,ebx		;用异或置零，初始ebx设置为0，后续我们不需要再处理
	mov edx,0x534d4150	;固定值
	mov di,ards_buf		;es:di指向缓冲区，es在mbr设置，这里修改di即可
	
.e820_mem_get_loop:
	mov eax,0x0000e820	;因为执行完int 0x15后eax，ebx，ecx会变化，所以每次循环都要重新设置
	mov ecx,20		;返回的字节数，固定为20
	
	int 0x15
	jc .e820_failed_so_try_e801 ;如果e820失败，尝试e801
	add di,cx		;+20字节指向下一个ards
	inc word [ards_nr]	;记录ards数量
	cmp ebx,0		;如果ebx=0且cf=0,所有ards全部返回
	jnz .e820_mem_get_loop 	;如果ebx!=0，继续循环
	
;遍历ards，找到最大的32位基地址+内存长度，即为最大内存容量
	mov cx,[ards_nr]
	mov ebx,ards_buf
	xor edx,edx		;edx保存最大内存容量，初始置0
.find_max_mem_area:
	mov eax,[ebx]		;32位基地址
	add eax,[ebx+8]		;内存长度
	add ebx,20		;指向下一个ards
	cmp edx,eax
	jge .next_ards		;如果edx>=eax,跳转到下一个ards，否则让edx=eax，最终效果是找到最大的ards
	mov edx,eax
	
.next_ards:
	loop .find_max_mem_area
	jmp .mem_get_ok
	
;方法2：利用0xe801获取内存 
;返回后，ax=cx单位是1kb，里面是小于16mb的单位数，bx=dx单位是64kb，里面是大于16mb的单位数。最终需要转化为字节数。
.e820_failed_so_try_e801:
	mov ax,0x0000e801
	int 0x15
	jc .e801_failed_so_try88	;如果e801失败，尝试88方法
;以下计算出低于16mb的内存容量大小
	mov cx,0x400		;1024
	mul cx			;16位乘法，结果是32位，低16在ax，高16在dx
	shl edx,16		;左移16位
	
	and eax,0x0000ffff	;保留低16位
	or edx,eax		;拼接edx的高16位和eax低16位，放到edx中
	add edx,0x100000	;+1mb,原因是获取的内存比实际内存少1mb
	mov esi,edx		;备份edx
;以下计算16mb以上内存容量大小
	xor eax,eax
	mov ax,bx		;大于16mb的单位数存在bx、dx里
	mov ecx,0x10000		;单位是64kb
	mul ecx			;32位乘法，结果是64位，低32位在eax，高32位在edx
	add esi,eax		;这种方法的上限就是4gb，所以不必理会高32位，只需要把低32位加进结果即可
	
	mov edx,esi
	jmp .mem_get_ok
	
;方法3：利用0x88获取内存 
;这部分是方法2的简化版，代码参考2,不再写注释
.e801_failed_so_try88:
	mov ah,0x88
	int 0x15
	jc .error_hlt
	and eax,0x0000ffff
	
	mov cx,0x400
	mul cx
	shl edx,16
	or edx,eax
	add edx,0x100000
	
;-------------------- 记录内存容量 --------------------------------

;如果三种方法都失败，跳转到这里，进行一个死循环
.error_hlt:
	jmp $
;不管使用了哪种方法，只要成功，都要跳转到这里记录，单位是1字节
.mem_get_ok:
	mov [total_mem_bytes],edx	;在total_mem_bytes地址记录总内存容量
	
;-------------------- 完成进入保护模式的三个步骤 --------------------------------

	in al,0x92 
	or al,0000_0010B 
	out 0x92,al 		;打开 A20

	lgdt [gdt_ptr] 		;加载 GDT

	mov eax, cr0 
	or eax, 0x00000001 
	mov cr0, eax		;cr0 第 0 位置 1
	
;-------------------- 进入保护模式 --------------------------------

	jmp dword SELECTOR_CODE:p_mode_start	;刷新流水线
[bits 32] 
p_mode_start: 
	mov ax,SELECTOR_DATA 
	mov ds,ax 
	mov es,ax 
	mov ss,ax 
	mov esp,LOADER_STACK_TOP
	

;-------------------- 加载内核到缓冲区 --------------------------------

	mov eax,KERNEL_START_SECTOR	;kernel.bin所在的扇区号
	mov ebx,KERNEL_BIN_BASE_ADDR	;从磁盘读出后，写入到 ebx指定的地址
	mov ecx,200			;读入的扇区数
	call rd_disk_m_32		;写入内存

;-------------------- 启动分页 --------------------------------

	call setup_page		;创建页目录和页表
	sgdt [gdt_ptr]		;要将描述符表地址及偏移量写入内存 gdt_ptr,一会儿用新地址重新加载
	mov ebx,[gdt_ptr+2]	;先获取gdt基址，基址存在后四字节
	add dword [gdt_ptr+2],0xc0000000	;将 gdt 的基址加上 0xc0000000 使其成为内核所在的高地址
	or dword [ebx+0x18+4],0xc0000000	;将 gdt描述符中视频段描述符中的段基址+0xc0000000
						;视频段是第 3 个段描述符，每个描述符是 8 字节，24=0x18 
						;段描述符的高 4 字节的最高位是段基址的第 31～24 位
	add esp,0xc0000000	;将栈指针同样映射到内核地址
	
	mov eax,PAGE_DIR_TABLE_POS 
	mov cr3,eax		;把页目录地址赋给 cr3

	mov eax,cr0 
	or eax,0x80000000
	mov cr0,eax		;打开 cr0的 pg位（第 31 位）
	
	lgdt [gdt_ptr]		;在开启分页后，用 gdt 新的地址重新加载
	

	
;-------------------- 跳转到内核区 --------------------------------

enter_kernel:
	call kernel_init
	mov esp,0xc009f000		;设立栈段的起始地址，这个数字是最接近高地址的和4KB对齐的数
	jmp KERNEL_ENTRY_POINT		;写在宏里面，0xc0001500
	
;-------------------- 创建页目录和页表 --------------------------------

;首先先明确一点，那就是我们这里只处理低1mb的页表，目标是让0～1mb和3gb～3gb+1mb都映射低1mb
setup_page:
;先逐字节清0
	mov ecx,4096			;循环4096次
	mov esi,0
.clear_page_dir:
	mov byte [PAGE_DIR_TABLE_POS+esi],0
	inc esi
	loop .clear_page_dir		;清理出4KB的空间
	
;创建两个页目录项
.create_pde:
	mov eax,PAGE_DIR_TABLE_POS
	add eax,0x1000			;起始位置+4KB，指向第一个页表，为后面的创建页表项作准备
	mov ebx,eax
	
	or eax,PG_US_U|PG_RW_W|PG_P	;将后12位和前20位地址拼接在一起，组成一个表项
					;后面三个或后=0x7,代表这个目录项允许任意特权级访问，可写，后续我们会用到init用户级进程。
	
	;将页目录0和0x300项都指向第一个页表的地址，都对应0-4mb的物理地址。
	mov [PAGE_DIR_TABLE_POS+0x0],eax	;0项
	mov [PAGE_DIR_TABLE_POS+0xc00],eax	;0xc00项转化为10进制是4*768，对应768项，每个项4字节
						;错误代码是mov [PAGE_DIR_TABLE_POS+0x300],eax，错误原因是一个项4字节忘了×4
	
	sub eax,0x1000
	mov [PAGE_DIR_TABLE_POS+4092],eax	;在页目录最后一项1023项写入页目录本身的地址，所以是4096-4
	
;创建页表项
	mov ecx,256			;目前我们先安排低1mb内存，1M/4k=256项
	mov esi,0
	mov edx,PG_US_U|PG_RW_W|PG_P	;edx存每个表项的相对地址，初始为0+0x7
.create_pte:
	mov [ebx+esi*4],edx		;每过4字节有一项，每项存着一页，一页是4k
	add edx,4096
	inc esi
	loop .create_pte		;循环256次
	
;创建内核其他页表的PDE
;这部分是3gb后的部分，共享给所有用户线程的，所以只关注页目录768项后的项，1023项用来存页目录本身的地址，不需要处理
;这里创建剩下的769～1022=254项
	mov eax,PAGE_DIR_TABLE_POS
	add eax,0x2000		;从第二个页表开始，映射到目录里
	or eax,PG_US_U|PG_RW_W|PG_P
	mov ebx, PAGE_DIR_TABLE_POS 
	mov ecx,254 		;范围为第 769～1022 的所有目录项数量
	mov esi,769 
.create_kernel_pde: 
	mov [ebx+esi*4],eax
	inc esi 
	add eax,0x1000 
	loop .create_kernel_pde 
	ret

;-------------------- 初始化内核 --------------------------------

kernel_init:
	xor eax,eax
	xor ebx,ebx		;记录程序头表的地址,即e_phoff,也就是第一个程序头的偏移地址
	xor ecx,ecx		;记录程序头表中，程序头的数量e_phnum，也就是segment数量
	xor edx,edx		;记录每个程序头的尺寸，即e_phentsize
	
	mov dx,[KERNEL_BIN_BASE_ADDR+42]	;程序头大小
	mov ebx,[KERNEL_BIN_BASE_ADDR+28]
	add ebx,KERNEL_BIN_BASE_ADDR		;程序头地址
	mov cx,[KERNEL_BIN_BASE_ADDR+44]	;程序头数量
	
.each_segment:
	cmp byte [ebx+0],PT_NULL;若相等，说明这个程序头未使用
	je .PTNULL		;未使用则跳转
	
	mov eax,[ebx+8]
	cmp eax,0xc0001500
	jb .PTNULL
	
;模拟c语言里的mem_cpy，输入三个参数：源地址，目标地址，拷贝内容字节数
	push dword [ebx+16]	;获取段字节数p_filesz
	mov eax,[ebx+4]		;距程序头偏移量为4字节的位置是p_offset
	add eax,KERNEL_BIN_BASE_ADDR
	push eax		;段偏移地址，也就是源地址
	push dword [ebx+8]	;目标地址p_vaddr
	call mem_cpy
	add esp,12		;一次是向栈段压入3*4=12字节的数据
	
.PTNULL:
	add ebx,edx		;指向下一个程序头
		
	
	loop .each_segment		
	ret
	
mem_cpy:
	cld			;将DF标志位设置为0，确保指针递增
	push ebp		;我们用ebp指针配合sp，而不是esp，干脆在这里存进栈备份一下
	mov ebp,esp
	push ecx		;ecx保存着程序头数，后续会改为段字节数，这里备份一下
	
	mov edi,[ebp+8]		;源地址
	mov esi,[ebp+12]	;目标地址
	mov ecx,[ebp+16]	;我们是逐字节复制，字节数也就是要复制的次数
	rep movsb		;rep是重复，次数是ecx，movsb是按字节从ds:ei复制内容到es:di处，和cld配合使用，让ei和di自动自增
	
	pop ecx			
	pop ebp			;恢复这两个参数，不影响上面循环的进行
	ret
;-------------------- 从硬盘读取内核的函数 --------------------------------

rd_disk_m_32:
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
	mov [ebx],ax
	add ebx,2
	loop .go_on_ready	;从0x1F0，data寄存器读取数据

	ret
