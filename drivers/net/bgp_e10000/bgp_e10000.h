/*
 * bgp_e10000.h: common header file for BlueGene/P 10 GbE driver
 *
 * Copyright (c) 2007, 2010 International Business Machines
 * Author: Andrew Tauferner <ataufer@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#include <linux/proc_fs.h>
#include <asm/io.h>

#ifndef _BGP_E10000_H
#define _BGP_E10000_H

#define DBG_LEVEL1      1
#define DBG_LEVEL2      (DBG_LEVEL1 | 2)
#define DBG_LEVEL3      (DBG_LEVEL2 | 4)
#define DBG_E10000	8
#define DBG_EMAC	16
#define DBG_TOMAL	32
#define DBG_XSGS	64
#define DBG_DEVBUS	128
#define DBG_NAPI        256
#define DBG_SCATTERGATHER          512

#define BGP_E10000_MIN_MTU 256
#define BGP_E10000_MAX_MTU 9000
#define BGP_E10000_FCS_SIZE 4


#ifdef CONFIG_BGP_E10000_DBG
#include <asm/udbg.h>
#define PRINTK(detail, format, args...) if (((detail) & CONFIG_BGP_E10000_DBG_LEVEL) == (detail)) udbg_printf("%s: " format, __FUNCTION__, ##args)
#else
#define PRINTK(detail, format, args...)
#endif

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;


typedef enum {
	e10000_ras_none			= 0x00,
	e10000_ras_hw_not_found		= 0x01,
	e10000_ras_netdev_alloc_failure	= 0x02,
	e10000_ras_netdev_reg_failure	= 0x03,
	e10000_ras_mtu_invalid		= 0x04,
	e10000_ras_tx_timeout		= 0x05,
	e10000_ras_internal_error	= 0x07,
	e10000_ras_hw_failure		= 0x09,
	e10000_ras_link_error		= 0x0a,
	e10000_ras_phy_reset_error	= 0x0b,
	e10000_ras_emac_config_error	= 0x0c,
	e10000_ras_link_loss		= 0x0d,

	e10000_ras_max			= 0xff
} e10000_ras_id;


typedef struct _E10000_PROC_ENTRY {
					char* name;
					void* addr;
					struct proc_dir_entry* entry;
} E10000_PROC_ENTRY;



/*  Generates a RAS event for ethernet. */
void e10000_printr(U16 subComponent,
		   U16 id,
		   char* format,
		   ...);


static inline U32 mfdcrx(U32 dcrNum)
{
        U32 dcrVal = 0;

        asm volatile("mfdcrx %0,%1": "=r" (dcrVal) : "r" (dcrNum) : "memory");

        return dcrVal;
}


static inline void mtdcrx(U32 dcrNum,
                          U32 dcrVal)
{
        asm volatile ("mtdcrx %0,%1": :"r" (dcrNum), "r" (dcrVal) : "memory");
        isync();

        return;
}


static inline void msync(void)
{
	do { asm volatile ("msync" : : : "memory"); } while(0);

	return;
}


static inline int e10000_proc_read(char* page,
				   char** start,
				   off_t off,
				   int count,
				   int* eof,
				   void* data)
{
        int rc = 0;
        int value;

	 /*  Read the value of the associated address and print it. */
	value = in_be32(data);
        rc = snprintf(page, count, "%08x\n", value);

        *eof = 1;

        return rc;
}


static inline int e10000_proc_write(struct file* file,
				    const char* buffer,
				    unsigned long len,
				    void* data)
{
        unsigned int value;
        char valStr[128];
        int strLen = sizeof(valStr)-1;

        if (strLen > len)
                strLen = len;
        if (copy_from_user(valStr, buffer, strLen))
                return -EFAULT;
        else if (len) {
		char* endp;

                 /*  NULL terminate the string of digits and convert to its numeric value. */
                if (valStr[strLen-1] == '\n')
                        strLen--;
                valStr[strLen] = '\0';
                value = simple_strtoul(valStr, &endp, 0);

		 /*  Write the value to the associated address. */
		out_be32(data, value);
        }

        return len;
}


static inline struct proc_dir_entry* e10000_create_proc_entry(struct proc_dir_entry* dir,
							      char* name,
							      void* addr)
{
        struct proc_dir_entry* entry = create_proc_entry(name, S_IRUGO, dir);
        if (entry) {
                entry->nlink = 1;
                entry->read_proc = e10000_proc_read;
                entry->write_proc = e10000_proc_write;
                entry->data = addr;
        }

        return entry;
}

#endif
