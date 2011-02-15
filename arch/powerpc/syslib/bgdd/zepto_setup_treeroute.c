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
#include <linux/zepto_debug.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>

#include <asm/bgcns.h>

#include <asm/bgp_personality.h>

#define __ZCL_KERNEL__

#define __ZCL__  /* we need MMIO address definition for linux*/

#include <bpcore/bgp_collective_inlines.h>
#include <common/bgp_personality_inlines.h>
#include <bpcore/ppc450_inlines.h>
#include <bpcore/bgp_global_ints.h>
#include <bpcore/collective.h>
#define  __INLINE__  extern inline
#include <spi/GlobInt.h>

#include <linux/zepto_task.h>

int bigmem_nprocs_per_node = 1;

#ifdef CONFIG_ZEPTO_COMPUTENODE

extern BGCNS_Descriptor bgcnsd;  /* defined in platforms/44x/bgp_cns.c */
extern int bluegene_getPersonality(void *buff, unsigned buffSize); /* defined in platforms/44x/bgp_cns.c */

static void _do_global_barrier(void)
{
    zepto_debug(3,"entering globalBarrier()...\n");
    local_irq_disable();
    bgcnsd.services->globalBarrier();
    local_irq_enable();
    zepto_debug(3,"globalBarrier() out\n");
}

static uint16_t _get_port_masks(int i)
{
    uint16_t masks;
    switch(i) {
	case 0: masks = _BGP_TREE_RDR_SRC0|_BGP_TREE_RDR_TGT0; break;
	case 1: masks = _BGP_TREE_RDR_SRC1|_BGP_TREE_RDR_TGT1; break;
	case 2: masks = _BGP_TREE_RDR_SRC2|_BGP_TREE_RDR_TGT2; break;
	case 3: masks = _BGP_TREE_RDR_SRCL|_BGP_TREE_RDR_TGTL;  /* local node */
    }
    return masks;
}

static void _write_classroute3_to_rdr1(uint16_t  classroute3)
{
    uint32_t  rdr1;
    _bgp_mbar();
    rdr1 = _bgp_mfdcrx(_BGP_DCR_TR_CLASS_RDR1);
    _bgp_mbar();
    _bgp_mtdcrx(_BGP_DCR_TR_CLASS_RDR1, (rdr1 & 0xFFFF0000) | classroute3);
    _bgp_mbar();
}


static uint32_t  _get_target_port(_BGP_TreePayload status)
{
    return status.u32[0];
}
static uint32_t  _get_subtree_status(_BGP_TreePayload status)
{
    return status.u32[1];
}

static _BGP_TreePayload  _set_target_port(_BGP_TreePayload status,uint32_t port)
{
    status.u32[0]=port;
    return status;
}
static _BGP_TreePayload  _set_subtree_status(_BGP_TreePayload status,uint32_t subtree_status)
{
    status.u32[1]=subtree_status;
    return status;
}

static void set_MSR_FP(int val)
{
    unsigned long msr;
    __asm__ __volatile__ ("mfmsr %0" : "=r" (msr));
    if( val ) {
	msr |= MSR_FP;
    }  else  {
	msr &= (~MSR_FP);
    }
    __asm__ __volatile__ ("mtmsr %0" : : "r" (msr));
}

