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
  BGP dma driver for ZCL
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
#include <linux/zepto_debug.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>

#include <asm/bgcns.h>
#include <asm/bgp_personality.h>

#define __ZCL_KERNEL__

/* #include <bpcore/ppc450_inlines.h> */
/* #include <bpcore/bgp_global_ints.h> */
/* #include <bpcore/collective.h> */
#define  __INLINE__  extern inline
/* #include <spi/GlobInt.h> */

#include <spi/DMA_Counter.h>
#include <spi/DMA_InjFifo.h>
#include <spi/DMA_RecFifo.h>


#include <linux/zepto_task.h>

#include <zepto/zepto_syscall.h>

extern BGCNS_Descriptor bgcnsd;  /* defined in platforms/44x/bgp_cns.c */

BGCNS_ServiceDirectory *bgcns(void) {
    return bgcnsd.services;
}


#define FUNCT    zepto_debug(3,"func: %s()\n",__func__)
#define FUNCB    zepto_debug(3," ret=%d\n",rc)
#define FUNCBP   zepto_debug(3," ret=%p\n",rc)
#define FUNCBNR  zepto_debug(3," ret=none\n")
#define Z        zepto_debug(3,"%s() %s:%d\n",__func__,__FILE__,__LINE__)
#define SCDEBUG  zepto_debug(3,"dmasc %s()\n", __func__)


static const char* str_BGCNS_FifoOperation(BGCNS_FifoOperation op)
{
    switch( op ) 
    {
	case BGCNS_Disable:
	    return "BGCNS_Disable";
	case BGCNS_Enable:
	    return "BGCNS_Enable";
	case BGCNS_Reenable:
	    return "BGCNS_Reenable";
    }
    return "????";
}

static const char* str_BGCNS_FifoFacility(BGCNS_FifoFacility facility)	    
{
    switch(facility) {
	case BGCNS_InjectionFifo:
	    return "BGCNS_InjectionFifo";
	case BGCNS_ReceptionFifo:
	    return "BGCNS_ReceptionFifo";
	case BGCNS_ReceptionHeaderFifo:
	    return "BGCNS_ReceptionHeaderFifo";
	case BGCNS_InjectionFifoInterrupt:
	    return "BGCNS_InjectionFifoInterrupt";
	case BGCNS_ReceptionFifoInterrupt:
	    return "BGCNS_ReceptionFifoInterrupt";
	case BGCNS_ReceptionHeaderFifoInterrupt:
	    return "BGCNS_ReceptionHeaderFifoInterrupt";
	case BGCNS_InjectionCounterInterrupt:
	    return "BGCNS_InjectionCounterInterrupt";
	case BGCNS_ReceptionCounterInterrupt:
	    return "BGCNS_ReceptionCounterInterrupt";
    }
    return "?????";
}
	    

int bgcns_setDmaFifoControls(BGCNS_FifoOperation op, BGCNS_FifoFacility facility, unsigned group, unsigned mask, unsigned* buffer)
{
    int rc;
    FUNCT;
    zepto_debug(3,"  op=%s facility=%s group=%d mask=0x%08x buffer=%p\n",
	    str_BGCNS_FifoOperation(op), 
	    str_BGCNS_FifoFacility(facility), group, mask, buffer );
    local_irq_disable();
    rc = bgcns()->setDmaFifoControls(op,facility,group, mask,buffer);
    local_irq_enable();
    FUNCB;
    return rc;
}



int bgcns_setDmaLocalCopies(BGCNS_FifoOperation operation, unsigned group, unsigned bits)
{
    int rc;
    FUNCT;
    zepto_debug(3, "  operation=%d group=%d bits=0x%08x\n",
	     operation,group,bits);

    local_irq_disable();
    rc = bgcns()->setDmaLocalCopies(operation, group, bits);
    local_irq_enable();

    FUNCB;
    return rc;
}


int bgcns_setDmaPriority(BGCNS_FifoOperation operation, unsigned group, unsigned bits)
{
    int rc;
    FUNCT;
    zepto_debug(3,"  operation=%d group=%d bits=0x%08x\n", operation, group, bits);
    local_irq_disable();
    rc = bgcns()->setDmaPriority(operation, group,  bits);
    local_irq_enable();
    FUNCB;
    return rc;
}

int bgcns_setDmaReceptionMap( BGCNS_ReceptionMap torus_reception_map, unsigned fifo_types[], unsigned header_types[], unsigned threshold[])
{
    int rc;
    int i;

    FUNCT;
    for(i=0; i<BGCNS_NUM_DMA_RECEPTION_GROUPS; i++ ) {
	zepto_debug(3,
	    "  recmap[%2d] %08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x\n",
	    i,
	    torus_reception_map[i][0],
	    torus_reception_map[i][1],
	    torus_reception_map[i][2],
	    torus_reception_map[i][3],
	    torus_reception_map[i][4],
	    torus_reception_map[i][5],
	    torus_reception_map[i][6],
	    torus_reception_map[i][7]   );
    }
    if( fifo_types ) {
	for(i=0; i<DMA_NUM_NORMAL_REC_FIFOS; i++ ) {
	    zepto_debug(3,"  fifo_types[%2d] = %d\n", 
		    i, fifo_types[i]);
	}
    }
    if( header_types ) {
	for(i=0; i<DMA_NUM_HEADER_REC_FIFOS; i++ ) {
	    zepto_debug(3,"  header_types[%2d] = %d\n",
		    i, header_types[i]);
	}
    }
    zepto_debug(3,"  threadhold = %08x:%08x\n",
	    threshold[0], threshold[1]);

    local_irq_disable();
    rc = bgcns()->setDmaReceptionMap(torus_reception_map, fifo_types,  
				   header_types,
				   threshold);
    local_irq_enable();


    FUNCB;
    return rc;
}


