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
  BGP lockbox driver for ZCL
*/

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

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>

#include <linux/zepto_bigmem.h>

#include <zepto/zepto_syscall.h>

#include <asm/bgcns.h>

#include <linux/zepto_debug.h>


#define __ZCL_KERNEL__
#define __ZCL__

#include <bpcore/lockbox.h> 
#include <bpcore/bgp_lockbox_inlines.h>

#include <linux/zepto_task.h>


/* static variables */

static void *bgplockbox_supervisor;
static void *bgplockbox_user;

static unsigned long _bgplockbox_array[4][_BGP_LOCKBOX_LOCKS_PER_PAGE/32];
/* static struct   semaphore  _sem_bgplockbox_array; */

/* XXX: this is not good idea but no header define MASKs */
#define LOCKBOX_FLAGS_ORDERED_ALLOC         0x100
#define LOCKBOX_FLAGS_MASTER_PROCESSOR_MASK  0xf0
#define LOCKBOX_FLAGS_PROCESSOR_MASK         0x0f

static atomic_t n_procs_joined1 = ATOMIC_INIT(0);
static atomic_t n_procs_joined2 = ATOMIC_INIT(0);

void internode_barrier(int mastercore, int num_cores)
{
    _bgp_LockBox_Barrier_Group((unsigned)bgplockbox_supervisor,mastercore,num_cores);
}


static uint32_t _bgplockbox_allocate(struct AllocateLockBox_struct *lb)
{
    int i;
    int coreid = smp_processor_id();
    unsigned flags = lb->flags;
    int master_core, num_cores;

    master_core = (flags&LOCKBOX_FLAGS_MASTER_PROCESSOR_MASK)>>4;
    num_cores = (flags&LOCKBOX_FLAGS_PROCESSOR_MASK);

    zepto_debug(3,"core=%d _bgplockbox_allocate() locknum=%d numlocks=%d flags=%08x master_core=%d num_cores=%d\n",
		coreid, lb->locknum, lb->numlocks, flags, master_core, num_cores );
    
    if( lb->locknum + lb->numlocks >= _BGP_LOCKBOX_LOCKS_PER_PAGE ) {
	printk(KERN_WARNING "bgplockbox: locknum %d is invalid\n",
	       lb->locknum );
	return -EINVAL;
    }

    /* check see if desired lockboxes are already taken */
    for( i=lb->locknum; i<lb->locknum + lb->numlocks; i++ ) {
	if( test_bit(i%32,  &(_bgplockbox_array[coreid][i/32]) ) ) {
	    printk(KERN_WARNING "Error: lockbox %d is already in use\n", i);
	    return -EAGAIN;
	}
    }

    /* VN or DUAL mode, we wait all processor to join */
    if( flags & LOCKBOX_FLAGS_ORDERED_ALLOC ) {
#if 0
	zepto_debug(3, "%10u: core=%d waits all %d procs to join. 1st\n", (unsigned)get_tbl(), coreid, bigmem_nprocs_per_node );
	atomic_inc( &n_procs_joined1 );
	/* generally this kind of coding is not good but probably ok for our CN usage */
	while( atomic_read( &n_procs_joined1 ) < bigmem_nprocs_per_node )   ;
	zepto_debug(3, "%10u: all procs joined. 1st\n", (unsigned)get_tbl() );
	atomic_dec( &n_procs_joined1 );
#else
	zepto_debug(3, "%10u: core=%d waits all %d procs to join. 1st\n", (unsigned)get_tbl(), coreid, bigmem_nprocs_per_node );
	_bgp_LockBox_Barrier_Group((unsigned)bgplockbox_supervisor,master_core, num_cores);
	zepto_debug(3, "%10u: all procs joined. 1st\n", (unsigned)get_tbl() );
#endif
    }

    for (i=lb->locknum; i<lb->locknum + lb->numlocks; i++) {
	if( coreid == master_core ) {
	    _bgp_LockBox_Write((uint32_t)bgplockbox_user, i, 0);
	}

	set_bit( i%32,  &(_bgplockbox_array[coreid][i/32]) );  

	lb->lockbox_va[i - lb->locknum] = 
	    (unsigned long)bgplockbox_user + _BGP_LOCKBOX_NUM2ADDR(i);

	zepto_debug(3, "core=%d  lockbox_va=%p\n", coreid, (void*)(lb->lockbox_va[i - lb->locknum]) );
    }

    /* VN or DUAL mode, we wait all processor to join */
    if( flags & LOCKBOX_FLAGS_ORDERED_ALLOC ) {
#if 0
	zepto_debug(3, "%10u: core=%d waits all %d procs to join. 2nd\n", (unsigned)get_tbl(), coreid, bigmem_nprocs_per_node );
	atomic_inc( &n_procs_joined2 );
	/* generally this kind of coding is not good but probably ok for our CN usage */
	while( atomic_read( &n_procs_joined2 ) < bigmem_nprocs_per_node )   ;
	zepto_debug(3, "%10u: all procs joined. 2nd\n", (unsigned)get_tbl() );
	atomic_dec( &n_procs_joined2 );
#else
	zepto_debug(3, "%10u: core=%d waits all %d procs to join. 2nd\n", (unsigned)get_tbl(), coreid, bigmem_nprocs_per_node );
	_bgp_LockBox_Barrier_Group((unsigned)bgplockbox_supervisor,master_core, num_cores);
	zepto_debug(3, "%10u: all procs joined. 2nd\n", (unsigned)get_tbl() );
#endif
    }

    return 0;
}

void bgplockbox_reset(void)
{
    memset( _bgplockbox_array, 0, sizeof(_bgplockbox_array) );
    zepto_debug(4,"Reset lockbox\n");
}

asmlinkage  long sys_zepto_lockbox(unsigned key, unsigned val)
{
    long ret = -EINVAL;

    switch( key ) {
	case ZEPTOSC_LOCKBOX_ALLOCATE:
	    if( !(enable_bigmem&&IS_ZEPTO_TASK(current)) ) return -EINVAL;
	    ret = _bgplockbox_allocate( (struct AllocateLockBox_struct*) val );
	    break;
	case ZEPTOSC_LOCKBOX_RESET:
	    if( !(enable_bigmem&&IS_ZEPTO_TASK(current)) ) return -EINVAL;
	    bgplockbox_reset();
	    break;
	default:
	    ret = -EINVAL;
	    break;
    }
    return ret;
}


int __init bgplockbox_init(void)
{
    bgplockbox_reset();

    /* TLBs are statically mapped for lockbox */
    bgplockbox_supervisor = (void*)0xffff0000;
    bgplockbox_user       = (void*)0xffff4000;

    zepto_debug(4,"bgplockbox_init()\n");

    return 0;
} 
__initcall(bgplockbox_init);
