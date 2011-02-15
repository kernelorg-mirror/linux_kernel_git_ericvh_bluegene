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
 * Description: Blue Gene low-level driver for sockets over torus
 *
 * Intent: Send a 'request block' to the partner's memory FIFO
 *         Partner initiates a 'remote read' from me
 *         Partner sends a 'response block' to my FIFO to say the data is transferred
 *
 ********************************************************************/
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
#include <linux/skbuff.h>
#include <linux/etherdevice.h>

#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/bootmem.h>


#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/time.h>
#include <linux/vmalloc.h>

#include <linux/dma-mapping.h>

#include <net/inet_connection_sock.h>
#include <net/inet_sock.h>
#include <net/inet_hashtables.h>
#include <net/tcp.h>
#include <net/tcp_hiatus.h>

#include <spi/linux_kernel_spi.h>
#include <asm/bgcns.h>

#include "bgp_dma_tcp.h"

#include "bgp_bic_diagnosis.h"
#include "../bgp_network/bgdiagnose.h"

/* #define TRUST_TORUS_CRC */

#define SEND_SHORT_FRAMES_INLINE
#define ENABLE_TUNING

#define ENABLE_LEARNING_ADDRESSES

#if !defined(CONFIG_BLUEGENE_TCP_WITHOUT_NAPI)
/*  Select operation with linux 'dev->poll' */
#define TORNIC_DEV_POLL

/* #if defined(CONFIG_SMP) && !defined(CONFIG_BLUEGENE_UNIPROCESSOR) && !defined(CONFIG_BGP_VRNIC) */
/* #define TORNIC_STEAL_POLL_CORE */
/* #endif */

#endif


/* #define REQUIRES_DUMPMEM */

/* #if defined(CONFIG_BLUEGENE_TORUS_TRACE) */
/* int bgp_dma_tcp_tracemask=k_t_general|k_t_lowvol|k_t_irqflow|k_t_irqflow_rcv|k_t_protocol ; */
int bgp_dma_tcp_tracemask = k_t_init | k_t_request | k_t_error | k_t_congestion ; // | k_t_scattergather ;
/* int bgp_dma_tcp_tracemask = k_t_init | k_t_request | k_t_error | k_t_congestion |k_t_irqflow|k_t_irqflow_rcv; */
/* int bgp_dma_tcp_tracemask = 0xffffffff ; */
/* int bgp_dma_tcp_tracemask =  k_t_request | k_t_error ; */
/* #endif */

/* extern int sysctl_somaxconn ; // listening socket backlog, will want to increase this to allow at least 'n' SYNs per node in the block */
/* #define DEBUG_CLEAR_SKB */

//extern int bgp_dma_irq ;  /*  Interrupt number that the torus is using */

enum {
	k_fifo_irq = 124 ,  /*  Linux interrupt number for 'fifo threshold crossing' interrupt */
	k_rec_counter_irq = 132   /*  Linux interrupt number for 'reception counter hit zero' interrupt */
};

enum {
	k_find_source_of_rst_flags = 1 /* Whether to enable making a fuss about the source of a 'rst' frame */
};

#if defined(CONFIG_SMP) && !defined(CONFIG_BLUEGENE_UNIPROCESSOR)
#define TORNIC_TORUS_AFFINITY
#endif

enum {
  k_TorusAffinityCPU =
#if defined(TORNIC_TORUS_AFFINITY)
	  2
#else
	  0
#endif
};

extern cpumask_t cpu_nouser_map;   /*  Added to support 'steal' of core prior to long-running softirq */

int  __init
dma_tcp_module_init    (void);
/* void __exit dma_tcp_module_cleanup (void); */

/* module_init(dma_tcp_module_init); */
/* module_exit(dma_tcp_module_cleanup); */

#if defined(CONFIG_BGP_STATISTICS)
int rtt_histogram[33] ;
int transit_histogram[33] ;
#endif


MODULE_DESCRIPTION("BG/P sockets over torus DMA driver");
MODULE_LICENSE("GPL");


#define TCP_DMA_NAME  "tcp_bgp_dma"
#ifndef CTL_UNNUMBERED
#define CTL_UNNUMBERED -2
#endif

/*  Routines related to interrupt management from bgp_bic.c */
void bic_disable_irq(unsigned int irq) ;   /*  Intended to be called from a FLIH to indicate that this interrupt will not fire again */
void bic_set_cpu_for_irq(unsigned int irq, unsigned int cpu) ;  /*  Intended to indicate which core will take the next interrupt of this type. Doesn't explocitly enable but other async things may enable */
void bic_unmask_irq(unsigned int irq) ;  /*  Explicitly enable this interrupt */



#define ENABLE_TIMESTAMP_TRACKING
enum {
  k_FLIH_Entry ,
  k_FLIH_Exit ,
  k_SLIH_Entry ,
  k_SLIH_Exit ,
  k_Poll_Entry ,
  k_Poll_Exit ,
  k_Enable ,
  k_CouldEnable ,
  k_Quantity
};

static char *timestamp_names[] = {
    "k_FLIH_Entry" ,
    "k_FLIH_Exit" ,
    "k_SLIH_Entry" ,
    "k_SLIH_Exit" ,
    "k_Poll_Entry" ,
    "k_Poll_Exit" ,
    "k_Enable" ,
    "k_CouldEnable"
};

typedef struct {
  unsigned int hi ;
  unsigned int lo ;
} timestamp_t ;

#if defined(ENABLE_TIMESTAMP_TRACKING)
enum {
  k_TimestampRingSize = 8
};

typedef struct {
  unsigned int current_index ;
  timestamp_t timestamp[k_TimestampRingSize] ;
} timestamp_ring_t;

static timestamp_ring_t timestamp_ring[k_Quantity] ;
#endif

