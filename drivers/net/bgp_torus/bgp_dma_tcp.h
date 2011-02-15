/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2010
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *
 ********************************************************************/
#ifndef __BGP_DMA_TCP_H__
#define __BGP_DMA_TCP_H__
#include <linux/bootmem.h>
#include <asm/div64.h>
#include <linux/timer.h>
#include <linux/bootmem.h>
#include <linux/sysctl.h>
#include <asm/atomic.h>

#include "../bgp_network/bgp_net_traceflags.h"

extern int bgp_dma_tcp_tracemask ;

/*  Can drop bits out of COMPILED_TRACEMASK if we want to selectively compile out trace */
/* #define COMPILED_TRACEMASK (0xffffffff-k_t_irqflow-k_t_irqflow_rcv-k_t_detail-k_t_fifocontents-k_t_toruspkt) */
#define COMPILED_TRACEMASK (0xffffffff)
/* #define COMPILED_TRACEMASK (k_t_error) */

/* #define TORNIC_DIAGNOSE_TLB */
#include <linux/KernelFxLog.h>
/*  'XTRACEN' would be a dummied-out trace statement */
#define XTRACEN(i,x...)
#if defined(CONFIG_BLUEGENE_TORUS_TRACE)
#define TRACING(i) (bgp_dma_tcp_tracemask & (COMPILED_TRACEMASK & (i)))
#define TRACE(x...)    KernelFxLog(bgp_dma_tcp_tracemask & k_t_general,x)
#define TRACE1(x...)   KernelFxLog(bgp_dma_tcp_tracemask & k_t_lowvol,x)
#define TRACE2(x...)   KernelFxLog(bgp_dma_tcp_tracemask & k_t_detail,x)
#define TRACEN(i,x...) KernelFxLog(bgp_dma_tcp_tracemask & (COMPILED_TRACEMASK & (i)),x)
#define TRACED(x...)   KernelFxLog(1,x)
#define TRACES(x...)   KernelFxLog(1,x)
#else
#define TRACING(x) 0
#define TRACE(x...)
#define TRACE1(x...)
#define TRACE2(x...)
#define TRACEN(i,x...)
#define TRACED(x...)
#define TRACES(x...)
#endif

#if defined(CONFIG_BLUEGENE_TCP)
#define ENABLE_FRAMES
#endif

#define AUDIT_FRAME_HEADER

#define KEEP_TCP_FLAG_STATS

#define BARRIER_WITH_IOCTL
/* #define EXERCISE_WITH_IOCTL */

void bgp_dma_diag_report_transmission_queue(int __user * report) ;

#if defined(BARRIER_WITH_IOCTL)
void dma_tcp_transfer_activate_sync(int sendBytes) ;
int dma_tcp_transfer_wait_sync(int demandCount) ;
void dma_tcp_transfer_clearcount(void) ;
#endif

#if defined(EXERCISE_WITH_IOCTL)
void dma_tcp_transfer_activate(int sendBytes) ;
void dma_tcp_transfer_activate_to_one(int sendBytes, unsigned int tg) ;
void dma_tcp_transfer_activate_minicube(int sendBytes) ;
int dma_tcp_transfer_wait(int demandCount) ;
#endif

/*  Whether we want a 'watchdog' on torus arrivals */
#define HAS_MISSED_INTERRUPT_TIMER

/*  Adaptive routing controls. */
/*  USE_ADAPTIVE_ROUTING builds a runtime capable of it; lower the value in /sys/module/bgp_torus/parameters/bgp_dma_adaptive_frame_limit to get frames send that way */
/*  INITIAL_ADAPTIVE_ROUTING sets things that way at boot (and may set params up so that attempted deterministic routing isn't actually deterministic) */
#if defined(CONFIG_BGP_TORUS_ADAPTIVE_ROUTING)
#define USE_ADAPTIVE_ROUTING
#define RESEQUENCE_ARRIVALS
#define INITIAL_ADAPTIVE_ROUTING
#endif

/*  Support for skbuff-to-skbuff DMA */
#define USE_SKB_TO_SKB

/*  What to use the 'dest-key' in the linkhdr for. Timestamping looks good ... */
/* #define ENABLE_LATENCY_TRACKING */
/* #define TRACK_SEQUENCE */
/* #define ENABLE_PROGRESS_TRACKING */

#define TORUS_RECEIVE_WITH_SLIH

/* #define TORUS_WITH_SIGNATURES */

/* Whether to support a soft-Iwarp data placement callback */
#define ENABLE_SIW_PLACEMENT

/*  Diagnosic options */
enum {
	k_allow_interrupts_while_injecting = 0 , /*  Select this for profiling injection */
	k_async_free = 1 ,  /*  Set this to allow timer-based freeing of skbuffs where the DMA has completed */
	k_dumpmem_diagnostic = 0 ,
	k_scattergather_diagnostic = 0 ,
	k_verify_target = 0 ,  /*  Whether to firewall-check that the target is reachable */
	k_detail_stats = 0 , /*  Whether to collect detailed statistics */
	k_counter_flow_control = 1 , /*  Whether to flow-control by limiting the number of reception counters allocates to a single source */
	k_force_eager_flow = 0 , /* Whether to start up with everything running 'eager' protocol (no 'rendezvous') */
	k_abbreviate_headlen = 1 , /* Whether to abbreviate the DMA transfer of 'head' in respect of the FIFO transfer */
	/* TODO: after testing that it works (on busy machines) , we should always take the 'deferral' path */
	k_allow_defer_skb_for_counter = 1, /* Whether to allow deferral allocating a 'full-size' skb until a reception counter is available */
	k_verify_ctlen = 1 , /* Whether to check that the length in the IP header matches the skbuff structure */
	k_configurable_virtual_channel = 1  /* Whether to allow runtime configuration of the virtual channel to use */
};



enum {
  numInjCounters = 1 ,
  recFifoId = 0
//  k_InjectionFifoGroup = 0 ,
//  k_ReceptionFifoGroup = 0 ,
//  k_InjectionCounterGroup = 0 ,
//  k_ReceptionCounterGroup = 0 ,
};

/*  We handle fragmented skbuffs if they are presented. The receive side doesn't need to know; */
/*  the send side injects additional 'direct put' descriptors as needed. */
/*  The bytes on the wire might be slightly different split between cells, but on the receive side this */
/*  is all handled by hardware. */
enum {
	k_support_scattergather = 1  /*  Whether we support a 'scattergather' skbuff */
};

/*  At one time, we ran per-core injection, to try to minimise the locking requirement. This is now changed to */
/*  per-destination injection, to try to minimise the out-of-order delivering. */
enum {
 k_injecting_cores = 4 ,
 k_skb_controlling_directions = 7 ,  /*  'directions' where we want to free skbuffs when sent */
#if defined(USE_SKB_TO_SKB)
 k_injecting_directions = 8 ,  /*  6 real directions, a 'taxi' for single packet messages, and a 'propose/accept stream' */
#else
 k_injecting_directions = 7 ,  /*  6 real directions, a 'taxi' for single packet messages */
#endif
};

/*  Following section for 'packets' style */
enum {
  k_torus_skb_alignment = 16 ,
  k_torus_link_payload_size = 240
};

enum {
  k_idma_descriptor_size = 32 ,
  k_injection_packet_size = 240
} ;

enum {
/*	k_concurrent_receives = 32  */ /*  Number of frames-in-flight we can handle from a source (in respect of adaptive routing) */
 	k_concurrent_receives = 128 /* Number of frames-in-flight we can handle from a source (in respect of adaptive routing) */
};

static inline void * local_permanent_alloc(unsigned int size)
  {
    void *result =  kmalloc(size, GFP_KERNEL) ;
    TRACEN(k_t_general,"size=0x%08x result=%p",size,result) ;
    return result ;
  }

/*  Using these when we are statically allocating buffers, or using alloc_bootmem_low */
enum {
  k_idma_descriptor_count = 16384,  /*  Design choice */
  k_injection_packet_count = 16384  /*  Matches IDMA descriptor count, to keep tagging simple */
   /*   k_injection_packet_count = (1<<22)/k_injection_packet_size // 4 megabytes of 'runway' */
};

enum {
  k_memcpy_idma_descriptor_count = 64,  /*  Design choice */
};

typedef struct {
  char buffer[k_idma_descriptor_size*k_memcpy_idma_descriptor_count] ;
} memcpy_packet_injection_memoryfifo_t __attribute__((aligned(16)));

typedef struct {
  char buffer[k_idma_descriptor_size*k_idma_descriptor_count] ;
} packet_injection_memoryfifo_t __attribute__((aligned(16)));

typedef struct {
  int tailx[k_injection_packet_count] ;
} packet_injection_tag_t ;

typedef struct {
	struct sk_buff * skb_array[k_injection_packet_count] ;
} packet_skb_array_t ;

static inline packet_injection_memoryfifo_t * allocate_packet_injection_memoryfifo(unsigned int core, unsigned int direction)
  {
    packet_injection_memoryfifo_t * rc = local_permanent_alloc(sizeof(packet_injection_memoryfifo_t)) ;
      BUG_ON(rc == NULL) ;
    XTRACEN(k_t_init,"allocate_packet_injection_memoryfifo core=%d direction=%d rc=%p",
        core, direction, rc ) ;
    BUG_ON( ( ((unsigned int) rc) & 0x1f) != 0 ) ;  /*  Need 32-byte alignment */
    return rc ;
  }

