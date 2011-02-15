/****************************************************************************/
/* ZEPTOOS:zepto-info */
/*     This file is part of ZeptoOS: The Small Linux for Big Computers.
 *     See www.mcs.anl.gov/zeptoos for more information.
 */
/* ZEPTOOS:zepto-info */
/* */
/* ZEPTOOS:zepto-fillin */
/*     $Id:  $
 *     ZeptoOS_Version: 2.0
 *     ZeptoOS_Heredity: FOSS_ORIG
 *     ZeptoOS_License: GPL
 */
/* ZEPTOOS:zepto-fillin */
/* */
/* ZEPTOOS:zepto-gpl */
/*      Copyright: Argonne National Laboratory, Department of Energy,
 *                 and UChicago Argonne, LLC.  2004, 2005, 2006, 2007, 2008
 *      ZeptoOS License: GPL
 * 
 *      This software is free.  See the file ZeptoOS/misc/license.GPL
 *      for complete details on your rights to copy, modify, and use this
 *      software.
 */
/* ZEPTOOS:zepto-gpl */
/****************************************************************************/
/*
  low-level implementation of zepto big memory
*/

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/lmb.h>

#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include <asm/bootx.h>
#include <asm/machdep.h>
#include <asm/setup.h>
#include <asm/time.h>

#ifdef CONFIG_ZEPTO
#include <asm/zepto_tlb.h>
#endif

#ifdef CONFIG_ZEPTO
extern int zepto_kparam_noU3; /* currently this variable is defined in arch/ppc/mm/init.c*/
#endif

#include "mmu_decl.h"

#define __ZCL_KERNEL__

#include <zepto/zepto_syscall.h>

#define __ZCL__    /* this is to avoid cnk/VirtualMap.h from ppc450_inlines.h */

#include <linux/zepto_task.h>

extern unsigned int tlb_44x_index;  /* defined in arch/powerpc/mm/44x_mmu.c */


#define _bgp_dcache_zero_line_index(v,i) do { asm volatile ("dcbz %1,%0" : : "r" (v), "Ob" (i) : "memory"); } while(0)
#define _bgp_dcache_zero_line(v)         do { asm volatile ("dcbz  0,%0" : : "r" (v) : "memory"); } while(0)
#define _bgp_dcache_flush_line(v)        do { asm volatile ("dcbf  0,%0" : : "r" (v) : "memory"); } while(0)
#define _bgp_dcache_store_line(v)        do { asm volatile ("dcbst 0,%0" : : "r" (v) : "memory"); } while(0)
#define _bgp_dcache_touch_line(v)        do { asm volatile ("dcbt  0,%0" : : "r" (v)           ); } while(0)
#define _bgp_dcache_invalidate_line(v)   do { asm volatile ("dcbi  0,%0" : : "r" (v) : "memory"); } while(0)
#define _bgp_icache_invalidate_line(v)   do { asm volatile ("icbi  0,%0" : : "r" (v) : "memory"); } while(0)
#define _bgp_icache_touch_line(v)        do { asm volatile ("icbt  0,%0" : : "r" (v)           ); } while(0)
#define _bgp_dcache_invalidate_all(void) do { asm volatile ("dccci 0,0"  : : : "memory"); } while(0)
#define _bgp_icache_invalidate_all(void) do { asm volatile ("iccci 0,0"  : : : "memory"); } while(0)
#define _bgp_isync(void)       do { asm volatile ("isync" : : : "memory"); } while(0)
#define _bgp_msync(void)       do { asm volatile ("msync" : : : "memory"); } while(0)



unsigned get_bigmem_size(void)
{
#ifdef CONFIG_ZEPTO_CNS_RELOCATION
    /* preliminary DUAL */
    switch( bigmem_nprocs_per_node  ) {
	case 4:
	    return __bigmem_size/4;  /*__bigmem_size is defined in include/linux/zepto_bigmem.h */
	    break;
	case 2:
	    return __bigmem_size/2;   /* not supported */
	case 1:
	    return __bigmem_size;
	default:
	    panic( "Bigmem: wrong running mode");
	    return 0;
    }
#else
#warning "faking bigmem size since CNS is not relocated"
    return __bigmem_size - 0x10000000U; /* XXXX: ad-hoc solution, which waste (256MB-256KB).
					   fix physical memory allocation code */
#endif
}



/* 
   If zepto task is running (i.e. MPI task is running), bigmem_process_count
   is equal to bigmem_nprocs_per_node.
*/

static atomic_t    bigmem_process_count = ATOMIC_INIT(0);
static unsigned    bigmem_cpucoremask = 0;

void bigmem_process_reset(void)
{
    atomic_set( &bigmem_process_count,0);
    bigmem_cpucoremask = 0;
}