static void record_timestamp(unsigned int x)
  {
#if defined(ENABLE_TIMESTAMP_TRACKING)
    unsigned int tbhi = get_tbu();
    unsigned int tblo = get_tbl();
    unsigned int tbhi2 = get_tbu();
    unsigned int tblo2 = ( tbhi == tbhi2 ) ? tblo : 0 ;
    timestamp_ring_t *tr = timestamp_ring+x ;
    unsigned int cx=tr->current_index ;
    unsigned int cxm=cx&(k_TimestampRingSize-1) ;
    tr->timestamp[cxm].hi = tbhi2 ;
    tr->timestamp[cxm].lo = tblo2 ;
    TRACEN(k_t_detail,"Timestamp %s[%d] = 0x%08x%08x",timestamp_names[x],cx,tbhi2,tblo2) ;
    tr->current_index=cx+1 ;
#endif
  }

static void show_timestamps(void)
  {
#if defined(ENABLE_TIMESTAMP_TRACKING)
    int x ;
    TRACEN(k_t_detail,"(>)") ;
    for(x=0;x<k_Quantity;x+=1)
      {
        timestamp_ring_t *tr = timestamp_ring+x ;
        unsigned int cx=tr->current_index ;
        int q ;
        for(q=-k_TimestampRingSize;q<0 ; q+=1)
          {
            unsigned int cxm=(cx+q)&(k_TimestampRingSize-1) ;
            TRACEN(k_t_request,"Timestamp %s[%03d] = 0x%08x%08x",timestamp_names[x],q,tr->timestamp[cxm].hi,tr->timestamp[cxm].lo) ;
          }
      }
    TRACEN(k_t_detail,"(<)") ;
#endif
  }

static void init_tuning(dma_tcp_t *dma_tcp)
  {
#if defined(CONFIG_BLUEGENE_TCP)
    dma_tcp->bluegene_tcp_is_built = 1 ;
#else
    dma_tcp->bluegene_tcp_is_built = 0 ;
#endif
    dma_tcp->tuning_num_packets = 0x7fffffff ;  /*  up from '1', used 16 at one time */
#if defined(KEEP_TCP_FLAG_STATS)
    dma_tcp->tcp_received_flag_count[0] = 0 ;
    dma_tcp->tcp_received_flag_count[1] = 0 ;
    dma_tcp->tcp_received_flag_count[2] = 0 ;
    dma_tcp->tcp_received_flag_count[3] = 0 ;
    dma_tcp->tcp_received_flag_count[4] = 0 ;
    dma_tcp->tcp_received_flag_count[5] = 0 ;
    dma_tcp->tcp_received_flag_count[6] = 0 ;
    dma_tcp->tcp_received_flag_count[7] = 0 ;
#endif
#if defined(TORNIC_DEV_POLL)
#if defined(TORNIC_STEAL_POLL_CORE)
     /*     dma_tcp->tuning_num_empty_passes = 1000000 ; // Try 1 second 'spin' if no data coming */
    dma_tcp->tuning_num_empty_passes = 5000 ;  /*  Try 5 millisecond 'spin' if no data coming if we have a whole core for it */
    dma_tcp->tuning_non_empty_poll_delay = 850 ;
#else
     /*  Sharing a core, but with 'poll' NAPI */
    dma_tcp->tuning_num_empty_passes = 1 ;  /*  Try 10 microsecond 'spin' if no data coming if we are sharing core with app */
    dma_tcp->tuning_non_empty_poll_delay = 1 ;
#endif
#else
     /*  'interrupts' NAPI */
    dma_tcp->tuning_num_empty_passes = 1 ;  /*  Try 10 microsecond 'spin' if no data coming if we are sharing core with app */
    dma_tcp->tuning_non_empty_poll_delay = 1 ;
#endif
    dma_tcp->tuning_poll_after_enabling = 1 ;  /*  changed from 0 on 20080619 */
    dma_tcp->tuning_run_handler_on_hwi = 0 ;  /*  was 1 */
    dma_tcp->tuning_clearthresh_slih = 1 ;  /*  = 0 , whether to clear the 'threshold crossed' bit in the slih */
    dma_tcp->tuning_clearthresh_flih = 0 ;  /*  = 0 , whether to clear the 'threshold crossed' bit in the flih */
    dma_tcp->tuning_disable_in_dcr = 1 ;  /*  = 1, whether to toggle the DCR interrupt enable/disable */
    dma_tcp->tuning_exploit_reversepropose = 1 ;  /*  which way to run the propose/accept protocol */
    dma_tcp->tuning_counters_per_source = 0 ;  /*  Max reception counters to commit per source node (0 indicates to use 'shareout' algorithm */
    dma_tcp->tuning_min_icsk_timeout = 200 ;  /*  Push TCP timeout on torus up to 200 jiffies, we think we have a reliable network ... */
    dma_tcp->tuning_injection_hashmask = 3 ;  /*  = 3, whether to mask down the number of injection fifos per direction */
    dma_tcp->tuning_virtual_channel = k_VC_anyway ; /* Select adaptive routing at boot time */
  }

dma_tcp_t dma_tcp_state ;


/* void __exit */
/* dma_tcp_module_cleanup (void) */
/* { */
//   /*  nothing to do */
/* } */



