/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2010
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
 *
 * Description: Blue Gene/P low-level driver for copy_tofrom_user thorough the
 * parallel floating point unit
 *
 *
 *
 ********************************************************************/
#define REQUIRES_DUMPMEM

#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/highmem.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>


#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/bitops.h>
#include <asm/time.h>

#include "../bgp_network/bgp_net_traceflags.h"
#include <common/bgp_bitnumbers.h>
//#include "bgp_bic_diagnosis.h"
#include "../bgp_network/bgdiagnose.h"
#include "../bgp_network/450_tlb.h"
/*  Can drop bits out of COMPILED_TRACEMASK if we want to selectively compile out trace */
#define COMPILED_TRACEMASK (0xffffffff)
/* #define COMPILED_TRACEMASK (k_t_error) */

#include <linux/KernelFxLog.h>

MODULE_DESCRIPTION("BG/P memory copy through parallel floating point registers");
MODULE_LICENSE("GPL");

#if defined(CONFIG_BLUEGENE_TORUS_TRACE)
int bgp_fpu_memcpy_tracemask = k_t_error ;
#define TRACEN(i,x...) KernelFxLog(bgp_fpu_memcpy_tracemask & (COMPILED_TRACEMASK & (i)),x)
#else
#define TRACEN(i,x...)
#endif

#include "bgp_memcpy.h"

#if defined(ADVENTUROUS_COPY_OPTIONS)
enum {
  k_force_mask = 0 ,
  k_enable_mask = 0 ,
  k_inhibit_fpu_in_slih = 0
};
#else
enum {
  k_force_mask = 0 ,
  k_enable_mask = 1 ,
  k_inhibit_fpu_in_slih = 0
};
#endif

enum {
  k_page_shift = PAGE_SHIFT ,
  k_page_size = 1 << k_page_shift ,
  k_page_offset_mask = k_page_size-1 ,
  k_fpu_alignment  = 16 ,
  k_fpu_align_mask = k_fpu_alignment - 1
} ;


static int source_alignment_statistics[k_fpu_alignment] ;
static int dest_alignment_statistics[k_fpu_alignment] ;
static int mutual_alignment_statistics[k_fpu_alignment] ;
struct ctl_table bgp_memcpy_table[] = {
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "use_dma",
	                .data           = &bgp_memcpy_control.use_dma,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "verify_fpu",
	                .data           = &bgp_memcpy_control.verify_fpu,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "verify_dma",
	                .data           = &bgp_memcpy_control.verify_dma,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "use_fpu",
	                .data           = &bgp_memcpy_control.use_fpu,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "dma_threshold",
	                .data           = &bgp_memcpy_control.dma_threshold,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "fpu_threshold",
	                .data           = &bgp_memcpy_control.fpu_threshold,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "faults_until_disable",
	                .data           = &bgp_memcpy_control.faults_until_disable,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "cycles_per_packet",
	                .data           = &bgp_memcpy_control.cycles_per_packet,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        } ,
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "rate_observe_report_count",
	                .data           = &bgp_memcpy_control.rate_observe_report_count,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        } ,
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "handle_pagecrossing",
	                .data           = &bgp_memcpy_control.handle_pagecrossing,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        } ,
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "fpu_handle_pagecrossing_read",
	                .data           = &bgp_memcpy_control.fpu_handle_pagecrossing_read,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        } ,
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "fpu_handle_pagecrossing_write",
	                .data           = &bgp_memcpy_control.fpu_handle_pagecrossing_write,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        } ,
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "mask",
	                .data           = &bgp_memcpy_control.mask,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        } ,
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "assist_active",
	                .data           = &bgp_memcpy_control.assist_active,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        } ,
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "statistics",
	                .data           = &bgp_dma_memcpy_statistics,
	                .maxlen         = k_copy_statistics*sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        } ,
          {
                  .ctl_name       = CTL_UNNUMBERED,
                  .procname       = "source_alignment_statistics",
                  .data           = &source_alignment_statistics,
                  .maxlen         = k_fpu_alignment*sizeof(int),
                  .mode           = 0644,
                  .proc_handler   = &proc_dointvec
          } ,
          {
                  .ctl_name       = CTL_UNNUMBERED,
                  .procname       = "dest_alignment_statistics",
                  .data           = &dest_alignment_statistics,
                  .maxlen         = k_fpu_alignment*sizeof(int),
                  .mode           = 0644,
                  .proc_handler   = &proc_dointvec
          } ,
          {
                  .ctl_name       = CTL_UNNUMBERED,
                  .procname       = "mutual_alignment_statistics",
                  .data           = &mutual_alignment_statistics,
                  .maxlen         = k_fpu_alignment*sizeof(int),
                  .mode           = 0644,
                  .proc_handler   = &proc_dointvec
          } ,
#if defined(CONFIG_BLUEGENE_TORUS_TRACE)
          {
                  .ctl_name       = CTL_UNNUMBERED,
                  .procname       = "tracemask",
                  .data           = &bgp_fpu_memcpy_tracemask,
                  .maxlen         = sizeof(int),
                  .mode           = 0644,
                  .proc_handler   = &proc_dointvec
          } ,
#endif
	        { 0 },
} ;

static struct ctl_path memcpy_ctl_path[] = {
	{ .procname = "bgp", .ctl_name = 0, },
	{ .procname = "copy", .ctl_name = 0, },
	{ },
};
bgp_memcpy_control_t bgp_memcpy_control =
	{
		.use_dma = 0 ,
		.use_fpu = 0 , // We suspect some kind of interaction with interrupts which makes it occasionally not work ...
		.dma_threshold = 10000 ,
		.fpu_threshold = 512 ,
		.verify_dma = 0 ,
		.verify_fpu = 0 ,
		.cycles_per_packet = 20 ,
		.rate_observe_report_count = 0xffffffff ,
		.faults_until_disable = 1 ,
		.handle_pagecrossing = 1 ,
		.fpu_handle_pagecrossing_read = 0 ,
		.fpu_handle_pagecrossing_write = 0 ,
		.mask = 1 ,
		.assist_active = 0
	};

unsigned int bgp_dma_memcpy_statistics[k_copy_statistics] ;


static void cause_fallback(void)
{
	TRACEN(k_t_request,"Turning off DH memcpy") ;
	bgp_memcpy_control.use_fpu = 0 ;
	dma_memcpy_statistic(k_copy_cause_fallback) ;
}
enum {
	k_diag_not_mapped=0
/* 	k_diagnose=1 */
};

enum {
	k_exploit_doublehummer = 1,
	k_verify_doublehummer = 1,
	k_fixup_faulty_memcpy=1,
	k_premark=0 ,
	k_map_write_check=0 ,
	k_map_read_check=0 ,
	k_disable_after_too_many_faults=1 ,
	k_inhibit_crosspage_write = 1 , // Set this if you want to not handle writes which cross a user-space page boundary
	k_inhibit_crosspage_read = 1 // Set this if you want to not handle reads which cross a user-space page boundary
};
static void report_faulty_memcpy(void * dest, const void * src, unsigned long size)
{
	unsigned int * di = (unsigned int *) dest ;
	const unsigned int * si = (const unsigned int *) src ;
	unsigned char * dc = (unsigned char *) (dest) ;
	const unsigned char * sc = (const unsigned char *) (src) ;
	unsigned int x ;
	unsigned int faultwordcount = 0 ;
	if( k_disable_after_too_many_faults)
		{
			int faults_to_go=bgp_memcpy_control.faults_until_disable-1 ;
			if( faults_to_go <= 0 )
				{
					cause_fallback() ;
				}
			else
				{
					bgp_memcpy_control.faults_until_disable=faults_to_go ;
				}
		}
	dma_memcpy_statistic(k_copy_verify_miscompares) ;
	TRACEN(k_t_error,"dest=%p src=%p size=0x%08lx",dest,src,size) ;
	for(x=0;x<size/sizeof(unsigned int);x+=1)
		{
			if( di[x] != si[x] )
				{
					TRACEN(k_t_error,"(E) x=0x%08x di+x=%p si+x=%p di[x]=0x%08x si[x]=0x%08x",
							x,di+x,si+x,di[x],si[x]) ;
					if( k_fixup_faulty_memcpy) di[x]=si[x] ;
					faultwordcount += 1 ;
				}
		}
	if( dc[size-3] != sc[size-3])
		{
			TRACEN(k_t_error,"(E) x=0x%08lx dc+x=%p sc+x=%p dc[x]=0x%02x sc[x]=0x%02x",
					size-3,dc+size-3,sc+size-3,dc[size-3],sc[size-3]) ;
			if( k_fixup_faulty_memcpy) dc[size-3]=sc[size-3] ;
		}
	if( dc[size-2] != sc[size-2])
		{
			TRACEN(k_t_error,"(E) x=0x%08lx dc+x=%p sc+x=%p dc[x]=0x%02x sc[x]=0x%02x",
					size-2,dc+size-2,sc+size-2,dc[size-2],sc[size-2]) ;
			if( k_fixup_faulty_memcpy) dc[size-2]=sc[size-2] ;
		}
	if( dc[size-1] != sc[size-1])
		{
			TRACEN(k_t_error,"(E) x=0x%08lx dc+x=%p sc+x=%p dc[x]=0x%02x sc[x]=0x%02x",
					size-1,dc+size-1,sc+size-1,dc[size-1],sc[size-1]) ;
			if( k_fixup_faulty_memcpy) dc[size-1]=sc[size-1] ;
		}
	TRACEN(k_t_error,"%d/%ld words incorrectly copied",faultwordcount,size/sizeof(unsigned int)) ;

}
/*  Check that a 'memcpy' was accurately done ... */
static void verify_memcpy(void * dest, const void * src, unsigned long size)
{
	unsigned int * di = (unsigned int *) dest ;
	const unsigned int * si = (const unsigned int *) src ;
	unsigned char * dc = (unsigned char *) (dest) ;
	const unsigned char * sc = (const unsigned char *) (src) ;
	unsigned int q = di[0] ^ si[0] ;
	unsigned int x ;
	dma_memcpy_statistic(k_copy_verify_attempts) ;
	TRACEN(k_t_fpucopy,"dest=%p src=%p size=0x%08lx di[0]=0x%08x si[0]=0x%08x",dest,src,size,di[0],si[0]) ;
	for(x=1;x<size/sizeof(unsigned int);x+=1)
		{
			q |= di[x] ^ si[x] ;
		}
	q |= (dc[size-3] ^ sc[size-3]) |(dc[size-2] ^ sc[size-2]) |(dc[size-1] ^ sc[size-1]) ;
	if(q) report_faulty_memcpy(dest,src,size) ;
}

typedef struct { unsigned char c[128] ; } miniblock ;

