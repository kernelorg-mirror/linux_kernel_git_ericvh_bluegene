/*
 * Blue Gene board definitions
 *
 * Todd Inglett <tinglett@us.ibm.com>
 *
 * Copyright 2005, 2007, 2009  International Business Machines, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASM_BLUEGENE_H__
#define __ASM_BLUEGENE_H__

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

void __init bgp_init_cns(void);
void bgp_udbg_putc(char c);
unsigned int bgp_get_irq(void);
void bgp_send_ipi(int cpu, int msg);
void bgp_init_IPI(int cpu, int msg);
void __init bgp_init_IRQ(void);

/* Interrupt encoding for Blue Gene/P hardware).
 * Given a BIC group and bit index within the group,
 * bic_hw_to_irq(group, gint) returns the Linux IRQ number.
 */
static inline unsigned bic_hw_to_irq(unsigned group, unsigned gint)
{
	return ((group+1) << 5) | (gint & 0x1f);
}


/* Wrappers for CNS calls.
 * Any pointers must point to locations that will not take TLB misses.
 */
int bluegene_testInboxAttention(void);
int bluegene_testForOutboxCompletion(void);
int bluegene_writeRASEvent_nonBlocking(unsigned facility,
				       unsigned unit,
				       unsigned short err_code,
				       unsigned numDetails,
				       unsigned details[]);
int bluegene_writeRASString(unsigned facility,
			    unsigned unit,
			    unsigned short err_code,
			    char* str);
int bluegene_writeRASString_nonBlocking(unsigned facility,
					unsigned unit,
					unsigned short err_code,
					char* str);
int bluegene_writeToMailboxConsole(char *msg, unsigned msglen);
int bluegene_writeToMailboxConsole_nonBlocking(char *msg, unsigned msglen);
unsigned bluegene_readFromMailboxConsole(char *buf, unsigned bufsize);

int bluegene_macResetPHY(void);
int bluegene_macTestRxLink(void);
int bluegene_macTestTxLink(void);

int bluegene_takeCPU(unsigned cpu, void *arg, void (*entry)(unsigned cpu, void *arg));

int bluegene_getPersonality(void* buff, unsigned buffSize);

int bluegene_mapXEMAC(void* baseAddr);

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif
