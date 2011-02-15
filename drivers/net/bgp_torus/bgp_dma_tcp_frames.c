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
 *
 * Intent: Carry torus packets as messages into memory FIFOs, and interpret them
 *          as eth frames for TCP
 *         Later on, add token-based flow control with a view to preventing
 *          congestion collapse as the machine gets larger and the loading gets higher
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
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/ip.h>


#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/bitops.h>
#include <linux/vmalloc.h>

#include <linux/dma-mapping.h>

#include <net/inet_connection_sock.h>
#include <net/inet_sock.h>
#include <net/inet_hashtables.h>
#include <net/tcp.h>



/* #include "bglink.h" */
#include <spi/linux_kernel_spi.h>

#include <asm/time.h>

/* #define CONFIG_BLUEGENE_TORUS_TRACE */
/* #define CRC_CHECK_FRAMES */
#define VERIFY_TARGET
/* #define SIDEBAND_TIMESTAMP */

#include "bgp_dma_tcp.h"
#include "bgp_bic_diagnosis.h"


static inline void frames_receive_torus(dma_tcp_t *dma_tcp,struct sk_buff * skb)
{
#if defined(CONFIG_BGP_STATISTICS)
	struct ethhdr *eth = (struct ethhdr *) (skb->data) ;
	struct iphdr *iph=(struct iphdr *) (eth+1) ;
	dma_tcp->bytes_received += iph->tot_len ;
#endif
    bgtornet_receive_torus(skb);
}

#if defined(TRACK_LIFETIME_IN_FIFO)
unsigned long long max_lifetime_by_direction[k_injecting_directions] ;
#endif

static void diag_skb_structure(struct sk_buff *skb)
{
	int f=skb_shinfo(skb)->nr_frags ;
	if(0 == f)
		{
			TRACEN(k_t_sgdiag,"len=0x%04x data_len=0x%04x frags=0 [0x%04x]",skb->len, skb->data_len, skb_headlen(skb)) ;
		}
	else if(1 == f)
		{
			TRACEN(k_t_sgdiag,"len=0x%04x data_len=0x%04x frags=1 [0x%04x 0x%04x]",skb->len, skb->data_len, skb_headlen(skb),
					skb_shinfo(skb)->frags[0].size
					) ;
		}
	else if(2 == f)
		{
			TRACEN(k_t_sgdiag,"len=0x%04x data_len=0x%04x frags=2 [0x%04x 0x%04x 0x%04x]",skb->len, skb->data_len, skb_headlen(skb),
					skb_shinfo(skb)->frags[0].size,
					skb_shinfo(skb)->frags[1].size
					) ;
		}
	else
		{
			TRACEN(k_t_sgdiag,"len=0x%04x data_len=0x%04x frags=%d [0x%04x 0x%04x 0x%04x 0x%04x ..]",skb_shinfo(skb)->nr_frags,
					skb->len, skb->data_len, skb_headlen(skb),
					skb_shinfo(skb)->frags[0].size,
					skb_shinfo(skb)->frags[1].size,
					skb_shinfo(skb)->frags[2].size
					) ;
		}
	if( TRACING(k_t_sgdiag_detail))
		{
			unsigned int dump_length = ( skb_headlen(skb) < 256 ) ? skb_headlen(skb) : 256 ;
			dumpmem(skb->data, dump_length, "skb_head") ;
		}
}

static inline int torus_frame_payload_memcpy(
                torus_frame_payload * target,
                torus_frame_payload * source
    )
{
	*target = *source ;
	return 0 ;
}

/*  This is as per the powerpc <asm/time.h> 'get_tb' */
/*  Dup'd here because we have to compile with ppc also, which doesn't have it defined */
static inline u64 get_powerpc_tb(void)
{
  unsigned int tbhi, tblo, tbhi2;

  tbhi = get_tbu();
  tblo = get_tbl();
  tbhi2 = get_tbu();
  /* tbhi2 might be different from tbhi, but that would indicate that there had been a 32-bit carry.
   * In that case (tbhi2,0) would be a reasonable representation of the timestamp that we usually
   * think of as being (tbhi,tblo)
   */
  if( tbhi == tbhi2)
	  {
		  return ((u64)tbhi << 32) | tblo;
	  }
  return ((u64)tbhi2 << 32) ;
}
static void display_skb_structure(struct sk_buff *skb) ;

static torus_frame_payload dummy_payload __attribute__((aligned(16)));
static inline void demux_vacate_slot(dma_tcp_t * dma_tcp, unsigned int slot)
  {
    set_rcv_payload(&dma_tcp->rcvdemux, slot, (char *)&dummy_payload);
    set_rcv_payload_alert(&dma_tcp->rcvdemux, slot, (char *)&dummy_payload);
    set_rcv_expect(&dma_tcp->rcvdemux, slot, 0xffffffff);
    set_rcv_skb(&dma_tcp->rcvdemux, slot, NULL);
    TRACEN(k_t_general,"Slot %d vacated", slot );
  }

static inline void demux_show_slot(dma_tcp_t * dma_tcp, unsigned int slot)
  {
    void *payload = get_rcv_payload(&dma_tcp->rcvdemux, slot);
    void *alert = get_rcv_payload_alert(&dma_tcp->rcvdemux, slot);
    unsigned int expect=get_rcv_expect(&dma_tcp->rcvdemux, slot);
    struct sk_buff *skb=get_rcv_skb(&dma_tcp->rcvdemux, slot);
    if( payload != &dummy_payload || expect != 0xffffffff || skb )
      {
        TRACEN(k_t_error,"(E) not-vacant slot=%08x (%d %d) payload=%p alert=%p expect=0x%08x skb=%p",
            slot, slot>>2, slot&3, payload, alert, expect, skb
            ) ;
      }
  }

static void init_demux_table(dma_tcp_t * dma_tcp, unsigned int node_count ) ;

static void init_demux_table(dma_tcp_t * dma_tcp, unsigned int node_count )
  {
  unsigned int x ;
  for( x = 0 ; x < k_slots_per_node*node_count ; x += 1)
    {
      demux_vacate_slot(dma_tcp,x) ;
#if defined(ENABLE_LATENCY_TRACKING)
      rcv_statistic_clear(&(dma_tcp->rcvdemux.rcv_per_slot_vector[x].latency));
/*       set_min_latency(&dma_tcp->rcvdemux, x, 0x7fffffff) ; */
/*       set_max_latency(&dma_tcp->rcvdemux, x, 0x80000000) ; */
#endif
    }
  }


static void show_protocol_header_tx(char * frame) __attribute__ ((unused)) ;
static void show_protocol_header_tx(char * frame)
  {
    int * f = (int *) frame ;
    TRACEN(k_t_request,"%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
        f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9],f[10],f[11],f[12],f[13],f[14],f[15],f[16]
        );
  }

static void show_protocol_header_fault(char * frame) __attribute__ ((unused)) ;
static void show_protocol_header_fault(char * frame)
  {
    int * f = (int *) frame ;
    TRACEN(k_t_error,"%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
        f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9],f[10],f[11],f[12],f[13],f[14],f[15],f[16]
        );
  }

static void show_protocol_header_rx(char * frame) __attribute__ ((unused)) ;
static void show_protocol_header_rx(char * frame)
  {
    int * f = (int *) frame ;
    TRACEN(k_t_general,"%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
        f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9],f[10],f[11],f[12],f[13],f[14],f[15],f[16]
        );
  }

/*  Polynomial picked as CRC-32-IEEE 802.3 from http://en.wikipedia.org/wiki/Cyclic_redundancy_check */
static int frametrace_rx(char * address, int length ) __attribute__ ((unused)) ;
static int frametrace_rx(char * address, int length )
  {
    int * a = (int *) address ;
    int x ;
    int csum32 = a[0] ;
    for(x=1;x<(length/sizeof(int));x+=1)
      {
        csum32 = (csum32 << 1 ) ^ a[x] ^ ( (csum32 & 0x80000000) ? 0x04C11DB7 : 0 ) ;
      }
    TRACEN(k_t_general,"address=%p length=%d csum32=0x%08x",address,length,csum32) ;
    return csum32 ;
  }

static int frametrace_tx(char * address, int length ) __attribute__ ((unused)) ;
static int frametrace_tx(char * address, int length )
  {
    int * a = (int *) address ;
    int x ;
    int csum32 = a[0] ;
    for(x=1;x<(length/sizeof(int));x+=1)
      {
        csum32 = (csum32 << 1 ) ^ a[x] ^ ( (csum32 & 0x80000000) ? 0x04C11DB7 : 0 ) ;
      }
    TRACEN(k_t_general,"address=%p length=%d csum32=0x%08x",address,length,csum32) ;
    return csum32 ;
  }

/*  For diagnosis, put the local clock into the packet. Drop 4 lsbs off the 64-bit clock. */
static unsigned int latency_timestamp(void) __attribute__ ((unused)) ;
static unsigned int latency_timestamp(void)
  {
    unsigned int tbu = get_tbu() ;
    unsigned int tbl = get_tbl() ;
    unsigned int tbu2 = get_tbu() ;
    unsigned int tbl2 = (tbu==tbu2) ? tbl : 0 ;
    return (tbu2 << 28) | (tbl2 >> 4) ;
  }



static void spot_examine_tcp_timestamp(int tsval, int tsecr)
{
	    if( tsecr != 0 )
		    {
			    int rtt=jiffies-tsecr ;
			    TRACEN(k_t_general,"rtt=%d",rtt) ;
#if defined(CONFIG_BGP_STATISTICS)
			    rtt_histogram[fls(rtt)] += 1 ;
#endif
		    }
	    if( tsval != 0 )
		    {
			    int transit=jiffies-tsval ;
			    TRACEN(k_t_general,"transit=%d",transit) ;
#if defined(CONFIG_BGP_STATISTICS)
			    if( transit >= 0)
				    {
					    transit_histogram[fls(transit)] += 1 ;
				    }
#endif
		    }

}

static void spot_parse_aligned_timestamp(struct tcphdr *th)
{
	__be32 *ptr = (__be32 *)(th + 1);
	int tsecr ;
	int tsval ;
	if (*ptr == htonl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16)
			  | (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP)) {
		++ptr;
		tsval = ntohl(*ptr);
		++ptr;
		tsecr = ntohl(*ptr);
#if defined(CONFIG_BGP_TORUS)
		spot_examine_tcp_timestamp(tsval,tsecr) ;
#endif
	}
}

static void spot_fast_parse_options(struct sk_buff *skb, struct tcphdr *th)
{
	if (th->doff == sizeof(struct tcphdr) >> 2) {
		return;
	} else if (
		   th->doff == (sizeof(struct tcphdr)>>2)+(TCPOLEN_TSTAMP_ALIGNED>>2)) {
		spot_parse_aligned_timestamp( th) ;
	}
}

static inline void analyse_tcp_flags(dma_tcp_t * dma_tcp,struct sk_buff * skb)
{
#if defined(KEEP_TCP_FLAG_STATS)
        struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
        struct iphdr *iph = (struct iphdr *)(eth+1) ;
        unsigned int * iph_word = (unsigned int *) iph ;
        struct tcphdr * tcph = (struct tcphdr *)(iph_word+(iph->ihl)) ;
        unsigned int eth_proto = eth->h_proto ;
        unsigned int ip_proto = iph->protocol ;
        if( eth_proto == ETH_P_IP && ip_proto == IPPROTO_TCP )
        	{
                     unsigned int flag_fin = tcph->fin ;
                     unsigned int flag_syn = tcph->syn ;
                     unsigned int flag_rst = tcph->rst ;
                     unsigned int flag_psh = tcph->psh ;
                     unsigned int flag_ack = tcph->ack ;
                     unsigned int flag_urg = tcph->urg ;
                     unsigned int flag_ece = tcph->ece ;
                     unsigned int flag_cwr = tcph->cwr ;
                     dma_tcp->tcp_received_flag_count[7] += flag_fin ;
                     dma_tcp->tcp_received_flag_count[6] += flag_syn ;
                     dma_tcp->tcp_received_flag_count[5] += flag_rst ;
                     dma_tcp->tcp_received_flag_count[4] += flag_psh ;
                     dma_tcp->tcp_received_flag_count[3] += flag_ack ;
                     dma_tcp->tcp_received_flag_count[2] += flag_urg ;
                     dma_tcp->tcp_received_flag_count[1] += flag_ece ;
                     dma_tcp->tcp_received_flag_count[0] += flag_cwr ;
                     spot_fast_parse_options(skb,tcph) ;
        	}

#endif
}

static inline int deliver_eagerly(const dma_tcp_t * dma_tcp)
{
	return dma_tcp->tuning_deliver_eagerly ;
}
/*
 * Frames from a source generally arrive in the order that they left the sender, but it is possible for some
 * nondeterminism to be introduced because of adaptive routing and because 'short' frames get sent 'eagerly' rather than
 * with DMA.
 * It is desireable to deliver frames for a given TCP session in-order, otherwise the network layer may call for a
 * 'fast' retransmit (thinking that a frame has been lost). This routine defers out-of-order frames until they can be
 * presnted in-order.
 */
static void deliver_from_slot(dma_tcp_t * dma_tcp, unsigned int slot, unsigned int conn_id, struct sk_buff * skb)
{
	if( ! deliver_eagerly(dma_tcp))
		{
		unsigned int slot_conn=get_rcv_conn_pending_delivery(&dma_tcp->rcvdemux,slot) ;
		unsigned int slot_advancement= (conn_id-slot_conn) & (k_concurrent_receives-1) ;
		TRACEN(k_t_general,"slot=0x%08x conn_id=0x%08x slot_conn=0x%08x skb=%p slot_advancement=%d",slot,conn_id,slot_conn,skb,slot_advancement) ;
	#if defined(CONFIG_BGP_STATISTICS)
		dma_tcp->resequence_histogram[slot_advancement] += 1;
	#endif
		if( 0 == slot_advancement)
				{
					 /*  'oldest' skb has arrived. Deliver it */
					frames_receive_torus(dma_tcp,skb) ;
					 /*  and check if any 'arrivals ahead' can be delivered now */
					{
						int x ;
						struct sk_buff * slot_skb  ;
						for(x=1; x<k_concurrent_receives-1 && (NULL != (slot_skb = get_rcv_skb_pending_resequence(&dma_tcp->rcvdemux,slot,slot_conn+x))); x+=1)
							{
								TRACEN(k_t_general,"Delivering slot=0x%08x conn_id=0x%08x skb=%p",slot,slot_conn+x,slot_skb) ;
								frames_receive_torus(dma_tcp,slot_skb) ;
								set_rcv_skb_pending_resequence(&dma_tcp->rcvdemux,slot,slot_conn+x,NULL) ;
							}
						set_rcv_conn_pending_delivery(&dma_tcp->rcvdemux,slot,slot_conn+x) ;
					}
				}
		else
			{
				struct sk_buff * slot_skb_old = get_rcv_skb_pending_resequence(&dma_tcp->rcvdemux,slot,conn_id);
				TRACEN(k_t_general,"Queuing slot=0x%08x conn_id=0x%08x skb=%p skb->len=%d slot_skb_old=%p",slot,conn_id,skb,skb->len,slot_skb_old) ;
				if( slot_skb_old)
					{
						 /*  Wrapped around all the possible reorder slots. Something seems to have gone missing. */
						TRACEN(k_t_error,"(E) resequence buffer wrapped, skb=%p conn_id=0x%08x. Delivering ",skb,conn_id) ;
						 /*  and check if any 'arrivals ahead' can be delivered now */
						{
							int x ;
							struct sk_buff * slot_skb  ;
							for(x=0; x<k_concurrent_receives-1 && (NULL != (slot_skb = get_rcv_skb_pending_resequence(&dma_tcp->rcvdemux,slot,slot_conn+x))); x+=1)
								{
									TRACEN(k_t_general,"Delivering slot=0x%08x conn_id=0x%08x skb=%p",slot,slot_conn+x,slot_skb) ;
									frames_receive_torus(dma_tcp,slot_skb) ;
									set_rcv_skb_pending_resequence(&dma_tcp->rcvdemux,slot,slot_conn+x,NULL) ;
								}
							set_rcv_conn_pending_delivery(&dma_tcp->rcvdemux,slot,slot_conn+x) ;
							slot_conn = slot_conn+x ;
						}
						if( 0 == ((slot_conn-conn_id) & (k_concurrent_receives-1)))
								{
									 /*  Everything is delivered ... */
									frames_receive_torus(dma_tcp,skb) ;
									set_rcv_conn_pending_delivery(&dma_tcp->rcvdemux,slot,slot_conn+1) ;
								}
						else
							{
								 /*  There's another gap, save the skb for future delivery */
								set_rcv_skb_pending_resequence(&dma_tcp->rcvdemux,slot,conn_id,skb) ;
							}


					}
				else
					{
						set_rcv_skb_pending_resequence(&dma_tcp->rcvdemux,slot,conn_id,skb) ;
					}

			}
		}
	else
		{
			TRACEN(k_t_general,"slot=0x%08x conn_id=0x%08x skb=%p",slot,conn_id,skb) ;
			if( TRACING(k_t_sgdiag_detail))
				{
					unsigned int dump_length = ( skb_headlen(skb) < 256 ) ? skb_headlen(skb) : 256 ;
					dumpmem(skb->data, dump_length, "received skb") ;
				}
			frames_receive_torus(dma_tcp,skb) ;
		}

}