/* #if defined(CONFIG_BLUEGENE_TCP) */
#if 1
static int bgp_dma_tcp_poll(dma_tcp_t *) ;
static int bgp_dma_tcp_poll(dma_tcp_t *dma_tcp)
{
/*  Values when I inherited the code, now taken from 'tuning params' */
/*   int num_packets = 1; // received packets one by one */
/*   int num_empty_passes = 512; */
/*   int non_empty_poll_delay = 850; */
/*  Other values I have tried */
/*   int num_packets = 100; */
/*   int num_empty_passes = 0; */
/*   int non_empty_poll_delay = 0; */
/*   int num_packets = 100; // received packets 100 at a time */
/*   int num_empty_passes = 5; */
/*   int non_empty_poll_delay = 10; */
/*   dumpmem(dma_tcp_state.receptionFIFO,128,"Reception memory FIFO") ; */

  int ret ;
  TRACEN(k_t_irqflow, "(>) tuning_num_packets=%d tuning_num_empty_passes=%d tuning_non_empty_poll_delay=%d",
      dma_tcp->tuning_num_packets,dma_tcp->tuning_num_empty_passes,dma_tcp->tuning_non_empty_poll_delay );
  dma_tcp->device_stats = bgtornet_stats() ;
  ret = DMA_RecFifoPollNormalFifoById( dma_tcp->tuning_num_packets,
               recFifoId,
               dma_tcp->tuning_num_empty_passes,
               dma_tcp->tuning_non_empty_poll_delay,
               dma_tcp->recFifoGroup,
               bgp_dma_tcp_empty_fifo_callback);
  touch_softlockup_watchdog() ;  /*  If we get a continuous stream of packets, we do not really want the softlockup watchdog to bark */
  TRACEN(k_t_irqflow, "(<) ret=%d",ret );
/*   ASSERT( ret >= 0 ); */
  return ret;
}


static void recfifo_disable(void)
  {
    TRACEN(k_t_detail,"(><)") ;
    mtdcrx(0xd71+dma_tcp_ReceptionFifoGroup(&dma_tcp_state),0) ;
  }

static void recfifo_enable(void)
  {
    TRACEN(k_t_detail,"(><)") ;
    record_timestamp(k_Enable) ;
    mtdcrx(0xd71+dma_tcp_ReceptionFifoGroup(&dma_tcp_state),0x80000000 >> (8*dma_tcp_ReceptionFifoGroup(&dma_tcp_state))) ;
  }

static void reccounter_disable(void)
  {
    TRACEN(k_t_detail,"(><)") ;
    mtdcrx(0xd7a+dma_tcp_ReceptionCounterGroup(&dma_tcp_state),0) ;
  }

static void reccounter_enable(void)
  {
    TRACEN(k_t_detail,"(><)") ;
    record_timestamp(k_Enable) ;
    mtdcrx(0xd7a+dma_tcp_ReceptionCounterGroup(&dma_tcp_state),0xffffffff) ;
  }

static void dma_tcp_slih_handler(unsigned long dummy)
  {
    int ret;
    dma_tcp_t *dma_tcp = &dma_tcp_state ;
    unsigned int is_up=dma_tcp->is_up ;
    record_timestamp(k_SLIH_Entry) ;

    TRACEN(k_t_irqflow,"(>)" );
    enable_kernel_fp() ;
     /*  Clear the 'threshold crossed' flag so we don't automatically reinterrupt */
    DMA_RecFifoSetClearThresholdCrossed( dma_tcp_state.recFifoGroup,
                 0x80000000 >> (8*dma_tcp_ReceptionFifoGroup(&dma_tcp_state)),
                 0 );
    ret = bgp_dma_tcp_poll(dma_tcp);
#if defined(HAS_MISSED_INTERRUPT_TIMER)
    if(is_up)
      {
        mod_timer(&dma_tcp->torus_missed_interrupt_timer, jiffies+200) ;  /*  Cause timer interrupt after 2000ms if things don't stay alive ... temp while diagnosing problem ... */
      }
#endif
    record_timestamp(k_SLIH_Exit) ;
#if !defined(TORNIC_DEV_POLL)
    recfifo_enable() ;
    if(is_up)
      {
        reccounter_enable() ;
      }
#endif
    TRACEN(k_t_irqflow,"(<)" );
  }

static void trip_missed_interrupt(dma_tcp_t *dma_tcp)
{
	unsigned int fifo_dcr = mfdcrx(0xd71) ;
	unsigned int counter_dcr = mfdcrx(0xd7a) ;
	struct bic_regs * bic_regs = bic.regs ;
	unsigned int target_2_3 = bic_regs->group[2].target[3] ;
	unsigned int target_3_0 = bic_regs->group[3].target[0] ;
	unsigned int notEmpty = DMA_RecFifoGetNotEmpty(dma_tcp->recFifoGroup,0) ;
	unsigned int thresholdCrossed = DMA_RecFifoGetThresholdCrossed(dma_tcp->recFifoGroup,0) ;
	if( fifo_dcr != 0x80000000 || counter_dcr != 0xffffffff || target_2_3 != 0x00006000 || target_3_0 != 0x00006000 || notEmpty != 0 )
		{
			TRACEN(k_t_general,"maybe missed interrupt fifo_dcr=0x%08x counter_dcr=0x%08x target_2_3=0x%08x target_3_0=0x%08x notEmpty=0x%08x thresholdCrossed=0x%08x",
					fifo_dcr,counter_dcr,target_2_3,target_3_0,notEmpty,thresholdCrossed) ;
			dma_tcp_slih_handler(0) ;
		}
}
#if defined(HAS_MISSED_INTERRUPT_TIMER)
static void dma_tcp_missed_interrupt(unsigned long dummy)
{
	    dma_tcp_t *dma_tcp = &dma_tcp_state ;
	    unsigned int is_up=dma_tcp->is_up ;
	TRACEN(k_t_irqflow,"(>) is_up=%d",is_up) ;
	if(is_up )
	  {
      trip_missed_interrupt(dma_tcp) ;
      mod_timer(&dma_tcp->torus_missed_interrupt_timer, jiffies+10) ;  /*  Cause timer interrupt after 100ms if things don't stay alive ... temp while diagnosing problem ... */
	  }
	TRACEN(k_t_irqflow,"(<)") ;
}
#endif
static volatile int dma_ticket_req ;
static volatile int dma_ticket_ack ;

void dma_tcp_poll_handler(void)
  {
    int cur_ticket_req = dma_ticket_req ;
    record_timestamp(k_Poll_Entry) ;

    dma_ticket_ack = cur_ticket_req ;
    TRACEN(k_t_irqflow,"dma_tcp_poll_handler: cur_ticket_req=%d (>)",cur_ticket_req );
    dma_tcp_slih_handler(0) ;
    TRACEN(k_t_irqflow,"dma_tcp_poll_handler: cur_ticket_req=%d (<)",cur_ticket_req );
    record_timestamp(k_Poll_Exit) ;
  }