/* 
   return the processor id on success.
   otherwise, -1 is returned. 
*/
int bigmem_process_new(void)
{
    int ret;
    int coreid;

    ret = atomic_inc_return(&bigmem_process_count);
    if( ret > bigmem_nprocs_per_node ) {
	return -1;
    }

    if( bigmem_nprocs_per_node == 2 ) {
	coreid = (ret-1)*2;
    } else {
	coreid = (ret-1);
    }

    bigmem_cpucoremask |= 1<<coreid;

    printk("bigmem_process_new: coreid=%d bigmem=%08x\n", coreid, bigmem_cpucoremask);

    return coreid;
}

/*
  On success, the number of active cores is returned.
  Otherwise, -1 is returned.
*/
int bigmem_process_release(void)
{
    int ret;
    int coreid=0;
    int coreidbit;

    coreid = bigmem_process_cid();

    coreidbit = (1<<coreid);

    if( bigmem_cpucoremask & coreidbit ) {
	ret = atomic_dec_return(&bigmem_process_count);
	bigmem_cpucoremask &= (~coreidbit);
    } else {
	return -1; /* this core is already free'ed */
    }
    printk("bigmem_process_release: ret=%d bigmem_cpucoremask=%08x\n", ret, bigmem_cpucoremask);

    return ret;
}

/*
  return the number of active bigmem processes.
*/
int bigmem_process_active_count(void)
{
    return atomic_read( &bigmem_process_count );
}

/* 
   return 1 if the number of zepto task is equal to bigmem_nprocs_per_node
*/
int bigmem_process_all_active(void)
{
    return (atomic_read(&bigmem_process_count)==bigmem_nprocs_per_node);
}



/* ---------------------------------------------------------------------- */

typedef struct _ppc450tlbentry_ {
  /* see ppc440 user guide (ppc440x6_um_v7_pub_2008.pdf) for definition */
  unsigned w0;
  unsigned w1;
  unsigned w2;
} ppc450tlbentry;

#ifdef CONFIG_ZEPTO_COMPUTENODE
static ppc450tlbentry  bigmem_tlbs[4][BIGMEM_N_TLBS] __attribute__((aligned(16)));
static int             n_bigmem_tlbs[4]={0,0,0,0};
#else
static ppc450tlbentry  bigmem_tlbs[BIGMEM_N_TLBS] __attribute__((aligned(16)));
static int             n_bigmem_tlbs=0;
#endif

ppc450tlbentry  mktlb(unsigned va, unsigned pa_hi, unsigned pa_lo,
		      unsigned tlbsizeflag, unsigned flag)
{
  ppc450tlbentry  ret;

  ret.w0 = (va    & 0xfffff000) | PPC44x_TLB_VALID | tlbsizeflag;
  ret.w1 = (pa_lo & 0xfffff000) | (pa_hi & 0xf);
  ret.w2 = flag;

  return ret;
}

#define _1G    (0x40000000)
#define _256M  (0x10000000)
#define _16M   (0x01000000)



#ifdef CONFIG_ZEPTO_COMPUTENODE

/*
  NOTE: this routine is incomplete since it doesn't check alignment on va.
  it only works with 4 * 256MB region.
 */
static int create_bigmem_vn_tlbs(int coreid,
				 unsigned va_start,
				 unsigned pa_start,
				 unsigned bigmemsize,
				 unsigned tlb_flags )
{
  /* start is 16M aligned at least. size is a multiple of 16M */
  int n_256M_tlbs = 0;
  unsigned pa_addr = pa_start;
  unsigned va_addr = va_start;
  unsigned start_256M_region, end_256M_region;
  int idx=0;

  if( !IS_ALIGNED(va_start,_16M) || !IS_ALIGNED(pa_start,_16M) ) {
    printk(KERN_ERR "Error: start address is not 16M aligned: va:%08x pa:%08x\n",
	   va_start, pa_start);
    return 0;
  }

  if( !IS_ALIGNED(bigmemsize,_16M) ) {
    printk(KERN_ERR "Error: bigmemsize is not 16M aligned: %08x\n",
	   bigmemsize);
    return 0;
  }

  if( IS_ALIGNED(pa_start, _256M) ) {
    start_256M_region = pa_start;
  } else {
    start_256M_region = (pa_start&(~(_256M-1))) + _256M;
  }
  end_256M_region =  (pa_start+bigmemsize)&(~(_256M-1));

  if( end_256M_region-start_256M_region >= _256M ) {
    n_256M_tlbs = (end_256M_region-start_256M_region)/_256M;
  } else {
    n_256M_tlbs=0;
  }

  if( pa_addr < start_256M_region ) {
    unsigned boarder_addr = start_256M_region;
    if( boarder_addr > (pa_start+bigmemsize) )  
      boarder_addr = pa_start+bigmemsize;

    for(;pa_addr<boarder_addr; pa_addr+=_16M, va_addr+=_16M,idx++) {
      bigmem_tlbs[coreid][idx] = 
	mktlb(va_addr,0,pa_addr,PPC44x_TLB_16M,tlb_flags);
    }
  }

  if( pa_addr == start_256M_region  ) {
    for(;pa_addr<end_256M_region; pa_addr+=_256M, va_addr+=_256M,idx++) {
      bigmem_tlbs[coreid][idx] = 
	mktlb(va_addr,0,pa_addr,PPC44x_TLB_256M,tlb_flags);
    }
  }

  if( pa_addr < pa_start+bigmemsize ) {
    for(;pa_addr<pa_start+bigmemsize; pa_addr+=_16M,va_addr+=_16M,idx++) {
      bigmem_tlbs[coreid][idx] = 
	mktlb(va_addr,0,pa_addr,PPC44x_TLB_16M,tlb_flags);
    }
  }
  return idx;
}




