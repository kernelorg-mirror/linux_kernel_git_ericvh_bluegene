/**********************************************************************
 *
 * Copyright (c) 2007, 2009 International Business Machines
 * Chris Ward <tjcw@uk.ibm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 **********************************************************************/


/* ************************************************************************* */
/*                includes                                                   */
/* ************************************************************************* */

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

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/vmalloc.h>

#include <linux/hugetlb.h>
/*  #include <asm/bluegene.h> */

#include <asm/bgcns.h>

#if defined(CONFIG_SMP) && !defined(CONFIG_BLUEGENE_UNIPROCESSOR)
#define TORNIC_TORUS_AFFINITY
#endif

/* int bgp_dma_irq ; */
#if defined(TORNIC_TORUS_AFFINITY)
void bic_set_cpu_for_irq(unsigned int irq, unsigned int cpu) ;
enum {
  k_TorusAffinityCPU = 2
};
#endif

#define TRACE(x) printk x

#define CHECK_RET(x) if (x) { TRACE((KERN_INFO                                  \
					"bgpdma: Return due error at line %d\n",\
                                     __LINE__)); \
                              return ret; }

#undef CHECK_PARAM
#define CHECK_PARAM(x) if (!(x)) { printk( KERN_INFO                             \
				      "(E) bgpdma: Assertion failed in %s:%d\n", \
				      __FILE__,__LINE__);                    \
                                      return -EINVAL; }
#undef  HPC_MODE
/* #define HPC_MODE */


/* ************************************************************************* */
/*                                                          Include firmware */
/* ************************************************************************* */

/* ************************************************************************* */
/* Defines and friends required by DMA SPI in kernel mode                    */
/* ************************************************************************* */

#include <spi/linux_kernel_spi.h>

/* #include "bgp_bic_diagnosis.h" */
/* ************************************************************************* */
/*                             IOCTL commands                                */
/* ************************************************************************* */

/*  size of mmap'ed IO memory */
#define BGP_DMA_MMAP_SIZE        (4096 * 4)
/*  */
///* ************************************************************************* */
///*                  network device structures                                */
///* ************************************************************************* */
/*  */
struct bgpdma_state_t
{
  uint32_t inj_counters[4];   /*  for each group, a bit mask of which injection counter subgroups allocated */
                              /*  bits 0 - 7 are valid, 8 subgroups of 8 counters/subgroup */
  uint32_t rec_counters[4];   /*  for each group, a bit mask of which reception counter subgroups allocated */
                              /*  bits 0 - 7 are valid, 8 subgroups of 8 counters/subgroup */
  uint32_t inj_fifos[4];      /*  for each group, a bit mask of which injection fifos have been allocated */
                              /*  bits 0 - 31 are valid */

  uint32_t rec_fifo_set_map;        /*  if 1, _bgp_DMA_RecFifoSetMap has already been called */

  uint32_t rec_fifo_init[2];      /*  set bit to 1 if receive fifo has already been intialized, */
                                  /*  bits 0-31 of rec_fifo_init[0] for normal fifos */
                                  /*  bits 0-3  of rec_fifo_init[1] for header fifos */
};

/* max number of registered interrupt handlers */
#define MAX_NUM_IRQ 4


/* interrupt info sctructure */
struct dma_irq
{
  int                        irq;         /*  irq number for this group */
                                          /*  (fixed at module init time) */
  Kernel_CommThreadHandler   func;
  u32                        arg1;
};



struct bgpdma_dev_t
{
  unsigned long long    pa_addr;                  /* physical address */
  struct bgpdma_state_t state;                    /* dma resource state */
  struct dma_irq        irqInfo[ MAX_NUM_IRQ  ];  /* dma interrupts */
};
/*  */
static struct bgpdma_dev_t bgpdma_dev;

/* ************************************************************************* */
/*                       Linux module header                                 */
/* ************************************************************************* */

MODULE_DESCRIPTION("BG/P DMA driver");
MODULE_LICENSE("GPL");

#define BGP_DMA_NAME  "bgpdma"

/*  Threshold crossed irq number for rec fifo groups */
#define DMA_RECFIFO_THRESHOLD_IRQ(group)  ((_BGP_IC_DMA_NFT_G2_HIER_POS<<5)|(28+group))
#define DMA_RECFIFO_THRESHOLD_IRQ_GINT(group)  (28+group)

/*  Threshold crossed irq number for rec fifo groups */
#define TORUS_RECFIFO_WATERMARK_IRQ(fifo)  ((_BGP_IC_DMA_NFT_G2_HIER_POS<<5)|(8+fifo))
#define TORUS_RECFIFO_WATERMARK_IRQ_GINT(fifo)  (8+fifo)

/* ************************************************************************* */
/*                 module initialization/cleanup                             */
/* ************************************************************************* */

static int  __init
  bgpdma_module_init    (void);
static void __exit
  bgpdma_module_cleanup (void);

extern BGCNS_Descriptor bgcnsd;

module_init(bgpdma_module_init);
module_exit(bgpdma_module_cleanup);

/* ************************************************************************* */
/*                       BG/P DMA initialization                             */
/* ************************************************************************* */

/*  dma physical address */
#define _BGP_UA_DMA          (0x6)
#define _BGP_PA_DMA          (0x00000000)

/*  virtual kernel based address of DMA */
void * bgpdma_kaddr;
EXPORT_SYMBOL(bgpdma_kaddr);


/*  check if DMA is mapped by the kernel */
#define CHECK_DMA_ACCESS  if ( ! bgpdma_kaddr ) { printk( KERN_INFO "(E) DMA is not mapped\n"); return -ENODEV; }



/* dma interrupt handler */
/* static unsigned int dmaHandlerCount ; */
irqreturn_t dmaIrqHandler(int irq, void * arg)
{
  struct dma_irq * irqInfo = ( struct dma_irq * )arg;


/*   dmaHandlerCount += 1 ; */
/*   if( irq != 92 || dmaHandlerCount < 20 ) */
/*     { */
/*   printk( KERN_INFO "(I) bgpdma: rec fifo irq dmaIrqHandler called irq:%d arg:%08x\n", */
/* 	  irq, (int)arg); */
/* //  show_bic_regs() ; */
/*     } */
  (*irqInfo->func)(irqInfo->arg1,0,0,0);
  return IRQ_HANDLED;
}

/* irqreturn_t watermarkIrqHandler(int irq, void * arg) */
/* { */
/*   struct dma_irq * irqInfo = ( struct dma_irq * )arg; */
/*  */
/*  */
/*   dmaHandlerCount += 1 ; */
/*   if( irq != 92 || dmaHandlerCount < 20 ) */
/*     { */
/*   printk( KERN_INFO "(I) bgpdma: rec fifo irq watermarkIrqHandler called irq:%d arg:%08x\n", */
/*           irq, (int)arg); */
/* //  show_bic_regs() ; */
/*     } */
/*   (*irqInfo->func)(irqInfo->arg1,0,0,0); */
/*   return IRQ_HANDLED; */
/* } */

