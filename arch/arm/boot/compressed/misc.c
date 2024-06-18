// SPDX-License-Identifier: GPL-2.0
/*
 * misc.c
 * 
 * This is a collection of several routines from gzip-1.0.3 
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 *
 * Modified for ARM Linux by Russell King
 *
 * Nicolas Pitre <nico@visuaide.com>  1999/04/14 :
 *  For this code to run directly from Flash, all constant variables must
 *  be marked with 'const' and all other variables initialized at run-time 
 *  only.  This way all non constant variables will end up in the bss segment,
 *  which should point to addresses in RAM and cleared to 0 on start.
 *  This allows for a much quicker boot time.
 */

unsigned int __machine_arch_type;

#include <linux/compiler.h>	/* for inline */
#include <linux/types.h>
#include <linux/linkage.h>

static void putstr(char *ptr);
extern void error(char *x);

#include CONFIG_UNCOMPRESS_INCLUDE

#ifdef CONFIG_DEBUG_ICEDCC

#if defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_V6K) || defined(CONFIG_CPU_V7)

static void icedcc_putc(int ch)
{
	int status, i = 0x4000000;

	do {
		if (--i < 0)
			return;

		asm volatile ("mrc p14, 0, %0, c0, c1, 0" : "=r" (status));
	} while (status & (1 << 29));

	asm("mcr p14, 0, %0, c0, c5, 0" : : "r" (ch));
}


#elif defined(CONFIG_CPU_XSCALE)

static void icedcc_putc(int ch)
{
	int status, i = 0x4000000;

	do {
		if (--i < 0)
			return;

		asm volatile ("mrc p14, 0, %0, c14, c0, 0" : "=r" (status));
	} while (status & (1 << 28));

	asm("mcr p14, 0, %0, c8, c0, 0" : : "r" (ch));
}

#else

static void icedcc_putc(int ch)
{
	int status, i = 0x4000000;

	do {
		if (--i < 0)
			return;

		asm volatile ("mrc p14, 0, %0, c0, c0, 0" : "=r" (status));
	} while (status & 2);

	asm("mcr p14, 0, %0, c1, c0, 0" : : "r" (ch));
}

#endif

#define putc(ch)	icedcc_putc(ch)
#endif

#define w32(addr, val)      (*(volatile unsigned int *)(addr) = (val))
#define r32(addr)           (*(volatile unsigned int *)(addr))
#define APB_UART1_BASE      0xA0000100

// void init_uart_new(){
// 	w32(APB_UART1_BASE + 0x0c, 0x83);
// 	w32(APB_UART1_BASE + 0x00, 0x8B);//400MHz,38400//0x34
// 	w32(APB_UART1_BASE + 0x04, 0x2);
// 	w32(APB_UART1_BASE + 0x0c, 0x03);
// 	w32(APB_UART1_BASE + 0x04, 0x01);//2
// 	w32(APB_UART1_BASE + 0x08, 0x07);
// }

void send_uart_new(char c){
	while( !(r32(APB_UART1_BASE + 0x14) & 0x20) ) asm("nop");
	w32(APB_UART1_BASE + 0x00, (c));
}

char t_only;
static void putstr(char *ptr)
{
	w32(APB_UART1_BASE + 0x0c, 0x83);
	w32(APB_UART1_BASE + 0x00, 0x8B);//400MHz,38400//0x34
	w32(APB_UART1_BASE + 0x04, 0x2);
	w32(APB_UART1_BASE + 0x0c, 0x03);
	w32(APB_UART1_BASE + 0x04, 0x01);//2
	w32(APB_UART1_BASE + 0x08, 0x07);

	t_only = '\r';
	while ((*ptr) != '\0') {
		if ((*ptr) == '\n'){
			send_uart_new(t_only);
		}
		send_uart_new(*ptr);
		ptr++;
	}
	flush();
}

// char t_only;
// char to_send;
// static void putstr(char *ptr)
// {
// 	w32(APB_UART1_BASE + 0x0c, 0x83);
// 	w32(APB_UART1_BASE + 0x00, 0x8B);//400MHz,38400//0x34
// 	w32(APB_UART1_BASE + 0x04, 0x2);
// 	w32(APB_UART1_BASE + 0x0c, 0x03);
// 	w32(APB_UART1_BASE + 0x04, 0x01);//2
// 	w32(APB_UART1_BASE + 0x08, 0x07);

// 	t_only = '\r';

// 	while ((to_send = *ptr++) != '\0') {
// 		if (to_send == '\n'){
// 			while( !(r32(APB_UART1_BASE + 0x14) & 0x20) ) asm("nop");
// 			w32(APB_UART1_BASE + 0x00, (t_only));
// 		}