#define nl "\n"
/* Returns 0 for a good copy, 1 if an exception (unmapped storage) occurred */
static int doublehummer_copy_unroll(void  *to, const void *from, int count)
{
	int x1=0x10 ;
	int x2=0x20 ;
	int x3=0x30 ;
	int x4=0x40 ;
	int x5=0x50 ;
	int x6=0x60 ;
	int x7=0x70 ;
	int x8=0x80 ;
	int xa=0xa0 ;
	int xc=0xc0 ;
	int xe=0xe0 ;
	int rc ;
        asm  volatile (
        		"mtctr %[count]" nl
        		"100: lfpdx  0,0,%[src]" nl
        		"101: lfpdx  2,%[index2],%[src]" nl
        		"102: lfpdx  4,%[index4],%[src]" nl
        		"103: lfpdx  6,%[index6],%[src]" nl
        		"104: lfpdx  1,%[index1],%[src]" nl
        		"105: lfpdx  3,%[index3],%[src]" nl
        		"106: lfpdx  5,%[index5],%[src]" nl
        		"107: lfpdx  7,%[index7],%[src]" nl
        		"108: stfpdx 0,0        ,%[dst]" nl
        		"109: lfpdx  0,%[index8],%[src]" nl
        		"110: stfpdx 2,%[index2],%[dst]" nl
        		"111: lfpdx  2,%[indexa],%[src]" nl
        		"112: stfpdx 4,%[index4],%[dst]" nl
        		"113: lfpdx  4,%[indexc],%[src]" nl
        		"114: stfpdx 6,%[index6],%[dst]" nl
        		"115: lfpdx  6,%[indexe],%[src]" nl
        		"bdz 1f" nl

        		"0:" nl
        		"addi %[src],%[src],128" nl

        		"116: stfpdx 1,%[index1],%[dst]" nl
        		"117: lfpdx  1,%[index1],%[src]" nl
        		"118: stfpdx 0,%[index8],%[dst]" nl
        		"119: lfpdx  0,%[index8],%[src]" nl

        		"120: stfpdx 3,%[index3],%[dst]" nl
        		"121: lfpdx  3,%[index3],%[src]" nl
        		"122: stfpdx 2,%[indexa],%[dst]" nl
        		"123: lfpdx  2,%[indexa],%[src]" nl

        		"124: stfpdx 5,%[index5],%[dst]" nl
        		"125: lfpdx  5,%[index5],%[src]" nl
        		"126: stfpdx 4,%[indexc],%[dst]" nl
        		"127: lfpdx  4,%[indexc],%[src]" nl

        		"128: stfpdx 7,%[index7],%[dst]" nl
        		"129: lfpdx  7,%[index7],%[src]" nl
        		"130: stfpdx 6,%[indexe],%[dst]" nl
        		"addi %[dst],%[dst],128" nl
        		"131: lfpdx  6,%[indexe],%[src]" nl

        		"bdnz 0b" nl


        		"1:" nl
        		"addi %[src],%[src],128" nl

        		"132: stfpdx 1,%[index1],%[dst]" nl
        		"133: lfpdx  1,%[index1],%[src]" nl
        		"134: stfpdx 0,%[index8],%[dst]" nl

        		"135: stfpdx 3,%[index3],%[dst]" nl
        		"136: lfpdx  3,%[index3],%[src]" nl
        		"137: stfpdx 2,%[indexa],%[dst]" nl

        		"138: stfpdx 5,%[index5],%[dst]" nl
        		"139: lfpdx  5,%[index5],%[src]" nl
        		"140: stfpdx 4,%[indexc],%[dst]" nl

        		"141: stfpdx 7,%[index7],%[dst]" nl
        		"142: lfpdx  7,%[index7],%[src]" nl
        		"143: stfpdx 6,%[indexe],%[dst]" nl

        		"addi %[dst],%[dst],128" nl
        		"144: stfpdx 1,%[index1],%[dst]" nl
        		"145: stfpdx 3,%[index3],%[dst]" nl
        		"146: stfpdx 5,%[index5],%[dst]" nl
        		"147: stfpdx 7,%[index7],%[dst]" nl
/* Following section needed to handle exceptions (user code passing addresses which SEGV) */
        		"li %[rc],0" nl
        		"b 3f" nl

        		"2:" nl
        		"li %[rc],1" nl
        		"3:" nl
        		".section __ex_table,\"a\"" nl

        		".align	2" nl
        		".long 100b,2b" nl
         		".long 101b,2b" nl
        		".long 102b,2b" nl
        		".long 103b,2b" nl
         		".long 104b,2b" nl
        		".long 105b,2b" nl
        		".long 106b,2b" nl
         		".long 107b,2b" nl
        		".long 108b,2b" nl
        		".long 109b,2b" nl
        		".long 110b,2b" nl
         		".long 111b,2b" nl
        		".long 112b,2b" nl
        		".long 113b,2b" nl
         		".long 114b,2b" nl
        		".long 115b,2b" nl
        		".long 116b,2b" nl
         		".long 117b,2b" nl
        		".long 118b,2b" nl
        		".long 119b,2b" nl
        		".long 120b,2b" nl
         		".long 121b,2b" nl
        		".long 122b,2b" nl
        		".long 123b,2b" nl
         		".long 124b,2b" nl
        		".long 125b,2b" nl
        		".long 126b,2b" nl
         		".long 127b,2b" nl
        		".long 128b,2b" nl
        		".long 129b,2b" nl
        		".long 130b,2b" nl
         		".long 131b,2b" nl
        		".long 132b,2b" nl
        		".long 133b,2b" nl
         		".long 134b,2b" nl
        		".long 135b,2b" nl
        		".long 136b,2b" nl
         		".long 137b,2b" nl
        		".long 138b,2b" nl
        		".long 139b,2b" nl
        		".long 140b,2b" nl
         		".long 141b,2b" nl
        		".long 142b,2b" nl
        		".long 143b,2b" nl
         		".long 144b,2b" nl
        		".long 145b,2b" nl
        		".long 146b,2b" nl
         		".long 147b,2b" nl
        		".text" nl

        		: /* Outputs */
        		  [rc] "=b" (rc)
        		: /* Inputs */
        		  [dst] "b" (to),
        		  [src] "b" (from),
        		  [count] "r" (count),
        		  [index1] "b" (x1),
        		  [index2] "b" (x2),
        		  [index3] "b" (x3),
        		  [index4] "b" (x4),
        		  [index5] "b" (x5),
        		  [index6] "b" (x6),
        		  [index7] "b" (x7),
        		  [index8] "b" (x8),
        		  [indexa] "b" (xa),
        		  [indexc] "b" (xc),
        		  [indexe] "b" (xe)
        		: /* Clobbers */
        		  "memory", "ctr",
        		  "fr0","fr1","fr2","fr3",
        		  "fr4","fr5","fr6","fr7"
        		) ;

  return rc ;
}
/* Block store, using t0 and t1 as temporaries because we need to preserve the complete FPU context */
static void doublehummer_store_quads(void *dest, int count, const double *v0, const double *v1, double *t0, double *t1)
{
        asm  volatile (
            "stfdx 0,0,%[t0]" nl
        		"lfdx  0,0,%[v0]" nl
        		"stfsdx  0,0,%[t1]" nl
            "lfsdx  0,0,%[v1]" nl
            "mtctr %[count]" nl
        		"0: stfpdx 0,0,%[dest]" nl
        		"addi %[dest],%[dest],16" nl
        		"bdnz 0b" nl
            "lfdx 0,0,%[t0]" nl
            "lfsdx  0,0,%[t1]" nl
        		: /* Outputs */
              "=m" (*t0),
              "=m" (*t1)
        		: /* Inputs */
        		  [dest] "b" (dest),
        		  [v0] "b" (v0),
        		  [v1] "b" (v1),
              [t0] "b" (t0),
              [t1] "b" (t1),
        		  [count] "r" (count)
        		: /* Clobbers */
        		  "memory", "ctr"
        		) ;

}