int  bgcns_getDmaReceptionMap( BGCNS_ReceptionMap torus_reception_map, unsigned fifo_types[], 
			       unsigned short* store_headers, unsigned header_types[], unsigned threshold[])

{
    int rc;
    int i;
    FUNCT;

    local_irq_disable();
    rc =  bgcns()->getDmaReceptionMap(
	torus_reception_map, fifo_types,
	store_headers,  header_types, threshold);
    local_irq_enable();

    for(i=0; i<BGCNS_NUM_DMA_RECEPTION_GROUPS; i++ ) {
	zepto_debug(3,
	    "  recmap[%2d] %08x:%08x:%08x:%08x:%08x:%08x:%08x:%08x\n",
	    i,
	    torus_reception_map[i][0],
	    torus_reception_map[i][1],
	    torus_reception_map[i][2],
	    torus_reception_map[i][3],
	    torus_reception_map[i][4],
	    torus_reception_map[i][5],
	    torus_reception_map[i][6],
	    torus_reception_map[i][7]   );
    }
    if( fifo_types ) {
	for(i=0; i<DMA_NUM_NORMAL_REC_FIFOS; i++ ) {
	    zepto_debug(3,"  fifo_types[%2d] = %d\n", 
		    i, fifo_types[i]);
	}
    }
    if( header_types ) {
	for(i=0; i<DMA_NUM_HEADER_REC_FIFOS; i++ ) {
	    zepto_debug(3,"  header_types[%2d] = %d\n",
		    i, header_types[i]);
	}
    }
    zepto_debug(3,"  threadhold = %08x:%08x\n",
	    threshold[0], threshold[1]);


    FUNCB;
    return rc;
}

int bgcns_setDmaInjectionMap(unsigned group, unsigned fifoIds[], unsigned char injection_map[], unsigned numberOfFifos)
{
    int rc;
    int i;
    FUNCT;
    zepto_debug(3,"  group=%d numberOfFifos=%d\n", group, numberOfFifos);
    for(i=0;i<numberOfFifos;i++) 
	zepto_debug(3,"  fifoIds[%2d] = %d\n", i, fifoIds[i]);
    for(i=0;i<numberOfFifos;i++) 
	zepto_debug(3,"  injection_map[%2d] = 0x%08x\n", i, injection_map[i]);

    local_irq_disable();
    rc = bgcns()->setDmaInjectionMap(group, fifoIds, 
				     injection_map, numberOfFifos);
    local_irq_enable();
    FUNCB;
    return rc;
}


int bgcns_disableInterrupt(unsigned group, unsigned irq)
{
    int rc;
    FUNCT;
    zepto_debug(3,"  group=%d irq=%d\n", group, irq);

    if( group>=10 || irq>=32 ) {
	return -EINVAL;
    }

    local_irq_disable();
    rc = bgcns()->disableInterrupt(group, irq);
    local_irq_enable();
    FUNCB;
    return rc;
}



int bgcns_dmaSetRange(unsigned numreadranges,  unsigned long long* read_lower_paddr, unsigned long long* read_upper_paddr, unsigned numwriteranges, unsigned long long* write_lower_paddr, unsigned long long* write_upper_paddr)
{
    int rc;
    FUNCT;
    local_irq_disable();
    rc = bgcns()->dmaSetRange(
	numreadranges,  read_lower_paddr, read_upper_paddr, 
	numwriteranges, write_lower_paddr, write_upper_paddr);
    local_irq_enable();
    FUNCB;
    return rc;
}


int bgcns_globalBarrier(void)
{
    int rc;
    FUNCT;
    local_irq_disable();
    rc = bgcns()->globalBarrier();
    local_irq_enable();
    FUNCB;
    return rc;
}

int bgcns_globalBarrierWithTimeout(unsigned timeoutInMillis)
{
    int rc;
    FUNCT;
    local_irq_disable();
    rc =bgcns()->globalBarrierWithTimeout(timeoutInMillis);
    local_irq_enable();
    FUNCB;
    return rc;
}




/* ==================================================
   Misc. functions
   ================================================== */

asmlinkage uint32_t sys_bg_sc_donothing(void) /* this is benchmark purpose */
{
    return 0;
}

asmlinkage uint32_t sys_bg_sc_barrier(unsigned msec)
{
    if( msec==0 ) {
	bgcns_globalBarrier();
    } else {
	bgcns_globalBarrierWithTimeout(msec);
    }
    return 0;
}

asmlinkage uint32_t sys_bg_sc_wildcard(int cmd)
{
    zepto_debug(3,"sys_bg_sc_wildcard()\n");
    switch(cmd) {
	case 0:
	    asm volatile("dccci 0,0");
	    break;
	default:
	    printk("cmd=%d is not implemented\n",cmd);
    }
    return 0;
}



/* ==================================================
   DMA driver impl.
   ================================================== */


/* Keep track DMA usage per node resource */

typedef struct _BGP_DMA_Resouce
{
    uint32_t  inj_ctr_used[4];       /* use bit 0-7  */
    uint32_t  rec_ctr_used[4];       /* use bit 0-7 bits */

    uint32_t  inj_fifo_used[4];      /* use 0-31 bits */

    uint32_t  rec_fifo_set_map;      /* set non-zero if setDmaReceptionMap has been called */
    uint32_t  rec_normal_fifo_init;  /* use bit 0-31 */
    uint32_t  rec_header_fifo_init;  /* use bit 0-3  */

}  BGP_DMA_Usage;

static BGP_DMA_Usage  _bgp_dma_usage;  

void force_clear_dma_usage(void)
{
    memset( &_bgp_dma_usage, 0, sizeof(_bgp_dma_usage) );
    zepto_debug(2, "clear dma usage\n");
}

