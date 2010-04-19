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




/* void bgp_dma_diag_reissue_rec_counters(dma_tcp_t *dma_tcp) */
/* { */
/* 	unsigned int x; */
/* 	for(x=0;x<DMA_NUM_COUNTERS_PER_GROUP;x+=1) */
/* 		{ */
/* 			struct sk_buff *skb=dma_tcp->rcv_skbs[x] ; */
/* 			if( skb) */
/* 				{ */
/* 					frame_injection_cb * ficb = (frame_injection_cb *) skb->cb ; */
/* 					TRACEN(k_t_general,"Redriving x=%d skb=%p",x,skb) ; */
/* 					inject_dma_descriptor_propose_accept(dma_tcp,0,&ficb->desc) ; */
/* 				} */
/* 		} */
/* } */

static inline void show_tx_skbs(tx_t *tx, unsigned int node_count)
{
	unsigned int slot_index ;
	unsigned int conn_id ;
	unsigned int tx_skb_count = 0 ;
	for(slot_index=0;slot_index<node_count;slot_index += 1)
		{
			for( conn_id=0;conn_id < k_connids_per_node;conn_id += 1)
				{
					struct sk_buff * skb=get_tx_skb(tx,slot_index,conn_id) ;
					if(skb)
						{
							  struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
							  struct iphdr *iph = (struct iphdr *) (eth+1) ;
							  unsigned int tot_len=iph->tot_len ;
							  unsigned int daddr=iph->daddr ;
							  tx_skb_count += 1 ;

							TRACEN(k_t_request,"(---) slot_index=0x%08x conn_id=0x%02x skb=%p tot_len=0x%04x daddr=%d.%d.%d.%d",
									slot_index,conn_id,skb,tot_len,daddr>>24, (daddr >> 16) & 0xff,(daddr >> 8) & 0xff, daddr & 0xff) ;
						}
				}
		}
	TRACEN(k_t_request,"tx_skb_count=%d",tx_skb_count) ;
}