irqreturn_t dummyIrqHandler(int irq, void * arg)
{
  printk( KERN_INFO "(I) bgpdma: dummy irq handler called irq:%d arg:%08x\n",
	  irq, (int)arg);
  return IRQ_HANDLED;
}


static int /*__init*/ bgpdma_module_init (void)
{
/*  int ret = -1; */
/*  dev_t devno; */

  TRACE((
		  KERN_INFO "bgpdma: module initialization\n"
		  ));

  bgpdma_dev.pa_addr = ((unsigned long long)_BGP_UA_DMA << 32) | _BGP_PA_DMA;

     /*  map DMA into kernel space */

  if (  bgcnsd.services->isIONode()  )
    {
      TRACE((
		      KERN_INFO "(I) DMA is not mapped on IO node\n"
		      ));
      bgpdma_kaddr = NULL;
      return 0;
    }

  bgpdma_kaddr = ioremap( bgpdma_dev.pa_addr, BGP_DMA_MMAP_SIZE );

  if ( bgpdma_kaddr == NULL )
    {
       printk( KERN_INFO "(E) bgpdma: vmap() failed\n" );
       return -ENOMEM;
    }

   /*  Let bgcnsd know about the new address of the dma */
  unsigned long flags;
  local_irq_save(flags);
  bgcnsd.services->mapDevice(BGCNS_DMA,  bgpdma_kaddr );
  local_irq_restore(flags);


  TRACE((
		  KERN_INFO "bgpdma: module initialization finished, dma kaddr:%08x\n",
	 (unsigned)bgpdma_kaddr));

  return 0;
}

/* ************************************************************************* */
/*                       BG/P net module cleanup                             */
/* ************************************************************************* */

static void __exit
	bgpdma_module_cleanup()
{

   /*  release kernel mapping of dma */
  iounmap ( bgpdma_kaddr );
}



/*
 *   Query free counter subgroups
 */
u32 Kernel_CounterGroupQueryFree( u32   type,
				  u32   grp,
				  u32 * num_subgrps,
				  u32 * subgrps )
{
  CHECK_DMA_ACCESS;

  int ret = 0;
  uint32_t counters;
  int i;

  if ( grp < 0 || grp >= 4 || type < 0 || type > 1 ) return -EINVAL;
  if ( num_subgrps == NULL || subgrps == NULL  )     return -EINVAL;

  if ( type == 0 )
   counters = bgpdma_dev.state.inj_counters[grp];
  else
   counters = bgpdma_dev.state.rec_counters[grp];

  (*num_subgrps) = 0;
  for(i=0; i < DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP; i++ )
    {
      if ( ( counters & _BN(i) ) == 0)
	{
	  subgrps[*num_subgrps] = i;
	  (*num_subgrps)++;
	}
    }

  TRACE((
		  KERN_INFO "Allocated counters:%08x  num_free:%d\n",counters,(int)num_subgrps));

  return ret;
}
EXPORT_SYMBOL(Kernel_CounterGroupQueryFree);


/*
 *   Allocate counter subgroups
 */
u32 Kernel_CounterGroupAllocate( u32   type,
				 u32   grp,
				 u32   num_subgrps,
				 u32 * subgrps,
				 u32   target,         /* not used */
				 u32   handler,        /* not used */
				 u32 * handler_parm,   /* not used */
				 u32   interruptGroup, /* not used */
				 u32 * cg )
{
  CHECK_DMA_ACCESS;

  unsigned i,j;
  u32 *counters;
  u32 c_bits;
  int min_id, max_id, word_id, bit_id, global_subgrp;
  DMA_CounterGroup_t * cg_ptr = (DMA_CounterGroup_t *)cg;
  if ( type > 1 )                                           return -EINVAL;
  if ( grp >= 4 )                                           return -EINVAL;
  if ( subgrps == NULL )                                    return -EINVAL;
  if ( num_subgrps <= 0 )                                   return -EINVAL;
  if ( num_subgrps >  DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP ) return -EINVAL;
  if ( cg_ptr == NULL )                                     return -EINVAL;

  if ( type == DMA_Type_Injection )
    counters = &bgpdma_dev.state.inj_counters[grp];
  else
    counters = &bgpdma_dev.state.rec_counters[grp];

  c_bits = 0;
  for(i=0;i< num_subgrps;i++)
    {
      if ( subgrps[i] < 0 )                                   return -EINVAL;
      if (subgrps[i] >= DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP ) return -EINVAL;
      if ( *counters & _BN(subgrps[i]) )
	{
	  printk( KERN_WARNING
		  "bgpdma: tried to allocate busy counters grp:%d subgrps:%d\n",
		  grp, subgrps[i]);
	  return -EBUSY;
	}
      c_bits |= _BN(subgrps[i]);
    }

  memset( cg_ptr, 0, sizeof(DMA_CounterGroup_t));
  cg_ptr->type     = type;
  cg_ptr->group_id = grp;

  if ( type == DMA_Type_Injection )
    cg_ptr->status_ptr = (DMA_CounterStatus_t *) _BGP_VA_iDMA_COUNTER_ENABLED(grp,0);
  else
    cg_ptr->status_ptr = (DMA_CounterStatus_t *) _BGP_VA_rDMA_COUNTER_ENABLED(grp,0);

  for(i=0;i< num_subgrps;i++)
    {
      min_id = subgrps[i] * DMA_NUM_COUNTERS_PER_SUBGROUP;
      max_id = min_id + DMA_NUM_COUNTERS_PER_SUBGROUP;
      global_subgrp = (grp * DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP ) + subgrps[i];

      cg_ptr->grp_permissions |= _BN( global_subgrp );
      for ( j = min_id; j < max_id; j++ )
	{
	  word_id =  DMA_COUNTER_GROUP_WORD_ID(j);
	  bit_id  =  DMA_COUNTER_GROUP_WORD_BIT_ID(j);
	  cg_ptr->permissions[ word_id ] |= _BN(bit_id);

	  if ( type == DMA_Type_Injection )
	    {
	      cg_ptr->counter[j].counter_hw_ptr =
		( DMA_CounterHw_t *)  _BGP_VA_iDMA_COUNTER(grp,j);
	      DMA_CounterSetValueBaseHw(cg_ptr->counter[j].counter_hw_ptr, 0, 0);
	       /* ret = put_user( 0, &cg_ptr->counter[j].counter_hw_ptr->counter); */
	       /* CHECK_RET(ret); */
	       /* ret = put_user( 0, &cg_ptr->counter[j].counter_hw_ptr->pa_base); */
	       /* CHECK_RET(ret); */

	      TRACE((
			      KERN_INFO "DMA Injection cntr allocated: %d(%08x)\n",
		     j,(unsigned)cg_ptr->counter[j].counter_hw_ptr));
	    }
	  else
	    {
	      cg_ptr->counter[j].counter_hw_ptr =
		( DMA_CounterHw_t *)  _BGP_VA_rDMA_COUNTER(grp,j);
	      DMA_CounterSetValueBaseMaxHw(cg_ptr->counter[j].counter_hw_ptr, 0, 0, 0);
	       /* ret = put_user( 0, &cg_ptr->counter[j].counter_hw_ptr->counter); */
	       /* CHECK_RET(ret); */
	       /* ret = put_user( 0, &cg_ptr->counter[j].counter_hw_ptr->pa_base); */
	       /* CHECK_RET(ret); */
	       /* ret = put_user( 0, &cg_ptr->counter[j].counter_hw_ptr->pa_max); */
	       /* CHECK_RET(ret); */

	      TRACE((
			      KERN_INFO "DMA Reception cntr allocated: %d(%08x)\n",
		     j,(unsigned)cg_ptr->counter[j].counter_hw_ptr));
	    }
	   /*  disable the counter, clear it's hit-zero */
	   /* DMA_CounterSetDisableById  ( cg_ptr,j ); */
	  cg_ptr->status_ptr->disable[word_id] = _BN(bit_id);
	   /* ret = put_user( _BN(bit_id), &cg_ptr->status_ptr->disable[word_id] ); */
	   /* CHECK_RET(ret); */
	   /* DMA_CounterClearHitZeroById( &cg,j ); */
	  cg_ptr->status_ptr->clear_hit_zero[word_id] = _BN(bit_id);
	   /* ret = put_user( _BN(bit_id), &cg_ptr->status_ptr->clear_hit_zero[word_id] ); */
	   /* CHECK_RET(ret); */
	}
    }

  _bgp_msync();

   /*  mark counters allocated in the global state */
  *counters |= c_bits;

  TRACE((
		  KERN_INFO "Allocated counters:%08x\n",*counters));

  return 0;
}
EXPORT_SYMBOL(Kernel_CounterGroupAllocate);


