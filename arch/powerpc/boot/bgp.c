/*
 * (C) Copyright IBM Corp. 2007, 2010
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses>.
 *
 * Author: Chris Ward <tjcw@uk.ibm.com>
 *
 * Based on earlier code:
 *   Copyright (C) Paul Mackerras 1997.
 *
 *   Matt Porter <mporter@kernel.crashing.org>
 *   Copyright 2002-2005 MontaVista Software Inc.
 *
 *   Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *   Copyright (c) 2003, 2004 Zultys Technologies
 *
 *   David Gibson, IBM Corporation, 2007
 *
 */
#include "types.h"
#include "ops.h"
#include "stdio.h"
#include "4xx.h"
#include "44x.h"
#include "bgcns.h"
/* Types needed for the personality */
typedef u8  uint8_t;
typedef u16 uint16_t;
typedef u32 uint32_t;
#include "bgp_personality.h"

/* Blue Gene firmware jumps to 0x10.
 * Simply branch to _zimage_start which is typically 0x800000.
 * Must also link with --section-start bgstart=0
 */
asm (
"       .section bgstart, \"ax\";		"
"       .=0x10;					"
"       lis     %r9, _zimage_start@h;		"
"       ori	%r9, %r9, _zimage_start@l;	"
"       mtlr    %r9;				"
"       blr;					"
"	.previous				"
);

/* This will point directly to CNS which remains mapped on entry. */
BGCNS_Descriptor* cns;

static void bgp_console_write(const char *msg, int len) __attribute__((unused)) ;

static void bgp_console_write(const char *msg, int len)
{
	if (cns)
		cns->services->writeToMailboxConsole((char *)msg, len);
}

static void bgp_fixup_bluegene_cns(BGCNS_Descriptor *cns)
{
	void *node = finddevice("/ibm,bluegene/cns");
	if (node) {
		setprop_val(node, "base-va", cns->baseVirtualAddress);
		setprop_val(node, "base-pa", cns->basePhysicalAddress);
		setprop_val(node, "size", cns->size);
		setprop_val(node, "services", cns->services);
		setprop_val(node, "version", cns->version);
	} else {
		fatal("could not find /ibm,bluegene/cns node in device tree");
	}
}

static void bgp_fixup_bluegene_personality(BGP_Personality_t *bgpers)
{
	void *node = finddevice("/ibm,bluegene/personality");
	if (node) {
		/* We could include individual fields of the personality as needed
		 * so that Linux doesn't need to decode the struct directly.  We
		 * provide raw-data for external tools and daemons.
		 * This can replace /proc/personality
		 */
		unsigned frequency = bgpers->Kernel_Config.FreqMHz * 1000000;
		setprop(node, "raw-data", bgpers, sizeof(*bgpers));
		setprop_val(node, "frequency", frequency);
	} else {
		fatal("could not find /ibm,bluegene/personality node in device tree");
	}
}

static void bgp_fixup_bluegene_initrd(void)
{
	void *node = finddevice("/chosen");
	if (node) {
		/* On Blue Gene we may have a gzipped ramdisk loaded at a fixed
		 * address (0x1000000).  It is preceeded by a 4-byte magic value and a
		 * 4-byte big endian length.
		 */
		unsigned *rd = (unsigned *)0x1000000;	/* 16M */

		if (rd[0] == 0xf0e1d2c3 && rd[1] != 0) {
			unsigned initrd_start = (unsigned)(rd+2);
			unsigned initrd_len = rd[1];
			unsigned initrd_end = initrd_start + initrd_len;
			setprop_val(node, "linux,initrd-start", initrd_start);
			setprop_val(node, "linux,initrd-end", initrd_end);
		}
	} else {
		fatal("could not find chosen node in device tree");
	}
}

static void bgp_fixups(void)
{
	BGP_Personality_t *bgpers = cns->services->getPersonalityData();
	unsigned int DDRSize = (bgpers->DDR_Config.DDRSizeMB << 20) - cns->size;
	unsigned int freq = bgpers->Kernel_Config.FreqMHz * 1000000;

/* For vRNIC configurations, turn down the memory that Linux thinks is on the node so the vRNIC can map it all */
	if ( (DDRSize & 0xf0000000 ) == 0xd0000000 ) DDRSize = 0xb0000000 ;

        dt_fixup_memory(0, DDRSize);
        dt_fixup_cpu_clocks(freq, freq, freq);

	bgp_fixup_bluegene_cns(cns);
	bgp_fixup_bluegene_personality(bgpers);
	bgp_fixup_bluegene_initrd();

#if 0
	 /*  FIXME: sysclk should be derived by reading the FPGA registers */
	unsigned long sysclk = 33000000;

	ibm440gp_fixup_clocks(sysclk, 6 * 1843200);
	ibm4xx_sdram_fixup_memsize();
	dt_fixup_mac_address_by_alias("ethernet0", ebony_mac0);
	dt_fixup_mac_address_by_alias("ethernet1", ebony_mac1);
	ibm4xx_fixup_ebc_ranges("/plb/opb/ebc");
	ebony_flashsel_fixup();
#endif
}


void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
                   unsigned long r6, unsigned long r7)
{
	cns = (BGCNS_Descriptor*) r3;
#if defined(CONFIG_BLUEGENE_NOISY_BOOT)
	console_ops.write = bgp_console_write;
#endif

	simple_alloc_init(_end, 256 << 20, 32, 64);

	platform_ops.fixups = bgp_fixups;
	platform_ops.exit = ibm44x_dbcr_reset;
	fdt_init(_dtb_start);

/*	serial_console_init(); */
}