/* ========================================
   DMA device MMIO map
   ======================================== */
   
static unsigned  bgp_dma_base;

static unsigned get_dma_inj_start(int gn, int fn)            { return bgp_dma_base + gn*0x1000 + fn*0x0010; }
static unsigned get_dma_inj_not_empty(int gn)                { return bgp_dma_base + gn*0x1000 +    0x0200; }
static unsigned get_dma_inj_counter_enabled(int gn, int fn)  { return bgp_dma_base + gn*0x1000 + 0x0300 + fn*0x0004; }
static unsigned get_dma_inj_counter(int gn, int fn)          { return bgp_dma_base + gn*0x1000 + 0x0400 + fn*0x0010; }

static unsigned get_dma_rec_start(int gn, int fn)            { return bgp_dma_base + gn*0x1000 + 0x0800 + fn*0x0010; }
static unsigned get_dma_rec_not_empty(int gn, int fn)        { return bgp_dma_base + gn*0x1000 + 0x0a00 + fn*0x0004; }
static unsigned get_dma_rec_counter_enabled(int gn, int fn ) { return bgp_dma_base + gn*0x1000 + 0x0b00 + fn*0x0004; }
static unsigned get_dma_rec_counter(int gn, int fn)          { return bgp_dma_base + gn*0x1000 + 0x0c00 + fn*0x0010; }



static int valid_dma_vaddr(unsigned vaddr)
{
    return  
	(get_bigmem_region_start()<=vaddr) 
	&& 
	(vaddr<get_bigmem_region_end()) ;
}

static void print_region_info(void) 
{
    zepto_debug(2,"region=[0x%08x,0x%08x)\n",
		get_bigmem_region_start(),
		get_bigmem_region_end() );
}


static int dma_CounterGroupQueryFree( 
    struct CounterGroupQueryFree_struct* commbuf )
{
    uint32_t  type = commbuf->type;
    uint32_t  group = commbuf->group;
    uint32_t  *n_subgroups = &(commbuf->n_subgroups); 
    uint32_t  subgroups[DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP];
    uint32_t  counters;
    int i;

    zepto_debug(2,"dma_CounterGroupQueryFree()\n");


    /* spi wrapper function does parameter validation
       we can skip check */

    // LOCK

    switch(type) {
        case DMA_Type_Injection:
            counters = _bgp_dma_usage.inj_ctr_used[group];
            break;
        case DMA_Type_Reception:
            counters = _bgp_dma_usage.rec_ctr_used[group];
            break;
        default:
            return -EINVAL;
    }

    *n_subgroups = 0;
    for(i=0;i< DMA_NUM_COUNTERS_PER_SUBGROUP; i++) {
        if( (counters & _BN(i)) == 0 ) {
            subgroups[*n_subgroups] = i;
            (*n_subgroups)++;
        }
    }
    /* UNLOCK */
    
    /* commbuf->subgroups points to an user space buffer */
    if( copy_to_user( commbuf->subgroups, subgroups,
		      (*n_subgroups) * sizeof(uint32_t)) ) {
	return -EINVAL;
    }
    
    return 0;
}


static int dma_CounterGroupAllocate( 
    struct   CounterGroupAllocate_struct* commbuf )
{
    uint32_t  type = commbuf->type;
    uint32_t  group = commbuf->group;
    uint32_t  num_subgroups = commbuf->num_subgroups;
    DMA_CounterGroup_t* cg_ptr = 
	(DMA_CounterGroup_t*)commbuf->cg_ptr; /* points to a special buffer,
					       * so no need to
					       * copy_from_user */
    /****/
    int subgroups[DMA_NUM_COUNTERS_PER_SUBGROUP];
    int i,j;
    int min_id,max_id,global_subgroup,word_id,bit_id;
    uint32_t *counters_ptr;
    uint32_t x;
    unsigned counterGroupMask = 0;
    unsigned counterNum;

    SCDEBUG;    
        
    if(!valid_dma_vaddr((unsigned)cg_ptr)) {
	printk("Error!  cg_ptr=%p %s(%d)\n",
	       cg_ptr, 
	       __FILE__, __LINE__);
	print_region_info();

	return -EINVAL;
    }

    switch(type) {
	case DMA_Type_Injection:
	    counters_ptr = &(_bgp_dma_usage.inj_ctr_used[group]);
	    break;
	case DMA_Type_Reception:
	    counters_ptr = &(_bgp_dma_usage.rec_ctr_used[group]);
		break;
	default:
	    return -EINVAL;
    }

    copy_from_user(subgroups, commbuf->subgroups, sizeof(int)*num_subgroups);

    for(i=0;i<num_subgroups;i++) {
	if(subgroups[i] < 0) {
	    return -EINVAL;
	}
	if(subgroups[i] >= DMA_NUM_COUNTERS_PER_SUBGROUP){
	    return -EINVAL;
	}
	if((*counters_ptr) & _BN(subgroups[i])){
	    return -EINVAL;
	}
    }



    memset( (void *)cg_ptr, 0, sizeof(DMA_CounterGroup_t));

    cg_ptr->type = type;
    cg_ptr->group_id = group;
    if(type == DMA_Type_Injection){
	cg_ptr->status_ptr = 
	    (DMA_CounterStatus_t *)get_dma_inj_counter_enabled(group,0);
    } else {
	cg_ptr->status_ptr = 
	    (DMA_CounterStatus_t *)get_dma_rec_counter_enabled(group,0);
    }


    zepto_debug(3,"cg_ptr->status_ptr=%p\n", (void*)cg_ptr->status_ptr);