/*
 *   Query free inj fifos
 */
u32 Kernel_InjFifoGroupQueryFree( u32 grp, u32 * num_fifos, u32 * fifo_ids )
{
  CHECK_DMA_ACCESS;

  int ret = 0;
  u32 state;
  int i;

  if ( grp  >= DMA_NUM_INJ_FIFO_GROUPS )        return  -EINVAL;
  if ( num_fifos == NULL || fifo_ids == NULL )  return  -EINVAL;

  state = bgpdma_dev.state.inj_fifos[grp];

  (*num_fifos) = 0;
  for(i=0;i< DMA_NUM_INJ_FIFOS_PER_GROUP;i++)
    {
      if ( ( state & _BN(i) ) == 0 )
	{
	  fifo_ids[(*num_fifos)] = i;
	  (*num_fifos)++;
	  TRACE((
			  KERN_INFO "Free inj fifo: %d\n",i));
	}
    }

  return ret;
}
EXPORT_SYMBOL(Kernel_InjFifoGroupQueryFree);


/*
 *   Allocate inj fifos from a group
 */
u32 Kernel_InjFifoGroupAllocate( u32   grp,
				 u32   num_fifos,
				 u32 * ids,
				 u16 * pri,
				 u16 * loc,
				 u8  * map,
				 u32 * fg )
{
  CHECK_DMA_ACCESS;

   /*  MUST be called when the DMA is inactive, prior to any DMA activity */
  int i;
  u32 f_bits =0;
  u32 p_bits =0;
  u32 l_bits =0;
  DMA_InjFifoGroup_t * fg_ptr = (DMA_InjFifoGroup_t *)fg;

  if ( fg_ptr == NULL )                                                return -EINVAL;
  if ( grp < 0 || grp >= DMA_NUM_FIFO_GROUPS )                         return -EINVAL;
  if ( num_fifos <= 0 || num_fifos > DMA_NUM_INJ_FIFOS_PER_GROUP )     return -EINVAL;
  if ( ids == NULL || pri == NULL || map == NULL )                     return -EINVAL;

  f_bits = 0;  /*  holds a bit vector of all fifos used in this allocation */
  for ( i = 0; i < num_fifos; i++ )
    {
      if ( ids[i] >= DMA_NUM_INJ_FIFOS_PER_GROUP ) return -EINVAL;
      if ( pri[i] > 1 || loc[i] > 1 )              return -EINVAL;
      if ( loc[i] == 0 && map[i] == 0 )            return -EINVAL;
      if ( loc[i] == 1 && map[i] != 0 )            return -EINVAL;

      if ( bgpdma_dev.state.inj_fifos[grp] & _BN(ids[i]) )
	{
	  printk( KERN_WARNING
		  "bgpdma: tried to allocate busy inj fifos grp:%d fifo_id:%d\n",
		  grp, ids[i]);
	  return -EBUSY;
	}

      f_bits |= _BN(ids[i]);
      if ( loc[i] == 1 ) l_bits |= _BN(i);
      if ( pri[i] == 1 ) p_bits |= _BN(i);
    }


  memset( fg_ptr, 0, sizeof(DMA_InjFifoGroup_t));
  fg_ptr->status_ptr   = (DMA_InjFifoStatus_t *) _BGP_VA_iDMA_NOT_EMPTY(grp);
  fg_ptr->group_id     = grp;
  fg_ptr->permissions |= f_bits;

   /*  Disable interrupts and the injection FIFOs */
  unsigned long flags;
  local_irq_save(flags);
   bgcnsd.services->
     setDmaFifoControls( BGCNS_Disable,BGCNS_InjectionFifoInterrupt, grp,f_bits,NULL );
   bgcnsd.services->
     setDmaFifoControls( BGCNS_Disable,BGCNS_InjectionFifo,          grp,f_bits,NULL );
   local_irq_restore(flags);

    /*  deactivate all these fifos */
   fg_ptr->status_ptr->deactivate = f_bits;
    /* ret = put_user( f_bits, &fg.status_ptr->deactivate ); */
    /* CHECK_RET(ret); */

   _bgp_mbar();  /*  make sure write is in the DMA */

   local_irq_save(flags);
   bgcnsd.services->setDmaInjectionMap( grp, (unsigned*)ids, map, num_fifos );
   local_irq_restore(flags);

   for ( i=0;i< num_fifos; i++)
      {
	fg_ptr->fifos[ids[i]].dma_fifo.fifo_hw_ptr =
	  ( DMA_FifoHW_t *) _BGP_VA_iDMA_START(grp, ids[i]);
	fg_ptr->fifos[ids[i]].fifo_id      = ids[i];
	fg_ptr->fifos[ids[i]].desc_count   = 0;
	fg_ptr->fifos[ids[i]].occupiedSize = 0;
	fg_ptr->fifos[ids[i]].priority     = pri[i] ;
	fg_ptr->fifos[ids[i]].local        = loc[i];
	fg_ptr->fifos[ids[i]].ts_inj_map   = map[i];

	 /*  write 0's to the hw fifo */
	fg_ptr->fifos[ids[i]].dma_fifo.fifo_hw_ptr->pa_start = 0;
	 /* ret = put_user( 0, &fg.fifos[ids[i]].dma_fifo.fifo_hw_ptr->pa_start ); */
	 /* CHECK_RET(ret); */
	fg_ptr->fifos[ids[i]].dma_fifo.fifo_hw_ptr->pa_head = 0;
	 /* ret = put_user ( 0, &fg.fifos[ids[i]].dma_fifo.fifo_hw_ptr->pa_head ); */
	 /* CHECK_RET(ret); */
	fg_ptr->fifos[ids[i]].dma_fifo.fifo_hw_ptr->pa_tail = 0;
	 /* ret = put_user( 0, &fg.fifos[ids[i]].dma_fifo.fifo_hw_ptr->pa_tail ); */
	 /* CHECK_RET(ret); */
	fg_ptr->fifos[ids[i]].dma_fifo.fifo_hw_ptr->pa_end = 0;
	 /* ret = put_user( 0, &fg.fifos[ids[i]].dma_fifo.fifo_hw_ptr->pa_end ); */
	 /* CHECK_RET(ret); */

/* 	TRACE((KERN_INFO "Allocate inj fifo: %d",ids[i])); */
      }

    /*  clear the threshold crossed */
   _bgp_mbar();    /*  no previous write will pass this one */
   fg_ptr->status_ptr->clear_threshold_crossed = f_bits;
    /* ret = put_user( f_bits, &fg.status_ptr->clear_threshold_crossed ); */
    /* CHECK_RET(ret); */

   local_irq_save(flags);
    /*  set the local copy bits */
   bgcnsd.services->setDmaLocalCopies(BGCNS_Enable, grp, l_bits);
    /*  set the priority bits */
   bgcnsd.services->setDmaPriority(BGCNS_Enable, grp, p_bits);

    /*  Enable interrupts for these fifos. */
    /*  NOTE: enablement of the injection FIFO will take place during FIFO init. */
    /*  _bgp_cns()->setDmaFifoControls( BGCNS_Enable, BGCNS_InjectionFifoInterrupt, grp, f_ids, NULL ); */
   local_irq_restore(flags);

    /*  mark fifos allocated in the global state */
   bgpdma_dev.state.inj_fifos[grp] |= f_bits;

   return 0;
}
EXPORT_SYMBOL(Kernel_InjFifoGroupAllocate);