void dma_tcp_rx_enable(void)
  {
	  unsigned long flags ;
    TRACEN(k_t_irqflow,"(>)" );
    record_timestamp(k_CouldEnable) ;
    recfifo_enable() ;
    reccounter_enable() ;
    bic_set_cpu_for_irq(k_fifo_irq+dma_tcp_ReceptionFifoGroup(&dma_tcp_state),k_TorusAffinityCPU) ;
    bic_set_cpu_for_irq(k_rec_counter_irq+dma_tcp_ReceptionCounterGroup(&dma_tcp_state),k_TorusAffinityCPU) ;
     /*  Both interrupts unmasked before we take one to avoid the chance of an interrupt after the first */
     /*   which (?) could go round the loop and 'do the wrong thing' with respect to napi and enabling the second */
     /*   while trying to run the napi poll */
    local_irq_save(flags) ;
    bic_unmask_irq(k_fifo_irq+dma_tcp_ReceptionFifoGroup(&dma_tcp_state)) ;
    bic_unmask_irq(k_rec_counter_irq+dma_tcp_ReceptionCounterGroup(&dma_tcp_state)) ;
    local_irq_restore(flags) ;
     /*  If we get here and there's an 'interrupt cause' in the DCRs, we have missed an interrupt. Trace it and fire the SLIH. */
    trip_missed_interrupt(&dma_tcp_state ) ;
    TRACEN(k_t_irqflow,"(<)" );

  }

static DECLARE_TASKLET(dma_tcp_slih, dma_tcp_slih_handler,0) ;

/*  This gets driven in the FLIH when a DMA interrupt occurs */
static void receiveFLIH(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
  TRACEN(k_t_irqflow,"(>) FLIH dma_tcp_state.active_quarter=%i",dma_tcp_state.active_quarter );
  record_timestamp(k_FLIH_Entry) ;
  bic_disable_irq(k_fifo_irq+dma_tcp_ReceptionFifoGroup(&dma_tcp_state)) ;
  bic_disable_irq(k_rec_counter_irq+dma_tcp_ReceptionCounterGroup(&dma_tcp_state)) ;
  bgtornet_rx_schedule() ;
  record_timestamp(k_FLIH_Exit) ;
  TRACEN(k_t_irqflow,"(<) FLIH" );
}

static void receiveCommHandler(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
	TRACEN(k_t_irqflow,"(>)" );
        recfifo_disable() ;
	receiveFLIH(arg1,arg2,arg3,arg4) ;
	TRACEN(k_t_irqflow,"(<)" );
}

/*  This gets driven in the FLIH when a DMA interrupt occurs */
static void receiveCounterZeroHandler(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
	TRACEN(k_t_irqflow,"(>)" );
        reccounter_disable() ;
	receiveFLIH(arg1,arg2,arg3,arg4) ;
	TRACEN(k_t_irqflow,"(<)" );
}


static int unknownActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           )
  {
    unsigned int SW_Arg __attribute__ ((unused)) =packet_ptr->SW_Arg ;
    unsigned int Func_Id __attribute__ ((unused)) =packet_ptr->Func_Id ;
    unsigned int x __attribute__ ((unused)) =SW_Arg >> 16 ;
    unsigned int y __attribute__ ((unused)) =( SW_Arg >> 8) & 0xff ;
    unsigned int z __attribute__ ((unused)) =SW_Arg & 0xff ;
    TRACEN(k_t_error,"(!!!) %08x %02x (%02x,%02x,%02x) payload_ptr=%p payload_bytes=%d", SW_Arg,Func_Id,x,y,z,payload_ptr, payload_bytes );
    return 0 ;
  }

/* static char reception_fifo_buffer[k_desired_reception_memory_fifo_size] __attribute__ ((__aligned__(32))) ; */
/*  We need a reception FIFO; we are prepared to compromise on its size */
static void __init
dma_tcp_setup_reception_fifo(dma_tcp_t *dma_tcp)
  {
    unsigned int allocation_size=k_desired_reception_memory_fifo_size ;
    void * allocation_address=local_permanent_alloc(k_desired_reception_memory_fifo_size) ;
    dma_tcp->receptionfifo = allocation_address ;
    dma_tcp->receptionfifoSize = allocation_size ;
     /*  Must get a memory FIFO area, and it must be L1-aligned */
    BUG_ON(allocation_address == NULL) ;
    BUG_ON(0 != (0x1f & (int)allocation_address)) ;
    if( allocation_address != NULL )
      {
        memset(allocation_address, 0xcc, allocation_size) ;
      }
    TRACEN(k_t_init,"reception_fifo address=%p length=%d=0x%08x",allocation_address,allocation_size,allocation_size) ;
  }

#endif


//void __init
//bgp_fpu_register_memcpy_sysctl(void) ;

enum
{
	k_enable_dma_memcpy = 1
} ;

//int bluegene_globalBarrier_nonBlocking(unsigned int channel, int reset, unsigned int timeoutInMillis) ;
//
//extern unsigned long long printk_clock_aligner ;
///* Determine the offset of the 'local' timebase from a 'common' time signal as per global barrier */
//static void __init
//align_timebase(void)
//  {
//    int rc0 ;
//    int rc1 = -1 ;
//    unsigned long flags ;
//    unsigned long long tb ;
//    local_irq_save(flags) ;
//    rc0 = bluegene_globalBarrier_nonBlocking(3,1,1000 ) ;
//    if( rc0 == BGCNS_RC_CONTINUE ) rc1 = bluegene_globalBarrier_nonBlocking(3,0,1000 ) ;
//    tb = get_tb() ;
//    printk_clock_aligner = tb ;
//    TRACEN(k_t_init,"rc0=%d rc1=%d tb=0x%016llx",rc0,rc1,tb) ;
//    local_irq_restore(flags) ;
//  }