    for(i=0;i<num_subgroups;i++) {
	*counters_ptr |= _BN(subgroups[i]);
	min_id = subgroups[i]*DMA_NUM_COUNTERS_PER_SUBGROUP;
	max_id = min_id + DMA_NUM_COUNTERS_PER_SUBGROUP;
	global_subgroup = (group * DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP)
	    + subgroups[i];

	cg_ptr->grp_permissions |= _BN(global_subgroup);

	for(j=min_id;j<max_id;j++){
	    word_id = DMA_COUNTER_GROUP_WORD_ID(j);
	    bit_id = DMA_COUNTER_GROUP_WORD_BIT_ID(j);
	    cg_ptr->permissions[word_id] |= _BN(bit_id);
	    
	    if(type == DMA_Type_Injection){
		cg_ptr->counter[j].counter_hw_ptr =
		    (DMA_CounterHw_t *)get_dma_inj_counter(group,j);
		DMA_CounterSetValueBaseHw(cg_ptr->counter[j].counter_hw_ptr,DMA_COUNTER_INIT_VAL,0);
	    }else{
		cg_ptr->counter[j].counter_hw_ptr =
		    (DMA_CounterHw_t *)get_dma_rec_counter(group,j);
		DMA_CounterSetValueBaseMaxHw(cg_ptr->counter[j].counter_hw_ptr,DMA_COUNTER_INIT_VAL,0,0);
	    }
	    DMA_CounterSetDisableById(cg_ptr,j);
	    DMA_CounterClearHitZeroById(cg_ptr,j);
	}
    }

    //

    for(counterNum=0; counterNum < DMA_NUM_COUNTERS_PER_GROUP;
	counterNum += DMA_NUM_COUNTERS_PER_SUBGROUP){
	if(cg_ptr->permissions[DMA_COUNTER_GROUP_WORD_ID(counterNum)] &
	   _BN(DMA_COUNTER_GROUP_WORD_BIT_ID(counterNum))){
	    counterGroupMask |= _BN(counterNum / DMA_NUM_COUNTERS_PER_SUBGROUP);
	}
    }

    switch(type) {
	case DMA_Type_Injection:
	    bgcns_setDmaFifoControls(BGCNS_Enable,
				     BGCNS_InjectionCounterInterrupt,
				     cg_ptr->group_id,
				     counterGroupMask >> ((cg_ptr->group_id) * 8),
				     NULL);
	    break;
	case DMA_Type_Reception:
	    bgcns_setDmaFifoControls(BGCNS_Enable,
					BGCNS_ReceptionCounterInterrupt,
					cg_ptr->group_id,
					counterGroupMask >> ((cg_ptr->group_id) * 8),
					NULL);
    }

    _bgp_mbar();


    x = DMA_CounterGetHitZero(cg_ptr,0);
    if(x != 0) {
	printk("[DMA_Counter_Alloc] Hit Zero Error x = 0x%08x\n",x);
	return -EFAULT;
    }
    _bgp_msync();
    _bgp_isync();


    zepto_debug(3,"cg_ptr: status_ptr=%p  counter[0].counter_hw_ptr=%p\n",
		cg_ptr->status_ptr, cg_ptr->counter[0].counter_hw_ptr);


    return 0;
}


static int dma_InjFifoGroupQueryFree( 
    struct   InjFifoGroupQueryFree_struct* commbuf )
{
    uint32_t group = commbuf->group;
    /****/
    int i;
    int num_fifos;
    int fifo_ids[DMA_NUM_INJ_FIFOS_PER_GROUP];
    uint32_t fifos;

    SCDEBUG;    

    fifos = _bgp_dma_usage.inj_fifo_used[group];
	
    num_fifos = 0;
    for(i=0;i< DMA_NUM_INJ_FIFOS_PER_GROUP; i++) {
	if((fifos & _BN(i)) == 0) {
	    fifo_ids[num_fifos] = i;
	    num_fifos++;
	}
    }

    /* return */
    commbuf->num_fifos = num_fifos;
    copy_to_user(commbuf->fifo_ids,fifo_ids,
		 DMA_NUM_INJ_FIFOS_PER_GROUP*sizeof(int));
    


    return 0;
}

static int dma_InjFifoGroupAllocate( 
    struct   InjFifoGroupAllocate_struct* commbuf )
{
    uint32_t group = commbuf->group;
    uint32_t num_fifos = commbuf->num_fifos;
    DMA_InjFifoGroup_t* fg_ptr  = (DMA_InjFifoGroup_t*)commbuf->fg_ptr;
    /*****/
    int i;
    uint32_t       value, f_ids, pri_bits = 0, local_bits = 0;
    unsigned short priorities[DMA_NUM_INJ_FIFOS_PER_GROUP];
    int            fifo_ids[DMA_NUM_INJ_FIFOS_PER_GROUP];
    char           ts_inj_maps[DMA_NUM_INJ_FIFOS_PER_GROUP];
    short          locals[DMA_NUM_INJ_FIFOS_PER_GROUP];

    SCDEBUG;    


    /* Parameter copy from user space to kernel */
    copy_from_user(priorities,  commbuf->priorities,  sizeof(short)*num_fifos);
    copy_from_user(fifo_ids,    commbuf->fifo_ids,    sizeof(int)*num_fifos);
    copy_from_user(ts_inj_maps, commbuf->ts_inj_maps, sizeof(char)*num_fifos);
    copy_from_user(locals,      commbuf->locals,      sizeof(short)*num_fifos);

    f_ids = 0;

    for(i=0;i<num_fifos;i++) {
	if(fifo_ids[i] >= DMA_NUM_INJ_FIFOS_PER_GROUP){
	    return -EINVAL;
	}
	f_ids |= _BN(fifo_ids[i]);
	if(priorities[i] > 1) {
	    return -EINVAL;
	}
	if(locals[i] > 1) {
	    return -EINVAL;
	}
	if(locals[i] == 0 && ts_inj_maps[i] == 0) {
	    return -EINVAL;
	}
	if(locals[i] == 1 && ts_inj_maps[i] != 0) {
	    return -EINVAL;
	}
	if(locals[i] == 1){
	    local_bits |= _BN(i);
	}
	if(priorities[i] == 1) {
	    pri_bits |= _BN(i);
	}
	if(_bgp_dma_usage.inj_fifo_used[group] & _BN(fifo_ids[i])) {
	    return -EBUSY;
	}
    }