/*
 *   General fifo init
 */
static inline int FifoInit( DMA_Fifo_t * f_ptr,
			    void       * va_start,
			    void       * va_head,
			    void       * va_end )
{
  int ret = 0;
  uint32_t pa_start, pa_head, pa_end;
  unsigned bytes;

  TRACE((
		  KERN_INFO "FifoInit va_start:%08x va_head:%08x va_end:%08x\n",
	 (u32)va_start,(u32)va_head,(u32)va_end));

  if ( f_ptr == NULL )                    return -EINVAL;
  if ( f_ptr->fifo_hw_ptr == NULL )       return -EINVAL;
  if ( ((uint32_t)va_start & 0x1F) != 0 ) return -EINVAL;
  if ( ((uint32_t)va_end   & 0x1F) != 0 ) return -EINVAL;
  if ( ((uint32_t)va_head  & 0xF ) != 0 ) return -EINVAL;

  bytes = (uint32_t)va_end - (uint32_t)va_start;

   /*  translate start address ( and check if the region is contigouos) */
  pa_start = virt_to_phys ( va_start );
/*   TRACE((KERN_INFO "bgpdma: FifoInit() va_start:%08x pa_start:%08x shifted:%08x", */
/* 	 (u32)va_start, pa_start, pa_start>>4 )); */
  pa_start >>= 4;  /*  we need 16-byte aligned address */

   /* ret = VaTo4bitShiftedPa( va_start, bytes, &pa_start ); */
   /* CHECK_RET(ret); */

   /*  physical region is contigouos, we can compute pa_end and pa_head */
  pa_end  = pa_start + ( bytes >> 4 );
  pa_head = pa_start + ( ((uint32_t)va_head - (uint32_t)va_start ) >> 4 );

 /* Write the start, end , head and tail(= head) */
  f_ptr->fifo_hw_ptr->pa_start = pa_start;
   /* ret = put_user ( pa_start, &f_ptr->fifo_hw_ptr->pa_start ); */
   /* CHECK_RET(ret); */
  f_ptr->fifo_hw_ptr->pa_head  = pa_head;
   /* ret = put_user( pa_head, &f_ptr->fifo_hw_ptr->pa_head ); */
   /* CHECK_RET(ret); */
  f_ptr->fifo_hw_ptr->pa_tail  = pa_head;
   /* ret = put_user( pa_head, &f_ptr->fifo_hw_ptr->pa_tail ); */
   /* CHECK_RET(ret); */
  f_ptr->fifo_hw_ptr->pa_end   = pa_end;
   /* ret = put_user( pa_end, &f_ptr->fifo_hw_ptr->pa_end ); */
   /* CHECK_RET(ret); */

  _bgp_mbar();

  /* Save the shadows in the structure */
  f_ptr->pa_start = pa_start;
  f_ptr->va_start = va_start;
  f_ptr->va_end   = va_end;
  f_ptr->va_head  = va_head;
  f_ptr->va_tail  = va_head;

  /* Compute the free space */
  f_ptr->fifo_size  = bytes >> 4; /* Number of 16B quads */
  f_ptr->free_space = f_ptr->fifo_size;

  return ret;
}


/*
 *   Initialize an injection fifo
 */