/*
  This function fills bigmem_tlbs[cid][], based on bigmem virtual, physical
  start address and bigmem size. 

  return the number of tlbs if succedded, otherwise return a negative
  number
*/
/* CN */
int  create_bigmem_tlbs_CN(int cid,unsigned va_start, unsigned pa_start, 
			unsigned bigmemsize)
{
  int i, n;
  unsigned flags = 
    PPC44x_TLB_SW|PPC44x_TLB_SR|PPC44x_TLB_SX|
    PPC44x_TLB_UW|PPC44x_TLB_UR|PPC44x_TLB_UX|
    PPC44x_TLB_U2|PPC44x_TLB_U3|PPC44x_TLB_WL1|PPC44x_TLB_M;
  unsigned va, pa, size;

  /* check see if bigmem start address and size is aligned 16M */
  if( ! (IS_ALIGNED(va_start,_16M) && IS_ALIGNED(pa_start,_16M) &&
	 IS_ALIGNED(bigmemsize,_16M))  )     {
    return -1;
  }

  /* Disable L2 Optimistic prefetch */
  if( zepto_kparam_noU3 ) flags = flags & (~PPC44x_TLB_U3);

  n_bigmem_tlbs[cid] = 0;

  va = va_start;
  pa = pa_start;

  if( bigmem_nprocs_per_node==4 ) {
      n_bigmem_tlbs[cid] = create_bigmem_vn_tlbs(cid, va, pa, bigmemsize, flags);
  } else if( bigmem_nprocs_per_node==2 ) {
      printk(KERN_ERR "VN mode is not supported\n");
  } else if( bigmem_nprocs_per_node==1 ) {
      if( IS_ALIGNED(va,_1G) && IS_ALIGNED(pa,_1G) &&
	  IS_ALIGNED(bigmemsize,_1G) ) {
    
	  n = bigmemsize / _1G;
	  for(i=0; i<n; i++) {
	      bigmem_tlbs[cid][i] = mktlb(va, 0, pa, PPC44x_TLB_1G, flags);
	      va += _1G;
	      pa += _1G;
	      n_bigmem_tlbs[cid] ++;
	  }
      } else {
	  /* fill 16M tlbs first */
	  size = _16M;
	  n = (bigmemsize/size)%16;
	  for(i=0;i<n;i++) {
	      bigmem_tlbs[cid][n_bigmem_tlbs[cid]] = mktlb(va,0,pa,PPC44x_TLB_16M, flags);
	      va += size;
	      pa += size;
	      bigmemsize -= size;
	      n_bigmem_tlbs[cid]++;
	      if( n_bigmem_tlbs[cid] >= BIGMEM_N_TLBS ) return -2;
	  }

	  /* fill 256M tlbs first */
	  size = _256M;
	  n = (bigmemsize/size)%16;
	  for(i=0;i<n;i++) {
	      bigmem_tlbs[cid][n_bigmem_tlbs[cid]] = mktlb(va,0,pa,PPC44x_TLB_256M, flags);
	      va += size;
	      pa += size;
	      bigmemsize -= size;
	      n_bigmem_tlbs[cid]++;
	      if( n_bigmem_tlbs[cid] >= BIGMEM_N_TLBS ) return -3;
	  }
      }
  } else {
      printk(KERN_ERR "Error: bigmem_nprocs_per_node=%d\n", bigmem_nprocs_per_node);
  }

  return n_bigmem_tlbs[cid];
}

#else  /* create_bigmem_tlbs for ION */

