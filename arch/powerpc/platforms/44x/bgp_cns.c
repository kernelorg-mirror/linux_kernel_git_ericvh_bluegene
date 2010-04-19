/*
 * Blue Gene/P Common Node Services (CNS) wrappers
 *
 * These are declared in asm/bluegene.h but implemented here.
 *
 * Copyright 2003-2009 International Business Machines, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Author: Todd Inglett <tinglett@us.ibm.com>
 */

#include <linux/init.h>
#include <linux/of_platform.h>
#include <asm/pgtable.h>
#include <asm/bluegene.h>
#include <asm/bgcns.h>

/* The descriptor for CNS identifies location and entry point of firmware.
 * We re-build it from data passed through the ibm,bluegene-cns device tree entry.
 */
BGCNS_Descriptor bgcnsd;

/* These functions spin on specific errors when we can't print messages.
 * They make it easy to find the cause of the error by finding the iar in the
 * kernel System.map.
 */
static void noinline __init bgp_fatal_no_ibm_bluegene_cns(void) { for (;;); }
static void noinline __init bgp_fatal_no_base_va(void) { for (;;); }
static void noinline __init bgp_fatal_no_base_pa(void) { for (;;); }
static void noinline __init bgp_fatal_no_services(void) { for (;;); }
static void noinline __init bgp_fatal_no_size(void) { for (;;); }
static void noinline __init bgp_fatal_no_version(void) { for (;;); }

/* Get the descriptor for CNS from the device tree.
 * Don't inline so we can make out the stack trace easier when it isn't working.
 */
static void noinline __init get_cns_descriptor(BGCNS_Descriptor *bgcnsd)
{
	int len;
	const unsigned *reg;
	struct device_node *devcns = of_find_node_by_path("/ibm,bluegene/cns");

	if (!devcns) bgp_fatal_no_ibm_bluegene_cns();

	reg = of_get_property(devcns, "base-va", &len);
	if (!reg) bgp_fatal_no_base_va();
	bgcnsd->baseVirtualAddress = *reg;
	reg = of_get_property(devcns, "base-pa", &len);
	if (!reg) bgp_fatal_no_base_pa();
	bgcnsd->basePhysicalAddress = *reg;
	bgcnsd->basePhysicalAddressERPN = 0;	/* assumes DDR <= 4G */
	reg = of_get_property(devcns, "services", &len);
	if (!reg) bgp_fatal_no_services();
	bgcnsd->services = (void *)(*reg);
	reg = of_get_property(devcns, "size", &len);
	if (!reg) bgp_fatal_no_size();
	bgcnsd->size = *reg;
	reg = of_get_property(devcns, "version", &len);
	if (!reg) bgp_fatal_no_version();
	bgcnsd->version = *reg;
}

void __init ppc44x_update_tlb_hwater(void);	/* from mm/44x_mmu.c */

static void noinline __init map_cns(BGCNS_Descriptor *bgcnsd)
{
	unsigned word0, word1, word2;
	int entry = 62;	/* We reserve one of the PPC44x_EARLY_TLBS in asm/mmu-44x.h */

	word0 = (bgcnsd->baseVirtualAddress & 0xfffff000) | PPC44x_TLB_VALID | PPC44x_TLB_256K;
	word1 = (bgcnsd->basePhysicalAddress & 0xfffff000) | (bgcnsd->basePhysicalAddressERPN & 0xf);
	word2 = PPC44x_TLB_SW | PPC44x_TLB_SR | PPC44x_TLB_SX | PPC44x_TLB_M | PPC44x_TLB_WL1 | PPC44x_TLB_U2;
	__asm__ __volatile__(
		"tlbwe	%1,%0,0\n"
		"tlbwe	%2,%0,1\n"
		"tlbwe	%3,%0,2\n"
		"isync\n" : : "r" (entry), "r" (word0), "r" (word1), "r" (word2));
}

extern int map_page(unsigned long va, phys_addr_t pa, int flags);

void __init bgp_init_cns(void)
{
	unsigned long v_start, v_end, v, p;

	if (bgcnsd.size == 0) {
		/* Get the descriptor, map CNS, and tell Linux about the mapping. */
		get_cns_descriptor(&bgcnsd);
		v_start = bgcnsd.baseVirtualAddress;
		v_end = v_start + bgcnsd.size;
		v_start -= PAGE_SIZE;		/* hack: reserve 1 extra page */
		v = v_start;
		p = bgcnsd.basePhysicalAddress;	/* always < 4G */
		/* We must be careful because we could hit 4G and wrap to v == 0.
		 * Hence the v > v_start check.
		 */
		for (; v < v_end && v > v_start; v += PAGE_SIZE, p += PAGE_SIZE)
			map_page(v, p, _PAGE_RAM_TEXT);
	}
	map_cns(&bgcnsd);
}