static inline memcpy_packet_injection_memoryfifo_t * allocate_memcpy_packet_injection_memoryfifo(unsigned int core)
  {
	  memcpy_packet_injection_memoryfifo_t * rc = local_permanent_alloc(sizeof(memcpy_packet_injection_memoryfifo_t)) ;
      BUG_ON(rc == NULL) ;
    TRACEN(k_t_general,"allocate_memcpy_packet_injection_memoryfifo core=%d rc=%p",
        core, rc ) ;
    BUG_ON( ( ((unsigned int) rc) & 0x1f) != 0 ) ;  /*  Need 32-byte alignment */
    return rc ;
  }

static inline packet_injection_tag_t * allocate_packet_injection_tag(unsigned int core, unsigned int direction)
  {
    packet_injection_tag_t * rc = kmalloc(sizeof(packet_injection_tag_t),GFP_KERNEL) ;
    BUG_ON(rc == NULL) ;
    XTRACEN(k_t_init,"allocate_packet_injection_tag core=%d direction=%d rc=%p",
        core, direction, rc ) ;
    BUG_ON( ( ((unsigned int) rc) & 0x1f) != 0 ) ;  /*  Need 32-byte alignment */
    return rc ;
  }

static inline packet_skb_array_t * allocate_packet_skb_array(unsigned int core, unsigned int direction)
  {
	  packet_skb_array_t * rc = kmalloc(sizeof(packet_skb_array_t),GFP_KERNEL) ;
    BUG_ON(rc == NULL) ;
    XTRACEN(k_t_init,"allocate_skb_array core=%d direction=%d rc=%p",
        core, direction, rc ) ;
    memset(rc,0,sizeof(packet_skb_array_t)) ;
    return rc ;
  }

enum {
  k_idma_frame_count = 16384  /*  Design choice */
};

typedef struct {
#if defined(ENABLE_PACKETS) || defined(ENABLE_FRAMES)
  packet_injection_memoryfifo_t * idma_fifo ;
  packet_injection_tag_t * idma_tag ;
  packet_skb_array_t * idma_skb_array ;
  unsigned int fifo_head_index ;
  unsigned int fifo_tail_index ;
  unsigned int buffer_head_index ;
  unsigned int buffer_tail_index ;
  unsigned int fifo_initial_head ;
  unsigned int packets_injected_count ;
  unsigned int injection_vacant ;
  unsigned int injection_high_watermark ;
#endif
#if defined(ENABLE_FRAMES)
  struct sk_buff_head frame_queue ;
#endif
} idma_direction_t ;

static inline void allocate_idma_direction(idma_direction_t * idma_direction,unsigned int core, unsigned int direction)
  {
#if defined(ENABLE_PACKETS) || defined(ENABLE_FRAMES)
    idma_direction->idma_fifo = allocate_packet_injection_memoryfifo(core, direction) ;
    idma_direction->idma_tag = allocate_packet_injection_tag(core,direction) ;
    idma_direction->idma_skb_array = allocate_packet_skb_array(core,direction) ;
    idma_direction->fifo_head_index = 0 ;
    idma_direction->fifo_tail_index = 0 ;
    idma_direction->buffer_head_index = 0 ;
    idma_direction->buffer_tail_index = 0 ;
    idma_direction->injection_vacant = 0 ;
    idma_direction->injection_high_watermark = 0 ;
    idma_direction->packets_injected_count = 0 ;
#endif
#if defined(ENABLE_FRAMES)
    skb_queue_head_init(&idma_direction->frame_queue)  ;
#endif
  }

typedef struct {
  idma_direction_t idma_direction[k_injecting_directions] ;
  memcpy_packet_injection_memoryfifo_t *memcpy_packet_fifo ;
  unsigned int memcpy_packet_fifo_head_index ;
  unsigned int memcpy_packet_fifo_tail_index ;
  unsigned int memcpy_fifo_initial_head ;
} idma_core_t ;

static inline void allocate_idma_core(idma_core_t * idma_core,unsigned int core)
  {
    int direction ;
    for( direction=0 ; direction<k_injecting_directions;direction+=1 )
      {
        allocate_idma_direction(idma_core->idma_direction+direction, core, direction) ;
      }
    idma_core->memcpy_packet_fifo=allocate_memcpy_packet_injection_memoryfifo(core) ;
  }

typedef struct {
  idma_core_t idma_core[k_injecting_cores] ;
} idma_t ;

static inline void allocate_idma(idma_t * idma)
  {
    int core ;
    for( core=0 ; core<k_injecting_cores;core+=1 )
      {
        allocate_idma_core(idma->idma_core+core, core) ;
      }
  }

/*  'per-slot' structures for demultiplexing received torus messages. */
/*  we are no longer running 1 slot per possubly-sending core, i.e. 4 per node in the partition; now running 1 per node */
/*  Get/set methods because for 'large' machines we might need bigger tables than can be kmalloced in one go */
#if defined(ENABLE_LATENCY_TRACKING)

typedef struct {
  unsigned long long s1 ;
  unsigned long long sx ;
  unsigned long long sxx ;
  unsigned int xmin ;
  unsigned int xmax ;
} rcv_statistic_t ;

static void rcv_statistic_clear(rcv_statistic_t *t)
  {
    t->s1 = 0;
    t->sx = 0;
    t->sxx = 0 ;
    t->xmin = 0xffffffff ;
    t->xmax = 0 ;
  }
static void rcv_statistic_observe(rcv_statistic_t *t, unsigned int x)
  {
    unsigned long long ullx = x ;
    unsigned long long ullxx = ullx*ullx ;
    t->s1 += 1 ;
    t->sx += x ;
    t->sxx += ullxx ;
    if( x<t->xmin ) t->xmin=x ;
    if( x>t->xmax ) t->xmax=x ;
  }
static unsigned int rcv_statistic_mean(rcv_statistic_t *t)
  {
    unsigned long long s1=t->s1 ;
    unsigned long long sx=t->sx ;
    unsigned long long rc = sx ;
    do_div(rc,(unsigned int)s1) ;
    TRACEN(k_t_detail,"sx=0x%08x%08x s1=0x%08x%08x mean=%u",
        (unsigned int)(sx>>32),(unsigned int)sx,
        (unsigned int)(s1>>32),(unsigned int)s1,(unsigned int)rc) ;
    return (unsigned int)rc ;
  }
static unsigned int rcv_statistic_variance(rcv_statistic_t *t, unsigned int m)
  {
    unsigned long long s1=t->s1 ;
    unsigned long long sx=t->sx ;
    unsigned long long sxx=t->sxx ;
    unsigned long long mm=m ;
    unsigned long long vv =  sxx - mm*mm ;
    unsigned long long rc=vv ;
    do_div(rc,(unsigned int)s1) ;
    TRACEN(k_t_detail,"sxx=0x%08x%08x sx=0x%08x%08x s1=0x%08x%08x mm=0x%08x%08x vv=0x%08x%08x variance=%u",
        (unsigned int)(sxx>>32),(unsigned int)sxx,
        (unsigned int)(sx>>32),(unsigned int)sx,
        (unsigned int)(s1>>32),(unsigned int)s1,
        (unsigned int)(mm>>32),(unsigned int)mm,
        (unsigned int)(vv>>32),(unsigned int)vv,
        (unsigned int)rc) ;
    return (unsigned int)rc ;
  }
#endif
/*  TODO: Can this be condensed ? Should be a 'char * payload' and a 'char * payload_alert', down to 8 bytes */
/*   or could even be a 28-bit address (since we know 16-byte alignment) and a 4-bit count so we treat things */
/*   in more detail every 16 packets or when the frame is done if sooner */
/*  TODO: also: maybe the injector should flag the last packet of a frame with a different function ? */
typedef struct  {
  unsigned char * payload ;
  unsigned char * payload_alert ;
  unsigned int expect ;
  int lastcell ;
  unsigned int proposals_active ;
  struct sk_buff_head proposals_pending_flow ;
#if defined(USE_ADAPTIVE_ROUTING)
  struct sk_buff * skb_per_conn[k_concurrent_receives] ;
#if defined(RESEQUENCE_ARRIVALS)
  struct sk_buff * skb_pending_resequence[k_concurrent_receives] ;
  unsigned int conn_id_pending_delivery ;
#endif
#endif
#if defined(ENABLE_LATENCY_TRACKING)
  rcv_statistic_t latency ;
  unsigned int basetime ;
#endif
#if defined(ENABLE_PROGRESS_TRACKING)
  unsigned long long timestamp ;
#endif
} rcv_per_slot_t ;

typedef struct {
  unsigned int  partner_ip_address ;
  unsigned int  partner_xyz ;
} learned_address_entry ;

typedef struct {
  rcv_per_slot_t * rcv_per_slot_vector ;
  struct sk_buff ** skb_per_slot_vector ;
} rcv_t ;

static inline char * get_rcv_payload(rcv_t *rcv, unsigned int slot_index)
  {
    return rcv->rcv_per_slot_vector[slot_index].payload ;
  }

static inline void set_rcv_payload(rcv_t *rcv, unsigned int slot_index, char * payload )
  {
    rcv->rcv_per_slot_vector[slot_index].payload = payload ;
  }

static inline unsigned int get_proposals_active(rcv_t *rcv, unsigned int slot_index)
  {
    return rcv->rcv_per_slot_vector[slot_index].proposals_active ;
  }