/*  Try a 'doublehummer' memcpy, return 0 if we could and 1 if we couldn't */
static int doublehummer_memcpy(void * dest, const void * src, unsigned long size)
{
	if( k_exploit_doublehummer)
		{
			unsigned int di = (unsigned int) dest ;
			unsigned int si = (unsigned int) src ;
			unsigned int mutual_alignment = (di - si) & k_fpu_align_mask ;
			unsigned int source_alignment = si & k_fpu_align_mask ;
			unsigned int precopy_size = source_alignment ? (k_fpu_alignment - source_alignment) : 0 ;
			unsigned int miniblock_di = di + precopy_size ;
			unsigned int miniblock_si  =si + precopy_size ;
			unsigned int miniblock_size = size - precopy_size ;
			unsigned int miniblock_count=miniblock_size/sizeof(miniblock) ;
			unsigned int size_floor=miniblock_count*sizeof(miniblock) ;
			unsigned int size_tail = size - size_floor - precopy_size ;
			int rc ;
			if( mutual_alignment )
				{
					dma_memcpy_statistic(k_copy_unaligned_rejects) ;
					source_alignment_statistics[source_alignment] += 1 ;
					dest_alignment_statistics[di & k_fpu_align_mask] += 1 ;
					mutual_alignment_statistics[mutual_alignment] += 1 ;
					return 1 ; // Alignment between source and destination not good enough
				}
      /* Using FPU in a FLIH is 'too hard' */
      if(in_irq())
        {
          dma_memcpy_statistic(k_in_irq) ;
          return 1 ;
        }
      /* Using FPU in a SLIH should be OK now we have an atomicity fix to  problem in giveup_fpu */
      if(in_softirq())
        {
          dma_memcpy_statistic(k_in_softirq) ;
          if(k_inhibit_fpu_in_slih ) return 1 ;
        }
			/* The source and dest are mutually aligned. Do we need a 1-15 byte pre-copy to get to quad alignment ? */
			if( precopy_size )
				{
					rc = __real__copy_tofrom_user(dest, src, precopy_size) ;
					if(rc)
						{
							dma_memcpy_statistic(k_precopy_segv_trap) ;
							return 1 ;
						}
/* 					memcpy(dest,src,precopy_size) ; */
				}

			enable_kernel_fp() ;

/*  The copy should work with interrupts enabled, but whenever I tried it there were occasional errors in copying. */
/*  TODO: Diagnose why, fix, and run the copy without disabling. Same for the 'page copy' and 'page clear later */
      if(k_force_mask || ( k_enable_mask && bgp_memcpy_control.mask))
        {
          unsigned long flags ;
          local_irq_save(flags) ;
          rc = doublehummer_copy_unroll((void *)miniblock_di,(void *)miniblock_si,miniblock_count-1) ;
          local_irq_restore(flags) ;
        }
      else
        {
          rc = doublehummer_copy_unroll((void *)miniblock_di,(void *)miniblock_si,miniblock_count-1) ;
        }
			if( rc )
				{
					dma_memcpy_statistic(k_copy_segv_trap) ;
					return 1 ;
				}

			if( size_tail )
				{
					 /*  TODO: Fix up what happens if this causes a 'segv' */
					rc = __real__copy_tofrom_user((void *)(miniblock_di+size_floor), (void *)(miniblock_si+size_floor), size_tail) ;
					if(rc)
						{
							dma_memcpy_statistic(k_postcopy_segv_trap) ;
							return 1 ;
						}
/* 					memcpy((void *)(miniblock_di+size_floor),(void *)(miniblock_si+size_floor),size_tail) ; */
				}
			if( k_verify_doublehummer && bgp_memcpy_control.verify_fpu)
				{
					verify_memcpy(dest,src,size) ;
				}
			return 0 ;
		}
	else
		{
			return 1 ;
		}
}