u32 Kernel_InjFifoInitById( u32 * fg,
			    int    fifo_id,
			    u32 * va_start,
			    u32 * va_head,
			    u32 * va_end )
{
  CHECK_DMA_ACCESS;

  int ret = 0;
  int grp;
  uint32_t x_phead, x_vstart, x_pstart, x_vtail;
  DMA_InjFifoGroup_t * fg_ptr = (DMA_InjFifoGroup_t *)fg;

  if ( fg_ptr == NULL )                                               return -EINVAL;
  if ( fifo_id < 0 || fifo_id >= DMA_NUM_INJ_FIFOS_PER_GROUP )        return -EINVAL;
  if ( va_start >= va_end || va_start > va_head || va_head > va_end ) return -EINVAL;
  if ( (u32)va_head+DMA_FIFO_DESCRIPTOR_SIZE_IN_BYTES > (u32)va_end ) return -EINVAL;
  if ( (u32)va_end - (u32)va_start < DMA_MIN_INJ_FIFO_SIZE_IN_BYTES ) return -EINVAL;
  if ( ((u32)va_start & 0x1F) != 0 )                                  return -EINVAL;
  if ( ((u32)va_end   & 0x1F) != 0 )                                  return -EINVAL;
  if ( ((u32)va_head  & 0xF)  != 0 )                                  return -EINVAL;

  if (( fg_ptr->permissions & _BN(fifo_id)) == 0 ) return -EBUSY;

  grp = fg_ptr->group_id;


   /*  Disable the injection FIFO and its interrupt: */
  unsigned long flags;
  local_irq_save(flags);
  bgcnsd.services->
    setDmaFifoControls(BGCNS_Disable, BGCNS_InjectionFifo, grp, _BN(fifo_id), NULL);
  bgcnsd.services->
    setDmaFifoControls(BGCNS_Disable, BGCNS_InjectionFifoInterrupt, grp, _BN(fifo_id), NULL );
  local_irq_restore(flags);


  /* Deactivate the fifo */
  fg_ptr->status_ptr->deactivate = _BN(fifo_id);
   /* ret = put_user ( _BN(fifo_id), &fg.status_ptr->deactivate ); */
   /* CHECK_RET(ret); */

  /* Initialize the fifo */
  ret = FifoInit( &fg_ptr->fifos[fifo_id].dma_fifo, va_start, va_head, va_end );
  CHECK_RET(ret);

  /* Initialize the descriptor count and occupied size */
  fg_ptr->fifos[fifo_id].desc_count   = 0;
  fg_ptr->fifos[fifo_id].occupiedSize = 0;

   /*  clear the threshold crossed */
  fg_ptr->status_ptr->clear_threshold_crossed = _BN(fifo_id);
   /* ret = put_user( _BN(fifo_id), &fg.status_ptr->clear_threshold_crossed ); */
   /* CHECK_RET(ret); */

   /*  read back something from the dma to ensure all writes have occurred */
   /*  head should equal tail */
  x_phead  = fg_ptr->fifos[fifo_id].dma_fifo.fifo_hw_ptr->pa_head;
   /* ret = get_user( x_phead, &fg.fifos[fifo_id].dma_fifo.fifo_hw_ptr->pa_head ); */
   /* CHECK_RET(ret); */
  x_vstart = (uint32_t)(fg_ptr->fifos[fifo_id].dma_fifo.va_start);
  x_pstart = (uint32_t)(fg_ptr->fifos[fifo_id].dma_fifo.pa_start);
  x_vtail  = (uint32_t)(fg_ptr->fifos[fifo_id].dma_fifo.va_tail);
  if ( x_vstart + ( (x_phead - x_pstart)  << 4 ) != x_vtail ) return -EIO;



   /*  Enable the FIFO and its interrupt: */
  local_irq_save(flags);
  bgcnsd.services->
    setDmaFifoControls(BGCNS_Enable, BGCNS_InjectionFifo, grp, _BN(fifo_id), NULL);
   /* bgcnsd.services->setDmaFifoControls(BGCNS_Enable, BGCNS_InjectionFifoInterrupt, grp, _BN(fifo_id), NULL); */
  local_irq_restore(flags);

   /*  Activate the fifo */
  fg_ptr->status_ptr->activate = _BN(fifo_id);
   /* ret = put_user( _BN(fifo_id), &fg.status_ptr->activate ); */
   /* CHECK_RET(ret); */

  return 0;
}
EXPORT_SYMBOL(Kernel_InjFifoInitById);


/*
 * Free inj fifos
 */
uint32_t Kernel_InjFifoGroupFree(uint32_t   grp,
				 uint32_t   num_fifos,
				 uint32_t * fifo_ids,
				 uint32_t * fg)
{
  int ret = 0;
  u32 f_bits =0;
  int i;
  DMA_InjFifoGroup_t * fg_ptr = (DMA_InjFifoGroup_t *)fg;

  if ( fg_ptr == NULL )                                            return -EINVAL;
  if ( grp < 0 || grp >= DMA_NUM_FIFO_GROUPS )                     return -EINVAL;
  if ( num_fifos <= 0 || num_fifos > DMA_NUM_INJ_FIFOS_PER_GROUP ) return -EINVAL;
  if ( fifo_ids == NULL )                                          return -EINVAL;

  f_bits = 0;  /*  holds a bit vector of all fifos used in this allocation */
  for ( i = 0; i < num_fifos; i++ )
    {
      if ( fifo_ids[i] >= DMA_NUM_INJ_FIFOS_PER_GROUP ) return -EINVAL;

      if ( ! (bgpdma_dev.state.inj_fifos[grp] & _BN(fifo_ids[i])) )
	{
	  printk( KERN_WARNING
		  "bgpdma: tried to free a non-allocated inj fifo grp:%d fifo_id:%d\n",
		  grp, fifo_ids[i]);
	  return -EBUSY;
	}

      f_bits |= _BN(fifo_ids[i]);
    }

   for ( i=0;i< num_fifos; i++)
     fg_ptr->fifos[fifo_ids[i]].dma_fifo.fifo_hw_ptr = NULL;

  fg_ptr->permissions ^= f_bits;
  fg_ptr->status_ptr->deactivate = f_bits;

  return ret;
}



/*
 * Set the reception fifos map
 */