int  create_bigmem_tlbs_ION(unsigned va_start, unsigned pa_end, 
			unsigned bigmemsize)
{
    int i, n, sz;
    unsigned flags = 
	PPC44x_TLB_SW|PPC44x_TLB_SR|PPC44x_TLB_SX|
	PPC44x_TLB_UW|PPC44x_TLB_UR|PPC44x_TLB_UX|
	PPC44x_TLB_U2|PPC44x_TLB_U3|PPC44x_TLB_WL1|PPC44x_TLB_M;
    /*
      PPC44x_TLB_U2:  L1 Store WithOut Allocate
      PPC44x_TLB_U3:  L2 Optimiztic Prefetch ("Automatic" when 0)
      PPC44x_TLB_WL1: Write-Thru L1
    */
    unsigned va, pa, size;

    /* check see if bigmem start address and size is aligned 16M */
    if( ! (IS_ALIGNED(va_start,_16M) && IS_ALIGNED(pa_end,_16M) &&
	   IS_ALIGNED(bigmemsize,_16M))  )     {
	return -1;
    }

    n_bigmem_tlbs = 0;

    va = va_start;
    pa = pa_end;
    sz = bigmemsize;

    if( IS_ALIGNED(va,_1G) && IS_ALIGNED(pa, _1G) ) {
	n = sz / _1G;
	for(i=0; i<n; i++) {
	    pa -= _1G;
	    bigmem_tlbs[i] = mktlb(va, 0, pa, PPC44x_TLB_1G, flags);
	    zepto_debug(2, "installed 1G TLB #%d at va=0x%08x pa=0x%08x\n", i, va, pa);
	    va += _1G;
	    n_bigmem_tlbs ++;
	    sz -= _1G;
	}
    }
    if ( IS_ALIGNED(va, _256M) && IS_ALIGNED(pa, _256M) ) {
	/* fill 256M tlbs first */
        size = _256M;
	n = (sz/size)%16;
	for(i=0;i<n;i++) {
	    pa -= size;
	    bigmem_tlbs[n_bigmem_tlbs] = mktlb(va,0,pa,PPC44x_TLB_256M, flags);
	    zepto_debug(2, "Configured 256M TLB #%d at va=0x%08x pa=0x%08x\n", i, va, pa);
	    va += size;
	    bigmemsize -= size;
	    n_bigmem_tlbs++;
	    sz -= _256M;
	    if( n_bigmem_tlbs >= BIGMEM_N_TLBS ) return -3;
	}
    }
    if ( IS_ALIGNED(va, _16M) && IS_ALIGNED(pa, _16M) ) {
	/* fill 16M tlbs first */
	size = _16M;
	n = (sz/size)%16;
	for(i=0;i<n;i++) {
	    pa -= size;
	    bigmem_tlbs[n_bigmem_tlbs] = mktlb(va,0,pa,PPC44x_TLB_16M, flags);
	    zepto_debug(2, "Configured 16M TLB #%d at va=0x%08x pa=0x%08x\n", i, va, pa);
	    va += size;
	    bigmemsize -= size;
	    n_bigmem_tlbs++;
	    sz -= _16M;
	    if( n_bigmem_tlbs >= BIGMEM_N_TLBS ) return -2;
	}
    }
    return n_bigmem_tlbs;
}

#endif

static void tlbwrite(int slot, ppc450tlbentry e)
{
    __asm__ __volatile__ (
	"tlbwe	%1,%0,0\n"
	"tlbwe	%2,%0,1\n"
	"tlbwe	%3,%0,2\n"
	"isync\n"                 /* to invalidate shadow TLBs */
	:
	: "r" (slot), "r" (e.w0), "r" (e.w1), "r" (e.w2)  );
}

#if 0
void tlbsearch(unsigned look_addr)
{
    unsigned  look_slot=-1;

    __asm__ __volatile__ (
	"tlbsx %0,0,%1\n"
	: "=r"(look_slot)
	: "r"(look_addr) 
	);
    if( look_slot>=0 ) {
	register unsigned w0,w1,w2;
	zepto_debug(2,"tlb matched slot=%d\n", look_slot);
	__asm__
	    __volatile__
	    ( "tlbre   %0,%3,%4 \n"
	      "tlbre   %1,%3,%5 \n"
	      "tlbre   %2,%3,%6 \n"
	      : "=r"(w0), "=r"(w1), "=r"(w2)
	      : "r"(look_slot),
	      /* %4 */  "i"(PPC44x_TLB_PAGEID),  /* ws=0 */
	      /* %5 */  "i"(PPC44x_TLB_XLAT),    /* ws=1 */
	      /* %6 */  "i"(PPC44x_TLB_ATTRIB)   /* ws=2 */
		);
	zepto_debug(2,"tlb matched w0=%08x w1=%08x w2=%08x\n", w0,w1,w2);
    } else {
	zepto_debug(2,"no tlb matched for %08x\n", look_addr);
    }
}
#endif

