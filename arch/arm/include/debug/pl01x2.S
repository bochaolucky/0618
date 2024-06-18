/* arch/arm/include/debug/pl01x.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/
#include <linux/amba/serial.h>

#ifdef CONFIG_DEBUG_ZTE_ZX
#undef UART01x_DR
#undef UART01x_FR
#define UART01x_DR     0x04
#define UART01x_FR     0x14
#endif

/** srliu: */
#undef UART01x_DR
#undef UART01x_FR
#undef UART01x_FR_TXFF
#undef UART01x_FR_BUSY
#define UART01x_DR		0x0
#define UART01x_FR     	0x14
#define UART01x_FR_TXFF 0x20
#define UART01x_FR_BUSY 0x20



#ifdef CONFIG_DEBUG_UART_PHYS
		.macro	addruart, rp, rv, tmp
		ldr	\rp, =CONFIG_DEBUG_UART_PHYS
		ldr	\rv, =CONFIG_DEBUG_UART_VIRT
		.endm
#endif

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #UART01x_DR]
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #UART01x_FR] @ ldr r3, [r1, #0x14]
ARM_BE8(	rev	\rd, \rd )
			tst	\rd, #UART01x_FR_TXFF	@ tst r3, 0x40 0100 0000
			beq	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #UART01x_FR]
ARM_BE8(	rev	\rd, \rd )
		tst	\rd, #UART01x_FR_BUSY
		beq	1001b
		.endm
