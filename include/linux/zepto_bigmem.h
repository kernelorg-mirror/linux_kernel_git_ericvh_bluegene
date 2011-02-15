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

#ifndef __ZEPTO_BIGMEM_H_DEFINED__
#define __ZEPTO_BIGMEM_H_DEFINED__

/* arch/powerpc/mm/zepto_bigmem.c */

extern int  init_bigmem_tlb(unsigned entry);
extern void fill_zero_bigmem(void);
extern void free_bigmem_tlb(void);
extern int  install_bigmem_tlb(void);
extern int  in_bigmem(unsigned address);
extern void init_bigmem_pa(void);

/* this is for DMA region */
extern unsigned long long get_entire_bigmem_pa_start(void);
extern unsigned long long get_entire_bigmem_pa_end(void); 

/* function for per proc */
extern unsigned get_bigmem_region_start(void); 
extern unsigned get_bigmem_region_end(void);
extern unsigned get_bigmem_size(void);
extern unsigned get_bigmem_pa_start(void);
extern unsigned get_bigmem_pa_end(void);
extern unsigned bigmem_virt2phy(unsigned long va);
extern unsigned bigmem_virt2phy_cid(unsigned long va,int cid);



extern void bigmem_process_reset(void);
extern int  bigmem_process_new(void);
extern int  bigmem_process_release(void);
extern int  bigmem_process_active_count(void);
extern int  bigmem_process_all_active(void);



/* arch/powerpc/syslib/bgdd/zepto_task.c   */
extern int  enable_bigmem;           

/* arch/powerpc/syslib/bgdd/zepto_setup_treeroute.c */
extern int  bigmem_nprocs_per_node;  

static inline int bigmem_process_cid(void) {
    if(bigmem_nprocs_per_node==4) 	
	return smp_processor_id();
    else if(bigmem_nprocs_per_node==2) 	
	return smp_processor_id()&0x2; /* core 0 and 2 will be used */
    else
	return 0;
}

/* defined in arch/powerpc/mm/init_32.c */
extern unsigned long __bigmem_size ;   /* total physical memory for bigmem */

extern int bgp4GB; /* =1 if BGP has 4GB of memory, otherwise we assume BGP memory size is 2GB */

#endif