/*
  Install TLBs for compute node process special address space. 
*/

#define BIGMEM_VA_UNINITIALIZED  (0xffffffff)
#define BIGMEM_PA_UNINITIALIZED  (0xffffffff)

#ifdef CONFIG_ZEPTO_COMPUTENODE
/* bigmem va start (per-core resource)*/ 
static unsigned bigmem_va_start[4] = { BIGMEM_VA_UNINITIALIZED,
				       BIGMEM_VA_UNINITIALIZED,
				       BIGMEM_VA_UNINITIALIZED,
				       BIGMEM_VA_UNINITIALIZED };

/* bigmem pa start (per-core resource). initialized by init_bigmem_pa() */
static unsigned long bigmem_pa_start[4] = { BIGMEM_PA_UNINITIALIZED,
					    BIGMEM_PA_UNINITIALIZED,
					    BIGMEM_PA_UNINITIALIZED,
					    BIGMEM_PA_UNINITIALIZED };
#else 
static unsigned bigmem_va_start = BIGMEM_VA_UNINITIALIZED;
static unsigned bigmem_pa_start = BIGMEM_PA_UNINITIALIZED;
#endif

/* scratchpad addresses (shared resources) */

static unsigned scratchpad_va;
static unsigned scratchpad_pa;
static unsigned scratchpad_len;

/* the following two functions are used for DMA'able region */
unsigned long long get_entire_bigmem_pa_start(void)
{
#ifdef CONFIG_ZEPTO_COMPUTENODE
    return (unsigned long long)bigmem_pa_start[0];
#else
    return (unsigned long long)bigmem_pa_start;
#endif
}

unsigned long long get_entire_bigmem_pa_end(void)
{
#ifdef CONFIG_ZEPTO_CNS_RELOCATION
    if( bgp4GB )  return 0x100000000ULL;
    else          return  0x80000000ULL;
#else
    if( bgp4GB )  return 0x100000000ULL - 0x01000000ULL;
    else          return  0x80000000ULL - 0x01000000ULL;
#endif
}



/* 
   init_bigmem_pa() is called from 
   zeptorc_init() @ arch/powerpc/syslib/bgdd/zepto_setup_treeroute.c

   With CN config, init_bigmem_pa() is called when /proc/setup_treeroute is
   written (by the zoid control process).

   With ION config, init_bigmem_pa() is called only once from zeptorc_init() @
   arch/powerpc/syslib/bgdd/zepto_setup_treeroute.c, which sets
   bigmem_nprocs_per_node 1.
*/
void init_bigmem_pa(void)
{
    unsigned bigmem_entire_pa_start;

    if( bgp4GB )   bigmem_entire_pa_start = 0          - __bigmem_size;
    else           bigmem_entire_pa_start = 0x80000000 - __bigmem_size;

#ifdef CONFIG_ZEPTO_COMPUTENODE
    if(bigmem_nprocs_per_node==4) {
#ifdef CONFIG_ZEPTO_CNS_RELOCATION
	int i;
	/* preliminary VN mode */
	if( __bigmem_size != 0x40000000 ) { 
	    printk(KERN_ERR "VN mode requires 1024MB\n");
	    return;
	}
	for(i=0;i<4;i++)    bigmem_pa_start[i] = (0x40000000/4)*i + bigmem_entire_pa_start;
#else
	printk(KERN_ERR "VN mode is not available. Recompile w/ ZEPTO_CNS_RELOCATION\n");
#endif
    } else if(bigmem_nprocs_per_node==2) {
	printk(KERN_ERR "DUAL mode is not implemented!\n");
	return ;
    } else {
	/* SMP mode */
	bigmem_pa_start[0] = bigmem_pa_start[1] = bigmem_pa_start[2] = bigmem_pa_start[3] = 
	    bigmem_entire_pa_start;
    }
#else
    bigmem_pa_start = bigmem_entire_pa_start;
#endif

    zepto_debug(2,"BIGMEM_TLB_START_SLOT=%d BIGMEM_TLB_END_SLOT=%d BIGMEM_N_TLBS=%d\n",
		BIGMEM_TLB_START_SLOT, BIGMEM_TLB_END_SLOT, BIGMEM_N_TLBS );

#ifdef CONFIG_ZEPTO_COMPUTENODE
    {
	int i;
	for(i=0;i<4;i++) {
	    zepto_debug(2,"bigmem_pa_start[%d]=0x%08lx\n",i, bigmem_pa_start[i]);
	}
    }
#else
    zepto_debug(2,"bigmem_pa_start=0x%08x\n",bigmem_pa_start);
#endif
}