    fg_ptr->status_ptr = (DMA_InjFifoStatus_t *)get_dma_inj_not_empty(group);
    fg_ptr->group_id   = group;


    zepto_debug(3,"fg_ptr->status_ptr=%p\n", (void*)fg_ptr->status_ptr);


    _bgp_dma_usage.inj_fifo_used[group] |= f_ids;

    fg_ptr->permissions = f_ids;

    bgcns_setDmaFifoControls(BGCNS_Disable,BGCNS_InjectionFifoInterrupt,group,f_ids,NULL);
    bgcns_setDmaFifoControls(BGCNS_Disable,BGCNS_InjectionFifo,group,f_ids,NULL);

    DMA_InjFifoSetDeactivate(fg_ptr,f_ids);

    _bgp_mbar();

    bgcns_setDmaInjectionMap(group,(unsigned *)fifo_ids,ts_inj_maps,num_fifos);
    
    for(i=0;i<num_fifos;i++) {

	fg_ptr->fifos[fifo_ids[i]].dma_fifo.fifo_hw_ptr = 
	    (DMA_FifoHW_t *)get_dma_inj_start(group,fifo_ids[i]);
	fg_ptr->fifos[fifo_ids[i]].fifo_id = fifo_ids[i];
	fg_ptr->fifos[fifo_ids[i]].desc_count = 0;
	fg_ptr->fifos[fifo_ids[i]].occupiedSize = 0;
	fg_ptr->fifos[fifo_ids[i]].priority = priorities[i];
	fg_ptr->fifos[fifo_ids[i]].local = locals[i];
	fg_ptr->fifos[fifo_ids[i]].ts_inj_map = ts_inj_maps[i];

	DMA_FifoSetStartPa( fg_ptr->fifos[fifo_ids[i]].dma_fifo.fifo_hw_ptr,0 );
	DMA_FifoSetHeadPa(  fg_ptr->fifos[fifo_ids[i]].dma_fifo.fifo_hw_ptr,0 );
	DMA_FifoSetTailPa(  fg_ptr->fifos[fifo_ids[i]].dma_fifo.fifo_hw_ptr,0 );
	DMA_FifoSetEndPa(   fg_ptr->fifos[fifo_ids[i]].dma_fifo.fifo_hw_ptr,0 );
    }
    _bgp_mbar();


    zepto_debug(3,"fg_ptr->fifos[fifo_ids[0]].dma_fifo.fifo_hw_ptr=%p\n",
		fg_ptr->fifos[fifo_ids[0]].dma_fifo.fifo_hw_ptr);


    DMA_InjFifoSetClearThresholdCrossed(fg_ptr,f_ids);

    bgcns_setDmaLocalCopies(BGCNS_Enable, group, local_bits);

    bgcns_setDmaPriority(BGCNS_Enable,group,pri_bits);

    _bgp_mbar();

    zepto_debug(3,"fg_ptr->fifos[fifo_ids[0]].dma_fifo.fifo_hw_ptr=%p\n",
		fg_ptr->fifos[fifo_ids[0]].dma_fifo.fifo_hw_ptr );

    value = DMA_FifoGetStartPa(fg_ptr->fifos[fifo_ids[0]].dma_fifo.fifo_hw_ptr);
    if(value != 0) {

	return -EFAULT;
    }

    bgcns_setDmaFifoControls(BGCNS_Enable, BGCNS_InjectionFifoInterrupt, 
				group, f_ids, NULL );


    return 0;
}



static int _bgp_DMA_FifoInit(
    DMA_Fifo_t *f_ptr,
    void *va_start,
    void *va_head,
    void *va_end)
{
    phys_addr_t pa_start, pa_head, pa_end;

    if( !valid_dma_vaddr((unsigned)va_start) ) {
	printk( KERN_WARNING "va_start %p is invalid\n",va_start);
	print_region_info();
    }
    if( !valid_dma_vaddr((unsigned)va_end) ) {
	printk( KERN_WARNING "va_end %p is invalid\n",va_end);
	print_region_info();
    }
    if( !valid_dma_vaddr((unsigned)va_head) ) {
	printk( KERN_WARNING "va_head %p is invalid\n",va_head);
	print_region_info();
    }
    /*
    pa_start = iopa((unsigned long)va_start);
    pa_end   = iopa((unsigned long)va_end);
    pa_head  = iopa((unsigned long)va_head);
    */
    pa_start = (phys_addr_t)bigmem_virt2phy((unsigned)va_start);
    pa_end   = (phys_addr_t)bigmem_virt2phy((unsigned)va_end);
    pa_head  = (phys_addr_t)bigmem_virt2phy((unsigned)va_head);



    zepto_debug(3,"va_start=%p pa_start=0x%08llx\n",  va_start, pa_start);


    /* fifo_hw_ptr->pa_* is 4-Bit Shifted phys address.*/
    f_ptr->fifo_hw_ptr->pa_start = (unsigned long)(pa_start >> 4);
    f_ptr->fifo_hw_ptr->pa_head  = (unsigned long)(pa_head >> 4);
    f_ptr->fifo_hw_ptr->pa_tail  = (unsigned long)(pa_head >> 4);
    f_ptr->fifo_hw_ptr->pa_end   = (unsigned long)(pa_end >> 4);
    
    _bgp_mbar();

    /* shadow variables */
    f_ptr->pa_start = (pa_start >> 4);

    f_ptr->va_start = va_start;
    f_ptr->va_end   = va_end;
    f_ptr->va_head  = va_head;
    f_ptr->va_tail  = va_head;


    zepto_debug(3,"va = %p, pa = %llx\n",va_start,pa_start);

	
    f_ptr->fifo_size = (pa_end - pa_start) >> 4;
    f_ptr->free_space = f_ptr->fifo_size;

    return 0;
}