static void display_pending_slot(dma_tcp_t * dma_tcp,unsigned int slot)
{
#if defined(RESEQUENCE_ARRIVALS)
	unsigned int slot_conn=get_rcv_conn_pending_delivery(&dma_tcp->rcvdemux,slot) ;
	int x ;
	int pending_count=0;
	for(x=0; x<k_concurrent_receives; x+=1)
		{
			struct sk_buff * skb=get_rcv_skb_pending_resequence(&dma_tcp->rcvdemux,slot,slot_conn+x) ;
			if(skb)
				{
					struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
					struct iphdr *iph = (struct iphdr *) (eth+1) ;
					unsigned int saddr=iph->saddr ;
					pending_count += 1;
					TRACEN(k_t_request,
							"(---) Pending slot=0x%08x slot_conn=0x%02x x=%d skb=%p skb->len=%d tot_len=0x%04x saddr=%d.%d.%d.%d\n",
							slot,slot_conn & (k_concurrent_receives-1),x,skb,skb->len, iph->tot_len,
							saddr>>24,
							(saddr >> 16) & 0xff,
							(saddr >> 8) & 0xff,
							saddr & 0xff
							) ;
				}
		}
	if( pending_count >0 )
		{
			TRACEN(k_t_request,"slot=0x%08x pending_count=%d",slot,pending_count) ;
		}

#endif
}

void bgp_dma_tcp_display_pending_slots(dma_tcp_t * dma_tcp, unsigned int nodecount )
{
	unsigned int slot ;
	for( slot=0; slot<nodecount; slot+=1 )
		{
			display_pending_slot(dma_tcp,slot) ;
		}
}


static void issueInlineFrameDataSingle(dma_tcp_t * dma_tcp,
    void  * request ,
    unsigned int src_key ,
    int payload_bytes)
  {
   unsigned int pad_head = src_key & 0x0f ;
    TRACEN(k_t_detail | k_t_general,"(>)(%08x)", src_key);
    if( k_dumpmem_diagnostic)
	    {
		    dumpmem(request,payload_bytes,"issueInlineFrameData") ;
	    }
      {
/*  We have a packet which represents a complete frame; quite a small frame ... */
        struct ethhdr *eth = (struct ethhdr *) (request+pad_head) ;
        struct iphdr *iph = (struct iphdr *)(request+pad_head+sizeof(struct ethhdr)) ;
        if( eth->h_proto == ETH_P_IP)
          {
             unsigned int totlen=iph->tot_len ;
             int bytes_remaining = totlen+sizeof(struct ethhdr)+pad_head-payload_bytes ;
             TRACEN(k_t_detail,"Frame total length=%d",totlen) ;
             if( bytes_remaining <= 0)
               {
/*  Largest amount of data we might need is ... */
/*                  k_injection_packet_size+k_torus_skb_alignment */
                 struct sk_buff * skb = alloc_skb(k_injection_packet_size+k_torus_skb_alignment , GFP_ATOMIC);
                 if(skb )
                   {
                     char * payload ;
                     skb_reserve(skb, k_torus_skb_alignment - ((unsigned int)(skb->data)) % k_torus_skb_alignment);
                     payload = skb->data ;
/*  TODO: rewrite with 'memcpy' or a copy through integer regs, to avoid using FP now this is 'rare' */
/*                      torus_frame_payload_load(request) ; */
/*                      torus_frame_payload_store(payload) ; */
                     torus_frame_payload_memcpy((torus_frame_payload *)payload,(torus_frame_payload *)request) ;
                     TRACEN(k_t_detail,"(=)(%08x) skb=%p payload=%p bytes_remaining=%d", src_key,skb,skb->data,bytes_remaining);
                     skb_reserve(skb,pad_head) ;
                     skb_put(skb,totlen+sizeof(struct ethhdr)) ;
                     analyse_tcp_flags(dma_tcp, skb) ;
                     deliver_from_slot(dma_tcp,-1,-1,skb) ;
                   }
                 else
                   {
                     TRACEN(k_t_protocol,"(E) (%08x) skb was null", src_key);
                     dma_tcp->device_stats->rx_dropped += 1;
                     if( k_detail_stats)
                	     {
                		     dma_tcp->count_no_skbuff += 1 ;
                	     }
                   }
               }
             else
               {
                 TRACEN(k_t_protocol,"(E) frame does not fit packet, discarded");
                 dma_tcp->device_stats->rx_frame_errors += 1;
               }
          }
        else
          {
            TRACEN(k_t_protocol,"Packet not IP ethhdr=[%02x:%02x:%02x:%02x:%02x:%02x][%02x:%02x:%02x:%02x:%02x:%02x](%04x)",
                eth->h_dest[0],eth->h_dest[1],eth->h_dest[2],eth->h_dest[3],eth->h_dest[4],eth->h_dest[5],
                eth->h_source[0],eth->h_source[1],eth->h_source[2],eth->h_source[3],eth->h_source[4],eth->h_source[5],
                eth->h_proto) ;
            dma_tcp->device_stats->rx_frame_errors += 1;
          }
      }
    TRACEN(k_t_detail,"(<)((%08x)", src_key);
  }

static int issueInlineFrameDataSingleActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           )
  {
    unsigned int SW_Arg=packet_ptr->SW_Arg ;
/*     enable_kernel_fp() ; // TODO: don't think this is needed nowadays */

    issueInlineFrameDataSingle(
        (dma_tcp_t *) recv_func_parm,
        (void *) payload_ptr,
        SW_Arg,
        payload_bytes
        ) ;
    return 0 ;
  }

#if defined(USE_ADAPTIVE_ROUTING)
typedef struct
{
	unsigned int conn_id ;
	unsigned int packet_count ;
	unsigned int packets_to_go ;
	int framestart_offset ;
	int prev_offset ;  /*  For constructing 'reordering' statistics */
} adaptive_skb_cb_t;

static void issueInlineFrameDataAdaptive(dma_tcp_t * dma_tcp,
    void  * request ,
    unsigned int src_key ,
    int payload_bytes,
    int Put_Offset
    )
  {
	  unsigned int conn_id =  ((unsigned int) Put_Offset) >> 25  ;
	  unsigned int packet_count  = (((unsigned int) Put_Offset) >> 16) & 0x1ff ;
	  int offset_in_frame = (Put_Offset & 0xfff0) | 0xffff0000 ;
	  unsigned int node_slot_mask=dma_tcp->node_slot_mask ;
	  rcv_t *rcvdemux = &dma_tcp->rcvdemux ;
	  unsigned int slot = (src_key >> 4) & node_slot_mask ;
	  unsigned int pad_head = src_key & 0x0f ;
	  struct sk_buff * candidate_skb=get_rcv_skb_for_conn(rcvdemux,slot,conn_id) ;
	  TRACEN(k_t_detail,
			"(>) request=%p slot=%08x pad_head=0x%08x payload_bytes=0x%02x Put_Offset=0x%08x\n",
			request,slot,pad_head,payload_bytes,Put_Offset);
	  if( candidate_skb)
		  {
			  adaptive_skb_cb_t * askb=(adaptive_skb_cb_t *)(candidate_skb->cb) ;
			  if(askb->conn_id != conn_id || askb->packet_count != packet_count)
				  {
					  TRACEN(k_t_error,"(E) askb mismatch, slot=%08x askb->conn_id=%04x conn_id=%04x askb->packet_count=%04x packet_count=%04x askb->packets_to_go=%04x",
							  slot,askb->conn_id,conn_id,askb->packet_count,packet_count,askb->packets_to_go) ;
					  dev_kfree_skb(candidate_skb) ;
					  candidate_skb = NULL ;
				  }
		  }
	  if( NULL == candidate_skb)
		  {
			  instrument_flow(dma_tcp,k_receive_eager) ;
			  candidate_skb=alloc_skb(packet_count*k_injection_packet_size+2*k_torus_skb_alignment+k_injection_packet_size,GFP_ATOMIC) ;  /*  TODO: refine the size */
			  if( candidate_skb)
			  {
				  adaptive_skb_cb_t * askb=(adaptive_skb_cb_t *)(candidate_skb->cb) ;
				  askb->conn_id = conn_id ;
				  askb->packet_count = packet_count ;
				  askb->packets_to_go = packet_count ;
				  askb->framestart_offset = 0 ;
				  askb->prev_offset = -65536 ;
				  skb_reserve(candidate_skb, (k_torus_skb_alignment - ((unsigned int)(candidate_skb->data)) % k_torus_skb_alignment));
				  skb_put(candidate_skb,packet_count*k_injection_packet_size) ;
			  }
			  else
				  {
					  TRACEN(k_t_error,"(E) skbuff allocation failed packet_count=%d slot=0x%08x conn_id=0x%08x",packet_count,slot,conn_id) ;
				  }
			  set_rcv_skb_for_conn(rcvdemux,slot,conn_id,candidate_skb) ;
		  }
	  if( candidate_skb)
		  {
			  unsigned char * end_of_frame=candidate_skb->tail ;
			  unsigned char * target = end_of_frame+offset_in_frame ;
			  int cand_start_offset = offset_in_frame + pad_head ;
			  TRACEN(k_t_detail,"candidate_skb skb=%p head=%p data=%p tail=%p end=%p offset_in_frame=0x%08x target=%p cand_start_offset=0x%08x",
					  candidate_skb,candidate_skb->head,candidate_skb->data,candidate_skb->tail,candidate_skb->end,offset_in_frame,target,cand_start_offset) ;
			  if( target < candidate_skb->head)
				  {
					  TRACEN(k_t_error,"data offset outside skb, dropping packet") ;
				  }
			  else
				  {
					  adaptive_skb_cb_t * askb=(adaptive_skb_cb_t *)(candidate_skb->cb) ;
					  int new_packets_to_go=askb->packets_to_go - 1 ;
					  int prev_offset = askb->prev_offset ;
#if defined(USE_ADAPTIVE_ROUTING)
/*  Statistics, count how often a packet came out-of-order */
					  if( offset_in_frame < prev_offset)
						  {
							  instrument_flow(dma_tcp,k_reordered) ;
						  }
					  askb->prev_offset = offset_in_frame ;
#endif
					  if( cand_start_offset < askb->framestart_offset )
						  {
							  askb->framestart_offset=cand_start_offset ;
						  }

					  TRACEN(k_t_detail,"memcpy(%p,%p,0x%08x) new_packets_to_go=%d",
							  target,request,payload_bytes,new_packets_to_go) ;
					  if( payload_bytes == k_injection_packet_size)
						  {
							   /*  doublehummer memcpy optimisation for 'full' packet */
					                      /*  TODO: rewrite with 'memcpy' or a copy through integer regs, to avoid using FP now this is 'rare' */
							  torus_frame_payload_memcpy((torus_frame_payload *)target,(torus_frame_payload *)request) ;
						  }
					  else
						  {
							  memcpy(target,request,payload_bytes) ;
						  }
					  if( new_packets_to_go <= 0)
						  {
					                     analyse_tcp_flags(dma_tcp, candidate_skb) ;
					                     skb_reserve(candidate_skb,packet_count*k_injection_packet_size+askb->framestart_offset);
					                     dumpframe(candidate_skb->data,candidate_skb->len,"Proposed frame") ;
					                     deliver_from_slot(dma_tcp,slot,conn_id,candidate_skb) ;
					                     set_rcv_skb_for_conn(rcvdemux,slot,conn_id,NULL) ;
						  }
					  else
						  {
							  askb->packets_to_go = new_packets_to_go ;
						  }
				  }
		  }
	  else
		  {
			  TRACEN(k_t_error,"(E) No memory for skb, dropping packet") ;
		  }

  }

static int issueInlineFrameDataAdaptiveActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           )
  {
    unsigned int SW_Arg=packet_ptr->SW_Arg ;
    int Put_Offset=packet_ptr->Put_Offset ;
/*     enable_kernel_fp() ; // TODO: don't think this is needed nowadays */

    issueInlineFrameDataAdaptive(
        (dma_tcp_t *) recv_func_parm,
        (void *) payload_ptr,
        SW_Arg,
        payload_bytes,
        Put_Offset
        ) ;
    return 0 ;
  }
#endif

#if defined(AUDIT_FRAME_HEADER)

frame_header_t all_headers_in_counters[DMA_NUM_COUNTERS_PER_GROUP] ;
#endif
unsigned int tot_len_for_rcv[DMA_NUM_COUNTERS_PER_GROUP] ;

static inline void create_dma_descriptor_propose_accept(dma_tcp_t *dma_tcp,
                void * address,
                unsigned int length,
                unsigned int x, unsigned int y, unsigned int z,
                unsigned int proto,
                unsigned int SW_Arg,
                unsigned int conn_id,
                unsigned int tag,
                DMA_InjDescriptor_t *desc,
                unsigned int propose_length
		)
{
	    dma_addr_t dataAddr ;
	    int ret1 ;
	    int PutOffset = (conn_id << 25) | (tag << 16) | ((-length) & 0xfff0) ;
	    TRACEN(k_t_general , "(>) injecting address=%p length=0x%08x x=%d y=%d z=%d proto=%d desc=%p",address,length,x,y,z,proto,desc);
	    dataAddr = dma_map_single(NULL, address, length, DMA_TO_DEVICE);
	    ret1 = DMA_TorusMemFifoDescriptor( desc,
                            x, y, z,
                            dma_tcp_ReceptionFifoGroup(dma_tcp),          /*  recv fifo grp id */
                            0,          /*  hints */
                            virtual_channel(dma_tcp,k_VC_anyway),          /*  vc - adaptive */
                            SW_Arg,          /*  softw arg */
                            proto,     /*  function id */
                            dma_tcp_InjectionCounterGroup(dma_tcp),          /*  inj cntr group id */
                            k_injCounterId,  /*  inj counter id */
                            dataAddr,        /*  send address */
                            propose_length          /*  proposal length */
                            );
	    if(ret1 != 0 )
		    {
			    TRACEN(k_t_error,"(E) ret1=%d",ret1) ;
		    }

	    DMA_DescriptorSetPutOffset(desc,PutOffset) ;  /*  For 'memory FIFO packets', the put offset has no hardware use. Set it to pass required data to receive actor */

	    TRACEN(k_t_general , "(<) ret1=%d",ret1);

}

static inline unsigned int ethhdr_src_x(struct ethhdr * eth)
{
	return eth->h_source[3] ;
}
static inline unsigned int ethhdr_src_y(struct ethhdr * eth)
{
	return eth->h_source[4] ;
}
static inline unsigned int ethhdr_src_z(struct ethhdr * eth)
{
	return eth->h_source[5] ;
}

static inline unsigned int ethhdr_dest_x(struct ethhdr * eth)
{
	return eth->h_dest[3] ;
}
static inline unsigned int ethhdr_dest_y(struct ethhdr * eth)
{
	return eth->h_dest[4] ;
}
static inline unsigned int ethhdr_dest_z(struct ethhdr * eth)
{
	return eth->h_dest[5] ;
}

#if defined(USE_SKB_TO_SKB)
static int get_reception_counter(dma_tcp_t * dma_tcp)
{
	unsigned int counters_available = dma_tcp->qtyFreeRecCounters ;
	if( counters_available > 0)
		{
			int cx ;
			int scanRecCounter=dma_tcp->scanRecCounter ;
			dma_tcp->qtyFreeRecCounters=counters_available-1 ;
			for(cx=0;cx<DMA_NUM_COUNTERS_PER_GROUP;cx+=1)
				{
					int cxx=(scanRecCounter+cx) & (DMA_NUM_COUNTERS_PER_GROUP-1) ;
					if(0 == dma_tcp->recCntrInUse[cxx])
						{
							dma_tcp->scanRecCounter=cxx+1 ;
							dma_tcp->recCntrInUse[cxx] = 1 ;
							return cxx ;
						}
				}
			TRACEN(k_t_error,"(E) Should have been %d counters available",counters_available) ;
		}
	return -1 ;   /*  No reception counters available */
}