/*
  install_bigmem_tlb() is called from :

    1. do_page_fault() @ arch/powerpc/mm/fault.c 
    2. load_elf_binary() @ fs/binfmt_elf.c

  return 0 if succeeded, otherwise return -1
*/
int install_bigmem_tlb(void)
{
    int i;
    unsigned va, pa;
#ifdef CONFIG_ZEPTO_COMPUTENODE
    int cid = 0;
#endif

#ifdef CONFIG_ZEPTO_COMPUTENODE
    cid = bigmem_process_cid();

    if( bigmem_pa_start[cid] == BIGMEM_PA_UNINITIALIZED )
	return -1;

    if( bigmem_va_start[cid] == BIGMEM_VA_UNINITIALIZED )
        return -1;

    va = bigmem_va_start[cid];
    pa = bigmem_pa_start[cid];

    if( n_bigmem_tlbs[cid] < 1 ) {
	int rc;
	zepto_debug(2, "cid=%d va=%08x pa=%08x size=%08x\n",
		    cid,va,pa,get_bigmem_size() );
	
	rc = create_bigmem_tlbs_CN(cid,va,pa,get_bigmem_size());

	if( rc < 0 ) {
	    printk(KERN_ERR "[Z] create_bigmem_tlbs(cid=%d) failed. rc=%d\n",cid,rc);
	    return -1;
	}
	
	for(i=0; i<n_bigmem_tlbs[cid];i++ ) {
	    zepto_debug(2, "slot=%d cid=%d w0:%08x w1:%08x w2=%08x\n",
			BIGMEM_TLB_START_SLOT + i,
			cid,
			bigmem_tlbs[cid][i].w0, 
			bigmem_tlbs[cid][i].w1, 
			bigmem_tlbs[cid][i].w2 ); 
	}
    }

    _tlbil_all(); /* invalidate normal tlbs and computenode tlbs */
    
    for(i=0; i<n_bigmem_tlbs[cid];i++ )   tlbwrite( BIGMEM_TLB_START_SLOT + i, bigmem_tlbs[cid][i] );

#else
    /* ION */
    if ( bigmem_va_start == BIGMEM_VA_UNINITIALIZED )
	return -1;
    va = bigmem_va_start;
    pa = get_bigmem_pa_end();  /* grow down from pa_end to preserve
				  alignment */

    if( n_bigmem_tlbs < 1 ) {
	int rc;
	rc = create_bigmem_tlbs_ION(va,pa,get_bigmem_size());

	if( rc < 0 ) {
	    printk(KERN_ERR "[Z] create_bigmem_tlbs() failed. rc=%d\n",rc);
	    return -1;
	}
	
	for(i=0; i<n_bigmem_tlbs;i++ ) {
	    zepto_debug(2, "Z: slot=%d  w0:%08x w1:%08x w2=%08x\n",
			BIGMEM_TLB_START_SLOT + i,
			bigmem_tlbs[i].w0, 
			bigmem_tlbs[i].w1, 
			bigmem_tlbs[i].w2 ); 
	}
    }

    _tlbil_all(); /* invalidate normal tlbs and computenode tlbs */
    
    for(i=0; i<n_bigmem_tlbs;i++ )   tlbwrite( BIGMEM_TLB_START_SLOT + i, bigmem_tlbs[i] );

#endif

    /* XXX: this might be a performance issue. do we need all of them? */
    _bgp_dcache_invalidate_all();
    _bgp_icache_invalidate_all();
    _bgp_msync();
    _bgp_isync();

    return 0;
}

void free_bigmem_tlb(void)
{
    int cid = 0;
#ifdef CONFIG_ZEPTO_COMPUTENODE
    extern void force_clear_dma_usage(void);    /* arch/ppc/syslib/bgdd/bluegene_dma.c */
    extern void bgplockbox_reset(void);         /* arch/ppc/syslib/bgdd/zepto_flatmem.c */
#endif

    cid = bigmem_process_cid();

    _tlbil_all();

    _bgp_dcache_invalidate_all();
    _bgp_icache_invalidate_all();
    _bgp_msync();
    _bgp_isync();

#ifdef CONFIG_ZEPTO_COMPUTENODE
    bigmem_va_start[cid] = BIGMEM_VA_UNINITIALIZED;
#else
    bigmem_va_start = BIGMEM_VA_UNINITIALIZED;
#endif

#ifdef CONFIG_ZEPTO_COMPUTENODE
    force_clear_dma_usage();
    bgplockbox_reset();
#endif

    zepto_debug(2,"free_bigmem_tlb() cid=%d\n",cid);
}