/* Simple udbg_putc.   We perform rudimentary buffering so it is readable. */
static int bgp_udbg_cur = 0;
static char bgp_udbg_buf[256];
void bgp_udbg_putc(char c)
{
	bgp_udbg_buf[bgp_udbg_cur++] = c;
	if (c == '\n' || bgp_udbg_cur >= sizeof(bgp_udbg_buf)) {
		if (bgcnsd.size)
			bluegene_writeToMailboxConsole(bgp_udbg_buf, bgp_udbg_cur);
		bgp_udbg_cur = 0;
	}
}


#define CALLCNS(service) \
	({ unsigned flags; \
	   typeof(bgcnsd.services->service) ret; \
	   local_save_flags(flags); \
	   local_irq_disable(); \
	   ret = bgcnsd.services->service; \
	   local_irq_restore(flags); \
	   ret; \
	 })


/* This returns non-zero if there is something in an input mailbox. */
int bluegene_testInboxAttention(void)
{
	/* ToDo: this should be fast.  Read the DCR directly. */
	return CALLCNS(testInboxAttention());
}

int bluegene_testForOutboxCompletion(void)
{
	return CALLCNS(testForOutboxCompletion());
}

int bluegene_writeRASEvent_nonBlocking(unsigned facility,
				       unsigned unit,
				       unsigned short err_code,
				       unsigned numDetails,
				       unsigned details[])
{
	return CALLCNS(writeRASEvent_nonBlocking(facility, unit, err_code, numDetails, details));
}

int bluegene_writeRASString(unsigned facility,
			    unsigned unit,
			    unsigned short err_code,
			    char* str)
{
	return CALLCNS(writeRASString(facility, unit, err_code, str));
}

int bluegene_writeRASString_nonBlocking(unsigned facility,
					unsigned unit,
					unsigned short err_code,
					char* str)
{
	return CALLCNS(writeRASString_nonBlocking(facility, unit, err_code, str));
}

int bluegene_writeToMailboxConsole(char *msg, unsigned msglen)
{
	return CALLCNS(writeToMailboxConsole(msg, msglen));
}

int bluegene_writeToMailboxConsole_nonBlocking(char *msg, unsigned msglen)
{
	return CALLCNS(writeToMailboxConsole_nonBlocking(msg, msglen));
}

unsigned bluegene_readFromMailboxConsole(char *buf, unsigned bufsize)
{
	return CALLCNS(readFromMailboxConsole(buf, bufsize));
}

int bluegene_macResetPHY(void)
{
	return CALLCNS(macResetPHY());
}
    /* ! @brief Tests the MAC unit's link but does not block. */
     /* ! @param[in] link_type specifies the type of link to be tested. */
     /* ! @param[out] result points to the link status, which is valid only when the return code is */
     /* !     BGCNS_RC_COMPLETE. A value of one (1) indicates that the link is active; zero (0) */
     /* !     indicates that it is inactive. */
     /* ! @param[in] reset indicates whether this is the beginning (1) or a continuation (0) of a */
     /* !     test link sequence.  That is, callers should initiate a sequence with reset=1 and then */
     /* !     if receiving a return code of BGCNS_RC_CONTINUE, should invoke this service again with */
     /* !     reset=0. */
     /* ! @param[in] timeoutInMillis the (approximate) number of milliseconds that this service can have */
     /* !     before returning.  If the allotted time is not sufficient, the service will return BGCNS_RC_CONTINUE */
     /* !     to indicate that it needs additional time. */
     /* ! @return BGCNS_RC_COMPLETE if the test is complete (result is valid only in this case). BGCNS_RC_CONTINUE */
     /* !     if the reset operation is not yet complete.  BGCNS_RC_ERROR if the reset operation failed. */
    int (*macTestLink_nonBlocking)(BGCNS_LinkType link_type, unsigned* result, int reset, unsigned timeoutInMillis);


int bluegene_macTestRxLink(void)
{
	return CALLCNS(macTestLink(BGCNS_Receiver));
}


int bluegene_macTestTxLink(void)
{
	return CALLCNS(macTestLink(BGCNS_Transmitter));
}

int bluegene_takeCPU(unsigned cpu, void *arg, void (*entry)(unsigned cpu, void *arg))
{
	return CALLCNS(takeCPU(cpu, arg, entry));
}


int bluegene_getPersonality(void *buff, unsigned buffSize)
{
	int sz;
	unsigned flags;

	local_save_flags(flags);
	local_irq_disable();
	sz = bgcnsd.services->getPersonalitySize();
	if (sz > buffSize)
		sz = buffSize;
	memcpy(buff, bgcnsd.services->getPersonalityData(), sz);
	local_irq_restore(flags);

	return sz;
}

int bluegene_mapXEMAC(void* baseAddr)
{
	return CALLCNS(mapDevice(BGCNS_XEMAC, baseAddr));
}

EXPORT_SYMBOL(bluegene_getPersonality) ;
EXPORT_SYMBOL(bgcnsd) ;