enum {
	k_PSKB_noRecCounter = 0x01 ,
	k_PSKB_freedRecCounter = 0x02
};
typedef struct
{
	unsigned int src_key ;
	  unsigned int slot ;
  unsigned int conn_id ;
  unsigned short tot_len ;
  unsigned char pad_head ;
} propose_skb_cb ;

/* Frame injection control, may live in skb->cb . */
/* 'desc' describes the 'non-fragmented' initial part of the skb data; code where the ficb is used will */
/* handle what has to happen to get the 'fragmented' part of the skb sent out */
enum {
	k_cattle_class,
	k_first_class
};

static int bgp_dma_tcp_s_and_f_frames_prepared(
    dma_tcp_t *dma_tcp,
    struct sk_buff *skb,
    unsigned int queue_at_head,
    unsigned int transport_class
    ) ;

static int isProp(dma_tcp_t * dma_tcp,struct ethhdr *eth,struct iphdr *iph)
{
	int h_source_x=eth->h_source[3] ;
	int h_source_y=eth->h_source[4] ;
	int h_source_z=eth->h_source[5] ;
	int my_x=dma_tcp->location.coordinate[0] ;
	int my_y=dma_tcp->location.coordinate[1] ;
	int my_z=dma_tcp->location.coordinate[2] ;

	if( h_source_x == my_x && h_source_y == my_y && h_source_z == my_z )
		{
			TRACEN(k_t_general,"non-propose from (%d,%d,%d)",eth->h_dest[3],eth->h_dest[4],eth->h_dest[5]) ;
			return 0 ;
		}
	return 1 ;
}

static int bgp_dma_tcp_s_and_f_frames_prepared(
    dma_tcp_t *dma_tcp,
    struct sk_buff *skb,
    unsigned int queue_at_head,
    unsigned int transport_class
    ) ;

struct accepthdr {
	struct iphdr iph ;
	unsigned int conn_id ;
	int reception_counter ;
};

static inline void create_dma_descriptor_direct_put_offset(dma_tcp_t *dma_tcp,
                unsigned int x, unsigned int y, unsigned int z,
                int injection_counter,
                int reception_counter,
                dma_addr_t dataAddr,
                int msglen,
                DMA_InjDescriptor_t *desc,
                unsigned int offset
		) ;

static void display_iphdr(struct iphdr *iph)
{
	TRACEN(k_t_request,"iphdr tot_len=0x%04x saddr=0x%08x daddr=0x%08x",iph->tot_len,iph->saddr,iph->daddr) ;
}

static unsigned int counted_length(struct sk_buff *skb)
{
	unsigned int rc=skb_headlen(skb) ;
	int f ;
	int nfrags = skb_shinfo(skb)->nr_frags ;
	struct skb_frag_struct* frag = &skb_shinfo(skb)->frags[0] ;
	for(f=0; f<nfrags; f+=1)
		{
			rc += frag[f].size ;
		}
	return rc ;

}

static int audit_skb_at_accept(dma_tcp_t * dma_tcp,struct sk_buff *skb, unsigned int totlen_at_propose, struct iphdr *iph_at_rcv)
{
	unsigned int ctlen = counted_length(skb) ;
	if( totlen_at_propose == 0 || totlen_at_propose > dma_tcp->mtu || totlen_at_propose != iph_at_rcv->tot_len || totlen_at_propose +sizeof(struct ethhdr) != ctlen)
		{
			TRACEN(k_t_error,"(E) skb=%p inconsistent, totlen_at_propose=0x%04x iph_at_rcv->tot_len=0x%04x skb->data_len=0x%04x counted_length(skb)=0x%04x",
					skb, totlen_at_propose, iph_at_rcv->tot_len, skb->data_len, ctlen
					) ;
			  display_skb_structure(skb) ;
			  display_iphdr(iph_at_rcv) ;
			  instrument_flow(dma_tcp,k_accept_audit_fail) ;
			  return 1 ;
		}
	return 0 ;
}
void issue_accept(dma_tcp_t * dma_tcp,struct accepthdr * accepth, unsigned int src_key )
{
	unsigned int conn_id=accepth->conn_id ;
	int reception_counter=accepth->reception_counter ;
	unsigned int node_slot_mask=dma_tcp->node_slot_mask ;
	unsigned int slot = (src_key >> 4) & node_slot_mask ;
	struct sk_buff *skb=get_tx_skb(&dma_tcp->tx_mux,slot,conn_id) ;
	TRACEN(k_t_general,"src_key=0x%08x conn_id=0x%08x reception_counter=0x%08x",src_key,conn_id,reception_counter) ;
	instrument_flow(dma_tcp,k_act_accept_rpc) ;
	if( skb)
		  {
			  struct ethhdr* eth = (struct ethhdr*)(skb->data) ;
			  unsigned int x=ethhdr_dest_x(eth) ;
			  unsigned int y=ethhdr_dest_y(eth) ;
			  unsigned int z=ethhdr_dest_z(eth) ;
			  frame_injection_cb *ficb = (frame_injection_cb *) skb->cb ;
			  unsigned int payload_length = skb_headlen(skb)  ;
			  unsigned int payload_address = (unsigned int)(skb->data) ;
			  unsigned int pad_head = payload_address & 0x0f ;
			  unsigned int aligned_payload_length = payload_length + pad_head ;
			  dma_addr_t dataAddr = dma_map_single(NULL, skb->data-pad_head, aligned_payload_length, DMA_TO_DEVICE);

			  set_tx_skb(&dma_tcp->tx_mux,slot,conn_id,NULL) ;
			  TRACEN(k_t_general,"Cop from slot=0x%08x conn_id=0x%04x reception_counter=0x%02x skb=%p x=%d y=%d z=%d msglen=0x%04x",
					  slot,conn_id,reception_counter,skb, x,y,z,payload_length+pad_head) ;
			  if(TRACING(k_t_sgdiag))
				  {
					  TRACEN(k_t_sgdiag,"Cop from slot=0x%08x conn_id=0x%04x reception_counter=0x%02x skb=%p x=%d y=%d z=%d msglen=0x%04x",
							  slot,conn_id,reception_counter,skb, x,y,z,payload_length+pad_head) ;
					  diag_skb_structure(skb) ;
				  }
#if defined(AUDIT_HEADLEN)
			  {
				  int rca = audit_skb_at_accept(dma_tcp,skb,ficb->tot_len,&accepth->iph) ;
				  if( rca)
					  {
						  TRACEN(k_t_error,"(!!!) dropping skb, will cause (x=%d y=%d z=%d) counter 0x%02x to leak", x,y,z,reception_counter) ;
						  dev_kfree_skb(skb) ;
						  return ;
					  }
			  }
#endif
			  {
				  int transfer_length = k_abbreviate_headlen ? (payload_length+pad_head-eth->h_source[0]) : (payload_length+pad_head) ;
				  dma_addr_t transfer_address = k_abbreviate_headlen ? (dataAddr+eth->h_source[0]) : dataAddr ;
				  unsigned int receive_offset = k_abbreviate_headlen ? eth->h_source[0] : 0 ;
			  if( 0 != transfer_length)
				  {
					  create_dma_descriptor_direct_put_offset(
							  dma_tcp,x, y, z,k_injCounterId,reception_counter,transfer_address,transfer_length,&ficb->desc,receive_offset
							  ) ;
				  }
			  else
				  {
					  TRACEN(k_t_general,"(I) head length is zero") ;
					   /*  Set up a descriptor for a non-zero length, then set its length to zero so that code later on can pick up the special case */
					  create_dma_descriptor_direct_put_offset(
							  dma_tcp,x, y, z,k_injCounterId,reception_counter,transfer_address,1,&ficb->desc,receive_offset
							  ) ;
					  ficb->desc.msg_length = 0 ;
					   instrument_flow(dma_tcp,k_headlength_zero) ;
				  }
			  }
			  ficb->free_when_done=1 ;
			  bgp_dma_tcp_s_and_f_frames_prepared(dma_tcp, skb, 0, k_first_class) ;

		  }
	else
		  {
			  TRACEN(k_t_error,"(E) Cop from slot=0x%08x conn_id=0x%04x reception_counter=0x%02x skb is null",
					  slot,conn_id,reception_counter ) ;
		  }
}

static int should_park(dma_tcp_t * dma_tcp,unsigned int proposals_active, unsigned int x0, unsigned int y0, unsigned int z0)
{
	unsigned int free_counters = dma_tcp->qtyFreeRecCounters ;
	unsigned int tuning_counters_per_source = dma_tcp->tuning_counters_per_source ;
/* 	unsigned int reported_transmission_fifo = report_transmission_fifo(dma_tcp,x0,y0,z0) ; */
	return ( tuning_counters_per_source > 0 )
		? (proposals_active > tuning_counters_per_source )
		: ((proposals_active > 1) && (proposals_active * proposals_active > free_counters )) ;
}

static void stamp_skb(struct sk_buff *skb, unsigned int size )
{
	if( skb->data + size <= skb->end)
		{
			memset(skb->data,0x11,size) ;
		}
	else
		{
			TRACEN(k_t_error,"(E) Stamp for 0x%08x bytes out of range, skb=%p head=%p data=%p tail=%p end=%p, skipped",
					size,skb,skb->head,skb->data,skb->tail,skb->end) ;
		}
}

static inline int defer_skb_for_counter(const dma_tcp_t * dma_tcp)
{
	return k_allow_defer_skb_for_counter ? dma_tcp->tuning_defer_skb_until_counter : 0 ;
}
static void receive_skb_using_counter(dma_tcp_t *dma_tcp,struct sk_buff *skb_next, unsigned int counter_index,
		unsigned int pad_head, unsigned int slot, unsigned int conn_id,
		unsigned int x, unsigned int y,unsigned int z,
		unsigned int tot_len,
		unsigned int src_key) ;
static void pending_rcv_skb_queue(dma_tcp_t *dma_tcp, struct sk_buff * skb, unsigned int x0, unsigned int y0, unsigned int z0 )
{
/* 	if( 1 == dma_tcp->tuning_select_fifo_algorithm) */
/* 		{ */
/* 			skb_queue_tail(&dma_tcp->balancer.b[k_pending_rcv_skb_classes-1].pending_rcv_skbs,skb) ; */
/* 		} */
/* 	else */
/* 		{ */
			unsigned int reported_fifo=report_transmission_fifo(dma_tcp,x0,y0,z0) ;
			TRACEN(k_t_general,"skb=%p would come from fifo=%d on node [%d,%d,%d]",skb,reported_fifo,x0,y0,z0) ;
			if( reported_fifo < k_pending_rcv_skb_classes)
				{
					skb_queue_tail(&dma_tcp->balancer.b[reported_fifo].pending_rcv_skbs,skb) ;
				}
			else
				{
					TRACEN(k_t_error,"(!!!) skb=%p would come from fifo=%d on node [%d,%d,%d] (out of range)",skb,reported_fifo,x0,y0,z0) ;
					skb_queue_tail(&dma_tcp->balancer.b[0].pending_rcv_skbs,skb) ;
				}
/* 		} */
}

static inline int over_quota(bgp_dma_balancer_direction *b)
{
	int ql = skb_queue_len(&b->pending_rcv_skbs) ;
	return ql ? b->outstanding_counters : 0x7fffffff ;
}
static struct sk_buff* pending_rcv_skb_dequeue(dma_tcp_t *dma_tcp)
{
	unsigned int q=0 ;
	int qq=over_quota(dma_tcp->balancer.b+0) ;
	int x ;
	for(x=1;x<k_pending_rcv_skb_classes;x+=1)
		{
			int qp=over_quota(dma_tcp->balancer.b+x) ;
			if( qp < qq)
				{
					qq=qp ;
					q=x ;
				}
		}
	return skb_dequeue(&dma_tcp->balancer.b[q].pending_rcv_skbs) ;
}

static void issueProp(dma_tcp_t * dma_tcp,
    void  * request ,
    unsigned int src_key ,
    int payload_bytes,
    int Put_Offset
    )
  {
	  unsigned int conn_id =  ((unsigned int) Put_Offset) >> 25  ;
	  unsigned int node_slot_mask=dma_tcp->node_slot_mask ;
	  unsigned int slot = (src_key >> 4) & node_slot_mask ;
	  unsigned int pad_head = src_key & 0x0f ;

	  struct ethhdr *eth = (struct ethhdr *)(request+pad_head) ;
	  unsigned int eth_proto = eth->h_proto ;

	  struct iphdr *iph = (struct iphdr *) (eth+1) ;
	  unsigned int tot_len=iph->tot_len ;
	  if( isProp(dma_tcp,eth,iph))
		  {
			  unsigned int x=ethhdr_src_x(eth) ;
			  unsigned int y=ethhdr_src_y(eth) ;
			  unsigned int z=ethhdr_src_z(eth) ;
			  rcv_t *rcvdemux = &dma_tcp->rcvdemux ;
			  unsigned int proposals_active=get_proposals_active(rcvdemux,slot) ;
				  instrument_flow(dma_tcp,k_act_propose_rpc) ;
				  set_proposals_active(rcvdemux,slot,proposals_active+1) ;
				   /*  If we're flow controlling by counters, we have a choice here. */
				   /*  We can either get on with it, or park it for later when a previously-started frame completes */
				  if( 0 == k_counter_flow_control || ! should_park(dma_tcp,proposals_active,x,y,z) )
				  {
					  int reception_counter=get_reception_counter(dma_tcp) ;
					  TRACEN(k_t_general|k_t_sgdiag,"Prop from slot=0x%08x conn_id=0x%04x eth_proto=0x%04x pad_head=0x%02x tot_len=0x%04x x=0x%02x y=0x%02x z=0x%02x msglen=0x%04x payload_bytes=0x%02x", slot,conn_id,eth_proto,pad_head,tot_len, x, y, z,tot_len+pad_head, payload_bytes) ;

					   /*  Now we need an 'skbuff' and a reception counter. Reception counters might be scarce */
					  if( reception_counter != -1 )
						  {
							  unsigned int allocation_size=tot_len+sizeof(struct ethhdr)+3*k_torus_skb_alignment ;/*  TODO: refine the size */
							  struct sk_buff *skb = alloc_skb((allocation_size > 256) ? allocation_size : 256, GFP_ATOMIC) ;  /*  TODO: refine the size */
							  if( skb)
								  {
									  if(k_scattergather_diagnostic) stamp_skb(skb,tot_len+sizeof(struct ethhdr)+3*k_torus_skb_alignment) ;
									  skb_reserve(skb, (k_torus_skb_alignment - ((unsigned int)(skb->data)) % k_torus_skb_alignment)+pad_head);
									   /*  Bring in the frame header for diagnosis later ... */
									  memcpy(skb->data-pad_head,request,payload_bytes) ;
									  skb_put(skb,tot_len+sizeof(struct ethhdr)) ;
									  if( k_scattergather_diagnostic) display_skb_structure(skb) ;
									  {
										  receive_skb_using_counter(dma_tcp,skb,reception_counter,pad_head,slot,conn_id,x,y,z,tot_len,src_key) ;
									  }
								  }
							  else
								  {
									  TRACEN(k_t_error,"(E) No memory available for skbuff") ;
								  }
						  }
					  else
						  {
							  unsigned int allocation_size = defer_skb_for_counter(dma_tcp) ?  (payload_bytes+2*k_torus_skb_alignment) : (tot_len+sizeof(struct ethhdr)+3*k_torus_skb_alignment) ;
							  unsigned int put_size = defer_skb_for_counter(dma_tcp) ? (payload_bytes-pad_head) : (tot_len+sizeof(struct ethhdr)) ;
							  /* TODO: Defer allocation of the full-size sk_buff until a reception counter is available */
							  struct sk_buff *skb = alloc_skb((allocation_size > 256) ? allocation_size : 256, GFP_ATOMIC) ;  /*  TODO: refine the size */
							  TRACEN(k_t_general,"allocation_size=0x%04x put_size=0x%04x skb=%p",allocation_size,put_size,skb) ;
							  instrument_flow(dma_tcp, k_no_reception_counter) ;
							  if( skb)
								  {
									  if(k_scattergather_diagnostic) stamp_skb(skb,allocation_size) ;
									  skb_reserve(skb, (k_torus_skb_alignment - ((unsigned int)(skb->data)) % k_torus_skb_alignment)+pad_head);
									   /*  Bring in the frame header for diagnosis later ... */
									  memcpy(skb->data-pad_head,request,payload_bytes) ;
									  skb_put(skb,put_size) ;
									  if( k_scattergather_diagnostic) display_skb_structure(skb) ;
									  {
										  propose_skb_cb * pskbcb = (propose_skb_cb *)skb->cb ;
										  pskbcb->src_key=src_key ;
										  pskbcb->slot = slot ;
										  pskbcb->conn_id = conn_id ;
										  pskbcb->tot_len = tot_len ;
										  pskbcb->pad_head = pad_head ;
									  }
									  instrument_flow(dma_tcp,k_defer_accept_rpc_counters) ;
									  pending_rcv_skb_queue(dma_tcp,skb,x,y,z) ;
									  TRACEN(k_t_flowcontrol|k_t_general,"No reception counters (%d,%d,%d) skb=%p src_key=0x%08x slot=0x%08x conn_id=0x%08x tot_len=0x%04x pad_head=0x%02x",x,y,z,skb,src_key,slot,conn_id,tot_len,pad_head) ;
								  }
							  else
								  {
									  TRACEN(k_t_error,"(E) No memory available for skbuff") ;
								  }
						  }
				  }
				  else
				  {
					 /*  Park the 'propose' until a previous frame from this node completes */

					  unsigned int allocation_size = defer_skb_for_counter(dma_tcp) ?  (payload_bytes+2*k_torus_skb_alignment) : (tot_len+sizeof(struct ethhdr)+3*k_torus_skb_alignment) ;
					  unsigned int put_size = defer_skb_for_counter(dma_tcp) ? (payload_bytes-pad_head) : (tot_len+sizeof(struct ethhdr)) ;
					  /* TODO: Defer allocation of the full-size sk_buff until a reception counter is available */
					  struct sk_buff *skb = alloc_skb(allocation_size, GFP_ATOMIC) ;  /*  TODO: refine the size */
					  TRACEN(k_t_general,"allocation_size=0x%04x put_size=0x%04x skb=%p",allocation_size,put_size,skb) ;
					  instrument_flow(dma_tcp, k_parked) ;
					  if( skb)
						  {
							  if(k_scattergather_diagnostic) stamp_skb(skb,allocation_size) ;
							  skb_reserve(skb, (k_torus_skb_alignment - ((unsigned int)(skb->data)) % k_torus_skb_alignment)+pad_head);
							   /*  Bring in the frame header for diagnosis later ... */
							  memcpy(skb->data-pad_head,request,payload_bytes) ;
							  skb_put(skb,put_size) ;
							  if( k_scattergather_diagnostic) display_skb_structure(skb) ;
							  {
								  propose_skb_cb * pskbcb = (propose_skb_cb *)skb->cb ;
								  pskbcb->src_key=src_key ;
								  pskbcb->slot = slot ;
								  pskbcb->conn_id = conn_id ;
								  pskbcb->tot_len = tot_len ;
								  pskbcb->pad_head = pad_head ;
							  }
							  instrument_flow(dma_tcp,k_defer_accept_rpc_nodeflow) ;
							  enq_pending_flow(&dma_tcp->rcvdemux,slot,skb) ;
							  TRACEN(k_t_general,"Flow control (%d,%d,%d) skb=%p src_key=0x%08x slot=0x%08x conn_id=0x%08x tot_len=0x%04x pad_head=0x%02x proposals_active=%d qtyFreeRecCounters=%d",x,y,z,skb,src_key,slot,conn_id,tot_len,pad_head,proposals_active,dma_tcp->qtyFreeRecCounters) ;
						  }
					  else
						  {
							  TRACEN(k_t_error,"(E) No memory available for skbuff") ;
						  }
				  }
		  }
	  else
		  {
			   /*  an 'accept' packet sent as a modified 'propose' ... */
			struct accepthdr * accepth=(struct accepthdr *)(eth+1) ;
			TRACEN(k_t_general,"'accept' src_key=0x%08x",src_key) ;
			issue_accept(dma_tcp,accepth,src_key) ;
		  }
  }