int init_bigmem_tlb(unsigned entry)
{
    int cid = 0;

    cid = bigmem_process_cid();

#ifdef CONFIG_ZEPTO_COMPUTENODE
    if( bigmem_va_start[cid] != 0xffffffff ) {
	printk(KERN_ERR "[Z] bigmem is in use. cid=%d\n",cid);
	return -1;
    }

    bigmem_va_start[cid] = entry & 0xf0000000;
#else
    if( bigmem_va_start != 0xffffffff ) {
	printk(KERN_ERR "[Z] bigmem is in use. cid=%d\n",cid);
	return -1;
    }

    bigmem_va_start = entry & 0xf0000000;
#endif


#ifdef CONFIG_ZEPTO_COMPUTENODE
    zepto_debug(2,"init_bigmem_tlb  bigmem_va_start[%d]=0x%08x\n", cid, bigmem_va_start[cid]);
#else
    zepto_debug(2,"init_bigmem_tlb  bigmem_va_start[%d]=0x%08x\n", cid, bigmem_va_start);
#endif
    return 0;
}


void fill_zero_bigmem(void)
{
    int cid = 0;
    unsigned va;

    cid = bigmem_process_cid();

#ifdef CONFIG_ZEPTO_COMPUTENODE
    va = bigmem_va_start[cid];
    if( bigmem_va_start[cid]==0xffffffff ) {
	printk(KERN_ERR "[Z] invalid bigmem_va_start[%d]\n", cid);
	return;
    }
#else
    va = bigmem_va_start;
    if( bigmem_va_start==0xffffffff ) {
	printk(KERN_ERR "[Z] invalid bigmem_va_start[%d]\n", cid);
	return;
    }
#endif
    zepto_debug(2,"fill_zero_bigmem() cid=%d va=%08x\n",cid, va);

    memset((void*)va, 0, get_bigmem_size()); 
    zepto_debug(2,"fill_zero_bigmem() out cid=%d\n", cid);
}

unsigned get_bigmem_region_start(void) 
{ 
#ifdef CONFIG_ZEPTO_COMPUTENODE
    int cid=0;

    cid = bigmem_process_cid();

    return bigmem_va_start[cid];
#else
    return bigmem_va_start;
#endif
}


unsigned get_bigmem_region_end(void)   
{ 
#ifdef CONFIG_ZEPTO_COMPUTENODE    
    int cid=0;

    cid = bigmem_process_cid();

    return bigmem_va_start[cid]+get_bigmem_size();
#else
    return bigmem_va_start+get_bigmem_size();
#endif
}

unsigned get_bigmem_pa_start(void)
{
#ifdef CONFIG_ZEPTO_COMPUTENODE
    int cid=0;

    cid = bigmem_process_cid();

    return bigmem_pa_start[cid];
#else
    return bigmem_pa_start;
#endif
}

unsigned get_bigmem_pa_end(void)
{
#ifdef CONFIG_ZEPTO_COMPUTENODE
    int cid=0;

    cid = bigmem_process_cid();

    /* ZXXX: fix this for VN/Dual */
    return bigmem_pa_start[cid] + get_bigmem_size();
#else
    return bigmem_pa_start + get_bigmem_size();
#endif
}

unsigned  bigmem_virt2phy_cid(unsigned long va,int cid)
{
#ifdef CONFIG_ZEPTO_COMPUTENODE
    return ( va - bigmem_va_start[cid] ) + bigmem_pa_start[cid];
#else
    return ( va - bigmem_va_start ) + bigmem_pa_start;
#endif

}

unsigned  bigmem_virt2phy(unsigned long va)
{
#ifdef CONFIG_ZEPTO_COMPUTENODE
    int cid= bigmem_process_cid();

    return ( va - bigmem_va_start[cid] ) + bigmem_pa_start[cid];
#else
    return ( va - bigmem_va_start ) + bigmem_pa_start;
#endif
}


static int bigmem_n_segs = 1;

/* XXX: find a better place for this function */
asmlinkage  long sys_zepto_generic(unsigned key, unsigned val)
{
    long ret = -EINVAL;

    switch(key) {
        case ZEPTOSC_NULL:
	    ret = 0;
	    break;
	case ZEPTOSC_FLIP:
	    ret = (~val);
	    break;
	case ZEPTOSC_COREID:
	    ret = smp_processor_id();
	    break;
	case ZEPTOSC_ZEPTO_TASK:
	    ret = IS_ZEPTO_TASK(current);
	    break;
	case ZEPTOSC_GETDEC:
	    ret = get_dec(); /* returns a 32-bit value */
	    break;
	default:
	    ret = -EINVAL;
	    break;
    }
    return ret;
}