static void __init
dma_tcp_init(dma_tcp_t *dma_tcp, BGP_Personality_t *pers)
  {
    int compute_node_count = pers->Network_Config.Xnodes*pers->Network_Config.Ynodes*pers->Network_Config.Znodes ;
    int i_am_compute_node= ( pers->Network_Config.Rank != pers->Network_Config.IOnodeRank ) ;
    TRACEN(k_t_init,"(>) PAGE_SHIFT=%d PAGE_SIZE=%lu", PAGE_SHIFT, PAGE_SIZE );
//    bgp_fpu_register_memcpy_sysctl() ;
    init_tuning(dma_tcp) ;
    dma_tcp->location.coordinate[0] = pers->Network_Config.Xcoord;
    dma_tcp->location.coordinate[1] = pers->Network_Config.Ycoord;
    dma_tcp->location.coordinate[2] = pers->Network_Config.Zcoord;
    dma_tcp->extent.coordinate[0]  = pers->Network_Config.Xnodes;
    dma_tcp->extent.coordinate[1]  = pers->Network_Config.Ynodes;
    dma_tcp->extent.coordinate[2]  = pers->Network_Config.Znodes;
    dma_tcp->configured_quarter = 0 ;
    dma_tcp->node_count = compute_node_count ;
    dma_tcp->node_slot_mask = (compute_node_count )-1 ;

    dma_tcp->SW_Arg = (pers->Network_Config.Xcoord << 16)
                   | (pers->Network_Config.Ycoord << 8)
                   | (pers->Network_Config.Zcoord) ;
    dma_tcp->src_key = dma_tcp->location.coordinate[0]*dma_tcp->extent.coordinate[1]*dma_tcp->extent.coordinate[2]
                      +dma_tcp->location.coordinate[1]*dma_tcp->extent.coordinate[2]
                      +dma_tcp->location.coordinate[2] ;

    dma_tcp->xbits = fls(pers->Network_Config.Xnodes)-1 ;
    dma_tcp->ybits = fls(pers->Network_Config.Ynodes)-1 ;
    dma_tcp->zbits = fls(pers->Network_Config.Znodes)-1 ;
    /* YKT BGP seems wired so that no partition less than 8x8x8 is a torus in any dimension */
    dma_tcp->is_torus_x = (pers->Network_Config.Xnodes >= 8 && pers->Network_Config.Ynodes >= 8 && pers->Network_Config.Znodes >= 8) ;
    dma_tcp->is_torus_y = dma_tcp->is_torus_x ;
    dma_tcp->is_torus_z = dma_tcp->is_torus_x ;
    dma_tcp->block_id = pers->Network_Config.BlockID & 0x00ffffff ;
    dma_tcp->i_am_compute_node = i_am_compute_node ;
    TRACEN(k_t_init,"SW_Arg=0x%08x rank=%d=0x%08x src_key=0x%08x xbits=%d ybits=%d zbits=%d ",
		    dma_tcp->SW_Arg, pers->Network_Config.Rank, pers->Network_Config.Rank, dma_tcp->src_key,
		    dma_tcp->xbits,dma_tcp->ybits,dma_tcp->zbits );

    if( 0 == dma_tcp->mtu)
      {
        bgp_dma_tcp_set_mtu(dma_tcp, 64996) ;
      }

#if defined(TORUS_RECEIVE_WITH_SLIH)
#else
    skb_queue_head_init(&dma_tcp->skb_pool) ;
    skb_queue_head_init(&dma_tcp->skb_list_free) ;
#endif
    {
	    int core ;
	    for( core=0; core<k_injecting_cores; core += 1)
		    {
			    int desired_fifo ;
			    for(desired_fifo=0;desired_fifo<k_injecting_directions;desired_fifo+=1)
			    spin_lock_init(&dma_tcp->dirInjectionLock[core*k_injecting_directions+desired_fifo]) ;
		    }
    }

#if defined(TORUS_RECEIVE_WITH_SLIH)
#else
    tasklet_schedule(&pool_filler_slih) ;
#endif

#if defined(CONFIG_BLUEGENE_TCP)
     /*  Only compute nodes are torus-capable ... */
    if( pers->Network_Config.Rank != pers->Network_Config.IOnodeRank )
      {
        dma_tcp_setup_reception_fifo(dma_tcp) ; // Need this 'early' (before ifup) in case of needing to allocate a lot of physically contiguous memory
#if defined(HAS_MISSED_INTERRUPT_TIMER)
        setup_timer(&dma_tcp->torus_missed_interrupt_timer,dma_tcp_missed_interrupt,0) ;
#endif
        dma_tcp_frames_init(dma_tcp) ;
      }
#endif
    dma_tcp_devfs_procfs_init(dma_tcp) ;
    TRACEN(k_t_init,"(<)" );
  }