static int issuePropActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           )
  {
    unsigned int SW_Arg=packet_ptr->SW_Arg ;
    int Put_Offset=packet_ptr->Put_Offset ;

    issueProp(
        (dma_tcp_t *) recv_func_parm,
        (void *) payload_ptr,
        SW_Arg,
        payload_bytes,
        Put_Offset
        ) ;
    return 0 ;
  }
typedef struct
{
  unsigned int reception_counter ;
  unsigned char x, y, z ;
} accept_skb_cb ;

static inline void create_dma_descriptor_direct_put_offset(dma_tcp_t *dma_tcp,
                unsigned int x, unsigned int y, unsigned int z,
                int injection_counter,
                int reception_counter,
                dma_addr_t dataAddr,
                int msglen,
                DMA_InjDescriptor_t *desc,
                unsigned int offset
		)
{
	    int ret1 __attribute((unused));
	    TRACEN(k_t_general|k_t_sgdiag , "(>) injecting x=%d y=%d z=%d injection_counter=0x%02x reception_counter=0x%02x dataAddr=0x%08llx msglen=0x%08x desc=%p offset=0x%04x",
			    x,y,z,injection_counter,reception_counter,dataAddr,msglen,desc,offset);
	    ret1 = DMA_TorusDirectPutDescriptor( desc,
	                                     x, y, z,
	                                     0,          /*  hints */
	                                     virtual_channel(dma_tcp,k_VC_anyway),          /*  vc - adaptive */
	                                     dma_tcp_InjectionCounterGroup(dma_tcp),          /*  inj cntr group id */
	                                     injection_counter,  /*  inj counter id */
	                                     dataAddr,        /*  send offset */
	                                     dma_tcp_ReceptionCounterGroup(dma_tcp),        /*  rec ctr grp */
	                                     reception_counter,
	                                     offset,        /*  reception offset */
	                                     msglen          /*  message length */
	                                     );
	    TRACEN(k_t_general , "(<) ret1=%d",ret1);

}

#endif

static dma_addr_t locate_dma_address(dma_tcp_t *dma_tcp,struct sk_buff *skb,unsigned int pad_head, unsigned int propose_len)
  {
    if( 0 == k_abbreviate_headlen || 0 == dma_tcp->tuning_enable_siw_placement || NULL == dma_tcp->siw_placement_callback)
      {
      return dma_map_single(NULL, skb->data-pad_head, skb->len+pad_head, DMA_FROM_DEVICE);
      }
    {
      dma_addr_t rc0 = (*dma_tcp->siw_placement_callback)(skb) ;
      dma_addr_t rc  =  rc0-pad_head+propose_len ;
      instrument_flow(dma_tcp,rc0 ? k_siw_placement_hit : k_siw_placement_miss) ;
      TRACEN(k_t_request,"siw_placement_callback returns 0x%016llx, rc=0x%016llx",rc0,rc) ;
      return rc0 ? rc : dma_map_single(NULL, skb->data-pad_head, skb->len+pad_head, DMA_FROM_DEVICE);
    }

  }
static void receive_skb_using_counter(dma_tcp_t *dma_tcp,struct sk_buff *skb_next, unsigned int counter_index,
		unsigned int pad_head, unsigned int slot, unsigned int conn_id,
		unsigned int x, unsigned int y,unsigned int z,
		unsigned int tot_len,
		unsigned int src_key)
{
	struct ethhdr* eth=(struct ethhdr *)(skb_next->data) ;
  unsigned int propose_len = eth->h_source[0] ;
  unsigned int dma_count = k_abbreviate_headlen ? (skb_next->len+pad_head-propose_len) : (skb_next->len+pad_head) ;
	frame_injection_cb * ficb = (frame_injection_cb *) skb_next->cb ;
  dma_addr_t dataAddr = locate_dma_address(dma_tcp, skb_next, pad_head, propose_len);
	  unsigned int counter_base=dataAddr>>4 ;
	  unsigned int counter_max=((dataAddr+tot_len+pad_head+sizeof(struct ethhdr)) >> 4)+1 ;

#if defined(AUDIT_FRAME_HEADER)
	  memcpy(all_headers_in_counters+counter_index,skb_next->data,sizeof(frame_header_t)) ;
#endif

	  dma_tcp->balancer.b[report_transmission_fifo(dma_tcp,x,y,z)].outstanding_counters += 1 ;

	dma_tcp->slot_for_rcv[counter_index]=slot ;
	dma_tcp->conn_for_rcv[counter_index]=conn_id | 0x80 ;  /*  Mark it up as having been delayed */
	TRACEN(k_t_general|k_t_scattergather|k_t_sgdiag,"Reception counter 0x%02x [%08x %08x %08x] assigned to (%d,%d,%d) conn_id=0x%08x skb=%p propose_len=0x%02x",
			counter_index,dma_count,counter_base,counter_max,x,y,z,conn_id,skb_next,propose_len) ;
	  ficb->free_when_done = 0 ;

	dma_tcp->rcv_skbs[counter_index] = skb_next ;
	dma_tcp->rcv_timestamp[counter_index] = jiffies ;
	{
		unsigned int proposed_dma_length = tot_len+pad_head+sizeof(struct ethhdr) ;
		unsigned int available_skb_length = skb_next->end - (skb_next->data-pad_head) ;
		if( proposed_dma_length > available_skb_length )
			{
				TRACEN(k_t_error,"(!!!) skb=%p not big enough, dma=0x%08x bytes, pad_head=0x%02x, skb(head=%p data=%p tail=%p end=%p)",
						skb_next,proposed_dma_length,pad_head,skb_next->head,skb_next->data,skb_next->tail,skb_next->end
						) ;
				show_stack(NULL,NULL) ;
			}
	}
	  DMA_CounterSetValueBaseMaxHw(dma_tcp->recCounterGroup.counter[counter_index].counter_hw_ptr,dma_count,dataAddr >> 4, ((dataAddr+tot_len+pad_head+sizeof(struct ethhdr)) >> 4)+1) ;
	  instrument_flow(dma_tcp,k_send_accept_rpc) ;
		  {
			   /*  Push out a 'reverse propose' frame, adjust it so it overlays the area beyond the initial frame which will be replaced by the response DMA */
			  struct iphdr* iph = (struct iphdr*)(eth+1) ;
			  struct ethhdr* accept_eth0 = (struct ethhdr *)(iph+1) ;
			  struct ethhdr* accept_eth = (struct ethhdr *)(skb_next->data-pad_head+propose_len) ;
			  struct accepthdr * accepth=(struct accepthdr *)(accept_eth+1) ;
			  TRACEN(k_t_general,"accept_eth0=%p accepth=%p",accept_eth0,accept_eth) ;
			  tot_len_for_rcv[counter_index] = iph->tot_len ; // For diagnostics if the torus hangs
			  memcpy(accept_eth,eth,sizeof(struct ethhdr)) ;
			  memcpy(&accepth->iph,iph,sizeof(iph)) ; // TODO: Diagnose the apparent 'scribble' at the sender, then take this away
			  accepth->conn_id=conn_id ;
			  accepth->reception_counter=counter_index ;
			  if( (unsigned int)(accepth+1) > (unsigned int)(skb_next->end))
				  {
						TRACEN(k_t_error,"(!!!) skb=%p not big enough, (accepth+1)=%p, skb(head=%p data=%p tail=%p end=%p)",
								skb_next,accepth+1,skb_next->head,skb_next->data,skb_next->tail,skb_next->end
								) ;
						show_stack(NULL,NULL) ;

				  }
			  TRACEN(k_t_general,"accept_eth=%p accepth=%p src_key=0x%08x conn_id=0x%08x counter_index=0x%08x",accept_eth,accepth,src_key,conn_id,counter_index) ;
			  create_dma_descriptor_propose_accept(dma_tcp,
					  (void *)(accept_eth),
					  48,
					  x,y, z,
					  dma_tcp->proto_transfer_propose,
					  (dma_tcp->src_key << 4),
					  conn_id,
					  0,
					  &ficb->desc,
					  48
					) ;
			  DMA_CounterSetEnableById(&dma_tcp->recCounterGroup,counter_index) ;
			  bgp_dma_tcp_s_and_f_frames_prepared(dma_tcp,skb_next,0, k_first_class) ;
		  }

}

static void handle_empty_recCounter_deliver(dma_tcp_t *dma_tcp, unsigned int counter_index)
{
	  rcv_t *rcvdemux = &dma_tcp->rcvdemux ;
	struct sk_buff *skb=dma_tcp->rcv_skbs[counter_index] ;
	unsigned int slot = dma_tcp->slot_for_rcv[counter_index] ;
	  unsigned int proposals_active=get_proposals_active(rcvdemux,slot) ;
	  set_proposals_active(rcvdemux,slot,proposals_active-1) ;
	TRACEN(k_t_general|k_t_sgdiag,"counter_index=0x%02x skb=%p",counter_index,skb) ;
	if( skb)
		{
#if defined(AUDIT_FRAME_HEADER)
			if(memcmp(skb->data,((char *)(all_headers_in_counters+counter_index)),32))
				{
					  TRACEN(k_t_request,"(!!!) header not as first seen") ;
					  dumpmem(skb->data,sizeof(frame_header_t),"header-now") ;
					  dumpmem(all_headers_in_counters+counter_index,sizeof(frame_header_t),"header-in-propose") ;

				}
#endif

			{
				struct ethhdr *eth=(struct ethhdr *)(skb->data) ;
				unsigned int x=ethhdr_src_x(eth) ;
				unsigned int y=ethhdr_src_y(eth) ;
				unsigned int z=ethhdr_src_z(eth) ;
				eth->h_source[0] = eth->h_dest[0] ; // Replug the item that got taken for DMA sideband
				dma_tcp->balancer.b[report_transmission_fifo(dma_tcp,x,y,z)].outstanding_counters -= 1 ;
			}
			deliver_from_slot(dma_tcp,slot,dma_tcp->conn_for_rcv[counter_index],skb) ;
		}
	else
		{
			TRACEN(k_t_error,"(E) counter_index=0x%02x no skbuff, slot=0x%08x proposals_active=%d",counter_index,slot,proposals_active) ;
		}

}

static void handle_empty_recCounter_flush(dma_tcp_t *dma_tcp, unsigned int counter_index)
{
	  rcv_t *rcvdemux = &dma_tcp->rcvdemux ;
	struct sk_buff *skb=dma_tcp->rcv_skbs[counter_index] ;
	unsigned int slot = dma_tcp->slot_for_rcv[counter_index] ;
	  unsigned int proposals_active=get_proposals_active(rcvdemux,slot) ;
	  unsigned int counter_value = DMA_CounterGetValueNoMsync(dma_tcp->recCounterGroup.counter+counter_index) ;
	  set_proposals_active(rcvdemux,slot,proposals_active-1) ;
	TRACEN(k_t_request,"(!!!) flushing counter_index=0x%02x skb=%p",counter_index,skb) ;
	DMA_CounterSetDisableById(&dma_tcp->recCounterGroup,counter_index) ;
	dma_tcp_show_reception_one(dma_tcp,counter_index,counter_value) ;
	if( skb)
		{
#if defined(AUDIT_FRAME_HEADER)
			if(memcmp(skb->data,((char *)(all_headers_in_counters+counter_index)),32))
				{
					  TRACEN(k_t_request,"(!!!) header not as first seen") ;
					  dumpmem(skb->data,sizeof(frame_header_t),"header-now") ;
					  dumpmem(all_headers_in_counters+counter_index,sizeof(frame_header_t),"header-in-propose") ;

				}
#endif
			dev_kfree_skb(skb) ;
		}
	else
		{
			TRACEN(k_t_error,"(E) counter_index=0x%02x no skbuff, slot=0x%08x proposals_active=%d",counter_index,slot,proposals_active) ;
		}

}