/*
  XXX: currently only 1 segment
 */
asmlinkage  long sys_zepto_bigmem(unsigned key, unsigned val)
{
    int cid=0;
    long ret = -EINVAL;


    if( !(enable_bigmem&&IS_ZEPTO_TASK(current)) ) return -EINVAL;

    cid = bigmem_process_cid();

    switch(key) {
	case ZEPTOSC_BIGMEM_N_SEGS:
	    ret = bigmem_n_segs;
	    break;
	case ZEPTOSC_BIGMEM_VA_START:
#ifdef CONFIG_ZEPTO_COMPUTENODE
	    ret = bigmem_va_start[cid];
#else
	    ret = bigmem_va_start;
#endif
	    break;
	case ZEPTOSC_BIGMEM_PA_START:
#ifdef CONFIG_ZEPTO_COMPUTENODE
	    ret = bigmem_pa_start[cid];
#else
	    ret = bigmem_pa_start;
#endif
	    break;
	case ZEPTOSC_BIGMEM_LEN:
	    ret = get_bigmem_size();
	    break;
        case ZEPTOSC_SCRATCHPAD_VA_START:
	    ret = scratchpad_va;
	    break;
	case ZEPTOSC_SCRATCHPAD_PA_START:
	    ret = scratchpad_pa;
	    break;
	case ZEPTOSC_SCRATCHPAD_LEN:
	    ret = scratchpad_len;
	    break;
	default:
	    ret = -EINVAL;
	    break;
    }
    return ret;
}


int  in_bigmem(unsigned address)
{
    if( (address >= get_bigmem_region_start() 	&& 
	 address < get_bigmem_region_end() ) ) {
	return 1;
    }
    return 0;
}


void zepto_init_tlbs_for_devices(void)
{
#ifdef CONFIG_ZEPTO_LOCKBOX_UPC_TLB 
    const unsigned f_SRSW   = PPC44x_TLB_SW|PPC44x_TLB_SR;
    const unsigned f_URUW   = PPC44x_TLB_UW|PPC44x_TLB_UR;
    const unsigned f_IALL   = PPC44x_TLB_I|PPC44x_TLB_G|PPC44x_TLB_IL1I|PPC44x_TLB_IL1D|PPC44x_TLB_IL2I|PPC44x_TLB_IL2D;

    ppc450tlbentry tlbe;

    tlbe = mktlb(0xffff0000, 0x7, 0xffff0000, PPC44x_TLB_16K,  f_SRSW|f_IALL );        tlbwrite(0,tlbe);  /* lockbox sup  */
    tlbe = mktlb(0xffff4000, 0x7, 0xffff4000, PPC44x_TLB_16K,  f_SRSW|f_URUW|f_IALL ); tlbwrite(1,tlbe);  /* lockbox user */
    tlbe = mktlb(0xfffda000, 0x7, 0x10000000, PPC44x_TLB_4K,   f_SRSW|f_URUW|f_IALL ); tlbwrite(2,tlbe);  /* UPC */

#ifdef CONFIG_ZEPTO_TREE_TORUS_TLB
    tlbe = mktlb(0xfffdc000, 0x6, 0x10000000, PPC44x_TLB_1K,   f_SRSW|f_URUW|f_IALL);  tlbwrite(3,tlbe);  /* TREE0 (CIO) */
    tlbe = mktlb(0xfffdd000, 0x6, 0x11000000, PPC44x_TLB_1K,   f_SRSW|f_URUW|f_IALL);  tlbwrite(4,tlbe);  /* TREE1 (MPI) */
    tlbe = mktlb(0xfffd0000, 0x6, 0x00000000, PPC44x_TLB_16K,  f_SRSW|f_URUW|f_IALL ); tlbwrite(5,tlbe);  /* DMA */
#endif

#endif // CONFIG_ZEPTO_LOCKBOX_UPC_TLB
    
    tlb_44x_index = REGULAR_TLB_START_SLOT;    /* set to paged TLB start slot. 
						  tlb_44x_index is reset to REGULAR_TLB_START_SLOT when rollover  
						  See InstructionTLBError() and DataTLBError() in head_44x.S */
}

/* ---------------------------------------------------------------------- */

void devicetlbs_init_smp(void* unused)
{
    zepto_init_tlbs_for_devices();
}

int __init zepto_tlb_init(void)
{
    zepto_init_tlbs_for_devices(); /* for core 0 */
    zepto_debug(1,"zepto_tlb_init() at core0\n");

    smp_call_function(devicetlbs_init_smp, NULL, 1); /* for other cores. */
    zepto_debug(1,"zepto_tlb_init() at other cores\n");
    return 0;
}
__initcall(zepto_tlb_init);