void dma_tcp_ifup(dma_tcp_t *dma_tcp, BGP_Personality_t *pers)
  {
    TRACEN(k_t_init,"(>)" );
#if defined(CONFIG_BLUEGENE_TCP)
     /*  Only compute nodes are torus-capable ... */
    if( pers->Network_Config.Rank != pers->Network_Config.IOnodeRank )
      {
        dma_tcp->active_quarter = dma_tcp->configured_quarter & 3 ;
        dma_tcp->is_up = 1 ;
//        align_timebase() ;
        {
    int subX ;
    for(subX=0;subX<DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP;subX +=1)
      {
        dma_tcp->injCntrSubgrps[ subX ] = subX ;
        dma_tcp->recCntrSubgrps[ subX ] = subX ;
      }
        }

       /*  register a receive function for 'unrecognised' memfifo packets */
        DMA_RecFifoRegisterRecvFunction(unknownActor, dma_tcp, 1, 0);

        dma_tcp->recMap.threshold[0] = dma_tcp->receptionfifoSize/16;    /*  generate interrupts when anything is in the fifo */
        {
          int i ;
          for(i=0;i<4;i+=1)
            {
              int j ;
              for(j=0;j<7;j+=1)
                {
                  dma_tcp->recMap.ts_rec_map[i][j] = 8*dma_tcp_ReceptionFifoGroup(dma_tcp) ;
                }
            }
        }
        {
            int ret  __attribute__ ((unused)) = DMA_RecFifoSetMap( &dma_tcp->recMap );  /*  fifo 0 will receive packets from everywhere */

            TRACEN(k_t_init,"(=)DMA_RecFifoSetMap rc=%d", ret );
        }
       /*  Register functions for 'frames' style access */
        dma_tcp_frames_ifup(dma_tcp) ;

         /*  set up rec fifo group */
        dma_tcp->recFifoGroup = DMA_RecFifoGetFifoGroup( dma_tcp_ReceptionFifoGroup(dma_tcp), 0, receiveCommHandler, NULL, NULL, NULL, NULL );


        TRACEN(k_t_init,"(=)DMA_RecFifoGetFifoGroup dma_tcp->recFifoGroup=%p", dma_tcp->recFifoGroup );

         /*  initalize rec fifo */
        {
        int ret  __attribute__ ((unused)) = DMA_RecFifoInitById ( dma_tcp->recFifoGroup,
            recFifoId,
            dma_tcp->receptionfifo,                 /*  fifo start */
            dma_tcp->receptionfifo,                 /*  fifo head */
            dma_tcp->receptionfifo+dma_tcp->receptionfifoSize    /*  fifo end */
                                );
        TRACEN(k_t_init,"(=)DMA_RecFifoInitById rc=%d", ret );
        }
        TRACEN(k_t_general, "(=)(I) testdma: CounterGroupAllocate");

        {
         /*  Initialize injection counter group */
        int ret  __attribute__ ((unused)) = DMA_CounterGroupAllocate( DMA_Type_Injection,
                              dma_tcp_InjectionCounterGroup(dma_tcp),  /*  group number */
                              DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP,
                              dma_tcp->injCntrSubgrps,
                              0,   /*  target core for interrupts */
                              NULL,
                              NULL,
                              NULL,
                              & dma_tcp->injCounterGroup );

        TRACEN(k_t_init,"(=)DMA_CounterGroupAllocate rc=%d", ret );
        }
        memset(dma_tcp->inj_skbs,0,DMA_NUM_COUNTERS_PER_GROUP*sizeof(struct sk_buff *)) ;

         /*  enable the counter */
        {
          int ret;
          DMA_CounterSetEnableById( & dma_tcp->injCounterGroup,0) ;
          ret=DMA_CounterSetValueWideOpenById ( & dma_tcp->injCounterGroup,0,0xffffffff) ;
          TRACEN(k_t_general, "(=)(I) testdma: DMA_CounterSetValueWideOpenById ret=%d",ret) ;

        }

#if defined(CONFIG_WRAP_COPY_TOFROM_USER) && defined(CONFIG_BLUEGENE_DMA_MEMCPY)
         /*  TODO: Investigate why 'dma_memcpy' needed to be initialised before 'dma_tcp counters' */
        if( k_enable_dma_memcpy)  bgp_dma_memcpyInit(dma_tcp) ;
#endif
       {
          /*  Initialize reception counter group */
         int ret  __attribute__ ((unused)) = DMA_CounterGroupAllocate( DMA_Type_Reception,
             dma_tcp_ReceptionCounterGroup(dma_tcp),  /*  group number */
             DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP,
                               dma_tcp->recCntrSubgrps,
                               k_TorusAffinityCPU,   /*  target core for interrupts */
                               receiveCounterZeroHandler,
                               NULL,
                               NULL,
                               & dma_tcp->recCounterGroup );
         TRACEN(k_t_init,"(=)DMA_CounterGroupAllocate rc=%d", ret );
       }
       memset(dma_tcp->recCntrInUse,0,DMA_NUM_COUNTERS_PER_GROUP) ;
       memset(dma_tcp->rcv_skbs,0,DMA_NUM_COUNTERS_PER_GROUP*sizeof(struct sk_buff *)) ;
       dma_tcp->qtyFreeRecCounters = 64 ;
       dma_tcp->scanRecCounter = 0 ;
       dma_tcp->framesDisposed = 0 ;
       atomic_set(&dma_tcp->framesProposed, 0 ) ;
      }
#endif
    TRACEN(k_t_init,"(<)" );
  }

// Currently there is no implementation of Kernel_CounterGroupFree , so we cannot free off the hardware used by the eth-on-torus
enum {
  k_has_counter_group_free = 0
};
static void dma_tcp_ifdown(dma_tcp_t *dma_tcp)
  {
    TRACEN(k_t_init,"(>)" );
    dma_tcp->is_up = 0 ;
    dma_tcp_frames_ifdown(dma_tcp) ;
    if( k_has_counter_group_free)
      {
        {
           /*  Free reception counter group */
          int ret  __attribute__ ((unused)) = DMA_CounterGroupFree(
              dma_tcp_ReceptionCounterGroup(dma_tcp),  /*  group number */
              DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP,
                                dma_tcp->recCntrSubgrps,
                                & dma_tcp->recCounterGroup );
          TRACEN(k_t_init,"(=)DMA_CounterGroupFree rc=%d", ret );
        }
        /*  disable the injection counter */
       {
         DMA_CounterSetDisableById( & dma_tcp->injCounterGroup,0) ;
       }
       {
        /*  Free injection counter group */
       int ret  __attribute__ ((unused)) = DMA_CounterGroupFree(
                             dma_tcp_InjectionCounterGroup(dma_tcp),  /*  group number */
                             DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP,
                             dma_tcp->injCntrSubgrps,
                             & dma_tcp->injCounterGroup );

       TRACEN(k_t_init,"(=)DMA_CounterGroupFree rc=%d", ret );
       }
      }
    else
      {
        TRACEN(k_t_request,"(!!!) No implementation of counter group free") ;
      }
    TRACEN(k_t_init,"dma_tcp->tuning_prep_dcmf=%d",dma_tcp->tuning_prep_dcmf) ;
    if( dma_tcp->tuning_prep_dcmf)
      {
        TRACEN(k_t_init,"Getting ready for DCMF use of torus") ;
        memset(&dma_tcp->recMap,0,sizeof(dma_tcp->recMap)) ;
      }

    TRACEN(k_t_init,"(<)" );
  }