static void setup_treeroute(int active, int job_size)
{
    BGP_Personality_t bgpers;
    uint16_t   classroute3=0;
    uint16_t   port_masks[4];
    unsigned   n_children = 0;
    int child_status = 0;
    int rc, tsize,  n_nodes, i;
    int parent_port = -1;

    set_MSR_FP(1);     /* Enable FPU. XXX: this is a bit brute-force. clean it up later */

    /* enable barrier ch0 for usermode */
    local_irq_disable();
    rc = bgcnsd.services->enableBarrier(0, 1); 
    local_irq_enable();
    zepto_debug(2,"enableBarrier(0,1) rc=%d cpu=%d\n",rc,smp_processor_id() );

    zepto_debug(3,"setup_treeroute(active=%d)\n",active);
    _bgp_mtdcrx(_BGP_DCR_GLOBINT_ASSERT_CH(0),active);
    _do_global_barrier();

    zepto_debug(3,"Set class3 route. cpu=%d\n",smp_processor_id() );

    bluegene_getPersonality(&bgpers, sizeof(bgpers));
    classroute3 = bgpers.Network_Config.TreeRoutes[1];
    zepto_debug(3,"classroute3=%08x\n", classroute3);

    /* calculate the number of nodes */

    switch( bgpers.Kernel_Config.ProcessConfig )     {
	case _BGP_PERS_PROCESSCONFIG_2x2 : tsize = 2; break;
	case _BGP_PERS_PROCESSCONFIG_VNM : tsize = 4; break;
	default:
	    tsize = 1;
    }
    zepto_debug(3,"tsize=%d\n", tsize);

    n_nodes =   
	bgpers.Network_Config.Xnodes * 
	bgpers.Network_Config.Ynodes * 
	bgpers.Network_Config.Znodes * tsize;

    zepto_debug(3,"n_nodes=%d job_size=%d\n", n_nodes, job_size);

    if( n_nodes == job_size ) {  /* fully occupied */
	_write_classroute3_to_rdr1(classroute3);
	zepto_debug(3,"configured as a fully occupied partition.\n");
	return ;
    }

    /* count up the number of children */
    n_children=0;
    for(i=0; i<3; i++) {
	if ( !BGP_Personality_treeInfo_isRedundant(&bgpers,i) ) {
	    if( BGP_Personality_treeInfo_commWorld(&bgpers,i)==_BGP_PERS_TREEINFO_COMMWORLD_CHILD ) {
		n_children++;
	    }
	}
    }
    zepto_debug(3,"n_children=%d\n", n_children);

    /* disable all nodes if not active */
    if( !active )   classroute3 &= (~ _get_port_masks(3));

    zepto_debug(3,"classroute3=%08x\n", classroute3);

    /* wait a msg from children */
    for( i=0; i<n_children; ++i ) {
	_BGP_TreeHwHdr    hdr;
	_BGP_TreePayload  status;

	/* check see if VC0 contains a header and payload */
	//zepto_debug(3,"setup_treeroute.c(%d)\n",__LINE__);

	while(!_bgp_TreeReadyToReceiveVC0() );
	//zepto_debug(3,"setup_treeroute.c(%d)\n",__LINE__);
	_bgp_TreeRawReceivePacketVC0(&hdr, &status);

	zepto_debug(3,"SUBTREE_STATUS=%d TARGET_PORT=%d\n",
		 _get_subtree_status(status), 
		 _get_target_port(status) );

	if( _get_subtree_status(status) ) {
	    child_status = 1;
	} else  {
	    uint32_t target_port = _get_target_port(status);
	    classroute3 &= (~ _get_port_masks(target_port) );

	    zepto_debug(3,"classroute3=%08x target_port=%08x\n", classroute3,target_port);
	}                                  
    }

    //zepto_debug(3,"setup_treeroute.c(%d)\n",__LINE__);
    /* send information to the parent */
    parent_port = -1;
    for(i=0; i< 3; i++) {
	if( BGP_Personality_treeInfo_commWorld(&bgpers,i)==_BGP_PERS_TREEINFO_COMMWORLD_PARENT) {
	    parent_port=i;
	    break;
	}
    }
    zepto_debug(3,"parent_port=%d\n",parent_port);

    if(parent_port>=0) {
	_BGP_TreeHwHdr hdr;
	int ptptarget;
	_BGP_TreePayload status;

	//zepto_debug(3,"setup_treeroute.c(%d)\n",__LINE__);
	ptptarget = BGP_Personality_treeInfo_destP2Paddr(&bgpers,parent_port);
	zepto_debug(3,"ptptarget=%d\n", ptptarget);

	_bgp_TreeMakePtpHdr(&hdr,
			    1,     /* class route? */
			    false, /* interrupt?  */
			    ptptarget, 
			    _BGP_TREE_CSUM_SOME    );


	// zepto_debug(3,"setup_treeroute.c(%d)\n",__LINE__);

	status = _set_target_port(status, 
				  BGP_Personality_treeInfo_destPort(&bgpers, parent_port) );
	// zepto_debug(3,"setup_treeroute.c(%d)\n",__LINE__);

	status = _set_subtree_status(status,
				     (active|child_status));

	// zepto_debug(3,"setup_treeroute.c(%d)\n",__LINE__);

	_bgp_TreeRawSendPacket(0, &hdr, &status);
    }
    // zepto_debug(3,"setup_treeroute.c(%d)\n",__LINE__);

    _write_classroute3_to_rdr1(classroute3);
    _do_global_barrier();
    set_MSR_FP(0);     /* Disable FPU */

    zepto_debug(3,"configured as a partially occupied partition.\n");
}



static int setup_treeroute_write(struct file *file, const char *buffer,
				 unsigned long len, void *data)
{
    char tmp[20];

    if( bigmem_process_active_count() > 0 ) {
	printk("[Z] bigmem is in use, so unable to reset treeroute!\n");
	return 1;
    }

    printk("[Z] Reset treeroute\n");

    bigmem_nprocs_per_node=1;

    if( len > 20 ) len = 20;

    if(copy_from_user(tmp, buffer,len) == 0 ) {
	extern void  bluegene_set_Kernel_Config_ProcessConfig(int nprocs); /* defined in arch/powerpc/platforms/44x/bgp_cns.c */

	int active = 0;
	int job_size;
	char* p;
	if( tmp[0] == (char)'1' )  active = 1;
	p = tmp+2;
	switch( *p ) {
	    case '4': bigmem_nprocs_per_node=4; break;
	    case '2': bigmem_nprocs_per_node=2; break;
	    default:
		bigmem_nprocs_per_node=1;
	}

	bluegene_set_Kernel_Config_ProcessConfig(bigmem_nprocs_per_node);

	p = tmp+4;
	job_size = simple_strtol(p,&p,0);
	setup_treeroute(active,job_size);

	zepto_debug(2, "setup_treeroute: active=%d nprocs=%d job_size=%d\n",active, bigmem_nprocs_per_node, job_size);
    } else {
	return -EFAULT;
    }

    init_bigmem_pa();
    
    return len;
}

static int __init  zeptorc_init(void)
{
    struct proc_dir_entry *p_setup_treeroute;

    bigmem_nprocs_per_node=1; /* SMP is default */
    init_bigmem_pa();

    p_setup_treeroute = create_proc_entry("setup_treeroute", S_IFREG|S_IRUGO|S_IWUGO, NULL );
    if( p_setup_treeroute ) {
	p_setup_treeroute->nlink = 1;
	p_setup_treeroute->write_proc = setup_treeroute_write; 
    } else {
	panic("Failed to register /proc/setup_treeroute\n");
    }
    return 0;
}

#else   /* for ION */

static int __init  zeptorc_init(void)
{
    bigmem_nprocs_per_node=1; /* SMP is default */
    init_bigmem_pa();
    return 0;
}

#endif

__initcall(zeptorc_init);