static inline void set_proposals_active(rcv_t *rcv, unsigned int slot_index, unsigned int proposals_active )
  {
    rcv->rcv_per_slot_vector[slot_index].proposals_active = proposals_active ;
  }

static inline char * get_rcv_payload_alert(rcv_t *rcv, unsigned int slot_index)
  {
    return rcv->rcv_per_slot_vector[slot_index].payload_alert ;
  }

static inline void set_rcv_payload_alert(rcv_t *rcv, unsigned int slot_index, char * payload_alert )
  {
    rcv->rcv_per_slot_vector[slot_index].payload_alert = payload_alert ;
  }

static inline unsigned int get_rcv_expect(rcv_t *rcv, unsigned int slot_index)
  {
    return rcv->rcv_per_slot_vector[slot_index].expect ;
  }

static inline void set_rcv_expect(rcv_t *rcv, unsigned int slot_index, unsigned int expect)
  {
    rcv->rcv_per_slot_vector[slot_index].expect = expect ;
  }

static inline int get_rcv_lastcell(rcv_t *rcv, unsigned int slot_index)
  {
    return rcv->rcv_per_slot_vector[slot_index].lastcell ;
  }

static inline void set_rcv_lastcell(rcv_t *rcv, unsigned int slot_index, int lastcell)
  {
    rcv->rcv_per_slot_vector[slot_index].lastcell = lastcell ;
  }

static inline struct sk_buff * get_rcv_skb(rcv_t *rcv, unsigned int slot_index)
  {
    return rcv->skb_per_slot_vector[slot_index] ;
  }

static inline void set_rcv_skb(rcv_t *rcv, unsigned int slot_index, struct sk_buff * skb)
  {
    rcv->skb_per_slot_vector[slot_index] = skb ;
  }

static inline void init_pending_flow(rcv_t *rcv, unsigned int slot_index)
{
	skb_queue_head_init(&rcv->rcv_per_slot_vector[slot_index].proposals_pending_flow) ;
}

static inline void enq_pending_flow(rcv_t *rcv, unsigned int slot_index, struct sk_buff * skb)
{
	skb_queue_tail(&rcv->rcv_per_slot_vector[slot_index].proposals_pending_flow,skb) ;
}

static inline struct sk_buff * deq_pending_flow(rcv_t *rcv, unsigned int slot_index)
{
	return skb_dequeue(&rcv->rcv_per_slot_vector[slot_index].proposals_pending_flow) ;
}

static inline unsigned int count_pending_flow(rcv_t *rcv, unsigned int slot_index)
{
	return skb_queue_len(&rcv->rcv_per_slot_vector[slot_index].proposals_pending_flow) ;
}

#if defined(USE_ADAPTIVE_ROUTING)
static inline struct sk_buff * get_rcv_skb_for_conn(rcv_t *rcv, unsigned int slot_index, unsigned int conn_id)
{
	return rcv->rcv_per_slot_vector[slot_index].skb_per_conn[conn_id & (k_concurrent_receives-1)] ;
}

static void set_rcv_skb_for_conn(rcv_t *rcv, unsigned int slot_index, unsigned int conn_id, struct sk_buff * skb) __attribute__((unused)) ;
static void set_rcv_skb_for_conn(rcv_t *rcv, unsigned int slot_index, unsigned int conn_id, struct sk_buff * skb)
{
	rcv->rcv_per_slot_vector[slot_index].skb_per_conn[conn_id & (k_concurrent_receives-1)] = skb ;
}
#if defined(RESEQUENCE_ARRIVALS)
  static inline struct sk_buff * get_rcv_skb_pending_resequence(rcv_t *rcv, unsigned int slot_index, unsigned int conn_id)
  {
	  return rcv->rcv_per_slot_vector[slot_index].skb_pending_resequence[conn_id & (k_concurrent_receives-1)] ;
  }
  static inline void set_rcv_skb_pending_resequence(rcv_t *rcv, unsigned int slot_index, unsigned int conn_id, struct sk_buff * skb)
  {
	  rcv->rcv_per_slot_vector[slot_index].skb_pending_resequence[conn_id & (k_concurrent_receives-1)] = skb;
  }
  static inline int get_rcv_conn_pending_delivery(rcv_t *rcv, unsigned int slot_index)
  {
	  return rcv->rcv_per_slot_vector[slot_index].conn_id_pending_delivery ;
  }
  static void set_rcv_conn_pending_delivery(rcv_t *rcv, unsigned int slot_index, unsigned int conn_id) __attribute__((unused)) ;
  static void set_rcv_conn_pending_delivery(rcv_t *rcv, unsigned int slot_index, unsigned int conn_id)
  {
	  rcv->rcv_per_slot_vector[slot_index].conn_id_pending_delivery=conn_id ;
  }

#endif

#endif

static inline unsigned long long get_timestamp(rcv_t *rcv, unsigned int slot_index)
  {
#if defined(ENABLE_PROGRESS_TRACKING)
    return rcv->rcv_per_slot_vector[slot_index].timestamp ;
#else
    return 0 ;
#endif
  }

static inline void set_timestamp(rcv_t *rcv, unsigned int slot_index, unsigned long long timestamp)
  {
#if defined(ENABLE_PROGRESS_TRACKING)
    rcv->rcv_per_slot_vector[slot_index].timestamp=timestamp ;
#endif
  }

enum {
	k_slots_per_node = 1 ,  /*  down from 4 ... */
	k_connids_per_node = 128  /*  Number of conn-ids we track per node on the sending side */
};
static inline void allocate_rcv(rcv_t *rcv, unsigned int node_count)
  {
    rcv->rcv_per_slot_vector = kmalloc(k_slots_per_node*node_count*sizeof(rcv_per_slot_t), GFP_KERNEL) ;
    BUG_ON(NULL == rcv->rcv_per_slot_vector) ;
    memset(rcv->rcv_per_slot_vector,0,k_slots_per_node*node_count*sizeof(rcv_per_slot_t)) ;
    rcv->skb_per_slot_vector = kmalloc(k_slots_per_node*node_count*sizeof(struct sk_buff *), GFP_KERNEL) ;
    BUG_ON(NULL == rcv->skb_per_slot_vector) ;
    BUG_ON(NULL == rcv->skb_per_slot_vector) ;
    {
	    unsigned int slot ;
	    for(slot=0;slot<node_count;slot+=1)
		    {
			    init_pending_flow(rcv,slot) ;
		    }
    }
  }

#if defined(USE_ADAPTIVE_ROUTING)

extern ulong bgp_dma_adaptive_frame_limit ;

typedef struct {
	atomic_t * conn_id ;
#if defined(USE_SKB_TO_SKB)
	struct sk_buff **skb ;
#endif
} tx_t ;

static inline void init_tx_conn_id(tx_t *tx, unsigned int slot_index)
{
	atomic_set(tx->conn_id+slot_index,0xffffffff) ;
}

static inline void allocate_tx(tx_t *tx, unsigned int node_count)
  {
    tx->conn_id = kmalloc(k_slots_per_node*node_count*sizeof(atomic_t), GFP_KERNEL) ;
    BUG_ON(NULL == tx->conn_id) ;
    {
	    int x ;
	    for(x=0;x<node_count;x+=1)
		    {
			    init_tx_conn_id(tx,x) ;
		    }
    }
#if defined(USE_SKB_TO_SKB)
    tx->skb = kmalloc(k_connids_per_node*node_count*sizeof(struct sk_buff *),GFP_KERNEL) ;
#endif
    BUG_ON(NULL == tx->skb) ;
    memset(tx->skb,0,k_connids_per_node*node_count*sizeof(struct sk_buff *)) ;
  }

static inline unsigned int take_tx_conn_id(tx_t *tx, unsigned int slot_index)
{
	unsigned int rc= atomic_inc_return(tx->conn_id+slot_index) ;
	TRACEN(k_t_general,"slot_index=0x%08x conn_id=0x%08x",slot_index,rc) ;
	return rc ;
}
#if defined(USE_SKB_TO_SKB)
static inline struct sk_buff * get_tx_skb(tx_t *tx, unsigned int slot_index, unsigned int conn_id)
{
	return tx->skb[slot_index*k_connids_per_node+(conn_id & (k_connids_per_node-1))] ;
}
static inline void set_tx_skb(tx_t *tx, unsigned int slot_index, unsigned int conn_id, struct sk_buff * skb)
{
	tx->skb[slot_index*k_connids_per_node+(conn_id & (k_connids_per_node-1))] = skb ;
}

#endif

#endif

/*  End of 'packets' style section */
enum {
  k_desired_reception_memory_fifo_size =
#if defined(CONFIG_BGP_RECEPTION_MEMORY_FIFO_SHIFT)
    1 << (CONFIG_BGP_RECEPTION_MEMORY_FIFO_SHIFT)
#else
    1 << 22   /*  Try 4MB as a static region, if not set externally */
/*     1 << 20  // Try 1MB as a static region, if not set externally */
#endif
} ;
enum {
  k_metadata_injection_memory_fifo_size = 4096 ,
  k_bulk_injection_memory_fifo_size = 4096
};

typedef struct {
  char buffer[k_metadata_injection_memory_fifo_size] ;
} metadata_injection_memoryfifo_t ;

typedef struct {
  char buffer[k_bulk_injection_memory_fifo_size] ;
} bulk_injection_memoryfifo_t ;