static void handle_empty_recCounter_reload(dma_tcp_t *dma_tcp, unsigned int counter_index, unsigned int x0, unsigned int y0, unsigned int z0)
{
	  rcv_t *rcvdemux = &dma_tcp->rcvdemux ;
		struct sk_buff * skb_next  ;
	unsigned int slot = dma_tcp->slot_for_rcv[counter_index] ;
	  unsigned int proposals_active=get_proposals_active(rcvdemux,slot)+1 ;
	if( k_counter_flow_control )
		{
			 /*  We're going to get a queued frame, but which queue we try first will depend on whether this source */
			 /*  is over quota at the moment */
			if (proposals_active > count_pending_flow(rcvdemux,slot)+1 && should_park(dma_tcp,proposals_active,x0,y0,z0))
				{
					 /*  If we have a 'queued' frame, take that */
					skb_next = pending_rcv_skb_dequeue(dma_tcp) ;
					TRACEN(k_t_general,"skb_next=%p",skb_next) ;
					if( ! skb_next)
						{
							 /*  Try a 'parked' frame */
							skb_next=deq_pending_flow(rcvdemux,slot) ;
						}

				}
			else
				{
					 /*  If we have a 'parked' frame from the same source, get it moving now */
					skb_next=deq_pending_flow(rcvdemux,slot) ;
					TRACEN(k_t_general,"skb_next=%p",skb_next) ;
					if( ! skb_next)
						{
							 /*  If nothing 'parked', try the general queue */
							skb_next = pending_rcv_skb_dequeue(dma_tcp) ;
						}

				}
		}
	else
		{
			skb_next = pending_rcv_skb_dequeue(dma_tcp) ;
		}
	if( skb_next)
		{
			 /*  A request was waiting for a receive counter, which is now available */
			propose_skb_cb * pskcb = (propose_skb_cb *)skb_next->cb ;
			unsigned int src_key=pskcb->src_key ;
			struct ethhdr* eth=(struct ethhdr *)(skb_next->data) ;
			unsigned int x=ethhdr_src_x(eth) ;
			unsigned int y=ethhdr_src_y(eth) ;
			unsigned int z=ethhdr_src_z(eth) ;
			unsigned int slot=pskcb->slot ;
			unsigned int conn_id=pskcb->conn_id ;
			unsigned int pad_head=pskcb->pad_head ;
			unsigned int tot_len=pskcb->tot_len ;
			if( defer_skb_for_counter(dma_tcp))
				{
					 /*  Need a new sk_buff; need to set up alignment */
					 /*  TODO: shouldn't need alignment */
					 /*  TODO: Copy in the data from the old skbuff, so that the DMA doesn't need to resend it */
					  unsigned int allocation_size =  (tot_len+sizeof(struct ethhdr)+3*k_torus_skb_alignment) ;
					  /* TODO: Defer allocation of the full-size sk_buff until a reception counter is available */
					  struct sk_buff *skb = alloc_skb((allocation_size > 256) ? allocation_size : 256, GFP_ATOMIC) ;  /*  TODO: refine the size */
					  TRACEN(k_t_general,"skb_next=%p skb=%p allocation_size=%d copying_length=%d src_key=0x%08x slot=0x%08x conn_id=0x%08x pad_head=0x%02x tot_len=0x%04x",skb_next,skb,allocation_size,skb_next->len,src_key,slot,conn_id,pad_head,tot_len) ;
					  if( skb)
						  {
							  if(k_scattergather_diagnostic) stamp_skb(skb,tot_len+sizeof(struct ethhdr)+3*k_torus_skb_alignment) ;
							  skb_reserve(skb, (k_torus_skb_alignment - ((unsigned int)(skb->data)) % k_torus_skb_alignment)+pad_head);
							  memcpy(skb->data,skb_next->data,skb_next->len) ;
							  skb_put(skb,tot_len+sizeof(struct ethhdr)) ;
							   TRACEN(k_t_general,"skb->data=%p skb->len=0x%04x skb_next->data=%p skb_next->len=0x%04x",
									  skb->data, skb->len, skb_next->data, skb_next->len) ;
							  if( k_scattergather_diagnostic) display_skb_structure(skb) ;
						  }
					  else
						  {
							  TRACEN(k_t_error,"(E) No memory available for skbuff, torus will jam") ;
							   /*  TODO: Could handle this by deferring until memory is available, or by sending a 'negative COP' and having the sender back off */
						  }
					  dev_kfree_skb(skb_next) ;
					  skb_next=skb ;
					  eth=(struct ethhdr *)(skb_next->data) ;  // Fix up, 'accept' setup uses this

				}
			if( skb_next)
				{
					receive_skb_using_counter(dma_tcp,skb_next,counter_index,pad_head,slot,conn_id,x,y,z,tot_len,src_key) ;
				}
			else
				{
					  TRACEN(k_t_error,"(E) No memory available for skbuff, torus will jam") ;
					   /*  TODO: Could handle this by deferring until memory is available, or by sending a 'negative COP' and having the sender back off */
				}
		}
	else
		{
			TRACEN(k_t_general|k_t_scattergather,"Reception counter 0x%02x vacant",counter_index) ;
			dma_tcp->recCntrInUse[counter_index] = 0 ;
			dma_tcp->rcv_skbs[counter_index] = NULL ;
			dma_tcp->qtyFreeRecCounters += 1 ;
			DMA_CounterSetDisableById(&dma_tcp->recCounterGroup,counter_index) ;
		}

}

static void handle_empty_recCounter(dma_tcp_t *dma_tcp, unsigned int counter_index)
{
	struct sk_buff *skb=dma_tcp->rcv_skbs[counter_index] ;
	struct ethhdr *eth=(struct ethhdr *)(skb->data) ;
	unsigned int x0 = ethhdr_src_x(eth) ;
	unsigned int y0 = ethhdr_src_y(eth) ;
	unsigned int z0 = ethhdr_src_z(eth) ;
	handle_empty_recCounter_deliver(dma_tcp,counter_index) ;
	handle_empty_recCounter_reload(dma_tcp,counter_index,x0,y0,z0) ;
}

static void handle_stuck_recCounter(dma_tcp_t *dma_tcp, unsigned int counter_index)
{
	struct sk_buff *skb=dma_tcp->rcv_skbs[counter_index] ;
	struct ethhdr *eth=(struct ethhdr *)(skb->data) ;
	unsigned int x0 = ethhdr_src_x(eth) ;
	unsigned int y0 = ethhdr_src_y(eth) ;
	unsigned int z0 = ethhdr_src_z(eth) ;

	instrument_flow(dma_tcp,k_receive_incomplete) ;
	handle_empty_recCounter_flush(dma_tcp,counter_index) ;
	handle_empty_recCounter_reload(dma_tcp,counter_index,x0,y0,z0) ;
}

static void check_stuck_recCounters(dma_tcp_t *dma_tcp)
{
	unsigned int x ;
	int j = jiffies ;
	for(x=0;x<DMA_NUM_COUNTERS_PER_GROUP;x+=1)
		{
			if(dma_tcp->rcv_skbs[x] && (j-dma_tcp->rcv_timestamp[x]) >= 3*HZ )
				{
					TRACEN(k_t_request,"(!!!) counter 0x%02x not completed after %d jiffies, freeing it",x,j-dma_tcp->rcv_timestamp[x]) ;
					handle_stuck_recCounter(dma_tcp,x) ;
				}
		}
}

void bgp_dma_tcp_empty_fifo_callback(void)
{
	dma_tcp_t *dma_tcp = &dma_tcp_state ;
	unsigned int word0 , word1 ;
	DMA_CounterGetAllHitZero(&dma_tcp->recCounterGroup, &word0, &word1) ;
	if( word0 != 0 )
		{
			DMA_CounterGroupClearHitZero(&dma_tcp->recCounterGroup, 0, word0) ;
			TRACEN(k_t_general,"recCounterGroup word0=0x%08x",word0) ;
			do {
				unsigned int counter_index=32-fls(word0) ;  /*  Find the highest-order bit that is set */
				word0 &= (0x7fffffff >> counter_index) ;   /*  Clear it */
				handle_empty_recCounter(dma_tcp,counter_index) ;
			} while ( word0 != 0) ;
		}
	if( word1 != 0)
		{
			DMA_CounterGroupClearHitZero(&dma_tcp->recCounterGroup, 1, word1) ;
			TRACEN(k_t_general,"recCounterGroup word1=0x%08x",word1) ;
			do {
				unsigned int counter_index=32-fls(word1) ;  /*  Find the highest-order bit that is set */
				word1 &= (0x7fffffff >> counter_index) ;   /*  Clear it */
				handle_empty_recCounter(dma_tcp,32+counter_index) ;
			} while ( word1 != 0) ;
		}
	 /*   'clear orphaned reception counters' only works correctly if we are doing eager delivery */
	if( deliver_eagerly(dma_tcp))
		{
			int checked_time = dma_tcp->rcv_checked_time ;
			int j = jiffies ;
			int elapsed = j - checked_time ;
			if( elapsed > HZ)
				{
					dma_tcp->rcv_checked_time = j ;
					check_stuck_recCounters(dma_tcp) ;
				}

		}


}

int bgp_dma_tcp_counter_copies[DMA_NUM_COUNTERS_PER_GROUP] ;


static inline int inject_into_dma_taxi(dma_tcp_t *dma_tcp, void * address, unsigned int length, unsigned int x, unsigned int y, unsigned int z, unsigned int my_injection_group, unsigned int desired_fifo, unsigned int proto, unsigned int SW_Arg )
  {
    dma_addr_t dataAddr ;
    DMA_InjDescriptor_t desc;
    int ret1, ret2 ;
    TRACEN(k_t_general , "(>) injecting address=%p length=0x%08x x=%d y=%d z=%d my_injection_group=%d desired_fifo=%d",address,length,x,y,z,my_injection_group,desired_fifo);
/*     TRACEN(k_t_scattergather,"injecting, length=0x%04x my_injection_group=%d desired_fifo=%d",length,my_injection_group,desired_fifo) ; */
    dataAddr = dma_map_single(NULL, address, length, DMA_TO_DEVICE);
    ret1 = DMA_TorusMemFifoDescriptor( &desc,
                                     x, y, z,
                                     dma_tcp_ReceptionFifoGroup(dma_tcp),          /*  recv fifo grp id */
                                     0,          /*  hints */
                                     virtual_channel(dma_tcp,k_VC_anyway),          /*  go whichver way it wants */
                                     SW_Arg,          /*  softw arg */
                                     proto,     /*  function id */
                                     dma_tcp_InjectionCounterGroup(dma_tcp),          /*  inj cntr group id */
                                     k_injCounterId,  /*  inj counter id */
                                     dataAddr,        /*  send address */
                                     length          /*  msg len */
                                     );


    DMA_DescriptorSetPutOffset(&desc,-length) ;  /*  For 'memory FIFO packets', the put offset has no hardware use. Set it to indicate the message (fragment) length */
    ret2 = wrapped_DMA_InjFifoInjectDescriptorById( &dma_tcp->injFifoGroupFrames,
                                            dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo],
                                            &desc );
    TRACEN(k_t_scattergather , "tgt=[%d %d %d] length=0x%04x injfifo[%d %02x]\n",
		    x,y,z,length,
		    my_injection_group,desired_fifo ) ;
    TRACEN(k_t_general , "(<) ret1=%d ret2=%d",ret1, ret2);
    return 1 ;
  }



/*  The injectors are currently set up so that each 'software FIFO' pushes to a single (different) 'hardware FIFO' */
/*  This isn't needed for 'adaptive'; things could be rearranged for all 'software FIFOs' to have access to all 'hardware FIFOs' */
enum {
	k_my_vc_for_adaptive = k_VC_anyway
/*  Diagnostically flip it to 'deterministic' ... */
/* 	k_my_vc_for_adaptive = k_VC_ordering */
};
static inline int inject_into_dma_adaptive(dma_tcp_t *dma_tcp,
		                           void * address,
		                           unsigned int length,
		                           unsigned int x, unsigned int y, unsigned int z,
		                           unsigned int my_injection_group,
		                           unsigned int desired_fifo,
		                           unsigned int proto,
		                           unsigned int SW_Arg,
		                           unsigned int conn_id )
  {
    dma_addr_t dataAddr ;
    DMA_InjDescriptor_t desc;
    int ret1, ret2 __attribute((unused));
    unsigned int firstpacketlength = ( length > k_injection_packet_size) ? k_injection_packet_size : length ;
    unsigned int midpacketcount = (length-(k_injection_packet_size+1)) / k_injection_packet_size ;
    unsigned int packetcount = (length > k_injection_packet_size) ? (midpacketcount+2) : 1 ;
    int PutOffset = (conn_id << 25) | (packetcount << 16) | ((-length) & 0xfff0) ;
    TRACEN(k_t_general , "(>) injecting address=%p length=0x%08x x=%d y=%d z=%d my_injection_group=%d desired_fifo=%d",address,length,x,y,z,my_injection_group,desired_fifo);
    dataAddr = dma_map_single(NULL, address, length, DMA_TO_DEVICE);
    if( length >= 10000)
	    {
		    TRACEN(k_t_request,"address=%p length=0x%08x dataAddr=0x%08llx",address,length,dataAddr) ;
	    }

/*  First injection is 'start of frame/fragment' */
    ret1 = DMA_TorusMemFifoDescriptor( &desc,
                                     x, y, z,
                                     dma_tcp_ReceptionFifoGroup(dma_tcp),          /*  recv fifo grp id */
                                     0,          /*  hints */
                                     virtual_channel(dma_tcp,k_my_vc_for_adaptive),          /*  vc - adaptive */
                                     SW_Arg,          /*  softw arg */
                                     proto,     /*  function id */
                                     dma_tcp_InjectionCounterGroup(dma_tcp),          /*  inj cntr group id */
                                     k_injCounterId,  /*  inj counter id */
                                     dataAddr,        /*  send address */
                                     packetcount*firstpacketlength          /*  msg len */
                                     );


    DMA_DescriptorSetPutOffset(&desc,PutOffset) ;  /*  For 'memory FIFO packets', the put offset has no hardware use. Set it to pass required data to receive actor */
    ret2 = wrapped_DMA_InjFifoInjectDescriptorById( &dma_tcp->injFifoGroupFrames,
                                            dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo],
                                            &desc );
    TRACEN(k_t_scattergather ,"tgt=[%d %d %d] length=0x%04x injfifo[%d %02x] conn_id=0x%02x\n",
		    x,y,z,length,
		    my_injection_group,desired_fifo,conn_id ) ;
    TRACEN(k_t_general , "proto=%d firstpacketlength=%d ret1=%d ret2=%d",proto,firstpacketlength,ret1, ret2);

    return 1 ;

  }

static inline void create_dma_descriptor_adaptive(dma_tcp_t *dma_tcp,
		                           void * address,
		                           unsigned int length,
		                           unsigned int x, unsigned int y, unsigned int z,
		                           unsigned int proto,
		                           unsigned int SW_Arg,
		                           unsigned int conn_id,
		                           DMA_InjDescriptor_t *desc)
  {
    dma_addr_t dataAddr ;
    int ret1 __attribute__((unused));
    unsigned int firstpacketlength = ( length > k_injection_packet_size) ? k_injection_packet_size : length ;
    unsigned int midpacketcount = (length-(k_injection_packet_size+1)) / k_injection_packet_size ;
    unsigned int packetcount = (length > k_injection_packet_size) ? (midpacketcount+2) : 1 ;
    int PutOffset = (conn_id << 25) | (packetcount << 16) | ((-length) & 0xfff0) ;
    TRACEN(k_t_general , "(>) address=%p length=0x%08x x=%d y=%d z=%d proto=%d SW_Arg=0x%08x desc=%p",address,length,x,y,z,proto,SW_Arg,desc);
    dataAddr = dma_map_single(NULL, address, length, DMA_TO_DEVICE);
    if( length >= 10000)
	    {
		    TRACEN(k_t_request,"address=%p length=0x%08x dataAddr=0x%08llx",address,length,dataAddr) ;
	    }

/*  First injection is 'start of frame/fragment' */
    ret1 = DMA_TorusMemFifoDescriptor( desc,
                                     x, y, z,
                                     dma_tcp_ReceptionFifoGroup(dma_tcp),          /*  recv fifo grp id */
                                     0,          /*  hints */
                                     virtual_channel(dma_tcp,k_my_vc_for_adaptive),          /*  vc - adaptive */
                                     SW_Arg,          /*  softw arg */
                                     proto,     /*  function id */
                                     dma_tcp_InjectionCounterGroup(dma_tcp),          /*  inj cntr group id */
                                     k_injCounterId,  /*  inj counter id */
                                     dataAddr,        /*  send address */
                                     packetcount*firstpacketlength          /*  msg len */
                                     );

    DMA_DescriptorSetPutOffset(desc,PutOffset) ;  /*  For 'memory FIFO packets', the put offset has no hardware use. Set it to pass required data to receive actor */
    TRACEN(k_t_general , "(<) firstpacketlength=%d ret1=%d",firstpacketlength,ret1);

  }

