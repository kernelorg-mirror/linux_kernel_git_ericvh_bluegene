/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2007,2010
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
 ********************************************************************/


#ifndef _LINUX_KERNEL_SPI_H_  /*  Prevent multiple inclusion */
#define _LINUX_KERNEL_SPI_H_

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>

#include <common/bgp_personality.h>

#ifndef __LINUX_KERNEL__
#define __LINUX_KERNEL__
#endif

#ifndef __BGP_HIDE_STANDARD_TYPES__
#define __BGP_HIDE_STANDARD_TYPES__
#endif


/*  this comes from src/arch/ppc/platforms/4xx/bluegene.c */
extern int bluegene_getPersonality(void *buf, int bufsize);
#define rts_get_personality(p,s)  bluegene_getPersonality(p,s)


/*   Lockbox used by DMA_InjFifoRgetFifoFullInit ... */
#define  LockBox_FetchAndClear(x)


/*  asm inlines used by dma spi */

#define _bgp_msync(void) asm volatile ("msync" : : : "memory")
#define _bgp_mbar(void)  asm volatile ("mbar"  : : : "memory")
#define _bgp_isync(void) asm volatile ("isync" : : : "memory")
extern inline void _bgp_msync_nonspeculative( void )
{
    do {
       asm volatile ("   b 1f;"
                     "   nop;"
                     "1: msync;"
                     : : : "memory");
       }
       while(0);
}

#define _bgp_QuadLoad(v,f)  asm volatile( "lfpdx  " #f ",0,%0" :: "r" (v) : "fr" #f )
#define _bgp_QuadStore(v,f) asm volatile( "stfpdx " #f ",0,%0" :: "r" (v) : "memory" )

#define _bgp_dcache_touch_line(v) do { asm volatile ("dcbt  0,%0" : : "r" (v)); } while(0)

/*  in ppc450_inlines.h */
/* #define _bgp_msync_nonspeculative(x) */
/* { */
/*     do { */
/*        asm volatile ("   b 1f;" */
/*                      "   nop;" */
/*                      "1: msync;" */
/*                      : : : "memory"); */
/*        } */
/*        while(0); */
/* } */

/*  assert and printf variants for kernel use */

#define assert(x) if ( !(x)) printk( KERN_ALERT "(E) bgpdma assertion at %s:%d\n",__FILE__,__LINE__);

#define SPI_assert(x) assert(x)

#define printf(...) printk(KERN_INFO __VA_ARGS__)

/*  we need a dummy errno for linking */
static int errno;

/*  general bgp quad struct */
/*  (better one in bgp_types.h , use that in preference) */
/* typedef struct { u32 w[4]; } __attribute__ ((aligned(16))) _bgp_QuadWord_t; */


/*  virtual base address of the DMA (see bgp_dma_memap.h) */
#define _BGP_VA_DMA  bgpdma_kaddr

#include <asm/bgp_personality.h>
#include <common/alignment.h>
#include <bpcore/bgp_dma_memmap.h>
#include <bpcore/ic_memmap.h>

#include <spi/DMA_Counter.h>
#include <spi/DMA_Fifo.h>
#include <spi/DMA_InjFifo.h>
#include <spi/DMA_RecFifo.h>



#endif