void bgp_torus_set_mtu(unsigned int mtu)
  {
    bgp_dma_tcp_set_mtu(&dma_tcp_state, mtu) ;
  }

int __init
dma_tcp_module_init(void)
{
  int ret = 0;

  BGP_Personality_t pers;

  bluegene_getPersonality(&pers, sizeof(pers));

  dma_tcp_init(&dma_tcp_state, &pers) ;

 TRACEN(k_t_init, "(I)initDMA finished ret:%d",ret);
  return ret;
}

static void fix_retransmit_timeout(struct sk_buff *skb)
{
	dma_tcp_t *dma_tcp = &dma_tcp_state ;
	struct sock *sk = skb->sk ;
	if( sk)
	  {
      unsigned int family=sk->sk_family ;
      struct inet_sock *inet = inet_sk(sk) ;
      struct inet_connection_sock *icsk = inet_csk(sk) ;
      int is_icsk = inet->is_icsk ;
      TRACEN(k_t_detail,"skb=%p sk=%p sk_family=0x%04x is_icsk=%d",skb,sk,family,is_icsk) ;
      if( AF_INET == family && is_icsk )
        {
          TRACEN(k_t_detail,"icsk_timeout-jiffies=%lu icsk_rto=%u",icsk->icsk_timeout-jiffies,icsk->icsk_rto) ;
          if( icsk->icsk_rto < dma_tcp->tuning_min_icsk_timeout )
            {
              icsk->icsk_rto=dma_tcp->tuning_min_icsk_timeout ;
            }
        }
	  }
}


int bgp_dma_tcp_send_and_free( struct sk_buff *skb )
{
	int rc ;
	if( k_find_source_of_rst_flags && dma_tcp_state.tuning_diagnose_rst )
		{
			struct ethhdr *eth = (struct ethhdr *)skb->data;
		        unsigned int h_proto =  eth->h_proto ;
			if( ETH_P_IP == h_proto )
				{
				        struct iphdr *iph = (struct iphdr *)(eth+1) ;
				        if(IPPROTO_TCP == iph->protocol )
				        	{
				        		struct tcphdr *tcph = (struct tcphdr *)(iph+1) ;
				        		if( tcph->rst)
				        			{
				        				TRACEN(k_t_request,"RST on frame to [%02x:%02x:%02x]",
				        						eth->h_dest[3],eth->h_dest[4],eth->h_dest[5]) ;
				        				show_stack(0,0) ; /* Stack back-chain may help explain why it was sent */

				        			}
				        	}

				}

		}
	fix_retransmit_timeout(skb) ;
	rc = bgp_dma_tcp_send_and_free_frames(skb) ;
	return rc ;
}

/*  Test if we think a socket is affected by torus congestion. Do this by looking to see if anything is in any software transmit FIFO */
unsigned int bgp_torus_congestion(struct sock *sk)
  {
    unsigned int core ;
    unsigned int direction ;
    struct inet_connection_sock *icskp = inet_csk(sk) ;
    struct inet_sock *inet = inet_sk(sk);
    unsigned int daddr=inet->daddr ;
    dma_tcp_t *dma_tcp=&dma_tcp_state ;
    struct sk_buff *skb = skb_peek(&sk->sk_write_queue) ;

    if( dma_tcp->i_am_compute_node
        )
      {
        if( NULL == skb )
          {
            TRACEN(k_t_congestion,"sk=%p skb=%p data=%p len=%d flags=0x%02x ip=%u.%u.%u.%u icsk_retransmits=%d icsk_rto=%d q-empty-retransmit",
                sk, skb, skb->data, skb->len, TCP_SKB_CB(skb)->flags,
                daddr>>24, (daddr>>16)&0xff,(daddr>>8)&0xff,daddr&0xff,
                icskp->icsk_retransmits, icskp->icsk_rto
                ) ;
            return 0 ;
          }
        if( 0 == skb->len)
          {
            TRACEN(k_t_general,"sk=%p skb=%p data=%p len=%d flags=0x%02x ip=%u.%u.%u.%u icsk_retransmits=%d icsk_rto=%d ack-transmit",
                sk, skb, skb->data, skb->len, TCP_SKB_CB(skb)->flags,
                daddr>>24, (daddr>>16)&0xff,(daddr>>8)&0xff,daddr&0xff,
                icskp->icsk_retransmits, icskp->icsk_rto
                ) ;
            return 0 ;
          }
#if defined(USE_SKB_TO_SKB)
        {
		unsigned int framesProposed=atomic_read(&dma_tcp->framesProposed) ;
		unsigned int framesDisposed=dma_tcp->framesDisposed ;
		if( framesProposed != framesDisposed)
			{
				TRACEN(k_t_general,
					    "sk=%p skb=%p data=%p len=%d flags=0x%02x ip=%u.%u.%u.%u propose=0x%08x disp=0x%08x\n",
				    sk, skb, skb->data, skb->len, TCP_SKB_CB(skb)->flags,
				    daddr>>24, (daddr>>16)&0xff,(daddr>>8)&0xff,daddr&0xff,
				    framesProposed,framesDisposed
				    ) ;
			      return 1 ;

			}
        }
#endif
        for( core=0; core<k_injecting_cores; core += 1)
           {
             for( direction=0;direction<k_injecting_directions; direction+=1)
                {
                  unsigned int  fifo_current_head =
                   (unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+direction]) ;
                  unsigned int  fifo_current_tail =
                   (unsigned int) DMA_InjFifoGetTailById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+direction]) ;
                if( fifo_current_head != fifo_current_tail)
                  {
                    TRACEN(k_t_general,
                		    "sk=%p skb=%p data=%p len=%d flags=0x%02x ip=%u.%u.%u.%u core=%d direction=%d fifo_current_head=0x%08x fifo_current_tail=0x%08x\n",
                        sk, skb, skb->data, skb->len, TCP_SKB_CB(skb)->flags,
                        daddr>>24, (daddr>>16)&0xff,(daddr>>8)&0xff,daddr&0xff,
                        core,direction,
                        fifo_current_head,fifo_current_tail
                        ) ;
                  return 1 ;
                  }
              }
           }
      }

    TRACEN(k_t_congestion,"sk=%p skb=%p data=%p len=%d flags=0x%02x ip=%u.%u.%u.%u icsk_retransmits=%d icsk_rto=%d retransmit",
        sk, skb, skb->data, skb->len, TCP_SKB_CB(skb)->flags,
        daddr>>24, (daddr>>16)&0xff,(daddr>>8)&0xff,daddr&0xff,
        icskp->icsk_retransmits, icskp->icsk_rto
        ) ;