void dma_tcp_show_reception(dma_tcp_t * dma_tcp)
{
	    int x ;
	    int slot ;
	    unsigned int inUseCount = 0 ;
	    TRACEN(k_t_request,"rec hitZero 0x%08x 0x%08x",DMA_CounterGetHitZero(&dma_tcp->recCounterGroup,0),DMA_CounterGetHitZero(&dma_tcp->recCounterGroup,1)) ;
	    for(x=0;x<DMA_NUM_COUNTERS_PER_GROUP;x+=1)
		    {
			    bgp_dma_tcp_counter_copies[x] = DMA_CounterGetValueNoMsync(dma_tcp->recCounterGroup.counter+x) ;
			    if( bgp_dma_tcp_counter_copies[x] != 0 || dma_tcp->recCntrInUse[x] != 0)
				    {
					    inUseCount += 1 ;
				    TRACEN(k_t_request,"rec_counter[0x%02x] value=0x%08x inUse=%d", x,bgp_dma_tcp_counter_copies[x],dma_tcp->recCntrInUse[x]) ;
				    if(dma_tcp->recCntrInUse[x])
					    {
						    dma_tcp_show_reception_one(dma_tcp,x,bgp_dma_tcp_counter_copies[x]) ;
/* 						struct sk_buff *skb=dma_tcp->rcv_skbs[x] ; */
/* 						if( skb) */
/* 							{ */
/* 								  struct ethhdr *eth = (struct ethhdr *)(skb->data) ; */
/* 								  unsigned int eth_proto = eth->h_proto ; */
/*  */
/* 								  struct iphdr *iph = (struct iphdr *) (eth+1) ; */
/* 								  unsigned int tot_len=iph->tot_len ; */
/* 								  unsigned int saddr=iph->saddr ; */
/* 								  if( tot_len != tot_len_for_rcv[x]) */
/* 									  { */
/* 										  TRACEN(k_t_error,"(!!!) tot_len trampled") ; */
/* 									  } */
/*  */
/* 								  TRACEN(k_t_request,"(---) skb=%p eth_proto=0x%04x tot_len=0x%04x saddr=%d.%d.%d.%d slot=0x%08x conn_id=0x%02x tot_len_for_rcv=0x%04x", */
/* 										  skb,eth_proto,tot_len,saddr>>24, (saddr >> 16) & 0xff,(saddr >> 8) & 0xff, saddr & 0xff, dma_tcp->slot_for_rcv[x], dma_tcp->conn_for_rcv[x], tot_len_for_rcv[x] */
/* 										                                                                                                                                           ) ; */
/* 								  dumpmem(skb->data,0x42,"eth-ip-tcp header") ; */
/* 								  show_dma_descriptor((DMA_InjDescriptor_t *)&skb->cb) ; */
/* #if defined(AUDIT_FRAME_HEADER) */
/* 					if(memcmp(skb->data,((char *)(all_headers_in_counters+x)),32)) */
/* 						{ */
/* 							  TRACEN(k_t_request,"(!!!) header not as first seen") ; */
/* 							  dumpmem(skb->data-14,sizeof(frame_header_t),"header-now") ; */
/* 							  dumpmem(all_headers_in_counters+x,sizeof(frame_header_t),"header-in-propose") ; */
/*  */
/* 						} */
/* #endif */
/* 							} */
/* 						else */
/* 							{ */
/* 								TRACEN(k_t_error|k_t_request,"(E) x=%d Counter in use but no skb !",x) ; */
/* 							} */
					    }
				    }
		    }
	    TRACEN(k_t_request,"inUseCount=%d",inUseCount) ;
	    show_tx_skbs(&dma_tcp->tx_mux,dma_tcp->node_count) ;
	    TRACEN(k_t_request,"skb_queue_len(pending_rcv_skbs)=%d",skb_queue_len(&dma_tcp->balancer.b[0].pending_rcv_skbs)) ;
	    {
		    struct sk_buff *skb = skb_peek(&dma_tcp->balancer.b[0].pending_rcv_skbs) ;
		    if(skb)
			    {

					  struct ethhdr *eth = (struct ethhdr *)(skb->data) ;
					  unsigned int eth_proto = eth->h_proto ;

					  struct iphdr *iph = (struct iphdr *) (eth+1) ;
					  unsigned int tot_len=iph->tot_len ;
					  unsigned int saddr=iph->saddr ;
					  TRACEN(k_t_request,"skb=%p eth_proto=0x%04x tot_len=0x%04x saddr=%d.%d.%d.%d",skb,eth_proto,tot_len,saddr>>24, (saddr >> 16) & 0xff,(saddr >> 8) & 0xff, saddr & 0xff ) ;
			    }

	    }
	    for( slot=0;slot<dma_tcp->node_count; slot+=1)
		    {
			    unsigned int proposals_active=get_proposals_active(&dma_tcp->rcvdemux,slot) ;
			    unsigned int count_pending_f=count_pending_flow(&dma_tcp->rcvdemux,slot) ;
			    unsigned int located_counters=0 ;
			    if( proposals_active || count_pending_f )
				    {
					    TRACEN(k_t_request,"slot=0x%08x proposals_active=%d count_pending_flow=%d",slot,proposals_active,count_pending_f) ;
				    }
			    for(x=0;x<DMA_NUM_COUNTERS_PER_GROUP;x+=1)
				    {
					    struct sk_buff *skb=dma_tcp->rcv_skbs[x] ;
					    if ( skb && slot == dma_tcp->slot_for_rcv[x] )
						    {
							    located_counters += 1 ;
						    }
				    }
			    if( located_counters + count_pending_f != proposals_active || ( 0 == located_counters && count_pending_f > 0 ))
				    {
					    TRACEN(k_t_request|k_t_error,"(E) slot=0x%08x located_counters=%d count_pending_f=%d proposals_active=%d",
							    slot,located_counters,count_pending_f,proposals_active) ;
				    }

		    }
}