#if defined(BARRIER_WITH_IOCTL)
enum {
	k_diag_target_data_size = 1<<20 ,  /*  Aim up to 1MB ... */
	k_diag_packet_count = k_diag_target_data_size/k_injection_packet_size ,  /*  Rounding down for packets ... */
};
typedef struct {
	char buffer[k_diag_target_data_size] ;
} diag_block_buffer_t ;

static inline diag_block_buffer_t * allocate_diag_block_buffer(void)
{
	diag_block_buffer_t * result = kmalloc(k_diag_target_data_size,GFP_KERNEL) ;
	BUG_ON(NULL == result) ;
	return result ;
}

static inline unsigned int * allocate_shuffle_vector(unsigned int xe, unsigned int ye, unsigned int ze)
{
	unsigned int * result = kmalloc(xe*ye*ze*sizeof(unsigned int),GFP_KERNEL) ;
	BUG_ON(NULL == result) ;
	return result ;
}
#endif


enum {
  k_Dimensionality = 3
};

typedef struct {
  unsigned char coordinate[k_Dimensionality] ;
} torusLocation_t ;

typedef enum {
	k_send_propose_rpc ,
	k_act_propose_rpc ,
	k_send_accept_rpc ,
	k_act_accept_rpc ,

	k_defer_accept_rpc_counters ,
	k_defer_accept_rpc_nodeflow ,
	k_send_eager ,
	k_receive_eager ,

	k_no_reception_counter ,
	k_parked ,
	k_scattergather ,
	k_receive_incomplete ,

	k_headlength_zero ,
	k_fraglength_zero ,
	k_accept_audit_fail ,
	k_receive_audit_fail ,

	k_counted_length_mismatch ,
	k_reordered ,
	k_queue_filled_propose_fifo ,

	k_siw_placement_hit ,
	k_siw_placement_miss ,

	k_flow_counters
} flowpoint_e ;

#if defined(CONFIG_BGP_STATISTICS)
extern int reception_fifo_histogram[33] ;
extern int reception_hi_watermark ;
extern int rtt_histogram[33] ;
extern int transit_histogram[33] ;
#endif

enum {
	k_pending_rcv_skb_classes = 6
};
typedef struct {
	struct sk_buff_head pending_rcv_skbs ; /* List of sk_buffs awaiting a reception counter */
	unsigned int outstanding_counters ; /* Number of counters awaiting completion in this direction */
} bgp_dma_balancer_direction ;
typedef struct {
	bgp_dma_balancer_direction b[k_pending_rcv_skb_classes] ;
} bgp_dma_balancer ;
typedef struct {
  torusLocation_t location ;
  torusLocation_t extent ;
   /*  Number of bits required to represent a node in each torus dimension */
  unsigned int xbits ;
  unsigned int ybits ;
  unsigned int zbits ;
  /* Which quarter of the DMA unit we should use */
  unsigned int active_quarter ; /* 0 .. 3 */
  unsigned int is_up ; // Whether the interface is 'up'

    DMA_RecFifoGroup_t * recFifoGroup;
     rcv_t rcvdemux ;  /*  Reception demultiplex */
#if defined(USE_ADAPTIVE_ROUTING)
     tx_t tx_mux ;  /*  Transmission multiplexer (conn_ids by slot) */
#endif
     unsigned int node_count ;  /*  Total number of nodes in the block */
     unsigned int node_slot_mask ;  /*  ((node_count << 2)-1) , for bit-masking to firewall check received data */
#ifdef ENABLE_PACKETS
    DMA_InjFifoGroup_t   injFifoGroupPackets;
    int injFifoPacketsIds[ k_injecting_cores*k_injecting_directions ];
    int proto_issue_packets ;

     /*  End of packets-style interface */
#endif
    idma_t idma ;  /*  Injection DMA buffering */
#ifdef ENABLE_PACKETS
    unsigned short int injFifoPacketsPri[ k_injecting_cores*k_injecting_directions ] ;
    unsigned short int injFifoPacketsLoc[ k_injecting_cores*k_injecting_directions ] ;
    unsigned char      injFifoPacketsMap[ k_injecting_cores*k_injecting_directions ] ;
#endif
    struct sk_buff_head inj_queue[k_injecting_directions] ;   /* Lists of skb's queued because DMA buffers have no space */
    unsigned int packets_received_count ;
    struct timer_list runway_check_timer ;
    struct timer_list transmission_free_skb_timer ;
#if defined(HAS_MISSED_INTERRUPT_TIMER)
    struct timer_list torus_missed_interrupt_timer ;
#endif
#ifdef ENABLE_FRAMES
    DMA_InjFifoGroup_t   injFifoGroupFrames;
    int injFifoFramesIds[ k_injecting_cores*k_injecting_directions ];
    int proto_issue_frames_single ;
#if defined(USE_ADAPTIVE_ROUTING)
    int proto_issue_frames_adaptive ;
#endif
#if defined(USE_SKB_TO_SKB)
    int proto_transfer_propose ;
    int eager_limit ;  /*  frames larger than this to be sent with skb-to-skb DMA */
    int flow_counter[k_flow_counters] ;
#endif
#if defined(BARRIER_WITH_IOCTL)
    int proto_issue_diag_sync ;
    diag_block_buffer_t * diag_block_buffer ;
    unsigned int * shuffle_vector ;
    unsigned int shuffle_seed ;
    int prev_tbl ;
    unsigned int timing_histogram_buckets[33] ;
#endif
    unsigned short int injFifoFramesPri[ k_injecting_cores*k_injecting_directions ] ;
    unsigned short int injFifoFramesLoc[ k_injecting_cores*k_injecting_directions ] ;
    unsigned char      injFifoFramesMap[ k_injecting_cores*k_injecting_directions ] ;
#endif

    DMA_CounterGroup_t   injCounterGroup;
    DMA_CounterGroup_t   recCounterGroup;

    void * receptionfifo ;
    unsigned int receptionfifoSize ;

    unsigned int mtu ;
    unsigned int max_packets_per_frame ;

    DMA_RecFifoMap_t recMap;   /*  rec fifo map structure */



#if defined(USE_SKB_TO_SKB)
    int injCntrSubgrps[ DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP ] ;
    int recCntrSubgrps[ DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP ] ;
    char recCntrInUse [ DMA_NUM_COUNTERS_PER_GROUP ] ;
    int qtyFreeRecCounters ;
    int scanRecCounter ;
    struct sk_buff * inj_skbs[DMA_NUM_COUNTERS_PER_GROUP] ;
    struct sk_buff * rcv_skbs[DMA_NUM_COUNTERS_PER_GROUP] ;
    unsigned int slot_for_rcv[DMA_NUM_COUNTERS_PER_GROUP] ;
    unsigned char conn_for_rcv[DMA_NUM_COUNTERS_PER_GROUP] ;
    int rcv_timestamp[DMA_NUM_COUNTERS_PER_GROUP] ;
    int rcv_checked_time ;
    bgp_dma_balancer balancer ;
    atomic_t framesProposed ;
    unsigned int framesDisposed ;
#endif
#if defined(ENABLE_SIW_PLACEMENT)
    dma_addr_t (*siw_placement_callback)(struct sk_buff *skb) ;
#endif

    unsigned short int memcpyInjFifoFramesPri[ k_injecting_cores ] ;
    unsigned short int memcpyInjFifoFramesLoc[ k_injecting_cores ] ;
    unsigned char      memcpyInjFifoFramesMap[ k_injecting_cores ] ;
    DMA_InjFifoGroup_t   memcpyInjFifoGroupFrames;
    int memcpyInjFifoFramesIds[ k_injecting_cores ];
    DMA_CounterGroup_t   memcpyRecCounterGroup;
    int memcpyRecCntrSubgrps[ DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP ] ;

    int proto_diagnose ; /* 'diagnose' frame to software reception FIFO */

    unsigned int SW_Arg ;  /* / 'Software Arg', we send our {x,y,z} */
    unsigned int src_key ;  /*  'source key', we send rank */


    spinlock_t dirInjectionLock[k_injecting_cores*k_injecting_directions] ;  /*  serialise access to injection FIFOs */

    void * previousActor ;  /*  FIFO address of previous Actor, for detecting replays */


     /*  sysctl entries */
    struct ctl_table_header * sysctl_table_header ;
/* Statistics */

    struct net_device_stats * device_stats ;
    unsigned int count_no_skbuff ;
    unsigned int tx_by_core[4] ;
    unsigned int tx_in_use_count[k_injecting_directions+1] ;
#if defined(KEEP_TCP_FLAG_STATS)
    unsigned int tcp_received_flag_count[8] ;
#endif
/*  Tuning parameters */
    int tuning_num_packets ;  /*  = 1 , number of packets to process per poll call */
    int tuning_num_empty_passes ;  /*  = 512 , number of times to spin before returning */
    int tuning_non_empty_poll_delay ;  /*  = 850 , number of cycles to spin between looks at the FIFO */
    int tuning_poll_after_enabling ;  /*  = 1 , whether to poll again after enabling for interrupts */
    int tuning_run_handler_on_hwi ;  /*  = 1 , whether to run the hander on FIFO hardware interrupts (as well as rDMA ones) */
    int tuning_clearthresh_slih ;  /*  = 1 , whether to clear the 'threshold crossed' bit in the slih */
    int tuning_clearthresh_flih ;  /*  = 1 , whether to clear the 'threshold crossed' bit in the flih */
    int tuning_disable_in_dcr ;  /*  = 1, whether to toggle the DCR interrupt enable/disable */
    int tuning_injection_hashmask ;  /*  = 3, whether to mask down the number of injection FIFOs in use per direction */

    int tuning_recfifo_threshold ;  /*  for moving to/from DCR */
    int tuning_dcr_c8b ;  /*  for moving to/from DCR */
    int tuning_enable_hwfifo ;  /*  For registering/unregistering 'hardware FIFO' interrupts */

    int tuning_exploit_reversepropose ;  /*  Whether to try the 'reverse propose' protocol */
    int tuning_counters_per_source ;  /*  How many reception counters to commit per source node */
    int tuning_defer_skb_until_counter ; /* Whether to defer sk_buff allocation until a reception counter is available */
    int tuning_deliver_eagerly ; /* Whether to skip the 'resequence arrivals' step */
    int tuning_diagnose_rst ; /* Whether to cut trace records when being asked to send a TCP segment with a 'rst' */

    int tuning_select_fifo_algorithm ; /* Which FIFO selection algorithm to use (head-of-line block minimisation) */

    int tuning_min_icsk_timeout ;  /*  What to push ICSK retransmit timeout up to if we find it low */

    int tuning_virtual_channel ; /* Which virtual channel to use (i.e. whether to force deterministic routing) */

    int tuning_enable_siw_placement ; /* Whether to allow siw to call for direct placement */

    int tuning_prep_dcmf ;/* Whether to get ready for DCMF at 'ifconfig down' time */

  unsigned int block_id ;
  unsigned char i_am_compute_node ;
  unsigned char bluegene_tcp_is_built ;
  unsigned char is_torus_x ;
  unsigned char is_torus_y ;
  unsigned char is_torus_z ;
  unsigned char last_queue_picked ;
#if defined(CONFIG_BGP_STATISTICS)
  unsigned int resequence_histogram[k_concurrent_receives] ;
  unsigned long long  bytes_sent ;
  unsigned long long  bytes_received ;
#endif
  unsigned int configured_quarter ;
} dma_tcp_t ;