/*     if( icskp->icsk_rto < 300) */
/* 	    { */
/* 		    icskp->icsk_rto = icskp->icsk_rto << 1 ; */
/* 		    return 1 ; */
/* 	    } */
    return 0 ;
  }

void analyse_retransmit(struct sock *sk, struct sk_buff *skb)
  {
    if( skb && skb->len>0 )           /*  Need a SKB,and if len=0 then it's an ACK with no data */
      {
        struct inet_sock *inet = inet_sk(sk);
        struct inet_connection_sock *icsk = inet_csk(sk);
        unsigned int daddr=inet->daddr ;
        unsigned int daddr_b0 = daddr >> 24 ;
        if( daddr_b0 == 11 || daddr_b0 == 12 )  /*  BGP fabric is 11.*.*.* and 12.*.*.* , only interested in those */
          {
            TRACEN(k_t_congestion,"(I) sk=%p skb=%p data=%p len=%d flags=0x%02x ip=%u.%u.%u.%u icsk_retransmits=%d icsk_rto=%d resending (BGP)",
                sk, skb, skb->data, skb->len, TCP_SKB_CB(skb)->flags,
                daddr>>24, (daddr>>16)&0xff,(daddr>>8)&0xff,daddr&0xff,icsk->icsk_retransmits, icsk->icsk_rto) ;
          }
      }

  }


/*  Seem to have picked up a half-implemented feature. Dummy it. */
DMA_CounterAppSegment_t *DMA_CounterAppSegmentArray;
int DMA_CounterInitAppSegments(void) { return 0 ; }

void dma_tcp_set_port(unsigned int port)   // Intended for configuring which quarter of the BGP DMA unit to use
  {
    TRACEN(k_t_request,"(><) port=0x%08x",port) ;
    if( port > 0)
      {
        dma_tcp_state.configured_quarter = (port-1) & 3 ;
      }
  }
void dma_tcp_open(void)  // 'ifconfig up' handler
  {
    BGP_Personality_t pers;
    TRACEN(k_t_request,"(>) ifconfig up") ;
    bluegene_getPersonality(&pers, sizeof(pers));
    dma_tcp_ifup(&dma_tcp_state, &pers) ;
    TRACEN(k_t_request,"(<) ifconfig up") ;
  }
void dma_tcp_close(void) // 'ifconfig down' handler
  {
    TRACEN(k_t_request,"(>) ifconfig down") ;
    dma_tcp_ifdown(&dma_tcp_state) ;
    TRACEN(k_t_request,"(<) ifconfig down") ;
  }

void set_siw_placement_callback(dma_addr_t (*siw_placement_callback)(struct sk_buff *skb))
  {
    TRACEN(k_t_init,"siw_placement_callback=%p",siw_placement_callback) ;
#if defined(ENABLE_SIW_PLACEMENT)
    dma_tcp_state.siw_placement_callback=siw_placement_callback ;
#endif
  }
EXPORT_SYMBOL(set_siw_placement_callback) ;
void show_personality(void) ;
void show_sprs(void) ;
/*  Issue a diagnostic op at the DMA layer */
void torus_diag(int op)
  {
    BGP_Personality_t pers;
    TRACES("(>)op=%d",op) ;

    bluegene_getPersonality(&pers, sizeof(pers));
    switch(op)
    {
    case 0:
      show_bic_regs() ;
      break ;
    case 1:
#if defined(CONFIG_BLUEGENE_TCP)
      if( pers.Network_Config.Rank != pers.Network_Config.IOnodeRank )
        {
        	tasklet_schedule(&dma_tcp_slih);
        }
#endif
      break ;
    case 2:
      if( pers.Network_Config.Rank != pers.Network_Config.IOnodeRank )
        {
        	dumpdmadcrs(k_t_request) ;
        }
      break ;
    case 3:
#if defined(CONFIG_BLUEGENE_TCP)
      if( pers.Network_Config.Rank != pers.Network_Config.IOnodeRank )
        {
	      dumpRecFifoGroup(dma_tcp_state.recFifoGroup)  ;
	      show_timestamps() ;
	      bgp_dma_tcp_display_pending_slots(&dma_tcp_state,dma_tcp_state.node_count) ;
        }
#endif
      break ;
    case 4:
/*       show_state() ; // kernel threads and their stacks */
      break ;
    case 5:
/*       show_tlbs() ; // This core's current TLBs */
/*       show_sprs() ; // Core special-purpose regs relevant to debugging */
/*       show_personality() ; // Items from the 'personality' from microcode */
      break ;
    case 6:
/* #if defined(USE_SKB_TO_SKB) */
/* 	    bgp_dma_diag_reissue_rec_counters(&dma_tcp_state) ; */
/* #endif */
	      break ;
	    case 7:
	#if defined(USE_SKB_TO_SKB)
		    dma_tcp_show_reception(&dma_tcp_state) ;
	#endif
		    break ;
    default:
      ;
    }
    TRACES("(<)") ;
  }