int Kernel_RecFifoSetMap( u32 * map )
{
  CHECK_DMA_ACCESS;

  int i, g;
  DMA_RecFifoMap_t * map_ptr = (DMA_RecFifoMap_t *)map;

   /*   NEED TO PUT A LOCK AROUND THIS, Assume either the syscall mechanism does this */
   /*   or it has to be put here */

   /*   MUST BE CALLED ONCE, Prior to Any DMA activity */
   /*   Specifically, must be called after _bgp_DMA_Reset_Release */
   /*   and prior to any  _BGP_rDMA_Fifo_Get_Fifo_Group calls */

  if ( map_ptr == NULL )            return -EINVAL;
  if ( map_ptr->save_headers > 1 )  return -EINVAL;

  for (i=0; i< DMA_NUM_NORMAL_REC_FIFOS; i++)
    if ( ( map_ptr->fifo_types[i] < 0 ) || ( map_ptr->fifo_types[i] > 1)) return -EINVAL;

   /*  rec fifo map can be set only once */
  if ( bgpdma_dev.state.rec_fifo_set_map != 0 ) return -EBUSY;

  if ( map_ptr->save_headers == 1)
    for (i=0; i< DMA_NUM_HEADER_REC_FIFOS; i++)
      if ( ( map_ptr->hdr_fifo_types[i] <0 ) ||  ( map_ptr->hdr_fifo_types[i] > 1 ))
	return  -EINVAL;

  for (g=0; g< DMA_NUM_REC_FIFO_GROUPS;g++)
    for (i=0; i<  DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP; i++)
      if ( map_ptr->ts_rec_map[g][i] >= DMA_NUM_NORMAL_REC_FIFOS)
	return  -EINVAL;

  TRACE((
		  KERN_INFO "bgpdma: Kernel_RecFifoSetMap() disabling reception FIFO interrupts\n"));

  unsigned long flags;
  local_irq_save(flags);
   /*  Disable the reception FIFOs */
  bgcnsd.services->setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionFifo, 0 /* group not used */, 0xFFFFFFFF, NULL );
  bgcnsd.services->setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionHeaderFifo, BGCNS_DMA_ALL_GROUPS, 0 /* mask not used */, NULL );

   /*  Set the map: */
  bgcnsd.services->setDmaReceptionMap(map_ptr->ts_rec_map,
			      map_ptr->fifo_types,
			      map_ptr->save_headers ? map_ptr->hdr_fifo_types : NULL,
			      map_ptr->threshold );

  local_irq_restore(flags);

   /*  Don't enable the fifos here,  the fifo init will do that */
  bgpdma_dev.state.rec_fifo_set_map = 1;

  return 0;
}
EXPORT_SYMBOL(Kernel_RecFifoSetMap);


/*
 * Get the reception fifos map
 */
int Kernel_RecFifoGetMap( u32 * map )
{
  CHECK_DMA_ACCESS;

  int ret;
  DMA_RecFifoMap_t * map_ptr = (DMA_RecFifoMap_t *)map;

  if ( map_ptr == NULL ) return -EINVAL;

  memset( map_ptr, 0, sizeof(DMA_RecFifoMap_t) );

  unsigned long flags;
  local_irq_save(flags);

  ret = bgcnsd.services->getDmaReceptionMap( map_ptr->ts_rec_map,
				     map_ptr->fifo_types,
				     &(map_ptr->save_headers),
				     map_ptr->hdr_fifo_types,
				     map_ptr->threshold);

  local_irq_restore(flags);

  CHECK_RET(ret);

  return 0;
}
EXPORT_SYMBOL(Kernel_RecFifoGetMap);

/*
 *   Initialize a receiver fifo group
 */
int Kernel_RecFifoGetFifoGroup( u32  * fg,
				int    grp,                /* group number */
				int    target,             /* not used */
				void * normal_handler,     /* not used */
				void * normal_handler_parm,/* not used */
				void * header_handler,     /* not used */
				void * header_handler_parm,/* not used */
				void * interruptGroup )    /* not used */
{
  CHECK_DMA_ACCESS;

  int ret;
  DMA_RecFifoMap_t   map;

  uint32_t used_fifos;
  int g,i,j,min_id,max_id,idx;
  uint32_t x;
  DMA_RecFifoGroup_t * fg_ptr = (DMA_RecFifoGroup_t *)fg;

  if ( fg_ptr == NULL )                           return -EINVAL;
  if ( grp < 0 || grp > DMA_NUM_REC_FIFO_GROUPS ) return -EINVAL;
   /*  if ( target < 0 || target > 4 )                 return -EINVAL; */

  memset( fg_ptr, 0, sizeof(DMA_RecFifoGroup_t) );


   /*  get the map */
  unsigned long flags;
  local_irq_save(flags);
  ret = bgcnsd.services->getDmaReceptionMap( map.ts_rec_map,
				     map.fifo_types,
				     &(map.save_headers),
				     map.hdr_fifo_types,
				     map.threshold);
  local_irq_restore(flags);

  CHECK_RET(ret);

   /*  set the mask */
  fg_ptr->group_id = grp;
  switch(grp)
    {
    case 0: fg_ptr->mask   = 0xFF000000; break;
    case 1: fg_ptr->mask   = 0x00FF0000; break;
    case 2: fg_ptr->mask   = 0x0000FF00; break;
    case 3: fg_ptr->mask   = 0x000000FF; break;
    }

   /*  set the status pointer */
  fg_ptr->status_ptr = ( DMA_RecFifoStatus_t *) _BGP_VA_rDMA_NOT_EMPTY(grp,0);

   /*  figure out which normal fifos are being used */
  min_id = (grp*DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP);
  max_id =  min_id +DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP-1;

  used_fifos = 0;
  for (g=0;g< DMA_NUM_REC_FIFO_GROUPS;g++)
    for(i=0;i<DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP;i++)
      if (  ( map.ts_rec_map[g][i] >=  min_id ) && (map.ts_rec_map[g][i] <=  max_id) )
	used_fifos |= _BN(map.ts_rec_map[g][i]);

  idx = 0;
  for(j= 0;j<DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP;j++)
    {
      i = min_id + j;
      if ( ( _BN(i) & used_fifos) != 0 )
	{
	  fg_ptr->fifos[idx].type           =  map.fifo_types[i];
	  fg_ptr->fifos[idx].global_fifo_id = i;
	  fg_ptr->fifos[idx].num_packets_processed_since_moving_fifo_head = 0;
	  fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr = ( DMA_FifoHW_t *) _BGP_VA_rDMA_START(grp,j);
	   /*  Make sure this fifo is disabled */
	  fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr->pa_start = 0;
	   /* ret = put_user( 0, &fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr->pa_start ); */
	   /* CHECK_RET(ret); */
	  fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr->pa_head = 0;
	   /* ret = put_user( 0, &fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr->pa_head ); */
	   /* CHECK_RET(ret); */
	  fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr->pa_tail = 0;
	   /* ret = put_user( 0, &fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr->pa_tail ); */
	   /* CHECK_RET(ret); */
	  fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr->pa_end = 0;
	   /* ret = put_user( 0, &fg_ptr->fifos[idx].dma_fifo.fifo_hw_ptr->pa_end ); */
	   /* CHECK_RET(ret); */

	  idx++;
	}
    }   /*  j loop */

   /*  are we saving headers? */
  if ( map.save_headers == 1 )
    {
      fg_ptr->num_hdr_fifos = 1;
      fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].type = map.hdr_fifo_types[grp];
      fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].global_fifo_id = DMA_NUM_NORMAL_REC_FIFOS+grp;
      fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].num_packets_processed_since_moving_fifo_head = 0;
      fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr =
	( DMA_FifoHW_t *) _BGP_VA_rDMA_START(grp, DMA_HEADER_REC_FIFO_ID);

      fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr->pa_start = 0;
       /* ret = */
       /* 	put_user( 0, &fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr->pa_start ); */
       /* CHECK_RET(ret); */

      fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr->pa_head = 0;
       /* ret = */
       /* 	put_user( 0, &fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr->pa_head ); */
       /* CHECK_RET(ret); */
      fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr->pa_tail = 0;
       /* ret = */
       /* 	put_user( 0, &fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr->pa_tail ); */
       /* CHECK_RET(ret); */
      fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr->pa_end = 0;
       /* ret = */
       /* 	put_user( 0, &fg_ptr->fifos[DMA_HEADER_REC_FIFO_ID].dma_fifo.fifo_hw_ptr->pa_end ); */
       /* CHECK_RET(ret); */
    }

  fg_ptr->num_normal_fifos = idx;
  fg_ptr->status_ptr->clear_threshold_crossed[0] = fg_ptr->mask;
   /* ret = put_user( fg_ptr->mask, &fg_ptr->status_ptr->clear_threshold_crossed[0] ); */
   /* CHECK_RET(ret); */
  fg_ptr->status_ptr->clear_threshold_crossed[1] = fg_ptr->mask;
   /* ret = put_user( fg_ptr->mask, &fg_ptr->status_ptr->clear_threshold_crossed[1] ); */
   /* CHECK_RET(ret); */

   /*  read back from the dma to ensure all writes have occurred */
  _bgp_mbar();
  x = fg_ptr->status_ptr->threshold_crossed[0];
   /* ret = get_user( x, &fg_ptr->status_ptr->threshold_crossed[0] ); */
   /* if ( ret )                 return ret; */
  if ( (x & fg_ptr->mask) != 0  ) return -EIO;

   /*  reenable interrupts, if necessary */
   /*  */
   /*  DCRs 0xD71, 0xD72, 0xD73, and 0xD74 contain bits indicating which */
   /*  reception fifos will be enabled for interrupt 0, 1, 2, and 3, respectively. */
   /*  These interrupts correspond to BIC interrupt group 2, IRQs 28, 29, 30, and */
   /*  31, respectively.  Thus, if bit i is on in DCR 0xD7z, and rec fifo i's */
   /*  free space drops below the threshold for that fifo, then IRQ 28 + (z-1) */
   /*  will fire. */
   /*  */
   /*  For each reception fifo in this group, turn on bit i in DCR 0xD7z, where */
   /*  z-1 is the group number. */
   /*  */

  used_fifos = 0;
  for (i = 0; i < fg_ptr->num_normal_fifos; i++)
    used_fifos |= _BN(fg_ptr->fifos[i].global_fifo_id);

  TRACE((
		  KERN_INFO "bgpdma: Kernel_RecFifoGetFifoGroup() enabling reception FIFO interrupts\n"));
  local_irq_save(flags);

  bgcnsd.services->setDmaFifoControls(BGCNS_Enable,
			       BGCNS_ReceptionFifoInterrupt,
			       fg_ptr->group_id,
			       used_fifos,
			       NULL);

   local_irq_restore(flags);


  _bgp_msync();
  _bgp_isync();



  return 0;
}
EXPORT_SYMBOL(Kernel_RecFifoGetFifoGroup);