int proc_do_dma_rec_counters(struct ctl_table *ctl, int write, struct file * filp,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	    int rc ;
	    dma_tcp_show_reception(&dma_tcp_state ) ;
	    TRACEN(k_t_entryexit,"(>)ctl=%p write=%d len=%d", ctl,write,*lenp) ;
	    rc = proc_dointvec(ctl,write,filp,buffer,lenp,ppos) ;
	    TRACEN(k_t_entryexit,"(<)") ;
	    return rc ;

}

/*  Routine to report how full the outgoing FIFOs are */
void bgp_dma_diag_report_transmission_queue(int __user * report)
  {
    dma_tcp_t *dma_tcp = &dma_tcp_state ;
    unsigned int core ;
    TRACEN(k_t_general,"report=%p",report) ;
    for( core=0 ; core<k_injecting_cores; core += 1)
	    {
		    unsigned int desired_fifo ;
			   for(desired_fifo=0; desired_fifo<k_injecting_directions; desired_fifo += 1 )
			   {
			       unsigned int fifo_initial_head = dma_tcp->idma.idma_core[core].idma_direction[desired_fifo].fifo_initial_head ;
			       unsigned int  fifo_current_head =
			        (unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+desired_fifo]) ;
			       unsigned int  fifo_current_tail =
			        (unsigned int) DMA_InjFifoGetTailById( &dma_tcp->injFifoGroupFrames, dma_tcp->injFifoFramesIds[core*k_injecting_directions+desired_fifo]) ;
			       unsigned int headx = (fifo_current_head-fifo_initial_head) >> 5 ;
			       unsigned int tailx = (fifo_current_tail-fifo_initial_head) >> 5 ;
			       unsigned int current_injection_used=packet_mod(tailx-headx) ;
			       put_user(current_injection_used, report) ;
			       report += 1 ;
			       TRACEN(k_t_detail,"core=%d desired_fifo=%d current_injection_used=%d",core,desired_fifo,current_injection_used) ;

			   }


	    }
    put_user(dma_tcp->qtyFreeRecCounters, report) ;
    report += 1 ;
    put_user(flow_count(dma_tcp,k_send_propose_rpc)-flow_count(dma_tcp,k_act_accept_rpc), report) ;
    report += 1 ;
    put_user(flow_count(dma_tcp,k_act_propose_rpc)-flow_count(dma_tcp,k_send_accept_rpc), report) ;
  }
static int issueDiagnose(
		DMA_RecFifo_t      *f_ptr,
		DMA_PacketHeader_t *packet_ptr,
		dma_tcp_t * dma_tcp,
    void  * request ,
    int payload_bytes,
    unsigned int src_key,
    int Put_Offset
    )
  {
	  unsigned int *payload=(unsigned int *)request ;
	  TRACEN(k_t_request,"src_key=0x%08x Put_Offset=0x%08x payload_bytes=0x%02x [%08x %08x %08x %08x]",
			  src_key,Put_Offset, payload_bytes,payload[0],payload[1],payload[2],payload[3]) ;
	  return 0 ;
  }

static int issueDiagnoseActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           )
  {
    unsigned int SW_Arg=packet_ptr->SW_Arg  ;
    int Put_Offset=packet_ptr->Put_Offset ;
    enable_kernel_fp() ; // TODO: don't think this is needed nowadays

    TRACEN(k_t_detail,"recv_func_parm=%p payload_ptr=%p SW_Arg=0x%08x payload_bytes=0x%08x Put_Offset=0x%08x",
		    recv_func_parm,payload_ptr,SW_Arg,payload_bytes,Put_Offset) ;
    return issueDiagnose(
		    f_ptr,
		    packet_ptr,
        (dma_tcp_t *) recv_func_parm,
        (void *) payload_ptr,
        payload_bytes,
        SW_Arg,
        Put_Offset
        ) ;
  }