// Intent to allow the 'quarter' of the DMA hardware to be chosen at runtime before 'ifconfig up'
static inline int dma_tcp_InjectionFifoGroup(dma_tcp_t  * dma_tcp)
  {
    return dma_tcp->active_quarter ;
  }
static inline int dma_tcp_ReceptionFifoGroup(dma_tcp_t  * dma_tcp)
  {
    return dma_tcp->active_quarter ;
  }
static inline int dma_tcp_InjectionCounterGroup(dma_tcp_t  * dma_tcp)
  {
    return dma_tcp->active_quarter ;
  }
static inline int dma_tcp_ReceptionCounterGroup(dma_tcp_t  * dma_tcp)
  {
    return dma_tcp->active_quarter ;
  }

typedef enum {
  k_VC_ordering = DMA_PACKET_VC_BN ,   /*  virtual channel to use when we want to order things, 'Bubble Normal' */
  k_VC_anyway = DMA_PACKET_VC_D0       /*  virtual channel to use otherwise ... 'Dynamic 0' */
} VC_e ;

static inline unsigned int virtual_channel(dma_tcp_t *dma_tcp, VC_e channel_hint)
{
	return k_configurable_virtual_channel ? dma_tcp->tuning_virtual_channel : channel_hint ;
}

static inline void instrument_flow(dma_tcp_t *dma_tcp,flowpoint_e flowpoint)
{
	dma_tcp->flow_counter[flowpoint] += 1 ;
}

static inline unsigned int flow_count(dma_tcp_t *dma_tcp,flowpoint_e flowpoint)
{
	return dma_tcp->flow_counter[flowpoint] ;
}

extern dma_tcp_t dma_tcp_state ;

void bgp_dma_tcp_display_pending_slots(dma_tcp_t * dma_tcp, unsigned int nodecount ) ;
void bgp_dma_diag_reissue_rec_counters(dma_tcp_t *dma_tcp) ;

void bgp_dma_tcp_empty_fifo_callback(void) ;

extern void bluegene_set_cpu_for_irq(unsigned int irq, unsigned int cpu) ;
extern void bluegene_bic_disable_irq(unsigned int irq) ;

int bgnet_receive_torus(struct sk_buff * skb) ;
int bgtornet_receive_torus(struct sk_buff * skb) ;
struct net_device_stats *bgtornet_stats(void) ;

void bgtornet_rx_schedule(void) ;


static inline int DMA_CounterSetValueWideOpen(
                                          DMA_Counter_t *c_sw,
                                          unsigned int   value
                                         )
{
  unsigned int pa_base=0, pa_max=0xffffffff;
  SPI_assert( c_sw != NULL );
  c_sw->pa_base = pa_base;
  c_sw->pa_max = pa_max;

  /*
   * Write the value, base, and max to the hardware counter
   */
  DMA_CounterSetValueBaseMaxHw(c_sw->counter_hw_ptr,
                               value,
                               pa_base,
                               pa_max);

  return (0);
}

static inline  int DMA_CounterSetValueWideOpenById(
                                     DMA_CounterGroup_t *cg_ptr,
                                     int                 counter_id ,
                                     unsigned int   value
                                    )
  {
    int rc;

    SPI_assert( (counter_id >= 0) && (counter_id < DMA_NUM_COUNTERS_PER_GROUP) );
    SPI_assert( cg_ptr != NULL );
    SPI_assert( (cg_ptr->permissions[DMA_COUNTER_GROUP_WORD_ID(counter_id)] &
             _BN(DMA_COUNTER_GROUP_WORD_BIT_ID(counter_id))) != 0 );

    rc = DMA_CounterSetValueWideOpen( &cg_ptr->counter[counter_id], value ) ;

     /*  Note: it is assumed that the above function call performs an MBAR */

    return rc;

  }

/*  Choose a transmission FIFO for a stream. This is 'approximately' the deterministic routing algorithm */
/*  (I think it is 'exactly' the deterministic routing algorithm, with the possible exception of what the hardware will do */
/*   if you send a packet to something half-way-round in one of the torus dimensions) */
/*  Return -1 if it is an attempted 'self-send'; this has to be done as a local DMA or a memcpy, not as a torus op */
static int select_transmission_fifo(dma_tcp_t *dma_tcp, unsigned int x, unsigned int y, unsigned int z) __attribute__ ((unused)) ;
static inline int sign_extend(int d, unsigned int bb)
{
	return (d << (32-bb)) >> (32-bb) ;
}
static inline int resolve_direction(int d, unsigned int is_torus, unsigned int bb,  int v0, int v1)
{
	if( is_torus) d = sign_extend(d,bb) ;
	return (d<0) ? v1 : v0 ;
}
static int select_transmission_fifo_v(dma_tcp_t *dma_tcp, unsigned int x0,unsigned int x, unsigned int y0,unsigned int y, unsigned int z0,unsigned int z)
  {
	  switch(dma_tcp->tuning_select_fifo_algorithm)
	  {
		  case 0:
		  case 1:
			  {
				  int dx = x0-x ;
				  int dy = y0-y ;
				  int dz = z0-z ;
				  if( dx != 0 ) return resolve_direction(dx, dma_tcp->is_torus_x,dma_tcp->xbits, 1, 0) ;
				  if( dy != 0 ) return resolve_direction(dy, dma_tcp->is_torus_y,dma_tcp->ybits, 3, 2) ;
				  return resolve_direction(dz,dma_tcp->is_torus_z,dma_tcp->zbits, 5, 4) ;
			  }
		  default:
			  /*   rank modulo 6 ... */
			  	  return ((x<<(dma_tcp->ybits+dma_tcp->zbits)) | (y<<(dma_tcp->zbits)) | (z)) % 6 ;

	  }
  }

static int select_transmission_fifo(dma_tcp_t *dma_tcp, unsigned int x, unsigned int y, unsigned int z)
{
	return select_transmission_fifo_v(dma_tcp,dma_tcp->location.coordinate[0],x,dma_tcp->location.coordinate[1],y,dma_tcp->location.coordinate[2],z) ;
}

/*  Report the transmission FIFO that a remote node will use to reach this node */
static int report_transmission_fifo(dma_tcp_t *dma_tcp, unsigned int x0, unsigned int y0, unsigned int z0) __attribute__ ((unused)) ;
static int report_transmission_fifo(dma_tcp_t *dma_tcp, unsigned int x0, unsigned int y0, unsigned int z0)
{
	return select_transmission_fifo_v(dma_tcp,x0,dma_tcp->location.coordinate[0],y0,dma_tcp->location.coordinate[1],z0,dma_tcp->location.coordinate[2]) ;
}



int handleSocketsRecvMsgActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           ) ;
int handleSocketsRecvMsgCompletedActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           ) ;
int handleSocketsBufferActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           ) ;


#ifdef ENABLE_PACKETS
void dma_tcp_packets_init(dma_tcp_t *dma_tcp) ;
int bgp_dma_tcp_send_and_free_packets( struct sk_buff *skb
                    ) ;
void dma_tcp_packets_show_counts(dma_tcp_t *dma_tcp) ;

#endif
#ifdef ENABLE_FRAMES
void dma_tcp_frames_init(dma_tcp_t *dma_tcp) ;
void dma_tcp_frames_ifup(dma_tcp_t *dma_tcp) ;
void dma_tcp_frames_ifdown(dma_tcp_t *dma_tcp) ;
int bgp_dma_tcp_send_and_free_frames( struct sk_buff *skb
                    ) ;