// 		while( !(r32(APB_UART1_BASE + 0x14) & 0x20) ) asm("nop");
// 		w32(APB_UART1_BASE + 0x00, (to_send));
// 	}

// 	flush();
// }

/*
 * gzip declarations
 */
extern char input_data[];
extern char input_data_end[];

unsigned char *output_data;

unsigned long free_mem_ptr;
unsigned long free_mem_end_ptr;

#ifndef arch_error
#define arch_error(x)
#endif

void error(char *x)
{
	arch_error(x);

	putstr("\n\n");
	putstr(x);
	putstr("\n\n -- System halted");

	while(1);	/* Halt */
}

asmlinkage void __div0(void)
{
	error("Attempting division by 0!");
}

unsigned long __stack_chk_guard;

void __stack_chk_guard_setup(void)
{
	__stack_chk_guard = 0x000a0dff;
}

void __stack_chk_fail(void)
{
	error("stack-protector: Kernel stack is corrupted\n");
}

extern int do_decompress(u8 *input, int len, u8 *output, void (*error)(char *x));


// //write bytes for __v7_ca9mp_proc_info
// void write_bytes()
// {
// 	// 定义目标内存地址，这需要确保在您的硬件上是正确的
//     volatile unsigned char *target = (volatile unsigned char *)0xc0413d98;

//     // 一系列要写入的字节
//     unsigned char bytes_to_write[] = {
//         0x90, 0xc0, 0x0f, 0x41, 0xf0, 0xff, 0x0f, 0xff, 0x0e, 0x0c, 0x00, 0x00,
//         0x02, 0x0c, 0x00, 0x00, 0x7c, 0x76, 0xcf, 0xff, 0x74, 0x05, 0x30, 0xc0,
//         0x7a, 0x05, 0x30, 0xc0, 0x97, 0x80, 0x00, 0x00, 0x38, 0xb3, 0x10, 0xc0,
//         0xd0, 0x90, 0x41, 0xc0, 0x1c, 0x4f, 0x40, 0xc0, 0x2c, 0x90, 0x41, 0xc0,
//         0x00, 0x90, 0x41, 0xc0
//     };

//     // 循环写入字节
//     for (size_t i = 0; i < sizeof(bytes_to_write); ++i) {
//         writeb(bytes_to_write[i], target + i);
//     }
// }

// #include <linux/mm.h>

/**
 * 比较两个内存地址处的数据
 * @param addr1 第一个地址的指针
 * @param addr2 第二个地址的指针
 * @param size 要比较的数据长度（字节）
 */
void compare_memory(const void *addr1, const void *addr2, size_t size) {
    u8 *ptr1 = addr1;
    u8 *ptr2 = addr2;
	size_t i;
    for (i = 0; i < size; ++i) {
        if (ptr1[i] != ptr2[i]) {
            //pr_info("Mismatch at offset %zu: addr1[0x%p] = 0x%02x, addr2[0x%p] = 0x%02x\n",i, &ptr1[i], ptr1[i], &ptr2[i], ptr2[i]);
			putstr("This data is inconsistent...");
			//ptr2[i]=ptr1[i];
        }
    }
}

void compare_memory_change(const void *addr1, const void *addr2, size_t size) {
    u8 *ptr1 = addr1;
    u8 *ptr2 = addr2;
	size_t i;
    for (i = 0; i < size; ++i) {
        if (ptr1[i] != ptr2[i]) {
            //pr_info("Mismatch at offset %zu: addr1[0x%p] = 0x%02x, addr2[0x%p] = 0x%02x\n",i, &ptr1[i], ptr1[i], &ptr2[i], ptr2[i]);
			// putstr("This data is inconsistent...");
			ptr2[i]=ptr1[i];
        }
    }
}


void
decompress_kernel(unsigned long output_start, unsigned long free_mem_ptr_p,
		unsigned long free_mem_ptr_end_p,
		int arch_id)
{
	compare_memory(0x40,0xc3000000,0x100000);

	int ret;

	__stack_chk_guard_setup();

	output_data		= (unsigned char *)output_start;
	free_mem_ptr		= free_mem_ptr_p;
	free_mem_end_ptr	= free_mem_ptr_end_p;
	__machine_arch_type	= arch_id;

	arch_decomp_setup();

	putstr("Uncompressing Linux...");
	ret = do_decompress(input_data, input_data_end - input_data,
			    output_data, error);

	//修改__v7_ca9mp_proc_info的数据
	//write_bytes();

	if (ret)
		error("decompressor returned an error");
	else
		putstr(" done, booting the kernel.\n");
}


void compare_flash_with_memeory(void)
{
	compare_memory_change(0x300000,0xc0008000,5404705);
}