static inline int inject_into_dma_diag_sync(dma_tcp_t *dma_tcp, void * address, unsigned int length, unsigned int x, unsigned int y, unsigned int z, unsigned int my_injection_group, unsigned int desired_fifo, unsigned int SW_Arg ,
		unsigned int proto_start )
  {
    dma_addr_t dataAddr ;
    DMA_InjDescriptor_t desc;
    int ret1, ret2 __attribute__((unused));
    unsigned int firstpacketlength =  length ;
    TRACEN(k_t_general , "(>) injecting address=%p length=0x%08x x=%d y=%d z=%d my_injection_group=%d desired_fifo=%d",address,length,x,y,z,my_injection_group,desired_fifo);
    dataAddr = dma_map_single(NULL, address, length, DMA_TO_DEVICE);

/*  First injection is 'start of frame/fragment' */
    ret1 = DMA_TorusMemFifoDescriptor( &desc,
                                     x, y, z,
                                     k_ReceptionFifoGroup,          /*  recv fifo grp id */
                                     0,          /*  hints */
                                     k_VC_anyway,          /*  vc - adaptive */
                                     SW_Arg,          /*  softw arg */
                                     proto_start,     /*  function id */
                                     k_InjectionCounterGroup,          /*  inj cntr group id */
                                     k_injCounterId,  /*  inj counter id */
                                     dataAddr,        /*  send address */
                                     firstpacketlength          /*  msg len */
                                     );

#if defined(SIDEBAND_TIMESTAMP)
    {
	    unsigned long now_lo=get_tbl() ;
	    DMA_DescriptorSetPutOffset(&desc,((-length) & 0x0000ffff ) | (now_lo & 0xffff0000)) ;

    }
#else
    DMA_DescriptorSetPutOffset(&desc,-length) ;  /*  For 'memory FIFO packets', the put offset has no hardware use. Set it to indicate the message (fragment) length */
#endif
    ret2 = wrapped_DMA_InjFifoInjectDescriptorById( &dma_tcp->injFifoGroupFrames,
                                            dma_tcp->injFifoFramesIds[my_injection_group*k_injecting_directions+desired_fifo],
                                            &desc );
    TRACEN(k_t_general , "(<)proto_start=%d firstpacketlength=%d ret1=%d ret2=%d",proto_start,firstpacketlength,ret1, ret2);
    return 1 ;
  }