static inline int inject_dma_descriptor_adaptive(dma_tcp_t *dma_tcp,
		                           unsigned int my_injection_group,
		                           unsigned int desired_fifo,
		                           DMA_InjDescriptor_t *desc)
  {
    int ret __attribute__((unused));
    TRACEN(k_t_general|k_t_sgdiag , "(>) injecting my_injection_group=%d desired_fifo=%d desc=%p",my_injection_group,desired_fifo,desc);
    TRACEN(k_t_sgdiag,"injecting 0x%04x bytes",desc->msg_length) ;
    ret = wrapped_DMA_InjFifoInjectDescriptorById( &dma_tcp->injFifoGroupFrames,
                                            dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo],
                                            desc );

    TRACEN(k_t_general , "(<) ret=%d",ret);
    return 1 ;

  }

static inline int inject_dma_descriptors_adaptive(dma_tcp_t *dma_tcp,
		                           unsigned int my_injection_group,
		                           unsigned int desired_fifo,
		                           DMA_InjDescriptor_t **desc,
		                           unsigned int count )
  {
    int ret __attribute__((unused));
    int r2 __attribute__((unused));
    unsigned int fifo_index = my_injection_group*k_injecting_directions+desired_fifo ;
    TRACEN(k_t_general|k_t_sgdiag , "(>) injecting my_injection_group=%d desired_fifo=%d desc=%p count=%d fifo_id=0x%02x",
	    my_injection_group,desired_fifo,desc,count, dma_tcp->injFifoFramesIds[fifo_index]);
    if( 0 == desc[0]->msg_length)
	    {
		    TRACEN(k_t_general,"(I) msg_length[0] zero, injection skipped") ;
		    desc += 1 ;
		    count -= 1 ;
	    }
    ret = DMA_InjFifoInjectDescriptorsById( &dma_tcp->injFifoGroupFrames,
					    dma_tcp->injFifoFramesIds[fifo_index],
					    count,
					    desc );
    r2=DMA_CounterSetValueWideOpenById ( & dma_tcp->injCounterGroup, k_injCounterId,  0xffffffff );
    if( ret != count)
	    {
		    TRACEN(k_t_error,"(!!!) count=%d ret=%d",count,ret) ;
	    }

    TRACEN(k_t_general , "(<) count=%d fifo_id=0x%02x",
		    count,dma_tcp->injFifoFramesIds[fifo_index]);

    return count ;
  }

/* Don't actually need this; the length is precise anyway, we just may waste some cells in the last packet */
#if 0
static inline int inject_dma_descriptor_adaptive_precise_length(dma_tcp_t *dma_tcp,
		                           unsigned int my_injection_group,
		                           unsigned int desired_fifo,
		                           DMA_InjDescriptor_t *desc)
  {
	unsigned int size=desc->msg_length ;
	unsigned int full_frame_count=size / k_torus_link_payload_size ;
	unsigned int full_frame_size = full_frame_count * k_torus_link_payload_size ;
	unsigned int trailing_frame_size = size - full_frame_size ;
	unsigned int rc=0 ;
	if(0 == trailing_frame_size || 0 == full_frame_count)  // These cases were already 'precise'
		{
		    int ret __attribute__((unused));
		    TRACEN(k_t_general , "(>) injecting my_injection_group=%d desired_fifo=%d desc=%p",my_injection_group,desired_fifo,desc);
		    ret = wrapped_DMA_InjFifoInjectDescriptorById( &dma_tcp->injFifoGroupFrames,
							    dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo],
							    desc );
		    TRACEN(k_t_general , "(<) ret=%d",ret);
		    return 1 ;
		}
	else
		{
			 /*  Need to split into 2 injections in order not to transmit extra cells */
			int ret __attribute__((unused));
			desc->msg_length=full_frame_size ;
			ret = wrapped_DMA_InjFifoInjectDescriptorById( &dma_tcp->injFifoGroupFrames,
			 			               dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo],
							       desc );
			desc->msg_length=trailing_frame_size ;
			desc->base_offset += full_frame_size ;
			desc->hwHdr.Chunks = DMA_PacketChunks(trailing_frame_size) - 1 ;
			ret = wrapped_DMA_InjFifoInjectDescriptorById( &dma_tcp->injFifoGroupFrames,
			 			               dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo],
							       desc );
			return 2 ;



		}

  }
#endif


static void analyse_skb(struct sk_buff *skb) __attribute__ ((unused)) ;
static void analyse_skb(struct sk_buff *skb)
  {
    struct sock   *sk=skb->sk ;
    struct inet_sock *inet = inet_sk(sk);
    struct inet_connection_sock *icsk = inet_csk(sk);
    unsigned int daddr=inet->daddr ;
    unsigned int flags = TCP_SKB_CB(skb)->flags ;
    if(icsk->icsk_retransmits > 0 )
      {
        TRACEN(k_t_congestion,"(I) sk=%p skb=%p data=%p len=%d flags=0x%02x ip=%u.%u.%u.%u icsk_retransmits=%d icsk_rto=%d resending (BGP)",
            sk, skb, skb->data, skb->len, flags,
            daddr>>24, (daddr>>16)&0xff,(daddr>>8)&0xff,daddr&0xff,
            icsk->icsk_retransmits, icsk->icsk_rto ) ;
      }
  }

static inline int selfsend(const torusLocation_t * t, unsigned int x, unsigned int y, unsigned int z)
{
	unsigned int tx=t->coordinate[0] ;
	unsigned int ty=t->coordinate[1] ;
	unsigned int tz=t->coordinate[2] ;
	return (tx == x && ty == y && tz == z) ;
}

static inline int offfabric(const torusLocation_t * t, unsigned int x, unsigned int y, unsigned int z)
{
	unsigned int tx=t->coordinate[0] ;
	unsigned int ty=t->coordinate[1] ;
	unsigned int tz=t->coordinate[2] ;
	return (x >= tx || y >= ty || z >= tz) ;
}
static inline void clear_dir_in_use(unsigned char * direction_is_in_use)
{
	int x ;
	for(x=0;x<=k_injecting_directions;x+=1)
		{
			direction_is_in_use[x] = 0 ;
		}
}

static inline void record_dir_in_use(dma_tcp_t * dma_tcp,unsigned char * direction_is_in_use)
{
	int x ;
	for(x=0;x<k_injecting_directions;x+=1)
		{
			dma_tcp->tx_in_use_count[x] += direction_is_in_use[x] ;
		}
	dma_tcp->tx_in_use_count[k_injecting_directions] += 1 ;
}

/*  Routine to free all the skbuffs that control data which has left the node */
static void dma_tcp_frames_transmission_free_skb(unsigned long parm)
  {
    dma_tcp_t *dma_tcp = &dma_tcp_state ;
    unsigned int core ;
    unsigned int total_injection_used = 0 ;
    unsigned char direction_is_in_use[k_skb_controlling_directions] ;
    clear_dir_in_use(direction_is_in_use) ;
#if defined(TRACK_LIFETIME_IN_FIFO)
    unsigned long long now=get_powerpc_tb() ;
#endif
    for( core=0 ; core<k_injecting_cores; core += 1)
	    {
		    unsigned int desired_fifo ;
			   for(desired_fifo=0; desired_fifo<k_skb_controlling_directions; desired_fifo += 1 )
			   {
			       spinlock_t * injectionLock = &dma_tcp->dirInjectionLock[core*k_injecting_directions+desired_fifo] ;
			       idma_direction_t * buffer = dma_tcp->idma.idma_core[core].idma_direction+desired_fifo ;
			       unsigned int fifo_initial_head = dma_tcp->idma.idma_core[core].idma_direction[desired_fifo].fifo_initial_head ;
			       unsigned int bhx = buffer->buffer_head_index ;
			       unsigned int btx = buffer->buffer_tail_index ;  /*  This indexes the oldest skbuff that might still be pending send by the DMA unit */
			       unsigned int  fifo_current_head =
			        (unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+desired_fifo]) ;
			       unsigned int  fifo_current_tail =
			        (unsigned int) DMA_InjFifoGetTailById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+desired_fifo]) ;
			       unsigned int headx = (fifo_current_head-fifo_initial_head) >> 5 ;
			       unsigned int tailx = (fifo_current_tail-fifo_initial_head) >> 5 ;
			       unsigned int current_injection_used=packet_mod(tailx-headx) ;
			       int skql2 = packet_mod(bhx-btx) ;
			       if( 0 != current_injection_used ) direction_is_in_use[desired_fifo] = 1 ;
			       if( skql2 != current_injection_used)
				       {
					       skb_group_t skb_group ;

					       skb_group_init(&skb_group) ;
					       if( spin_trylock(injectionLock))
					       {
						       unsigned int bhx = buffer->buffer_head_index ;
						       unsigned int btx = buffer->buffer_tail_index ;  /*  This indexes the oldest skbuff that might still be pending send by the DMA unit */
						       unsigned int  fifo_current_head =
							(unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+desired_fifo]) ;
						       unsigned int  fifo_current_tail =
							(unsigned int) DMA_InjFifoGetTailById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+desired_fifo]) ;
						       unsigned int headx = (fifo_current_head-fifo_initial_head) >> 5 ;
						       unsigned int tailx = (fifo_current_tail-fifo_initial_head) >> 5 ;
						       unsigned int current_injection_used=packet_mod(tailx-headx) ;
						       int skql2 = packet_mod(bhx-btx) ;
					               int count_needing_freeing = skql2-current_injection_used ;
					               int count_to_free = ( count_needing_freeing > k_skb_group_count) ? k_skb_group_count : count_needing_freeing ;
					               TRACEN(k_t_detail,"current_injection_used=%d skql2=%d count_needing_freeing=%d count_to_free=%d",current_injection_used,skql2,count_needing_freeing,count_to_free);
					               skb_group_queue(&skb_group,dma_tcp->idma.idma_core[core].idma_direction[desired_fifo].idma_skb_array->skb_array,btx,count_to_free
#if defined(TRACK_LIFETIME_IN_FIFO)
					        		       , core, desired_fifo, now
#endif
					        		       ) ;
					               btx = packet_mod(btx+count_to_free) ;
					               buffer->buffer_tail_index = btx ;
					               TRACEN(k_t_detail ,"buffer=%p buffer->buffer_tail_index=%d",buffer,buffer->buffer_tail_index);
						       total_injection_used += current_injection_used ;

						       spin_unlock(injectionLock) ;
						       skb_group_free(&skb_group) ;
					       }
					       else
						       {
							       total_injection_used += current_injection_used ;
						       }
				       }
			   }
	    }
    TRACEN(k_t_detail,"total_injection_used=%d",total_injection_used) ;
    record_dir_in_use(dma_tcp,direction_is_in_use) ;
    if( total_injection_used > 0 )
	    {
		     mod_timer(&dma_tcp->transmission_free_skb_timer, jiffies+1) ;
	    }
  }


static void display_skb_structure(struct sk_buff *skb)
{
	int f ;
	unsigned int headlen=skb_headlen(skb) ;
	TRACEN(k_t_request, "sk_buff(head=%p data=%p tail=%p end=%p len=0x%08x data_len=0x%08x nr_frags=%d",
			skb->head, skb->data, skb->tail, skb->end, skb->len, skb->data_len, skb_shinfo(skb)->nr_frags) ;
	dumpmem(skb->data,(headlen > 256) ? 256 : headlen,"skb head") ;
	for(f=0; f<skb_shinfo(skb)->nr_frags; f+=1)
		{
			   struct skb_frag_struct* frag = &skb_shinfo(skb)->frags[f];
			   unsigned int page_offset=frag->page_offset ;
			   unsigned int size = frag->size ;
			   TRACEN(k_t_request, " frags[%d](page_offset=0x%08x size=0x%08x)",
					f,page_offset,size) ;
		}
}

static inline unsigned int imin2(unsigned int a, unsigned int b)
{
	return (a>b) ? b : a ;
}
#if defined(USE_SKB_TO_SKB)
static void bgp_dma_tcp_s_and_f_frames_dma(
    dma_tcp_t *dma_tcp,
    struct sk_buff *skb
    )
{
	  frame_injection_cb * ficb = (frame_injection_cb *) skb->cb ;
	    struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
	    unsigned int x = eth->h_dest[3] ;
	    unsigned int y = eth->h_dest[4] ;
	    unsigned int z = eth->h_dest[5] ;
	unsigned int payload_address = (unsigned int)(skb->data) ;
	unsigned int aligned_payload_address = payload_address & (~ 0x0f) ;
	unsigned int pad_head = payload_address & 0x0f ;
	unsigned int src_key = (dma_tcp->src_key << 4) | pad_head ;  /*  Everything to a given node will go on the same stream, no point coding injection group in */
	unsigned int headlen = skb_headlen(skb) ;
	TRACEN(k_t_general ,"(>)skb=%p (%02x,%02x,%02x) data=%p length=%d data_len=%d headlen=%d", skb,x,y,z,skb->data, skb->len, skb->data_len,headlen);
	dumpframe(skb->data, skb_headlen(skb), "skbuff to send") ;

	TRACEN(k_t_general, "(=)(I) testdma: Sending to (%d,%d,%d)",
		x, y, z );

	 /*  Make sure we're not trying to send off the partition or to self */
	if( k_verify_target)
		    {
			    if( offfabric(&(dma_tcp->extent),x,y,z))
				    {
						TRACEN(k_t_error, "(W) Target (%d,%d,%d) not in range",x,y,z) ;
						WARN_ON(1) ;
						dev_kfree_skb(skb) ;
						return ;
				    }
			    if( selfsend(&(dma_tcp->location),x,y,z))
				    {
						TRACEN(k_t_error, "(W) Self-send not supported by hardware (%d %d %d)",x,y,z) ;
						WARN_ON(1) ;
						dev_kfree_skb(skb) ;
						return ;
				    }
		    }

	TRACEN(k_t_protocol,"(=)sending packet to (%02x,%02x,%02x) length=%d",
		 x,y,z,skb->len) ;

	 /*  copy descriptor into the inj fifo */
	{
		unsigned int dest_key =  x*dma_tcp->extent.coordinate[1]*dma_tcp->extent.coordinate[2]
					      +y*dma_tcp->extent.coordinate[2]
					      +z ;
		unsigned int conn_id = take_tx_conn_id(&dma_tcp->tx_mux,dest_key) ;
		atomic_inc(&dma_tcp->framesProposed) ;
	    TRACEN(k_t_general,"Saving skb=%p for dest_key=0x%08x conn_id=0x%08x",skb,dest_key,conn_id) ;
	    set_tx_skb(&dma_tcp->tx_mux,dest_key,conn_id,skb) ;
	    ficb->free_when_done = 0 ;

#if defined(AUDIT_HEADLEN)
	    {
		    struct iphdr *iph = (struct iphdr *)(eth+1) ;
		    ficb->tot_len = iph->tot_len ;
	    }
#endif
	    {
		     /*  If we have a 'scatter-gather' skb, try to put the head into the 'propose' packet */
		    unsigned int nr_frags = skb_shinfo(skb)->nr_frags ;
		    unsigned int propose_length = (nr_frags == 0 ) ? 48 : imin2(pad_head+headlen,k_torus_link_payload_size) ;
		    eth->h_source[0] = propose_length ; // Use a byte on-the-side to say how much data was actually sent
		    TRACEN(k_t_general,"nr_frags=%d propose_length=%d",nr_frags,propose_length) ;
		  create_dma_descriptor_propose_accept(dma_tcp,
				  (void *)aligned_payload_address,
				  propose_length,
				  x,y, z,
				  dma_tcp->proto_transfer_propose,
				  src_key,
				  conn_id,
				  0,
				  &ficb->desc,
				  propose_length
				) ;
	    }
	}
	instrument_flow(dma_tcp,k_send_propose_rpc) ;
	  bgp_dma_tcp_s_and_f_frames_prepared(dma_tcp, skb, 0, k_cattle_class) ;
}
#endif