#endif

/*  ethem codings are ... */
/*  0 : run things on the tree */
/*  1 : run things with 'actors' and DMA to/from SKBUFFs */
/*  2 : run things with 'messages' between memory FIFOs */
/*  3 : send both (1) and (2), for bringup. */
/*  until it's working correctly, we will deliver the '1' eth frames and discard the '2' eth frames at the receiver. */
/*  Additionally we can set a '4' bit, which will send packets over the tree; */
/*   so we could set '6' and get a working tree drive, and 'messages' flows to go through the motions on a prototype driver without any 'actors' flows */

extern int bgp_dma_ethem ;

/**********************************************************************
 * DCR access wrapper
 **********************************************************************/

static inline uint32_t mfdcrx(uint32_t dcrn)
{
    uint32_t value;
    asm volatile ("mfdcrx %0,%1": "=r" (value) : "r" (dcrn) : "memory");
    return value;
}

static inline void mtdcrx(uint32_t dcrn, uint32_t value)
{
    asm volatile("mtdcrx %0,%1": :"r" (dcrn), "r" (value) : "memory");
}


static void dumpdmadcrs(unsigned int tracelevel) __attribute__ ((unused)) ;
static void dumpdmadcrs(unsigned int tracelevel)
  {
    int x ;
    for(x=0xd00; x<=0xdff ; x += 8 )
      {
        int d0 __attribute__ ((unused)) = mfdcrx(x) ;
        int d1 __attribute__ ((unused)) = mfdcrx(x+1) ;
        int d2 __attribute__ ((unused)) = mfdcrx(x+2) ;
        int d3 __attribute__ ((unused)) = mfdcrx(x+3) ;
        int d4 __attribute__ ((unused)) = mfdcrx(x+4) ;
        int d5 __attribute__ ((unused)) = mfdcrx(x+5) ;
        int d6 __attribute__ ((unused)) = mfdcrx(x+6) ;
        int d7 __attribute__ ((unused)) = mfdcrx(x+7) ;
        TRACEN(tracelevel,"Torus DMA dcrs 0x%04x %08x %08x %08x %08x %08x %08x %08x %08x",
            x,d0,d1,d2,d3,d4,d5,d6,d7
            ) ;
      }
  }

static void dumptorusdcrs(void) __attribute__ ((unused)) ;
static void dumptorusdcrs(void)
  {
    int x ;
    for(x=0xc80; x<=0xc8f ; x += 8 )
      {
        int d0 __attribute__ ((unused)) = mfdcrx(x) ;
        int d1 __attribute__ ((unused)) = mfdcrx(x+1) ;
        int d2 __attribute__ ((unused)) = mfdcrx(x+2) ;
        int d3 __attribute__ ((unused)) = mfdcrx(x+3) ;
        int d4 __attribute__ ((unused)) = mfdcrx(x+4) ;
        int d5 __attribute__ ((unused)) = mfdcrx(x+5) ;
        int d6 __attribute__ ((unused)) = mfdcrx(x+6) ;
        int d7 __attribute__ ((unused)) = mfdcrx(x+7) ;
        TRACEN(k_t_request,"Torus control dcrs 0x%04x %08x %08x %08x %08x %08x %08x %08x %08x\n",
            x,d0,d1,d2,d3,d4,d5,d6,d7
            ) ;
      }
  }

#if defined(REQUIRES_DUMPMEM)
static inline char cfix(char x) __attribute__ ((unused)) ;
static void dumpmem(const void *address, unsigned int length, const char * label) __attribute__ ((unused)) ;
static void dumpframe(const void *address, unsigned int length, const char * label) __attribute__ ((unused)) ;

static inline char cfix(char x)
  {
    return ( x >= 0x20 && x < 0x80 ) ? x : '.' ;
  }
static void dumpmem(const void *address, unsigned int length, const char * label)
  {
    int x ;
    TRACEN(k_t_fifocontents|k_t_scattergather|k_t_request,"(>)Memory dump length=0x%08x: %s",length,label) ;
    for (x=0;x<length;x+=32)
      {
        int *v __attribute__ ((unused)) = (int *)(address+x) ;
        char *c __attribute__ ((unused)) = (char *)(address+x) ;
        TRACEN(k_t_fifocontents|k_t_scattergather|k_t_request,"%p: %08x %08x %08x %08x %08x %08x %08x %08x %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
            v,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],
            cfix(c[0]),cfix(c[1]),cfix(c[2]),cfix(c[3]),
            cfix(c[4]),cfix(c[5]),cfix(c[6]),cfix(c[7]),
            cfix(c[8]),cfix(c[9]),cfix(c[10]),cfix(c[11]),
            cfix(c[12]),cfix(c[13]),cfix(c[14]),cfix(c[15]),
            cfix(c[16]),cfix(c[17]),cfix(c[18]),cfix(c[19]),
            cfix(c[20]),cfix(c[21]),cfix(c[22]),cfix(c[23]),
            cfix(c[24]),cfix(c[25]),cfix(c[26]),cfix(c[27]),
            cfix(c[28]),cfix(c[29]),cfix(c[30]),cfix(c[31])
                    ) ;
      }
    TRACEN(k_t_fifocontents|k_t_scattergather|k_t_request,"(<)Memory dump") ;
  }

static void dumpframe(const void *address, unsigned int length, const char * label)
  {
    int x ;
    unsigned int limlen = (length>1024) ? 1024 : length ;
    TRACEN(k_t_fifocontents,"(>)ethframe dump length=%d: %s",length,label) ;
    for (x=0;x<limlen;x+=32)
      {
        int *v __attribute__ ((unused)) = (int *)(address+x) ;
        char *c __attribute__ ((unused)) = (char *)(address+x) ;
        TRACEN(k_t_fifocontents,"%p: %08x %08x %08x %08x %08x %08x %08x %08x %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
            v,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],
            cfix(c[0]),cfix(c[1]),cfix(c[2]),cfix(c[3]),
            cfix(c[4]),cfix(c[5]),cfix(c[6]),cfix(c[7]),
            cfix(c[8]),cfix(c[9]),cfix(c[10]),cfix(c[11]),
            cfix(c[12]),cfix(c[13]),cfix(c[14]),cfix(c[15]),
            cfix(c[16]),cfix(c[17]),cfix(c[18]),cfix(c[19]),
            cfix(c[20]),cfix(c[21]),cfix(c[22]),cfix(c[23]),
            cfix(c[24]),cfix(c[25]),cfix(c[26]),cfix(c[27]),
            cfix(c[28]),cfix(c[29]),cfix(c[30]),cfix(c[31])
                    ) ;
      }
    TRACEN(k_t_fifocontents,"(<)ethframe dump") ;
  }
#else
static inline void dumpmem(const void *address, unsigned int length, const char * label) __attribute__ ((unused)) ;
static inline void dumpmem(const void *address, unsigned int length, const char * label)
  {
  }
static void dumpframe(const void *address, unsigned int length, const char * label) __attribute__ ((unused)) ;
static void dumpframe(const void *address, unsigned int length, const char * label)
  {
  }
#endif

static void dumpRecFifoGroup(DMA_RecFifoGroup_t * recFifoGroup) __attribute__ ((unused)) ;
static void dumpRecFifoGroup(DMA_RecFifoGroup_t * recFifoGroup)
  {
    TRACEN(k_t_request,"(>)recFifoGroup=%p",recFifoGroup) ;
    if( recFifoGroup != NULL )
      {
        TRACEN(k_t_request,"group_id=%d num_normal_fifos=%d num_hdr_fifos=%d mask=%08x status_ptr=%p",
            recFifoGroup->group_id,recFifoGroup->num_normal_fifos,recFifoGroup->num_hdr_fifos,recFifoGroup->mask,recFifoGroup->status_ptr
            ) ;
        TRACEN(k_t_request,"not_empty=%08x%08x available=%08x%08x threshold_crossed=%08x%08x",
            recFifoGroup->status_ptr->not_empty[0],recFifoGroup->status_ptr->not_empty[1],
            recFifoGroup->status_ptr->available[0],recFifoGroup->status_ptr->available[1],
            recFifoGroup->status_ptr->threshold_crossed[0],recFifoGroup->status_ptr->threshold_crossed[1]
            ) ;
        TRACEN(k_t_request,"fifos[0] global_fifo_id=%d type=%d num_packets_processed_since_moving_fifo_head=%d",
            recFifoGroup->fifos[0].global_fifo_id,
            recFifoGroup->fifos[0].type,
            recFifoGroup->fifos[0].num_packets_processed_since_moving_fifo_head
        ) ;
        TRACEN(k_t_request,"fifos[0] fifo_hw_ptr=%p free_space=%08x fifo_size=%08x pa_start=%08x va_start=%p va_head=%p va_tail=%p va_end=%p %s",
            recFifoGroup->fifos[0].dma_fifo.fifo_hw_ptr,
            recFifoGroup->fifos[0].dma_fifo.free_space,
            recFifoGroup->fifos[0].dma_fifo.fifo_size,
            recFifoGroup->fifos[0].dma_fifo.pa_start,
            recFifoGroup->fifos[0].dma_fifo.va_start,
            recFifoGroup->fifos[0].dma_fifo.va_head,
            recFifoGroup->fifos[0].dma_fifo.va_tail,
            recFifoGroup->fifos[0].dma_fifo.va_end,
            (recFifoGroup->fifos[0].dma_fifo.free_space != recFifoGroup->fifos[0].dma_fifo.fifo_size) ? "!!!" : ""
        ) ;
        if( recFifoGroup->fifos[0].dma_fifo.fifo_hw_ptr != NULL )
          {
            TRACEN(k_t_request,"hwfifos[0] pa_start=%08x pa_end=%08x pa_head=%08x pa_tail=%08x %s",
                recFifoGroup->fifos[0].dma_fifo.fifo_hw_ptr->pa_start,
                recFifoGroup->fifos[0].dma_fifo.fifo_hw_ptr->pa_end,
                recFifoGroup->fifos[0].dma_fifo.fifo_hw_ptr->pa_head,
                recFifoGroup->fifos[0].dma_fifo.fifo_hw_ptr->pa_tail,
                (recFifoGroup->fifos[0].dma_fifo.fifo_hw_ptr->pa_head != recFifoGroup->fifos[0].dma_fifo.fifo_hw_ptr->pa_tail) ? "!!!" : ""
                ) ;
          }
      }
    TRACEN(k_t_request,"(<)") ;

  }