static void bgp_dma_diag_drive_sync_at(dma_tcp_t *dma_tcp, int x,int y,int z, int sendBytes)
{
  unsigned int desired_fifo= select_transmission_fifo(dma_tcp,x,y,z) ;
  unsigned long flags ;
  unsigned int current_injection_used=0xffffffff ;
  unsigned int aligned_payload_address = (unsigned int)dma_tcp->diag_block_buffer ;
  unsigned int aligned_payload_length = sendBytes  ;
  unsigned int pad_head = 0 ;

  int ret = 0;
  int ring_ok ;

  int my_injection_group ;
  skb_group_t skb_group ;
  TRACEN(k_t_general ,"(>) at (%02x,%02x,%02x)", x,y,z);
  skb_group_init(&skb_group) ;

  my_injection_group=injection_group_hash(dma_tcp,x,y,z) ;
  spin_lock_irqsave(&dma_tcp->dirInjectionLock[my_injection_group*k_injecting_directions+desired_fifo],flags) ;
   {
     unsigned int src_key = (dma_tcp->src_key << 6) | (my_injection_group << 4) | pad_head ;
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
     TRACEN(k_t_general ,"Injection my_injection_group=%d desired_fifo=%d bhx=0x%08x headx=%08x tailx=%08x",
         my_injection_group, desired_fifo, bhx, headx,tailx
         ) ;
     if ( ring_ok )
       {
          /*  We are going to send something. Display its protocol headers .. */

          /*  Bump the injection counter. Actually only needs doing once per 4GB or so */
         ret=DMA_CounterSetValueWideOpenById ( & dma_tcp->injCounterGroup, k_injCounterId,  0xffffffff );

	    /*  and inject it */
		   {

			   injection_count = inject_into_dma_diag_sync(dma_tcp,(void *)aligned_payload_address,aligned_payload_length,x,y,z,my_injection_group,desired_fifo,
					   src_key,
					   dma_tcp->proto_issue_diag_sync
					   ) ;



		   }
         {
	   unsigned int nhx=packet_mod(bhx+injection_count) ;
		    /*  Record the skbuff so it can be freed later, after data is DMA'd out */
		   dma_tcp->idma.idma_core[my_injection_group].idma_direction[desired_fifo].idma_skb_array->skb_array[nhx] = NULL  ;
		    /*  Remember where we will be pushing the next injection in */
	           buffer->buffer_head_index = nhx ;
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

 TRACE("(<) desired_fifo=%d",desired_fifo);

}
static void init_shuffle_vector(unsigned int * shuffle_vector, unsigned int xe, unsigned int ye, unsigned int ze)
{
	unsigned int x;
	unsigned int y;
	unsigned int z;
	for( x=0; x<xe; x+=1)
		{
			for(y=0;y<ye;y+=1)
				{
					for( z=0;z<ze;z+=1)
						{
							*shuffle_vector = (x<<16)|(y<<8)|z ;
							shuffle_vector += 1 ;
						}
				}
		}
}

static inline int next_prbs(int seed)
{
	int ncmask = seed >> 31 ;  /*  0x00000000 or 0xffffffff */
	return (seed << 1) ^ (0x04C11DB7 & ncmask) ;   /*  CRC-32-IEEE 802.3 from http://en.wikipedia.org/wiki/Cyclic_redundancy_check */
}

static int scatter_prbs(int seed)
{
	int a ;
	for(a=0;a<32;a+=1)
		{
			seed=next_prbs(seed) ;
		}
	return seed ;
}
static int shuffle_shuffle_vector(unsigned int * shuffle_vector, unsigned int xe, unsigned int ye, unsigned int ze, int seed)
{
	unsigned int vsize = xe*ye*ze ;
	unsigned int vmask = vsize-1 ;
	unsigned int a ;

	for( a=0; a<vsize;a+=1)
		{
			unsigned int b = (seed & vmask) ;
			unsigned int va = shuffle_vector[a] ;
			unsigned int vb = shuffle_vector[b] ;
			shuffle_vector[a] = vb ;
			shuffle_vector[b] = va ;
			seed=next_prbs(seed) ;

		}
	return seed ;
}
#if 0
void dma_tcp_transfer_activate(int sendBytes)
{
	dma_tcp_t *dma_tcp = &dma_tcp_state ;
	int a ;
	int my_x=dma_tcp->location.coordinate[0] ;
	int my_y=dma_tcp->location.coordinate[1] ;
	int my_z=dma_tcp->location.coordinate[2] ;
	int ext_x=dma_tcp->extent.coordinate[0] ;
	int ext_y=dma_tcp->extent.coordinate[1] ;
	int ext_z=dma_tcp->extent.coordinate[2] ;
	int vsize=ext_x*ext_y*ext_z ;
	 /*  Push the 'diagnostic block' through the DMA unit */
	TRACEN(k_t_request,"diagnostic transfer request, sendBytes=0x%08x",sendBytes) ;
	dma_tcp->shuffle_seed = shuffle_shuffle_vector(dma_tcp->shuffle_vector,ext_x,ext_y,ext_z,dma_tcp->shuffle_seed) ;
	for(a=0;a<vsize;a+=1)
		{
			unsigned int tg=dma_tcp->shuffle_vector[a] ;
			unsigned int tg_x=tg>>16 ;
			unsigned int tg_y=(tg>>8) & 0xff ;
			unsigned int tg_z=tg & 0xff ;
			TRACEN(k_t_detail,"shuffle_vector[%d]=0x%08x",a,dma_tcp->shuffle_vector[a]) ;
			if( my_x != tg_x || my_y != tg_y || my_z != tg_z )
				{
					bgp_dma_diag_drive_block_at(dma_tcp,tg_x,tg_y,tg_z,sendBytes) ;
				}
		}
}

void dma_tcp_transfer_activate_to_one(int sendBytes, unsigned int tg)
{
	dma_tcp_t *dma_tcp = &dma_tcp_state ;
	int my_x=dma_tcp->location.coordinate[0] ;
	int my_y=dma_tcp->location.coordinate[1] ;
	int my_z=dma_tcp->location.coordinate[2] ;
	 /*  Push the 'diagnostic block' through the DMA unit */
	TRACEN(k_t_request,"diagnostic transfer request, sendBytes=0x%08x tg=0x%08x",sendBytes,tg) ;
		{
			unsigned int tg_x=tg>>16 ;
			unsigned int tg_y=(tg>>8) & 0xff ;
			unsigned int tg_z=tg & 0xff ;
			if( my_x != tg_x || my_y != tg_y || my_z != tg_z )
				{
					bgp_dma_diag_drive_block_at(dma_tcp,tg_x,tg_y,tg_z,sendBytes) ;
				}
		}
}
#endif
void dma_tcp_transfer_activate_sync(int sendBytes)
{
	dma_tcp_t *dma_tcp = &dma_tcp_state ;
	int a ;
	int my_x=dma_tcp->location.coordinate[0] ;
	int my_y=dma_tcp->location.coordinate[1] ;
	int my_z=dma_tcp->location.coordinate[2] ;
	int ext_x=dma_tcp->extent.coordinate[0] ;
	int ext_y=dma_tcp->extent.coordinate[1] ;
	int ext_z=dma_tcp->extent.coordinate[2] ;
	int vsize=ext_x*ext_y*ext_z ;
	 /*  Push the 'diagnostic block' through the DMA unit */
	TRACEN(k_t_general,"diagnostic transfer request, sendBytes=0x%08x",sendBytes) ;
	dma_tcp->shuffle_seed = shuffle_shuffle_vector(dma_tcp->shuffle_vector,ext_x,ext_y,ext_z,dma_tcp->shuffle_seed) ;
	for(a=0;a<vsize;a+=1)
		{
			unsigned int tg=dma_tcp->shuffle_vector[a] ;
			unsigned int tg_x=tg>>16 ;
			unsigned int tg_y=(tg>>8) & 0xff ;
			unsigned int tg_z=tg & 0xff ;
			TRACEN(k_t_detail,"shuffle_vector[%d]=0x%08x",a,dma_tcp->shuffle_vector[a]) ;
			if( my_x != tg_x || my_y != tg_y || my_z != tg_z )
				{
					bgp_dma_diag_drive_sync_at(dma_tcp,tg_x,tg_y,tg_z,sendBytes) ;
				}
		}
}

/*  'across faces' transfer in x,y,z directions, as a 'towards peak performance' test */
#if 0
void dma_tcp_transfer_activate_minicube(int sendBytes)
{
	dma_tcp_t *dma_tcp = &dma_tcp_state ;
	int my_x=dma_tcp->location.coordinate[0] ;
	int my_y=dma_tcp->location.coordinate[1] ;
	int my_z=dma_tcp->location.coordinate[2] ;
	 /*  Push the 'diagnostic block' through the DMA unit */
	TRACEN(k_t_request,"diagnostic transfer request, sendBytes=0x%08x",sendBytes) ;
	bgp_dma_diag_drive_block_at(dma_tcp,my_x^1,my_y,my_z,sendBytes) ;
	bgp_dma_diag_drive_block_at(dma_tcp,my_x,my_y^1,my_z,sendBytes) ;
	bgp_dma_diag_drive_block_at(dma_tcp,my_x,my_y,my_z^1,sendBytes) ;
}

int dma_tcp_transfer_wait(int demandCount)
{
	int spincount = 0 ;
	TRACEN(k_t_request,"(>) demandCount=%d",demandCount) ;
	while(DiagEndCount < demandCount && spincount < 100 )
		{
			int rc ;
			set_current_state(TASK_INTERRUPTIBLE);
			rc=schedule_timeout(1) ;
			if( 0 != rc) break ;
			spincount += 1 ;
		}
	TRACEN(k_t_request,"(<) DiagEndCount=%d spincount=%d",DiagEndCount,spincount) ;
	return DiagEndCount >= demandCount ;
}
#endif
#if defined(BARRIER_WITH_IOCTL)
volatile static int DiagSyncCount ;

static int issueInlineFrameDiagSync(
		DMA_RecFifo_t      *f_ptr,
		DMA_PacketHeader_t *packet_ptr,
		dma_tcp_t * dma_tcp,
    void  * request ,
    int payload_bytes,
    unsigned int src_key,
    int Put_Offset
    )
  {
	  timing_histogram(dma_tcp) ;
	  DiagSyncCount += 1 ;
	  return 0 ;
  }

static int issueInlineFrameDiagSyncActor(DMA_RecFifo_t      *f_ptr,
                           DMA_PacketHeader_t *packet_ptr,
                           void               *recv_func_parm,
                           char               *payload_ptr,
                           int                 payload_bytes
                           )
  {
    unsigned int SW_Arg=packet_ptr->SW_Arg  ;
    int Put_Offset=packet_ptr->Put_Offset ;

    enable_kernel_fp() ; // TODO: don't think this is needed nowadays
    TRACEN(k_t_detail,"recv_func_parm=%p payload_ptr=%p SW_Arg=0x%08x payload_bytes=0x%08x Put_Offset=0x%08x",
		    recv_func_parm,payload_ptr,SW_Arg,payload_bytes,Put_Offset) ;
    return issueInlineFrameDiagSync(
		    f_ptr,
		    packet_ptr,
        (dma_tcp_t *) recv_func_parm,
        (void *) payload_ptr,
        payload_bytes,
        SW_Arg,
        Put_Offset
        ) ;
  }

#endif

int dma_tcp_transfer_wait_sync(int demandCount)
{
	int spincount = 0 ;
	TRACEN(k_t_general,"(>) demandCount=%d",demandCount) ;
	while(DiagSyncCount < demandCount && spincount < 100 )
		{
			int rc ;
			set_current_state(TASK_INTERRUPTIBLE);
			rc=schedule_timeout(1) ;
			if( 0 != rc) break ;
			spincount += 1 ;
		}
	TRACEN(k_t_general,"(<) DiagSyncCount=%d spincount=%d",DiagSyncCount,spincount) ;
	return DiagSyncCount >= demandCount ;
}

void dma_tcp_transfer_clearcount(void)
{
	TRACEN(k_t_general,"count cleared") ;
/* 	DiagEndCount = 0 ; */
	DiagSyncCount = 0 ;
}

void __init
dma_tcp_diagnose_init(dma_tcp_t *dma_tcp)
  {
#if defined(BARRIER_WITH_IOCTL)
    dma_tcp->diag_block_buffer=allocate_diag_block_buffer() ;
    dma_tcp->shuffle_vector=allocate_shuffle_vector(dma_tcp->extent.coordinate[0],dma_tcp->extent.coordinate[1],dma_tcp->extent.coordinate[2]) ;
    dma_tcp->shuffle_seed = scatter_prbs(dma_tcp->SW_Arg + 1) ;
    init_shuffle_vector(dma_tcp->shuffle_vector,dma_tcp->extent.coordinate[0],dma_tcp->extent.coordinate[1],dma_tcp->extent.coordinate[2]) ;
    dma_tcp->proto_issue_diag_sync=DMA_RecFifoRegisterRecvFunction(issueInlineFrameDiagSyncActor, dma_tcp, 0, 0);
    memset(dma_tcp->timing_histogram_buckets,0,33*sizeof(int)) ;
#endif
    dma_tcp->proto_diagnose=DMA_RecFifoRegisterRecvFunction(issueDiagnoseActor, dma_tcp, 0, 0);

  }