static int dma_InjFifoInitByID( 
    struct   InjFifoInitByID_struct* commbuf )
{
    DMA_InjFifoGroup_t *fg_ptr = (DMA_InjFifoGroup_t *)commbuf->fg_ptr;
    int  fifo_id = commbuf->fifo_id;
    uint32_t* va_start = commbuf->va_start;
    uint32_t* va_head = commbuf->va_head;
    uint32_t* va_end = commbuf->va_end;
    /***/
    int group;
    int ret;
    void *x;

    SCDEBUG;    

    group = fg_ptr->group_id;

    bgcns_setDmaFifoControls(BGCNS_Disable,BGCNS_InjectionFifo,
				group,_BN(fifo_id),NULL);
    bgcns_setDmaFifoControls(BGCNS_Disable,BGCNS_InjectionFifoInterrupt,
				group,_BN(fifo_id),NULL);

    DMA_InjFifoSetDeactivate(fg_ptr, _BN(fifo_id));

    ret = _bgp_DMA_FifoInit(&(fg_ptr->fifos[fifo_id].dma_fifo),
			    va_start, va_head, va_end);
    if(ret != 0) {
	return ret;
    }
    fg_ptr->fifos[fifo_id].desc_count = 0;
    fg_ptr->fifos[fifo_id].occupiedSize = 0;

    DMA_InjFifoSetClearThresholdCrossedById(fg_ptr,fifo_id);

    x = DMA_FifoGetHead(&(fg_ptr->fifos[fifo_id].dma_fifo) );
    if( x != fg_ptr->fifos[fifo_id].dma_fifo.va_tail) {
	printk( "[Z] Error @ %s(%d)\n", __FILE__, __LINE__);
	printk( "[Z] x=%p tail=%p fifo_id=%d\n",
		x, fg_ptr->fifos[fifo_id].dma_fifo.va_tail, fifo_id);
	return 0x03; // = _bgp_err_dma_sram_init
    }
    
    bgcns_setDmaFifoControls(BGCNS_Enable,BGCNS_InjectionFifo, 
				group,_BN(fifo_id),NULL);
    bgcns_setDmaFifoControls(BGCNS_Enable,BGCNS_InjectionFifoInterrupt,
				group,_BN(fifo_id),NULL);

    /* Activate the fifo */
    DMA_InjFifoSetActivate(fg_ptr, _BN(fifo_id));

    return 0;
}


static int dma_RecFifoSetMap(uint32_t __user *rec_map)
{
    DMA_RecFifoMap_t map;
    int i,g;

    SCDEBUG;    
   
    if(copy_from_user(&map,rec_map,sizeof(DMA_RecFifoMap_t)) != 0) {
	return -EINVAL;
    }

    for(i=0;i<DMA_NUM_NORMAL_REC_FIFOS;i++) {
	if(map.fifo_types[i] < 0 || map.fifo_types[i] > 1) {
	    return -EINVAL;
	}
    }

    if(map.save_headers > 1) {
	return -EINVAL;
    }

    if(_bgp_dma_usage.rec_fifo_set_map != 0) {
	/* called twice */
	return -EFAULT;
    }

    if(map.save_headers == 1){
	for(i=0; i<DMA_NUM_HEADER_REC_FIFOS; i++) {
	    if(map.hdr_fifo_types[i] < 0 || map.hdr_fifo_types[i] > 1) {
		return -EINVAL;
	    }
	}
    }

    for(g=0;g<DMA_NUM_REC_FIFO_GROUPS;g++) {
	for(i=0;i<DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP;i++) {
	    if(map.ts_rec_map[g][i] >= DMA_NUM_NORMAL_REC_FIFOS) {
		return -EINVAL;
	    }
	}
    }

    
    bgcns_setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionFifo,        0,                   0xFFFFFFFF,NULL);


    bgcns_setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionHeaderFifo, BGCNS_DMA_ALL_GROUPS, 0 /* mask not used */, NULL );


    bgcns_setDmaReceptionMap(map.ts_rec_map, map.fifo_types,
				map.save_headers ? map.hdr_fifo_types : NULL,
				map.threshold );


    _bgp_dma_usage.rec_fifo_set_map = 1;


    return 0;
}

static int _bgp_DMA_RecFifoGetMap(DMA_RecFifoMap_t *rec_map)
{
    if(rec_map == NULL){
	return -EINVAL;
    }
    memset(rec_map,0,sizeof(DMA_RecFifoMap_t));

    return bgcns_getDmaReceptionMap(rec_map->ts_rec_map,rec_map->fifo_types,&(rec_map->save_headers),rec_map->hdr_fifo_types,rec_map->threshold);
}