static void dumpInjFifoGroup(DMA_InjFifoGroup_t * injFifoGroup) __attribute__ ((unused)) ;
static void dumpInjFifoGroup(DMA_InjFifoGroup_t * injFifoGroup)
  {
    TRACEN(k_t_request,"(>)injFifoGroup=%p",injFifoGroup) ;
    if( injFifoGroup != NULL )
      {
        DMA_InjFifoStatus_t *injStatus = injFifoGroup->status_ptr ;
        int x ;
        TRACEN(k_t_request,"status_ptr=%p permissions=0x%08x group_id=%d",
            injFifoGroup->status_ptr, injFifoGroup->permissions, injFifoGroup->group_id) ;
        if( injStatus)
          {
            unsigned int available = injStatus->available ;
            TRACEN(k_t_request,"status not_empty=0x%08x available=0x%08x threshold_crossed=0x%08x activated=0x%08x",
                injStatus->not_empty, available, injStatus->threshold_crossed, injStatus->activated
            ) ;
            for( x=0; x<DMA_NUM_INJ_FIFOS_PER_GROUP; x+=1)
              {
                if( (0x80000000 >> x) & available)
                  {
                    DMA_InjFifo_t *fifo=injFifoGroup->fifos+x ;
                    DMA_FifoHW_t *hw_ptr = fifo->dma_fifo.fifo_hw_ptr ;
                    if( fifo->occupiedSize)
                      {
                    TRACEN(k_t_request, " fifos[%d] fifo_id=%d desc_count=0x%08x%08x occupiedSize=0x%08x priority=%d local=%d ts_inj_map=0x%02x %s",
                        x, fifo->fifo_id, (unsigned int)(fifo->desc_count >> 32),(unsigned int)(fifo->desc_count), fifo->occupiedSize, fifo->priority, fifo->local, fifo->ts_inj_map,
                        (fifo->occupiedSize) ? "!!!" : ""
                    ) ;
                      }
                    if( fifo->dma_fifo.va_head != fifo->dma_fifo.va_tail)
                      {
                    TRACEN(k_t_request," fifos[%d] fifo_hw_ptr=%p free_space=%08x fifo_size=%08x pa_start=%08x va_start=%p va_head=%p va_tail=%p va_end=%p",
                        x,
                        hw_ptr,
                        fifo->dma_fifo.free_space,
                        fifo->dma_fifo.fifo_size,
                        fifo->dma_fifo.pa_start,
                        fifo->dma_fifo.va_start,
                        fifo->dma_fifo.va_head,
                        fifo->dma_fifo.va_tail,
                        fifo->dma_fifo.va_end
                    ) ;
                      }
                    if( hw_ptr)
                      {
                        if( hw_ptr->pa_head != hw_ptr->pa_tail)
                          {
                        TRACEN(k_t_request," hwfifos[%d] pa_start=%08x pa_end=%08x pa_head=%08x pa_tail=%08x %s",
                            x,
                            hw_ptr->pa_start,
                            hw_ptr->pa_end,
                            hw_ptr->pa_head,
                            hw_ptr->pa_tail,
                            (hw_ptr->pa_head != hw_ptr->pa_tail) ? "!!!" : ""
                            ) ;
                          }
                      }
                  }
              }
          }
      }
    TRACEN(k_t_request,"(<)") ;
  }

static void bgp_dma_tcp_set_mtu(dma_tcp_t *dma_tcp, unsigned int mtu) __attribute__ ((unused)) ;
static void bgp_dma_tcp_set_mtu(dma_tcp_t *dma_tcp, unsigned int mtu)
  {
    unsigned int max_packets_per_frame=(mtu+k_torus_link_payload_size-1) / k_torus_link_payload_size ;
    unsigned int max_packets_per_frame2=(mtu+k_injection_packet_size-1) / k_injection_packet_size ;
    unsigned int mtu1=max_packets_per_frame * k_torus_link_payload_size + k_torus_skb_alignment ;
    unsigned int mtu2=max_packets_per_frame2 * k_injection_packet_size + k_torus_skb_alignment ;
    dma_tcp->max_packets_per_frame = max_packets_per_frame ;
    dma_tcp->mtu = (mtu1>mtu2) ? mtu1 : mtu2 ;
  }

/*  Test if we think a socket is affected by torus congestion */
unsigned int bgp_torus_congestion(struct sock *sk) ;


static inline unsigned int stack_pointer(void)
{
    uint32_t value;
    asm volatile ("mr %0,1": "=r" (value) );
    return value;
}

/*  Fragment reassembly control for 'frames' */
/*
 * When the first packet of a frame arrives, examine the eth and ip headers to allocate a skbuff which will have
 * enough data for the frame. Arrange to assemble the first fragment into the data area.
 *
 * When the last packet of a fragment arrives, we know whether the frame is complete. If it is a one-frag frame,
 * hand it off. I
 */

typedef struct
{
	unsigned int frame_size ;   /*  IP frame size, from IP header */
	unsigned int frag_size ;   /*  fragment size */
	unsigned int frag_pad_head ;  /*  Displacement of first byte of first fragment from alignment */
	unsigned int fragment_index ;  /*  Index of fragment, starts at 0 */
	unsigned int bytes_accounted_for ;  /*  Number of bytes in accounted for including the current fragment */
	unsigned char * frag_base ;  /*  Where to pack this frag down to */
	unsigned char * frag_data ;  /*  First byte free after current fragment is received */
	unsigned char * frag_payload ;  /*  Aligned address to drop first packet of next fragment into skb */
} fragment_reassembler;

static inline fragment_reassembler * frag_re(struct sk_buff *skb)
{
	return (fragment_reassembler *) &(skb->cb) ;
}

void dma_tcp_show_reception(dma_tcp_t * dma_tcp) ;

int proc_do_dma_rec_counters(struct ctl_table *ctl, int write, struct file * filp,
               void __user *buffer, size_t *lenp, loff_t *ppos) ;
extern int bgp_dma_tcp_counter_copies[DMA_NUM_COUNTERS_PER_GROUP] ;
static void show_dma_descriptor(DMA_InjDescriptor_t *d) __attribute((unused)) ;
static void show_dma_descriptor(DMA_InjDescriptor_t *d)
{
	unsigned int * di = (unsigned int *) d ;
	TRACEN(k_t_request,"DMA_InjDescriptor_t(0x%08x 0x%08x 0x%08x 0x%08x (0x%08x 0x%08x 0x%08x 0x%08x))",
			d->word1, d->word2, d->base_offset, d->msg_length, d->hwHdr.word0, d->hwHdr.word1, d->hwHdr.word2, d->hwHdr.word3) ;
	TRACEN(k_t_request,"prefetch_only=%d local_copy=%d",(di[0] >> 1)& 1,di[0] & 1) ;
}

typedef struct
{
	long long int sxx ;
	long long int sxy ;
/* 	long long int m0 ; */
/* 	long long int m1 ; */
/* 	long long int det ; */
	int s1 ;
	int sx ;
	int sy ;
} dma_statistic_t ;
extern dma_statistic_t bgp_dma_rate ;

enum {
  k_injCounterId = 0 // Injection counter number to use
} ;

/*  Support for freeing 'a few' skbuffs when outbound DMA is complete each time we go around */
enum {
	k_skb_group_count = 8
};
typedef struct {
	unsigned int count ;
	struct sk_buff * group[k_skb_group_count] ;
} skb_group_t ;
static void skb_group_init(skb_group_t * skb_group) __attribute__((unused)) ;
static void skb_group_init(skb_group_t * skb_group)
{
	skb_group->count = 0 ;
}