static int inject_scattergather(
		    dma_tcp_t *dma_tcp,
		    struct sk_buff *skb,
                    unsigned int my_injection_group,
                    unsigned int desired_fifo
)
{
	    frame_injection_cb * ficb = (frame_injection_cb *) skb->cb ;
	  unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	    struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
	    unsigned int aligned_payload_length = ficb->desc.msg_length ;
	    unsigned int x=ficb->desc.hwHdr.X ;
	    unsigned int y=ficb->desc.hwHdr.Y ;
	    unsigned int z=ficb->desc.hwHdr.Z ;
	   unsigned int f ;
	   unsigned int dest_offset=k_abbreviate_headlen ? (aligned_payload_length+eth->h_source[0]): aligned_payload_length ;
	   unsigned int base_offset=ficb->desc.base_offset ;
	   unsigned int rctr=ficb->desc.hwHdr.rDMA_Counter % DMA_NUM_COUNTERS_PER_GROUP ;
	    struct iphdr *iph = (struct iphdr *)(eth+1) ;
	    unsigned int daddr=iph->daddr ;

	    DMA_InjDescriptor_t descVector[MAX_SKB_FRAGS] ;
	    DMA_InjDescriptor_t * descPtr[1+MAX_SKB_FRAGS] ;
	    unsigned int total_inj_length = ficb->desc.msg_length ;
	   TRACEN(k_t_scattergather|k_t_sgdiag,"injecting, base_offset=0x%04x length=0x%04x my_injection_group=%d desired_fifo=%d dest_offset=0x%04x",
			   base_offset,ficb->desc.msg_length,my_injection_group,desired_fifo, dest_offset) ;

	   /* Prepare the initial not-fragment part */
	   descPtr[0] = &ficb->desc ;
	   /* scatter-gather fragments to be pushed out here */
	   for(f=0;f<nr_frags;f+=1)
		   {
			   struct skb_frag_struct* frag = &skb_shinfo(skb)->frags[f];
			   struct page *page = frag->page ;
			   unsigned int page_offset=frag->page_offset ;
			   unsigned int size = frag->size ;
			   dma_addr_t buffAddr = dma_map_page(NULL, page, page_offset, size, DMA_TO_DEVICE);
			   TRACEN(k_t_scattergather|k_t_sgdiag,"f=%d page=%p page_offset=0x%04x size=0x%04x buffAddr=0x%08llx dest_offset=0x%04x",
					   f,page,page_offset,size,buffAddr,dest_offset) ;
			   total_inj_length += size ;
			   if( 0 != size)
				   {
					   create_dma_descriptor_direct_put_offset(dma_tcp,x,y,z,k_injCounterId,rctr,buffAddr,size,descVector+f,dest_offset) ;
				   }
			   else
				   {
					   TRACEN(k_t_request,"(I) frag length zero") ;
					   DMA_ZeroOutDescriptor(descVector+f) ;
					   instrument_flow(dma_tcp,k_fraglength_zero) ;
				   }
			   descPtr[1+f]=descVector+f ;
			   dest_offset += size ;

		   }
	   TRACEN(k_t_sgdiag,"Injecting tgt=[%d,%d,%d] length=0x%04x ctr=0x%02x",x,y,z,total_inj_length,rctr) ;


	   TRACEN(k_t_scattergather ,"tgt=[%d %d %d] daddr=%d.%d.%d.%d tot_len=0x%04x, length=0x%04x headlen=0x%04x data_len=0x%04x dest_offset=0x%04x nr_frags=%d fragsizes[0x%04x 0x%04x 0x%04x] counter=0x%02x injfifo[%d %02x]\n",
				    x,y,z,
				    daddr>>24, (daddr >> 16) & 0xff,(daddr >> 8) & 0xff, daddr & 0xff,iph->tot_len,
				    skb->len,skb_headlen(skb), skb->data_len, dest_offset,
				    nr_frags,skb_shinfo(skb)->frags[0].size,skb_shinfo(skb)->frags[1].size,skb_shinfo(skb)->frags[2].size,rctr,my_injection_group,desired_fifo ) ;
	    if( skb_headlen(skb) < sizeof(struct ethhdr)+sizeof(struct iphdr))
		    {
			    TRACEN(k_t_request,"(!!!) length=0x%04x data_len=0x%04x nr_frags=%d fragsizes[0x%04x 0x%04x 0x%04x]",skb->len, skb->data_len, nr_frags,skb_shinfo(skb)->frags[0].size,skb_shinfo(skb)->frags[1].size,skb_shinfo(skb)->frags[2].size) ;
			    display_skb_structure(skb) ;
		    }
	    return inject_dma_descriptors_adaptive(dma_tcp,my_injection_group,desired_fifo,descPtr,1+nr_frags) ;

}
/*  Send-and-free a frame with an already-prepared injection descriptor (which might be DMA-put or FIFO-put) */
static int bgp_dma_tcp_s_and_f_frames_prepared(
    dma_tcp_t *dma_tcp,
    struct sk_buff *skb,
    unsigned int queue_at_head,
    unsigned int transport_class
    )
  {
	  unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	  unsigned int is_scattergather = (nr_frags > 0 ) ;
	    unsigned int payload_length = (skb -> len) - (skb->data_len) ;
	    unsigned int payload_address = (unsigned int)(skb->data) ;
	    unsigned int aligned_payload_address = payload_address & (~ 0x0f) ;
	    unsigned int pad_head = payload_address & 0x0f ;
	    unsigned int aligned_payload_length = payload_length + pad_head ;
	#if 1
	    unsigned int use_taxi = 0 ;
	#else
	    unsigned int use_taxi = (aligned_payload_length<=k_injection_packet_size) && (0 == nr_frags);
	#endif
	    unsigned long flags ;
	    unsigned int current_injection_used=0xffffffff ;

	    int ret = 0;
	    int ring_ok ;

	    int my_injection_group ;
	    skb_group_t skb_group ;
	    frame_injection_cb * ficb = (frame_injection_cb *) skb->cb ;
	    unsigned int x=ficb->desc.hwHdr.X ;
	    unsigned int y=ficb->desc.hwHdr.Y ;
	    unsigned int z=ficb->desc.hwHdr.Z ;
	    unsigned int header_dma_length=ficb->desc.msg_length ; // If this is zero, then we can free the skb as soon as its 'frags' are in software injection fifo
    TRACEN(k_t_general ,"(>)skb=%p (%02x,%02x,%02x) data=%p length=%d data_len=%d nr_frags=%d", skb,x,y,z,skb->data, skb->len, skb->data_len, nr_frags);
    if(is_scattergather ) instrument_flow(dma_tcp,k_scattergather) ;

    skb_group_init(&skb_group) ;

    TRACEN(k_t_general, "(=)(I) testdma: Sending to (%d,%d,%d)",
            x, y, z );

/*  Make sure we're not trying to send off the partition or to self */
    if( k_verify_target)
	    {
		    if( offfabric(&(dma_tcp->extent),x,y,z))
			    {
					TRACEN(k_t_error, "(W) Target (%d,%d,%d) not in range",x,y,z) ;
					WARN_ON(1) ;
					dev_kfree_skb(skb) ;
					return -EINVAL;
			    }
		    if( selfsend(&(dma_tcp->location),x,y,z))
			    {
					TRACEN(k_t_error, "(W) Self-send not supported by hardware (%d %d %d)",x,y,z) ;
					WARN_ON(1) ;
					dev_kfree_skb(skb) ;
					return -EINVAL;
			    }
	    }
    TRACEN(k_t_protocol,"(=)sending packet to (%02x,%02x,%02x) length=%d",
             x,y,z,skb->len) ;

     /*  copy descriptor into the inj fifo */
    {
    unsigned int desired_fifo=((transport_class != k_cattle_class) && (aligned_payload_length<=k_injection_packet_size) && (0 == nr_frags)) ? (k_skb_controlling_directions-1) : select_transmission_fifo(dma_tcp,x,y,z) ;
    my_injection_group=injection_group_hash(dma_tcp,x,y,z) ;
    spin_lock_irqsave(&dma_tcp->dirInjectionLock[my_injection_group*k_injecting_directions+desired_fifo],flags) ;
     {
       unsigned int src_key = (dma_tcp->src_key << 4) | pad_head ;  /*  Everything to a given node will go on the same stream, no point coding injection group in */
        /*  Work out which buffer we are going to use for the packet stream */
       idma_direction_t * buffer = dma_tcp->idma.idma_core[my_injection_group].idma_direction+desired_fifo ;
        /*  Set up the payload */
       unsigned int bhx = buffer->buffer_head_index ;
       unsigned int lastx = packet_mod(bhx) ;
       unsigned int fifo_initial_head = dma_tcp->idma.idma_core[my_injection_group].idma_direction[desired_fifo].fifo_initial_head ;
       unsigned int  fifo_current_head =
        (unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo]) ;
       unsigned int  fifo_current_tail =
        (unsigned int) DMA_InjFifoGetTailById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo]) ;
       unsigned int headx = (fifo_current_head-fifo_initial_head) >> 5 ;
       unsigned int tailx = (fifo_current_tail-fifo_initial_head) >> 5 ;
       unsigned int injection_count ;
#if defined(TRACK_LIFETIME_IN_FIFO)
       unsigned long long now=get_powerpc_tb() ;
       *(unsigned long long*)(skb->cb) = now ;
#endif
       current_injection_used=packet_mod(tailx-headx) ;
        /*  If the network is backing up, we may have to skip out here, */
        /*  so that we don't overwrite unsent data. */
       TRACEN(k_t_general ,"Runway desired_fifo=%d headx=%d tailx=%d bhx=%d current_injection_used=%d",
           desired_fifo,headx,tailx,bhx,current_injection_used) ;
       if( current_injection_used > buffer->injection_high_watermark )
         {
           buffer->injection_high_watermark=current_injection_used ;  /*  Congestion statistic */
         }
         {
        	  /*  Need to have room to inject the in-skbuff data plus all attached 'fragments', each of which may be sent in 3 injections */
           if( current_injection_used+3*(MAX_SKB_FRAGS+1) < k_injection_packet_count-1)
             {
                ring_ok = 1 ;
                TRACEN(k_t_general,"Runway slot granted") ;
             }
           else
             {
                ring_ok = 0 ;
                TRACEN(k_t_congestion,"Runway slot denied tailx=%08x headx=%08x",tailx,headx) ;
             }
         }
       TRACEN(k_t_general ,"Injection my_injection_group=%d desired_fifo=%d bhx=0x%08x headx=%08x tailx=%08x nr_frags=%d",
           my_injection_group, desired_fifo, bhx, headx,tailx,nr_frags
           ) ;
       if ( ring_ok )
         {
            /*  We are going to send something. */

            /*  Bump the injection counter. Actually only needs doing once per 4GB or so */
           ret=DMA_CounterSetValueWideOpenById ( & dma_tcp->injCounterGroup, k_injCounterId,  0xffffffff );

	    /*  and inject it */
	   if(use_taxi)
		   {
		           injection_count = inject_into_dma_taxi(dma_tcp,(void *)aligned_payload_address,aligned_payload_length,x,y,z,my_injection_group,desired_fifo,
		        		   dma_tcp->proto_issue_frames_single,src_key) ;
		   }
	   else
		   {
			   if( is_scattergather && 0 != ficb->free_when_done)
				   {
					   injection_count = inject_scattergather(
							    dma_tcp,skb,my_injection_group,desired_fifo
							   ) ;
				   }
			   else
				   {
					    /*  Prop, or accept, or unfragmented skbuff */
					   injection_count = inject_dma_descriptor_adaptive(dma_tcp,my_injection_group,desired_fifo,
					   &ficb->desc
					   ) ;
				   }

		   }
	   {
		   unsigned int nhx=packet_mod(bhx+injection_count) ;
		   /*  Remember where we will be pushing the next injection in */
		   TRACEN(k_t_detail,"Next injection will be at nhx=0x%08x",nhx) ;
		   buffer->buffer_head_index = nhx ;
		    /*  Record the skbuff so it can be freed later, after data is DMA'd out */
		   if( ficb->free_when_done && header_dma_length > 0 )
			   {
				   TRACEN(k_t_detail,"Saving skb=%p at [%p] for freeing later",skb,dma_tcp->idma.idma_core[my_injection_group].idma_direction[desired_fifo].idma_skb_array->skb_array+nhx) ;
				   dma_tcp->idma.idma_core[my_injection_group].idma_direction[desired_fifo].idma_skb_array->skb_array[nhx] = skb ;
			   }
	   }
            /*  hang on to the skbs until they are sent ... */
           if( current_injection_used != 0xffffffff)
             {
               unsigned int btx = buffer->buffer_tail_index ;  /*  This indexes the oldest skbuff that might still be pending send by the DMA unit */
               int skql2 = packet_mod(bhx-btx) ;
               int count_needing_freeing = skql2-current_injection_used ;
               int count_to_free = ( count_needing_freeing > k_skb_group_count) ? k_skb_group_count : count_needing_freeing ;
               TRACEN(k_t_detail ,"current_injection_used=%d btx=%d skql2=%d count_needing_freeing=%d count_to_free=%d",current_injection_used,btx,skql2,count_needing_freeing,count_to_free);
               skb_group_queue(&skb_group,dma_tcp->idma.idma_core[my_injection_group].idma_direction[desired_fifo].idma_skb_array->skb_array,btx,count_to_free
#if defined(TRACK_LIFETIME_IN_FIFO)
					        		       , my_injection_group, desired_fifo, now
#endif
               ) ;
               btx = packet_mod(btx+count_to_free) ;
               buffer->buffer_tail_index = btx ;
               TRACEN(k_t_detail ,"buffer=%p buffer->buffer_tail_index=%d",buffer,buffer->buffer_tail_index);
             }
         }
       else
         {
           TRACEN(k_t_congestion,"Would overrun my_injection_group=%d desired_fifo=%d bhx=0x%08x headx=%08x tailx=%08x lastx=%08x",
               my_injection_group, desired_fifo, bhx, headx,tailx, lastx
               ) ;
         }
     }
     spin_unlock_irqrestore(&dma_tcp->dirInjectionLock[my_injection_group*k_injecting_directions+desired_fifo],flags) ;
     skb_group_free(&skb_group) ;
     if( k_async_free ) mod_timer(&dma_tcp->transmission_free_skb_timer, jiffies+1) ;
   if( 0 == ring_ok )
     {
       TRACEN(k_t_congestion,"(=)Queuing skb=%p desired_fifo=%d (%u %u %u)", skb,desired_fifo,x,y,z) ;
       if( queue_at_head)
         {
           skb_queue_head(dma_tcp->inj_queue+desired_fifo, skb) ;
         }
       else
         {
           skb_queue_tail(dma_tcp->inj_queue+desired_fifo, skb) ;
         }
     }
   else
	   {
		   if( 0 == header_dma_length)
			   {
				   TRACEN(k_t_general,"Freeing skb=%p, its header has left the node",skb) ;
				   dev_kfree_skb(skb) ;
			   }
	   }



   TRACEN(k_t_general ,"(<) ring_ok=%d desired_fifo=%d",ring_ok,desired_fifo);

   return  ring_ok ? desired_fifo : -1 ;
    }

  }

/*  ... return 'direction' if we sent the packet, '-1' if we queued it */
static int bgp_dma_tcp_s_and_f_frames(
    dma_tcp_t *dma_tcp,
    struct sk_buff *skb,
    unsigned int queue_at_head
    )
{
#if defined(USE_ADAPTIVE_ROUTING)
  struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
  unsigned int x = eth->h_dest[3] ;
  unsigned int y = eth->h_dest[4] ;
  unsigned int z = eth->h_dest[5] ;
  unsigned int payload_length = (skb -> len) - (skb->data_len) ;
  unsigned int payload_address = (unsigned int)(skb->data) ;
  unsigned int aligned_payload_address = payload_address & (~ 0x0f) ;
  unsigned int pad_head = payload_address & 0x0f ;
  unsigned int src_key = (dma_tcp->src_key << 4) | pad_head ;  /*  Everything to a given node will go on the same stream, no point coding injection group in */
  unsigned int aligned_payload_length = payload_length + pad_head ;
  frame_injection_cb * ficb = (frame_injection_cb *) skb->cb ;

   unsigned int dest_key =  x*dma_tcp->extent.coordinate[1]*dma_tcp->extent.coordinate[2]
			      +y*dma_tcp->extent.coordinate[2]
			      +z ;
   unsigned int conn_id = take_tx_conn_id(&dma_tcp->tx_mux,dest_key) ;
   instrument_flow(dma_tcp,k_send_eager) ;
   ficb->free_when_done = 1 ;

  if(TRACING(k_t_sgdiag))
	  {
		  diag_skb_structure(skb) ;
	  }
   create_dma_descriptor_adaptive(dma_tcp,(void *)aligned_payload_address,aligned_payload_length,x,y,z,
		   dma_tcp->proto_issue_frames_adaptive,src_key,conn_id, &ficb->desc
   ) ;

#endif
   if( k_verify_ctlen)
           {
                   unsigned int ctlen = counted_length(skb) ;
                   struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
                   struct iphdr *iph = (struct iphdr *)(eth+1) ;
                   if( ctlen != iph->tot_len + sizeof(struct ethhdr))
                           {
                                  TRACEN(k_t_error,"(E) Counted length mismatch, skb=%p, conuted_length=0x%04x, tot_len=0x%04x",skb,ctlen,iph->tot_len ) ;
                                  display_skb_structure(skb) ;
                                  display_iphdr(iph) ;
                                  dev_kfree_skb(skb) ; // It would cause trouble later, to try and send it. So drop it.
                                  instrument_flow(dma_tcp,k_counted_length_mismatch) ;
                                  return 0 ; // Not really 'direction 0', but this will not cause the caller a problem.
                           }
           }

	return  bgp_dma_tcp_s_and_f_frames_prepared(dma_tcp,skb,queue_at_head, 0) ;
}