static int dma_RecFifoGetFifoGroup( struct RecFifoGetFifoGroup_struct* commbuf)
{
    DMA_RecFifoGroup_t *fg_ptr = (DMA_RecFifoGroup_t *)commbuf->fg_ptr;
    int group = commbuf->group;
    // int target = commbuf->target;
    /***/
    DMA_RecFifoMap_t rec_map;
    int min_id,max_id,g,i,j,idx;
    uint32_t used_fifos = 0,x;
    unsigned long fifoMask;
    int fifoIndex;

    SCDEBUG;

    _bgp_DMA_RecFifoGetMap(&rec_map);

    fg_ptr->group_id = group;
    switch(group) {
	case 0:  fg_ptr->mask  = 0xFF000000; break;
	case 1:  fg_ptr->mask  = 0x00FF0000; break;
	case 2:  fg_ptr->mask  = 0x0000FF00; break;
	default: fg_ptr->mask  = 0x000000FF; break;
    }
    fg_ptr->status_ptr = (DMA_RecFifoStatus_t *)get_dma_rec_not_empty(group,0);

    min_id = (group*DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP);
    max_id =  min_id + DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP-1;

    for(g=0;g<DMA_NUM_REC_FIFO_GROUPS;g++) {
	for(i=0;i<DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP;i++) {
	    if(rec_map.ts_rec_map[g][i] >= min_id &&
	       rec_map.ts_rec_map[g][i] <= max_id) {
		used_fifos |= _BN(rec_map.ts_rec_map[g][i]);
	    }
	}
    }

    idx = 0;
    for(j=0;j<DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP;j++) {
	i = min_id + j;
	if((_BN(i) & used_fifos) != 0){
	    fg_ptr->fifos[idx].type = rec_map.fifo_types[j];
	    fg_ptr->fifos[idx].global_fifo_id = i;
	    fg_ptr->fifos[idx].num_packets_processed_since_moving_fifo_head = 0;
	    fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr = 
		(DMA_FifoHW_t *)get_dma_rec_start(group,j);
	    
	    DMA_FifoSetStartPa(fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr,0);
	    DMA_FifoSetHeadPa( fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr,0);
	    DMA_FifoSetTailPa( fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr,0);
	    DMA_FifoSetEndPa(  fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr,0);
	    idx++;
	}
    }

    if(rec_map.save_headers == 1){
	fg_ptr->num_hdr_fifos = 1;
	fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].type = rec_map.hdr_fifo_types[group];
	fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].global_fifo_id = DMA_NUM_NORMAL_REC_FIFOS+group;
	fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].num_packets_processed_since_moving_fifo_head = 0;
	fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr =
	    ( DMA_FifoHW_t *) get_dma_rec_start(group, DMA_HEADER_REC_FIFO_ID);
	
	DMA_FifoSetStartPa( fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr,0);
	DMA_FifoSetHeadPa(  fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr,0);
	DMA_FifoSetTailPa(  fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr,0);
	DMA_FifoSetEndPa(   fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr,0);
    }

    fg_ptr->num_normal_fifos = idx;
    fg_ptr->status_ptr->clear_threshold_crossed[0] = fg_ptr->mask;
    fg_ptr->status_ptr->clear_threshold_crossed[1] = fg_ptr->mask;


    _bgp_mbar();

    x = (fg_ptr->status_ptr->threshold_crossed[0] & fg_ptr->mask);
    if(x != 0) {
	printk(KERN_WARNING "Error: _bgp_err_dma_sram_init  %s(%d)\n", __FILE__,__LINE__ );
	return -EFAULT;
    }

    fifoMask = 0;

    for(fifoIndex=0;fifoIndex< fg_ptr->num_normal_fifos;fifoIndex++) {
	fifoMask |= _BN(fg_ptr->fifos[fifoIndex].global_fifo_id);
    }

    bgcns_setDmaFifoControls(BGCNS_Enable,
				BGCNS_ReceptionFifoInterrupt,
				fg_ptr->group_id,
				fifoMask,NULL);
    _bgp_msync();
    _bgp_isync();

    return 0;
}

/* ============== */

static int dma_RecFifoInitByID( struct RecFifoInitByID_struct* commbuf)
{
    DMA_RecFifoGroup_t *fg_ptr = (DMA_RecFifoGroup_t *)commbuf->fg_ptr;
    int                fifo_id = commbuf->fifo_id;
    void               *va_start = commbuf->va_start;
    void               *va_head = commbuf->va_head;
    void               *va_end  = commbuf->va_end;
    /****/
    int group;
    int g_fifo_id;
    int i;
    uint32_t xint[4] = {0,0,0,0};
    void *x;

    SCDEBUG;


    group = fg_ptr->group_id;
    g_fifo_id = fg_ptr->fifos[fifo_id].global_fifo_id;
    
    
    if(g_fifo_id < DMA_NUM_NORMAL_REC_FIFOS) {
	if((_bgp_dma_usage.rec_normal_fifo_init & _BN(g_fifo_id)) != 0){
	    printk( KERN_WARNING "Error: %s(%d)\n", __FILE__,__LINE__);
	    return -EFAULT;
	}
	_bgp_dma_usage.rec_normal_fifo_init |= _BN(g_fifo_id);
	bgcns_setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionFifo,0,_BN(g_fifo_id),NULL);
	for(i=0;i<4;i++) {
	    bgcns_setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionFifoInterrupt,i,0xffffffff,&(xint[i]));
	}
    } else {
	if((_bgp_dma_usage.rec_header_fifo_init & _BN(g_fifo_id - 32)) != 0) {
	    printk( KERN_WARNING "Error: %s(%d)\n", __FILE__,__LINE__);
	    return -EFAULT;
	}

	// remember the reception header FIFO has been initialized
	_bgp_dma_usage.rec_header_fifo_init |= _BN(g_fifo_id-32);

	bgcns_setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionHeaderFifo,group,0,NULL);
	bgcns_setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionHeaderFifoInterrupt,0,0xffffffff,xint);
    }

    _bgp_DMA_FifoInit( &(fg_ptr->fifos[fifo_id].dma_fifo),
		       va_start,va_head,va_end);

    DMA_RecFifoSetClearThresholdCrossedById(fg_ptr,fifo_id);

    /* DMA_FifoGetHead */
    x =  DMA_FifoGetHead(&(fg_ptr->fifos[fifo_id].dma_fifo) );
    if ( x != fg_ptr->fifos[fifo_id].dma_fifo.va_tail) {
	printk( KERN_WARNING "Error: %s(%d)\n", __FILE__,__LINE__);
	return -EFAULT;
    }

    if(g_fifo_id < DMA_NUM_NORMAL_REC_FIFOS) {
	bgcns_setDmaFifoControls(BGCNS_Enable, BGCNS_ReceptionFifo, 0, _BN(g_fifo_id), NULL);
	for(i=0;i<4;i++) {
	    bgcns_setDmaFifoControls(BGCNS_Reenable, BGCNS_ReceptionFifoInterrupt,i,0,&(xint[i]));
	}
    } else {
	bgcns_setDmaFifoControls(BGCNS_Enable,   BGCNS_ReceptionHeaderFifo,         group,0,NULL);
	bgcns_setDmaFifoControls(BGCNS_Reenable, BGCNS_ReceptionHeaderFifoInterrupt,0,  0,xint);
    }

    return 0;
}