/*
 *   Initialize a reception fifo
 */
int Kernel_RecFifoInitById( u32  * fg,
			    int    fifo_id,
			    void * va_start,
			    void * va_head,
			    void * va_end )
{
  CHECK_DMA_ACCESS;

  int ret;
  uint32_t st_word, st_mask;
  uint32_t x_phead, x_vtail, x_vstart, x_pstart;
  int i, grp, g_fifo_id;
  DMA_RecFifoGroup_t * fg_ptr = (DMA_RecFifoGroup_t *)fg;
  uint32_t xint[4] = {0,0,0,0};

  if ( fg_ptr == NULL )                                                return -EINVAL;
  if ( fifo_id < 0 || fifo_id >= DMA_NUM_REC_FIFOS_PER_GROUP )         return -EINVAL;
  if ( va_start >= va_end || va_start > va_head || va_head > va_end )  return -EINVAL;
  if ( ((u32)va_start & 0x1F) != 0 )                                   return -EINVAL;
  if ( ((u32)va_end   & 0x1F) != 0 )                                   return -EINVAL;
  if ( ((u32)va_head  & 0xF)  != 0 )                                   return -EINVAL;
   /* if ( (u32)va_end - (u32)va_start <  DMA_MIN_REC_FIFO_SIZE_IN_BYTES ) return -EINVAL; */

  /*
   * Note:  The reception fifos are in a disabled state upon return from
   *        DMA_RecFifoSetMap(), so we assume they are disabled at this point,
   *        making it safe to set the start, head, etc.
   */

   /*  NOTE:  This assumes the interrupt enables have been previously set as desired, */
   /*   in _bgp_DMA_RecFifoGetFifoGroup, so we simply read those dcrs, disable all fifos, */
   /*   and write them back at the end */

  grp       = fg_ptr->group_id;
  g_fifo_id = fg_ptr->fifos[fifo_id].global_fifo_id;

  if ( g_fifo_id <  DMA_NUM_NORMAL_REC_FIFOS)  /*  normal fifo */
     {
       st_word = 0;                         /*  status word for this fifo */
       st_mask = _BN(g_fifo_id) & fg_ptr->mask;  /*  status mask for this fifo */

        /*  see if this fifo has already been initialized */
       if ((bgpdma_dev.state.rec_fifo_init[st_word] & _BN(g_fifo_id)) !=0 ) return -EBUSY;
        /*  Disable the FIFO and all interrupts (interrupts will be restored below) */
       TRACE((
		       KERN_INFO "bgpdma: Kernel_RecFifoInitById() disabling reception FIFO interrupts\n"));
       unsigned long flags;
       local_irq_save(flags);
       bgcnsd.services->setDmaFifoControls( BGCNS_Disable, BGCNS_ReceptionFifo, 0 /* group not used */, _BN(g_fifo_id), NULL );

       for (i=0; i<4; i++)
	 bgcnsd.services->setDmaFifoControls( BGCNS_Disable, BGCNS_ReceptionFifoInterrupt, i, 0xFFFFFFFF, &(xint[i]) );  /*  save for re-enablement below */
       local_irq_restore(flags);
     }
  else  /*  header fifo */
     {
       st_word = 1;        /*  status word for this fifo */
       st_mask = fg_ptr->mask;  /*  status mask for this fifo (only one bit is used by the HW) */

        /*  see if this fifo has already been initialized */
       if ( (bgpdma_dev.state.rec_fifo_init[st_word] & _BN(g_fifo_id-32)) != 0 )
	 return -EBUSY;

        /*  remember that this fifo has been initialized */
       bgpdma_dev.state.rec_fifo_init[st_word] |= _BN(g_fifo_id-32);

        /*  Disable the reception header FIFO and its interrupts */
       TRACE((
		       KERN_INFO "bgpdma: Kernel_RecFifoInitById() disabling reception header FIFO interrupts\n"));
       unsigned long flags;
       local_irq_save(flags);
       bgcnsd.services->setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionHeaderFifo, grp, 0 /* mask not used */, NULL );
       bgcnsd.services->setDmaFifoControls(BGCNS_Disable, BGCNS_ReceptionHeaderFifoInterrupt, 0, 0xFFFFFFFF, xint );
       local_irq_restore(flags);

     }

   /*  Initialize the fifo */
  ret = FifoInit( &fg_ptr->fifos[fifo_id].dma_fifo, va_start, va_head, va_end );
  CHECK_RET(ret);


   /*  remember that this fifo has been initialized */
  if ( g_fifo_id <  DMA_NUM_NORMAL_REC_FIFOS )                 /*  normal fifo */
    bgpdma_dev.state.rec_fifo_init[0] |= _BN(g_fifo_id);
  else                                                         /*  header fifo */
    bgpdma_dev.state.rec_fifo_init[1] |= _BN(g_fifo_id-32);

   /*  clear the threshold crossed */
  fg_ptr->status_ptr->clear_threshold_crossed[st_word] = st_mask;
   /* ret = put_user( st_mask, &fg_ptr->status_ptr->clear_threshold_crossed[st_word] ); */
   /* CHECK_RET(ret); */

   /*  read back something from the dma to ensure all writes have occurred */
   /*  head should equal tail */
  x_phead  = fg_ptr->fifos[fifo_id].dma_fifo.fifo_hw_ptr->pa_head;
   /* ret = get_user( x_phead, &fg_ptr->fifos[fifo_id].dma_fifo.fifo_hw_ptr->pa_head ); */
   /* CHECK_RET(ret); */
  x_vstart = (uint32_t)fg_ptr->fifos[fifo_id].dma_fifo.va_start;
  x_pstart = (uint32_t)fg_ptr->fifos[fifo_id].dma_fifo.pa_start;
  x_vtail  = (uint32_t)fg_ptr->fifos[fifo_id].dma_fifo.va_tail;
  if ( x_vstart + ( (x_phead - x_pstart)  << 4 ) != x_vtail ) return -EIO;

   /*  Enable the FIFO and re-enable interrupts */
   unsigned long flags;
  local_irq_save(flags);

  if ( g_fifo_id <  DMA_NUM_NORMAL_REC_FIFOS) {  /*  Normal fifo */
    TRACE((
		    KERN_INFO "bgpdma: Kernel_RecFifoInitById() enabling reception FIFO interrupts\n"));
    bgcnsd.services->setDmaFifoControls(BGCNS_Enable, BGCNS_ReceptionFifo, 0 /* group not used */, _BN(g_fifo_id), NULL);

    for (i=0; i<4; i++)
      bgcnsd.services->setDmaFifoControls(BGCNS_Reenable, BGCNS_ReceptionFifoInterrupt, i, 0 /* mask not used */, &(xint[i]) );  /*  Restore saved state */
  }
  else {  /*  Header FIFO */
    TRACE((
		    KERN_INFO "bgpdma: Kernel_RecFifoInitById() enabling reception header FIFO interrupts\n"));
      bgcnsd.services->setDmaFifoControls(BGCNS_Enable,   BGCNS_ReceptionHeaderFifo, grp, 0 /* mask not used */, NULL );
       /*  bgcnsd.services->setDmaFifoControls(BGCNS_Reenable, BGCNS_ReceptionHeaderFifoInterrupt, 0, 0, xint ); */
  }

  local_irq_restore(flags);

  return 0;
}
EXPORT_SYMBOL(Kernel_RecFifoInitById);