/*  Try to clear a pending skbuff queue into the mem-fifo */
/*  return 0 if queue cleared */
/*        -1 if the queue cannot be cleared because the FIFO gets full */
static int bgp_dma_tcp_try_to_clear_queue(dma_tcp_t *dma_tcp, unsigned int direction) noinline ;
static int bgp_dma_tcp_try_to_clear_queue(dma_tcp_t *dma_tcp, unsigned int direction)
  {
    struct sk_buff_head *skq = dma_tcp->inj_queue+direction ;
    TRACEN(k_t_general,"(>) direction=%u",direction );
     if( ! skb_queue_empty(skq))
       {
          /*  We sent something, and there is a pending list which we might be able to send as well */
         for(;;)
           {
             struct sk_buff * askb = skb_dequeue(skq) ;
             if( askb)
               {
                  TRACEN(k_t_congestion,"(=)Dequeuing dir=%d askb=%p length=%u", direction, askb,askb->len) ;
                    {
                      int arc= bgp_dma_tcp_s_and_f_frames_prepared(dma_tcp,askb,1,k_cattle_class) ;
                      if( -1 == arc)
                        {
                          TRACEN(k_t_congestion,"still-congested dir=%d",direction );
                          TRACEN(k_t_general,"(<) still-congested" );
                          instrument_flow(dma_tcp,k_queue_filled_propose_fifo) ;
                          return -1 ;  /*  Queue not cleared */
                        }
                    }
               }
             else
               {
                 TRACEN(k_t_congestion,"(=)Dequeuing askb=NULL") ;
                 break ;
               }

           }

       }

     TRACEN(k_t_general,"(<) clear" );
     return 0 ;  /*  Queue cleared */
  }

static void dma_tcp_frames_runway_check(unsigned long parm)
  {
    dma_tcp_t *dma_tcp = &dma_tcp_state ;
    int direction ;
    int anything_queued = 0 ;
    TRACEN(k_t_congestion,"(>)");
    for(direction=0;direction<k_injecting_directions;direction+=1)
      {
        anything_queued += bgp_dma_tcp_try_to_clear_queue(dma_tcp,direction) ;
      }
    if( anything_queued)
      {
        mod_timer(&dma_tcp->runway_check_timer,jiffies+1) ;  /*  Redrive on the next timer tick */
      }
    TRACEN(k_t_congestion,"(<) anything_queued=%d",anything_queued);
  }

/*  Take an skbuff bound for (x,y,z), and either put it in the software FIFO or queue it for when congestion abates */
int bgp_dma_tcp_send_and_free_frames( struct sk_buff *skb  )
{
  TRACEN(k_t_general,"(>)skb=%p data=%p length=%d", skb,skb->data, skb->len) ;
  {
    dma_tcp_t *dma_tcp = &dma_tcp_state ;
    dma_tcp->tx_by_core[smp_processor_id() & 3] += 1 ;  /*  Stats on which core(s) are busy */
#if defined(CONFIG_BGP_STATISTICS)
    {
	struct ethhdr *eth = (struct ethhdr *) (skb->data) ;
	struct iphdr *iph=(struct iphdr *) (eth+1) ;
	dma_tcp->bytes_sent += iph->tot_len ;
    }
#endif

    if( 0 == skb_headlen(skb))
	    {
		    TRACEN(k_t_request,"(I) head length zero") ;
	    }

#if defined(USE_SKB_TO_SKB)
    if( skb->len > dma_tcp->eager_limit  || 0 != skb_shinfo(skb)->nr_frags )
	    {
		    bgp_dma_tcp_s_and_f_frames_dma(dma_tcp,skb) ;
	    }
    else
#endif
	    {
		    int rc = bgp_dma_tcp_s_and_f_frames(dma_tcp,skb,
		 /* 		    x,y,z, */
				    0) ;
		    if( rc == -1)
		      {
			mod_timer(&dma_tcp->runway_check_timer,jiffies+1) ;  /*  Redrive on the next timer tick */
		      }
	    }
  }
  TRACEN(k_t_general,"(<)");
  return 0 ;
}

#if defined(ENABLE_LATENCY_TRACKING)

static unsigned int isqrt(unsigned int x)
  {
    unsigned int rc=0 ;
    unsigned int i ;
    for( i=0;i<16;i+=1)
      {
        unsigned int c= rc | (0x8000 >> i) ;
        if( c*c <= x ) rc = c ;
      }
    return rc ;
  }
#endif

#if defined(TRACK_SEQUENCE)
static void dma_tcp_frames_show_sequence(dma_tcp_t *dma_tcp)
  {
    unsigned int x ;
    unsigned int y ;
    unsigned int z ;
    unsigned int core ;
    unsigned int xsize = dma_tcp->extent.coordinate[0] ;
    unsigned int ysize = dma_tcp->extent.coordinate[1] ;
    unsigned int zsize = dma_tcp->extent.coordinate[2] ;
    unsigned int myx = dma_tcp->location.coordinate[0] ;
    unsigned int myy = dma_tcp->location.coordinate[1] ;
    unsigned int myz = dma_tcp->location.coordinate[2] ;
    for(x=0;x<xsize; x+=1 )
      {
        for( y = 0; y<ysize; y+=1)
          {
            for( z = 0 ; z<zsize; z+=1 )
              {
                unsigned int slot_base = x*(ysize*zsize) + y*zsize + z ;
                for( core=0; core<k_injecting_cores; core+=1)
                  {
                    unsigned int slot = (slot_base << 2) | core ;
                    unsigned int txcount = send_sequences[slot] ;
                    unsigned int rxcount = receive_sequences[slot] ;
                    if( txcount || rxcount)
                      {
                        TRACEN(k_t_request,"( %d %d %d ) show_sequence( %d %d %d %d )=( %d %d )", myx, myy, myz, x,y,z,core, txcount,rxcount) ;
                      }
                  }
              }
          }
      }
  }
#endif

#if defined(ENABLE_PROGRESS_TRACKING)
static void dma_tcp_frames_show_progress(dma_tcp_t *dma_tcp)
  {
    unsigned int x ;
    unsigned int y ;
    unsigned int z ;
    unsigned int core ;
    unsigned int xsize = dma_tcp->extent.coordinate[0] ;
    unsigned int ysize = dma_tcp->extent.coordinate[1] ;
    unsigned int zsize = dma_tcp->extent.coordinate[2] ;
    unsigned int myx = dma_tcp->location.coordinate[0] ;
    unsigned int myy = dma_tcp->location.coordinate[1] ;
    unsigned int myz = dma_tcp->location.coordinate[2] ;
    unsigned long long now=get_powerpc_tb() ;
    TRACEN(k_t_entryexit,">") ;
    for(x=0;x<xsize; x+=1 )
      {
        for( y = 0; y<ysize; y+=1)
          {
            for( z = 0 ; z<zsize; z+=1 )
              {
                unsigned int slot_base = x*(ysize*zsize) + y*zsize + z ;
                for( core=0; core<k_injecting_cores; core+=1)
                  {
                    unsigned int slot = (slot_base << 2) | core ;
                    if( get_rcv_skb(&dma_tcp->rcvdemux,slot))
                      {
                        unsigned long long timestamp=get_timestamp(&dma_tcp->rcvdemux,slot) ;
                        unsigned long long age=now-timestamp ;
                        TRACEN(k_t_request,"( %d %d %d ) age( %d %d %d %d )= 0x%08x%08x !!!", myx, myy, myz, x,y,z,core,(unsigned int)(age>>32),(unsigned int)age) ;
                      }
                  }
              }
          }
      }
    TRACEN(k_t_entryexit,"<") ;
  }
#endif

static void balancer_init(bgp_dma_balancer *balancer)
{
	int x;
	for(x=0;x<k_pending_rcv_skb_classes;x+=1)
		{
			TRACEN(k_t_general,"balancer init[%d]",x) ;
			skb_queue_head_init(&balancer->b[x].pending_rcv_skbs) ;
			balancer->b[x].outstanding_counters=0 ;
		}
}

/*
 * We set up 32 software injection FIFOs. We arrange them in 4 groups of 8; the group number is chosen as a function of the
 * destination node, For the group of 8, we use 6 FIFOs to control 'bulk data' nominally one for each outbound link (though
 * adaptive routing may take a packet out of a different link when the time comes); 1 FIFO to control single-packet frames
 * which are sent high-priority because they may be 'ack' frames which will enable more data to flow from a far-end node; and
 * 1 FIFO to control 'accept' packets which are sent high-priority because a scarce local resource (a reception counter) has been
 * allocated to the transfer and we would like it underway as soon as possible.
 */

void dma_tcp_frames_init(dma_tcp_t *dma_tcp)
  {
	  TRACEN(k_t_general,"sizeof(frame_injection_cb)=%d sizeof(DMA_PacketHeader_t)=%d sizeof(DMA_InjDescriptor_t)=%d",sizeof(frame_injection_cb),sizeof(DMA_PacketHeader_t),sizeof(DMA_InjDescriptor_t)) ;

	  if( k_async_free ) setup_timer(&dma_tcp->transmission_free_skb_timer,dma_tcp_frames_transmission_free_skb,0) ;
    setup_timer(&dma_tcp->runway_check_timer,dma_tcp_frames_runway_check,0) ;
    dma_tcp->rcv_checked_time = jiffies ;
    dma_tcp->packets_received_count = 0 ;
    allocate_idma(&dma_tcp->idma) ;  /*  Buffering for packets-style injection DMA */
    allocate_rcv(&dma_tcp->rcvdemux,dma_tcp->node_count) ;  /*  Demultiplexing for packets-style reception */
#if defined(USE_ADAPTIVE_ROUTING)
    allocate_tx(&dma_tcp->tx_mux,dma_tcp->node_count) ;  /*  Multiplexing for adaptive-routing transmit */
#endif
#if defined(TRACK_SEQUENCE)
    track_sequence_init(dma_tcp->node_count) ;
#endif
    init_demux_table(dma_tcp, dma_tcp->node_count) ;
     /*  Allocate injection FIFOs for 'packets' style access */
    {
      int core ;
      int direction ;
      for( core=0; core< k_injecting_cores; core += 1  )
        {
          for( direction=0; direction< k_injecting_directions; direction += 1  )
            {
              dma_tcp->injFifoFramesPri[ core*k_injecting_directions+direction ] = 0 ;
              dma_tcp->injFifoFramesLoc[ core*k_injecting_directions+direction ] = 0 ;
              dma_tcp->injFifoFramesIds[ core*k_injecting_directions+direction ] = core*k_injecting_directions+direction ;
            }
          dma_tcp->injFifoFramesMap[ core*k_injecting_directions+0 ] = 0x80;  /*  Set deterministic injection FIFO per direction */
          dma_tcp->injFifoFramesMap[ core*k_injecting_directions+1 ] = 0x40;  /*  Set deterministic injection FIFO per direction */
          dma_tcp->injFifoFramesMap[ core*k_injecting_directions+2 ] = 0x20;  /*  Set deterministic injection FIFO per direction */
          dma_tcp->injFifoFramesMap[ core*k_injecting_directions+3 ] = 0x08;  /*  Set deterministic injection FIFO per direction */
          dma_tcp->injFifoFramesMap[ core*k_injecting_directions+4 ] = 0x04;  /*  Set deterministic injection FIFO per direction */
          dma_tcp->injFifoFramesMap[ core*k_injecting_directions+5 ] = 0x02;  /*  Set deterministic injection FIFO per direction */
          dma_tcp->injFifoFramesMap[ core*k_injecting_directions+6 ] = 0x11;  /*  Set 'high priority' FIFO for taxi channel */
          dma_tcp->injFifoFramesPri[ core*k_injecting_directions+k_injecting_directions-1 ] = 1 ; // 'high priority' for taxi channel
/*           dma_tcp->injFifoFramesMap[ core*k_injecting_directions+6 ] = 0xee; // Set any FIFO for taxi channel */
#if defined(USE_SKB_TO_SKB)
          dma_tcp->injFifoFramesMap[ core*k_injecting_directions+7 ] = 0x11;  /*  Set 'high priority' FIFO for propose/accept channel */
/*           dma_tcp->injFifoFramesMap[ core*k_injecting_directions+7 ] = 0xee; //  propose/accept channel can go in any fifo, but regular pri */
          dma_tcp->injFifoFramesPri[ core*k_injecting_directions+7 ] = 1 ; // 'high priority' for propose/accept channel
#endif
        }
    }
     /*  register receive functions for the memfifo packets */
    dma_tcp->proto_issue_frames_single=DMA_RecFifoRegisterRecvFunction(issueInlineFrameDataSingleActor, dma_tcp, 0, 0);
#if defined(USE_ADAPTIVE_ROUTING)
    dma_tcp->proto_issue_frames_adaptive=DMA_RecFifoRegisterRecvFunction(issueInlineFrameDataAdaptiveActor, dma_tcp, 0, 0);
#endif

#if defined(USE_SKB_TO_SKB)
    dma_tcp->proto_transfer_propose=DMA_RecFifoRegisterRecvFunction(issuePropActor, dma_tcp, 0, 0);
    /* If we want to start up with everything flowing through the reception FIFO , do this by setting the 'eager limit' longer than the largest IP frame */
    dma_tcp->eager_limit = k_force_eager_flow ? 10000000 : 1024 ;  /*  Frames smaller than this get sent through the FIFO rather than the DMA (set it above 65536 to run everything through receive FIFO) */
    balancer_init(&dma_tcp->balancer) ;
#endif
    dma_tcp_diagnose_init(dma_tcp) ;
    TRACEN(k_t_general,"(=)DMA_RecFifoRegisterRecvFunction proto_issue_frames_single=%d",
		    dma_tcp->proto_issue_frames_single);
  }

void dma_tcp_frames_ifup(dma_tcp_t *dma_tcp)
  {
      {
        int ret = DMA_InjFifoGroupAllocate( dma_tcp_InjectionFifoGroup(dma_tcp),
            k_injecting_cores*k_injecting_directions,   /*  num inj fifos */
                                    dma_tcp->injFifoFramesIds,
                                    dma_tcp->injFifoFramesPri,
                                    dma_tcp->injFifoFramesLoc,
                                    dma_tcp->injFifoFramesMap,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    & dma_tcp->injFifoGroupFrames );

        TRACEN(k_t_init,"(=)DMA_InjFifoGroupAllocate rc=%d", ret );
      }

    {
      int core ;
      int direction ;
      for( core=0; core< k_injecting_cores; core += 1  )
        {
          for( direction=0; direction< k_injecting_directions; direction += 1  )
            {
              int ret = DMA_InjFifoInitById( &dma_tcp->injFifoGroupFrames,
                  dma_tcp->injFifoFramesIds[core*k_injecting_directions+direction],
                  dma_tcp->idma.idma_core[core].idma_direction[direction].idma_fifo,
                  dma_tcp->idma.idma_core[core].idma_direction[direction].idma_fifo,   /*  head */
                  dma_tcp->idma.idma_core[core].idma_direction[direction].idma_fifo+1   /*  end */
                                 );
              dma_tcp->idma.idma_core[core].idma_direction[direction].fifo_initial_head =
                (unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+direction]) ;
              TRACEN(k_t_general,"(=)DMA_InjFifoInitById rc=%d initial_head=0x%08x", ret , dma_tcp->idma.idma_core[core].idma_direction[direction].fifo_initial_head);
            }
        }
    }

  }

void dma_tcp_frames_ifdown(dma_tcp_t *dma_tcp)
  {
    int ret = DMA_InjFifoGroupFree( dma_tcp_InjectionFifoGroup(dma_tcp),
        k_injecting_cores*k_injecting_directions,   /*  num inj fifos */
                                dma_tcp->injFifoFramesIds,
                                & dma_tcp->injFifoGroupFrames );

    TRACEN(k_t_init,"(=) DMA_InjFifoGroupFree rc=%d", ret );

  }