int __init bgpdma_device_init(void)
{
    unsigned long long dma_phy_lower, dma_phy_upper;
    int rc;
    extern int bgp4GB; /* defined in arch/powerpc/mm/init_32.c */
    extern char _end[];  /* ELF symbol */

    bgp_dma_base = 0xFFFD0000;  /* XXX: fix hard-coded */

    zepto_debug(2,"bgp_dma_base=%08x\n", bgp_dma_base);

    dma_phy_lower = (unsigned long long)((unsigned long)_end-PAGE_OFFSET);

#ifdef CONFIG_ZEPTO_CNS_RELOCATION
    if( bgp4GB )  dma_phy_upper = 0x100000000ULL;
    else  	  dma_phy_upper = 0x80000000ULL;
#else
    if( bgp4GB )  dma_phy_upper = 0x100000000ULL - 0x01000000ULL ;
    else  	  dma_phy_upper = 0x80000000ULL - 0x01000000ULL;
#endif

    
    zepto_debug(2,"dma_phy_lower=%08llx dma_phy_upper=%08llx\n", 
		dma_phy_lower, dma_phy_upper );

    rc = bgcns_dmaSetRange(1, &dma_phy_lower, &dma_phy_upper,
			   1, &dma_phy_lower, &dma_phy_upper);

    if( rc!=0 ) {
	panic("ERROR: Failed to dmaSetRange()  dma_phy_lower=0x%08llx dma_phy_upper=0x%08llx\n",
	      dma_phy_lower, dma_phy_upper);
    }

    return 0;
}


asmlinkage  long sys_zepto_dma(unsigned cmd, unsigned arg)
{
    int rc=0;

    /* arg contains the address of bgpdma communication buffer which is
     * allocated from statictlb area.
     */

    zepto_debug(3,"sys_zepto_dma cmd=%08x arg=%08x\n",cmd, arg);

    switch(cmd) {
	case ZEPTOSC_DMA_COUNTERGROUPQUERYFREE:
	    rc = dma_CounterGroupQueryFree( (struct CounterGroupQueryFree_struct*)arg );
	    break;
	case ZEPTOSC_DMA_COUNTERGROUPALLOCATE:
	    rc = dma_CounterGroupAllocate( (struct CounterGroupAllocate_struct*)arg );
	    break;
	case ZEPTOSC_DMA_INJFIFOGROUPQUERYFREE:
	    rc = dma_InjFifoGroupQueryFree( (struct InjFifoGroupQueryFree_struct*)arg );
	    break;
	case ZEPTOSC_DMA_INJFIFOGROUPALLOCATE:
	    rc = dma_InjFifoGroupAllocate( (struct InjFifoGroupAllocate_struct*)arg );
	    break;
	case ZEPTOSC_DMA_INJFIFOINITBYID:
	    rc = dma_InjFifoInitByID( (struct InjFifoInitByID_struct*)arg );
	    break;
	case ZEPTOSC_DMA_RECFIFOSETMAP:
	    rc = dma_RecFifoSetMap( (uint32_t __user *)arg );
	    break;
	case ZEPTOSC_DMA_RECFIFOGETFIFOGROUP:
	    rc = dma_RecFifoGetFifoGroup( (struct RecFifoGetFifoGroup_struct*)arg);
	    break;
	case ZEPTOSC_DMA_RECFIFOINITBYID:
	    rc = dma_RecFifoInitByID( (struct RecFifoInitByID_struct*)arg );
	    break;
	case ZEPTOSC_DMA_CHGCOUNTERINTERRUPTENABLES:
	    if( arg ) {
		rc = -EINVAL;
	    } else {
		bgcns_disableInterrupt( 3, 10 );
		bgcns_disableInterrupt( 3, 11 );
		bgcns_disableInterrupt( 3, 12 );
		bgcns_disableInterrupt( 3, 13 );
		rc = 0;
	    }
	    break;
	default:
	   printk(KERN_ERR "[Z] sys_zepto_dma: unknown cmd=%u arg=%08x\n", cmd, arg);
           return -1;
    };
    
    zepto_debug(3,"sys_zepto_dma cmd=%08x passed.\n",cmd);

    return rc;
}

int __init bgpdma_init(void)
{
    extern BGCNS_Descriptor bgcnsd;

    bgpdma_device_init();

    zepto_debug(2,"bgpdma is initialized\n");

    /* just debug */

    zepto_debug(2,"baseVirtualAddress=0x%08x  size=0x%08x basePhysicalAddress=0x%08x basePhysicalAddressERPN=0x%08x\n",
		bgcnsd.baseVirtualAddress,
		bgcnsd.size,
		bgcnsd.basePhysicalAddress,
		bgcnsd.basePhysicalAddressERPN);
    return 0;
} 
__initcall(bgpdma_init);