/*
 *  Register interrupt handlers
 */
int Kernel_SetCommThreadConfig(int irq,
			       int opcode,
			       LockBox_Counter_t cntrid,
			       Kernel_CommThreadHandler handler,
			       uint32_t arg1,
			       uint32_t arg2,
			       uint32_t arg3,
			       uint32_t arg4)
{
  int ret = 0;
  int i;

  CHECK_PARAM( arg2 == 0 && arg3 == 0 &&  arg4 == 0 );



  for ( i = 0; i < MAX_NUM_IRQ; i++ )
    if ( bgpdma_dev.irqInfo[i].irq == 0 || bgpdma_dev.irqInfo[i].irq == irq )
      break;

  if ( i == MAX_NUM_IRQ )
    {
      printk(KERN_INFO "bgpdma: Kernel_SetCommThreadConfig: No more irq info slot\n" );
      return -ENOSPC;
    }

  bgpdma_dev.irqInfo[i].func = handler;
  bgpdma_dev.irqInfo[i].arg1 = arg1;

  if ( bgpdma_dev.irqInfo[i].irq == irq )
    {
      TRACE((
		      KERN_INFO "bgpdma: Kernel_SetCommThreadConfig: Re-registering handler "
	     "for irq:%d func:%08x arg1:%d\n",irq, (int)handler, arg1 ));
      return 0;
    }

  bgpdma_dev.irqInfo[i].irq  = irq;

/*   bgp_dma_irq = irq ; */
#if defined(TORNIC_TORUS_AFFINITY)
  bic_set_cpu_for_irq(irq,k_TorusAffinityCPU) ;
  TRACE((
		  KERN_INFO "bgpdma: setting affinity irq=%d affinity=%d\n",irq, k_TorusAffinityCPU ));
#endif


  ret = request_irq(irq,
		    dmaIrqHandler,
		    IRQF_DISABLED,
		    BGP_DMA_NAME,
		    &bgpdma_dev.irqInfo[i]);

  TRACE((
		  KERN_INFO "bgpdma: request_irq irq=%d i=%d func=%p arg1=%08x ret=%d\n",irq, i, handler, arg1, ret ));
  CHECK_RET(ret);

  TRACE((
		  KERN_INFO "bgpdma: Kernel_SetCommThreadConfig() finished\n"));
  return ret;
}

EXPORT_SYMBOL(Kernel_SetCommThreadConfig) ;

/*
 * Remove commthread from the run queue ... not implemented
 */
int pthread_poof_np( void )
{
  printk(KERN_INFO "bgpdma: pthread_poof_np() called !!! (bgp_dma.c:%d)\n",
	 __LINE__);
  return 0;
}