static unsigned int operate_vcopy(unsigned long address, void * partner_vaddr, unsigned long size)
{
	TRACEN(k_t_detail,"address=0x%08lx partner_vaddr=%p size=0x%08lx",address,partner_vaddr,size) ;
	return doublehummer_memcpy(partner_vaddr,(const void *)address,size) ;
}


static int all_pages_mapped_read(unsigned long address, unsigned long size)
{
	unsigned int start_page=(address >> k_page_shift) ;
	unsigned int end_page=((address+size) >> k_page_shift) ;
	unsigned int page_count = end_page-start_page+1 ;
	unsigned int x ;
	if( is_kernel_addr(address)) return 0 ; // If we have a 'kernel address', assume it's OK
	if( k_inhibit_crosspage_read && page_count > 1 && 0 == bgp_memcpy_control.fpu_handle_pagecrossing_read)
		{
			 /*  TODO: Should be able to handle page-crossings, but have seen kernel traps related to this */
			dma_memcpy_statistic(k_copy_crosspage_limitation_rejects) ;
			return 1 ;
		}
	 /*  Defend against the possibility that the user application has posted an unmapped address */
	for(x=0;x<page_count;x+=1)
		{
			int pageInt ;
			int __user * pageIntP = (int __user *) ((start_page+x) << k_page_shift)  ;
			if( get_user(pageInt,pageIntP) )
				{
					TRACEN(k_t_general,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x",((start_page+x) << k_page_shift),start_page,page_count) ;
					if( k_diag_not_mapped)
					{
						tlb_t t ;
						unsigned int r=v_to_r_maybe((void *)address, &t) ;
						TRACEN(k_t_request,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x",((start_page+x) << k_page_shift),start_page,page_count) ;
						TRACEN(k_t_request,"address=0x%08lx r=0x%08x",address,r) ;
						diagnose_tlb(&t) ;
					}

					return 1;
				}

		}
	return 0 ;
}
static int all_pages_mapped_write(unsigned long address, unsigned long size)
{
	unsigned int start_page=(address >> k_page_shift) ;
	unsigned int end_page=((address+size) >> k_page_shift) ;
	unsigned int page_count = end_page-start_page+1 ;
	unsigned int x ;
/* 	int pageInt ; */
	char __user * pageCharP = (char __user *) address ;
	if( is_kernel_addr(address)) return 0 ; // If we have a 'kernel address', assume it's OK
	if( k_inhibit_crosspage_write && page_count > 1 && 0 == bgp_memcpy_control.fpu_handle_pagecrossing_write )
		{
			 /*  TODO: Should be able to handle page-crossings, but have seen kernel traps related to this */
			dma_memcpy_statistic(k_copy_crosspage_limitation_rejects) ;
			return 1 ;
		}
	if(put_user(0,pageCharP))
		{
			TRACEN(k_t_general,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x",((start_page+x) << k_page_shift),start_page,page_count) ;
			if( k_diag_not_mapped)
			{
				tlb_t t ;
				unsigned int r=v_to_r_maybe((void *)address, &t) ;
				TRACEN(k_t_request,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x",((start_page+x) << k_page_shift),start_page,page_count) ;
				TRACEN(k_t_request,"address=0x%08lx r=0x%08x",address,r) ;
				diagnose_tlb(&t) ;
			}

			return 1;
		}
	 /*  Defend against the possibility that the user application has posted an unmapped address */
	for(x=1;x<page_count;x+=1)
		{
/* 			int pageInt ; */
			char __user * pageCharP = (char __user *) ((start_page+x) << k_page_shift)  ;
/* 			put_user(current_injection_used, report) ; */
			if( put_user(0,pageCharP) )
				{
					TRACEN(k_t_general,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x",((start_page+x) << k_page_shift),start_page,page_count) ;
					if( k_diag_not_mapped)
					{
						tlb_t t ;
						unsigned int r=v_to_r_maybe((void *)address, &t) ;
						TRACEN(k_t_request,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x",((start_page+x) << k_page_shift),start_page,page_count) ;
						TRACEN(k_t_request,"address=0x%08lx r=0x%08x",address,r) ;
						diagnose_tlb(&t) ;
					}

					return 1;
				}

		}
	return 0 ;
}

static int instrument_copy_user_address_by_touch(unsigned long address, unsigned long size,void * partner_vaddr)
{

	if( k_map_read_check && all_pages_mapped_read(address,size))
		{
			dma_memcpy_statistic(k_copy_source_rejects) ;
			return 1 ;
		}
	if( k_map_write_check && all_pages_mapped_write((unsigned int) partner_vaddr,size))
		{
			dma_memcpy_statistic(k_copy_target_rejects) ;
			return 1 ;
		}

	 /*  Looks like we can run the transfer with the FPU */
	return operate_vcopy(address,partner_vaddr,size) ;

}

static int instrument_copy_tofrom_user(unsigned long to, unsigned long from, unsigned long size)
{

	int rc=1 ;
	TRACEN(k_t_fpucopy,"(>)") ;
	 /*  TODO: Check by touching and poking that all pages in 'to' and 'from' are appropriately mapped, before going into the hummer loop */
	rc= instrument_copy_user_address_by_touch(from,size,(void *)to) ;
	TRACEN(k_t_fpucopy,"(<) rc=%d",rc) ;
	return rc ;
}

enum {
	k_enable_dma_memcpy = 1 // TODO: Get DMA memcopy working, and enable it here
};
/* Returns 1 if we could DMA-copy things, 0 if we couldn't */
extern unsigned long bgp_fpu_instrument_copy_tofrom_user(void  *to,
                const void __user *from, unsigned long size)
{
	if( k_premark && bgp_memcpy_control.verify_dma) memset(to,0x11,size) ; // Mark the memory so we know if we write it
// No advantage yet seen by using the DMA unit to do 'memcpy'
//#if defined(CONFIG_BLUEGENE_DMA_MEMCPY)
//	if( k_enable_dma_memcpy && bgp_memcpy_control.use_dma)
//		{
//			if( bgp_memcpy_control.mask)
//				{
//					unsigned long flags ;
//					unsigned long rc ;
//					local_irq_save(flags) ;
//					rc = bgp_dma_instrument_copy_tofrom_user(to, from, size) ;
//					local_irq_restore(flags) ;
//					return rc ;
//				}
//			else
//				{
//					return bgp_dma_instrument_copy_tofrom_user(to, from, size) ;
//				}
//		}
//	else
//#endif
		{
			dma_memcpy_statistic(k_copy_tofrom_user_calls) ;
			if( size > 0 && bgp_memcpy_control.use_fpu && size >= bgp_memcpy_control.fpu_threshold )
				{
						{
							TRACEN(k_t_fpucopy,"to=%p from=%p size=0x%08lx",to,from,size) ;
							{
								unsigned long rc= instrument_copy_tofrom_user((unsigned long)to,(unsigned long)from,size) ;
								dma_memcpy_statistic((0==rc) ? k_copy_accelerate_successes : k_copy_accelerate_rejects) ;

								return rc ;
							}

						}
				}
			dma_memcpy_statistic(k_copy_size_rejects) ;
			return 1 ; // Not copied, size under threshold
		}
}

#if defined(CONFIG_WRAP_COPY_TOFROM_USER)
void copy_page(void  *to, void *from)
{
	TRACEN(k_t_fpucopy,"to=%p from=%p",to,from) ;
	if(bgp_memcpy_control.assist_active )
		{
			unsigned int miniblock_count = k_page_size / sizeof(miniblock) ;
			enable_kernel_fp() ;

			if(k_force_mask || ( k_enable_mask && bgp_memcpy_control.mask))
			  {
		      unsigned long flags ;
          local_irq_save(flags) ;
          doublehummer_copy_unroll((void *)to,(void *)from,miniblock_count-1) ;
          local_irq_restore(flags) ;
			  }
			else
			  {
			    doublehummer_copy_unroll((void *)to,(void *)from,miniblock_count-1) ;
			  }
		}
	else
		{
			memcpy(to,from,k_page_size) ;
		}

}

static const double v=0.0 ;
void clear_pages(void *p, int order)
{
	TRACEN(k_t_fpucopy,"p=%p order=%d",p,order) ;
	if(bgp_memcpy_control.assist_active )
		{
			unsigned int quadcount=(k_page_size/16) << order ;
			double t0, t1 ;
			enable_kernel_fp() ;
/* 			double v=0.0 ; */
      if(k_force_mask || ( k_enable_mask && bgp_memcpy_control.mask))
        {
          unsigned long flags ;
          local_irq_save(flags) ;
          doublehummer_store_quads(p,quadcount,&v,&v, &t0, &t1) ;
          local_irq_restore(flags) ;
        }
      else
        {
          doublehummer_store_quads(p,quadcount,&v,&v, &t0, &t1) ;
        }


		}
	else
		{
			memset(p,0,k_page_size << order)  ;
		}


}
#endif

static void __init
bgp_fpu_register_memcpy_sysctl(void)
{
	register_sysctl_paths(memcpy_ctl_path,bgp_memcpy_table) ;
	TRACEN(k_t_init, "memcpy sysctl registered") ;

}

void __init
bgp_fpu_memcpy_init(void)
  {
    bgp_fpu_register_memcpy_sysctl() ;
  }

module_init(bgp_fpu_memcpy_init);