static void skb_group_add(skb_group_t * skb_group, struct sk_buff * skb) __attribute__((unused)) ;
static void skb_group_add(skb_group_t * skb_group, struct sk_buff * skb)
{
	unsigned int count=skb_group->count ;
	if( count < k_skb_group_count )
		{
			skb_group->group[count] = skb ;
			TRACEN(k_t_general,"Queueing skb_group->group[%d]=%p for free",count,skb) ;
			skb_group->count = count+1 ;
		}
	else
		{
			TRACEN(k_t_error,"Overrunning queue of skbs to free skb=%p",skb) ;
			dev_kfree_skb(skb) ;
		}
}
static void skb_group_free(skb_group_t * skb_group) __attribute__((unused)) ;
static void skb_group_free(skb_group_t * skb_group)
{
	unsigned int count=skb_group->count ;
	unsigned int index ;
	struct sk_buff ** skb_array=skb_group->group ;
	BUG_ON(count > k_skb_group_count) ;
	if( count > k_skb_group_count) count=k_skb_group_count ;
	for(index=0;index<count;index+=1)
		{
			TRACEN(k_t_general,"freeing skb_array[%d]=%p",index,skb_array[index]) ;
			if( skb_array[index])
				{
					dev_kfree_skb(skb_array[index]) ;
					skb_array[index]=NULL ;
				}
		}
}

static void skb_group_queue_seq(skb_group_t * group, struct sk_buff ** skb_array, unsigned int count
#if defined(TRACK_LIFETIME_IN_FIFO)
		       , unsigned int core, unsigned int desired_fifo, unsigned long long now, unsigned int x
#endif
		)
{
	unsigned int index ;

	for( index=0 ; index<count; index+=1)
		{
			if( skb_array[index])
				{
#if defined(TRACK_LIFETIME_IN_FIFO)
					struct sk_buff *skb=skb_array[index] ;
					unsigned long long lifetime_in_fifo = now - *(unsigned long long *) skb_array[index]->cb ;
					TRACEN(k_t_detail ,"core=%d desired_fifo=%d lifetime=0x%016llx",core, desired_fifo,lifetime_in_fifo) ;
					if( skb->len >= 4096 && desired_fifo < k_injecting_directions && lifetime_in_fifo > max_lifetime_by_direction[desired_fifo])
						{
							max_lifetime_by_direction[desired_fifo] = lifetime_in_fifo ;
						}
					if( skb->len >= 4096 && lifetime_in_fifo > 0x7fffffff)
						{
							struct sock   *sk=skb->sk ;
							struct inet_sock *inet = inet_sk(sk);
							struct inet_connection_sock *icsk = inet_csk(sk);
							unsigned int daddr=inet->daddr ;
							unsigned int flags = TCP_SKB_CB(skb)->flags ;
						        TRACEN(k_t_congestion,"sk=%p skb=%p data=%p len=%d flags=0x%02x ip=%u.%u.%u.%u x=%d in-fifo-time=0x%016llx",
						            sk, skb, skb->data, skb->len, flags,
						            daddr>>24, (daddr>>16)&0xff,(daddr>>8)&0xff,daddr&0xff,
						            x+index,
						            lifetime_in_fifo
						             ) ;
						}
#endif
					skb_group_add(group,skb_array[index]) ;
					skb_array[index] = NULL ;
				}
		}
}
static void skb_group_queue(skb_group_t * group, struct sk_buff ** skb_array, unsigned int start, unsigned int count
#if defined(TRACK_LIFETIME_IN_FIFO)
        		       , unsigned int core, unsigned int desired_fifo, unsigned long long now
#endif
		) __attribute__ ((unused)) ;
static void skb_group_queue(skb_group_t * group, struct sk_buff ** skb_array, unsigned int start, unsigned int count
#if defined(TRACK_LIFETIME_IN_FIFO)
        		       , unsigned int core, unsigned int desired_fifo, unsigned long long now
#endif
		)
{
	TRACEN(k_t_detail , "Queuing skbs for freeing start=%d count=%d", start, count) ;
	if( start+count <= k_injection_packet_count)
		{
			skb_group_queue_seq(group,skb_array+start, count
#if defined(TRACK_LIFETIME_IN_FIFO)
					        		       , core, desired_fifo, now, 0
#endif
			) ;
		}
	else
		{
			skb_group_queue_seq(group,skb_array+start, k_injection_packet_count-start
#if defined(TRACK_LIFETIME_IN_FIFO)
					        		       , core, desired_fifo, now,0
#endif
					) ;
			skb_group_queue_seq(group,skb_array, count - (k_injection_packet_count-start)
#if defined(TRACK_LIFETIME_IN_FIFO)
					        		       , core, desired_fifo, now,k_injection_packet_count-start
#endif
					)  ;
		}

}

/*  We will be using the injection machinery as circular buffers; this is the 'circle' function */
static inline unsigned int packet_mod(unsigned int index)
  {
    return index & (k_injection_packet_count-1) ;
  }

/*  Try to minimise the 'needless' spins if several cores try to inject contemporaneously -- not anymore, best not to overtake on a path */
static inline int injection_group_hash(dma_tcp_t *dma_tcp,int x,int y, int z)
{
/* 	return 0 ; */
	return ( x/2 + y/2 + z/2 ) & 3 & (dma_tcp->tuning_injection_hashmask);
}

#if defined(BARRIER_WITH_IOCTL)

static inline void timing_histogram(dma_tcp_t * dma_tcp)
{
	int current_tbl=get_tbl() ;
	int delta_tbl=current_tbl-dma_tcp->prev_tbl ;
	dma_tcp->timing_histogram_buckets[fls(delta_tbl)] += 1 ;
	dma_tcp->prev_tbl = current_tbl ;

}
#endif


static inline int wrapped_DMA_InjFifoInjectDescriptorById(
		DMA_InjFifoGroup_t    *fg_ptr,
		int                    fifo_id,
		DMA_InjDescriptor_t   *desc
		)
{
	int rc ;
	rc = DMA_InjFifoInjectDescriptorById(fg_ptr,fifo_id,desc) ;
	return rc ;
}



/* #define AUDIT_HEADLEN */
/* #define TRACK_LIFETIME_IN_FIFO */

typedef struct
{
	DMA_InjDescriptor_t desc ;
#if defined(TRACK_LIFETIME_IN_FIFO)
	unsigned long long injection_timestamp ;
#endif
#if defined(AUDIT_HEADLEN)
	unsigned short tot_len ;
#endif
	char free_when_done ;
} frame_injection_cb ;
extern unsigned int tot_len_for_rcv[DMA_NUM_COUNTERS_PER_GROUP] ; // TODO: fix the name if we leave it extern ...

#if defined(AUDIT_FRAME_HEADER)
typedef struct {
	struct ethhdr eth ;
	struct iphdr iph ;
} frame_header_t ;
extern frame_header_t all_headers_in_counters[DMA_NUM_COUNTERS_PER_GROUP] ; // TODO: fix the name if we leave it extern ...
#endif

static void dma_tcp_show_reception_one(dma_tcp_t * dma_tcp, unsigned int x, unsigned int counter_value)  __attribute__((unused)) ;
static void dma_tcp_show_reception_one(dma_tcp_t * dma_tcp, unsigned int x, unsigned int counter_value)
{
	struct sk_buff *skb=dma_tcp->rcv_skbs[x] ;
	if( skb)
		{
			  struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
			  unsigned int eth_proto = eth->h_proto ;

			  struct iphdr *iph = (struct iphdr *) (eth+1) ;
			  unsigned int tot_len=iph->tot_len ;
			  unsigned int saddr=iph->saddr ;
			  if( tot_len != tot_len_for_rcv[x])
				  {
					  TRACEN(k_t_error,"(!!!) tot_len trampled") ;
				  }

			  TRACEN(k_t_request,"(---) skb=%p eth_proto=0x%04x tot_len=0x%04x saddr=%d.%d.%d.%d slot=0x%08x conn_id=0x%02x tot_len_for_rcv=0x%04x counter_value=0x%04x",
					  skb,eth_proto,tot_len,saddr>>24, (saddr >> 16) & 0xff,(saddr >> 8) & 0xff, saddr & 0xff, dma_tcp->slot_for_rcv[x], dma_tcp->conn_for_rcv[x], tot_len_for_rcv[x],counter_value
					                                                                                                                                           ) ;
			  dumpmem(skb->data,0x42,"eth-ip-tcp header") ;
			  show_dma_descriptor((DMA_InjDescriptor_t *)&skb->cb) ;
#if defined(AUDIT_FRAME_HEADER)
			if(memcmp(skb->data,((char *)(all_headers_in_counters+x)),32))
				{
					  TRACEN(k_t_request,"(!!!) header not as first seen") ;
					  dumpmem(skb->data-14,sizeof(frame_header_t),"header-now") ;
					  dumpmem(all_headers_in_counters+x,sizeof(frame_header_t),"header-in-propose") ;

				}
#endif
		}
	else
		{
			TRACEN(k_t_error|k_t_request,"(E) x=%d Counter in use but no skb !",x) ;
		}

}

void dma_tcp_set_port(unsigned int port) ;  // Intended for configuring which quarter of the BGP DMA unit to use
void dma_tcp_open(void) ; // 'ifconfig up' handler
void dma_tcp_close(void) ; // 'ifconfig down' handler

void dma_tcp_diagnose_init(dma_tcp_t *dma_tcp) ;

void __init
bgp_dma_memcpyInit(dma_tcp_t *dma_tcp) ;

void __init
dma_tcp_devfs_procfs_init(dma_tcp_t *dma_tcp) ;

#if defined(TRACK_LIFETIME_IN_FIFO)
extern unsigned long long max_lifetime_by_direction[k_injecting_directions] ;
#endif

#if defined(CONFIG_BGP_TORUS_DIAGNOSTICS)
extern int tcp_scattergather_frag_limit  ;
#endif

typedef struct { unsigned char c[240] ; } torus_frame_payload ;

#endif
