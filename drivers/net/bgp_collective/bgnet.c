/*********************************************************************
 *
 * Description: Blue Gene driver exposing col and torus as a NIC
 *
 * Copyright (c) 2007, 2010 International Business Machines
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
 * Authors:
 * Chris Ward <tjcw@uk.ibm.com>
 * Volkmar Uhlig <vuhlig@us.ibm.com>
 * Andrew Tauferner <ataufer@us.ibm.com>
 *
 ********************************************************************/

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
#include <linux/etherdevice.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/workqueue.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/bgp_personality.h>
#include <asm/delay.h>

#include <asm/bluegene.h>

#include "bglink.h"
#include "bgnet.h"
#include "bgcol.h"
/* #include "bgtor.h" */


/**********************************************************************
 *                           defines
 **********************************************************************/

#define DRV_NAME	"bgnet"
#define DRV_VERSION	"0.5"
#define DRV_DESC	"Blue Gene NIC (IBM)"

MODULE_DESCRIPTION(DRV_DESC);
MODULE_AUTHOR("IBM");

/* #define TRUST_TREE_CRC */

#include <linux/KernelFxLog.h>

#include "../bgp_network/bgp_net_traceflags.h"


#define XTRACEN(i,x...)
#if defined(REQUIRE_TRACE)
#define TRACE(x...) { printk(KERN_EMERG x) ; }
#define TRACE1(x...) { printk(KERN_EMERG x) ; }
#define TRACE2(x...) { printk(KERN_EMERG x) ; }
#define TRACEN(i,x...) { printk(KERN_EMERG x) ; }
#define TRACED(x...) { printk(KERN_EMERG x) ; }
#elif  defined(CONFIG_BLUEGENE_COLLECTIVE_TRACE)
#define TRACE(x...)    KernelFxLog(bgcol_debug_tracemask & k_t_general,x)
#define TRACE1(x...)   KernelFxLog(bgcol_debug_tracemask & k_t_lowvol,x)
#define TRACE2(x...)   KernelFxLog(bgcol_debug_tracemask & k_t_detail,x)
#define TRACEN(i,x...) KernelFxLog(bgcol_debug_tracemask & (i),x)
#define TRACED(x...)   KernelFxLog(1,x)
#define TRACES(x...)   KernelFxLog(1,x)
#else
#define TRACE(x...)
#define TRACE1(x...)
#define TRACE2(x...)
#define TRACEN(i,x...)
#define TRACED(x...)
#define TRACES(x...)
#endif

/*  An IPv4 address for slotting into a trace message */
#define NIPQ(X) ((X)>>24)&0xff,((X)>>16)&0xff,((X)>>8)&0xff,(X)&0xff

#define BGNET_FRAG_MTU		240
#define BGNET_MAX_MTU		(BGNET_FRAG_MTU * 254)
#define BGNET_DEFAULT_MTU	ETH_DATA_LEN


static BGP_Personality_t bgnet_personality;
/* static struct net_device *static_dev ; */

/* static struct bglink_proto bgnet_lnk; */

/* static DEFINE_SPINLOCK(bgnet_lock); */
static LIST_HEAD(bgnet_list);

struct skb_cb_lnk {
    struct bglink_hdr_col lnkhdr;
    union bgcol_header dest;
};

int bgtorus_start_xmit(struct sk_buff *skb, struct net_device *dev, unsigned int x, unsigned int y, unsigned int z) ;

/**********************************************************************
 *                         Linux module
 **********************************************************************/

MODULE_DESCRIPTION("BlueGene Ethernet driver");
MODULE_LICENSE("GPL");

int bgnic_driverparm = 0 ;

static void dumpmem(const void *address, unsigned int length, const char * label)
  {
    int x ;
    TRACEN(k_t_fifocontents,"Memory dump, length=%d: %s",length,label) ;
    if( length > 256 ) {
      length = 256 ;
    }
    for (x=0;x<length;x+=32)
      {
        int *v = (int *)(address+x) ;
        TRACEN(k_t_fifocontents,"%p: %08x %08x %08x %08x %08x %08x %08x %08x",
            v,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]
            ) ;
      }
  }


/**********************************************************************
 *                   Linux' packet and skb management
 **********************************************************************/


static int bgnet_open(struct net_device* dev)
{
     struct bgnet_dev* bgnet = (struct bgnet_dev*) netdev_priv(dev);
     bgcol_eth_up(bgnet->bgcol) ; /* Indicate that we want to operate as ethernet */

/*     bgcol_enable_rcv_wm_interrupt(&bgnet->col->chn[bgnet->col_channel]); */

    TRACEN(k_t_napi,"netif_start_queue(dev=%p)",dev) ;
    netif_start_queue(dev);

    return 0;
}

static int bgnet_stop(struct net_device* dev)
{
    struct bgnet_dev* bgnet = (struct bgnet_dev*) netdev_priv(dev);
    bgcol_eth_down(bgnet->bgcol) ; /* Indicate that we want to stop operating as ethernet */

    TRACEN(k_t_napi,"netif_stop_queue(dev=%p)",dev) ;
    netif_stop_queue(dev);
/*     bgcol_disable_rcv_wm_interrupt(&bgnet->col->chn[bgnet->col_channel]); */
/*     bgcol_disable_inj_wm_interrupt(&bgnet->col->chn[bgnet->col_channel]); */

    return 0;
}


static int bgnet_change_mtu(struct net_device *dev, int new_mtu)
{
  struct bgnet_dev *bgnet = netdev_priv(dev);
    if (new_mtu < 60 || new_mtu > BGNET_MAX_MTU )
	return -EINVAL;
    dev->mtu = new_mtu;
    bgcol_set_mtu(bgnet->bgcol, new_mtu+sizeof(struct ethhdr)) ;
    return 0;
}


static inline void stamp_checksum_place_in_skb(struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
        unsigned int eth_proto = eth->h_proto ;
        struct iphdr *iph = (struct iphdr *)((skb->data)+sizeof(struct ethhdr)) ;
        /* unsigned int iphlen = 4*iph->ihl ; */
        /* struct tcphdr *tcph = (struct tcphdr *) ( ((char *)(iph)) + (iphlen) ); */
        /* struct udphdr *udph = (struct udphdr *) ( ((char *)(iph)) + (iphlen) ); */
        unsigned int ip_proto = iph->protocol ;
        skb->csum_start = skb_transport_header(skb) - skb->head;

        if( eth_proto == ETH_P_IP) {
        	if( ip_proto == IPPROTO_TCP) skb->csum_offset = offsetof(struct tcphdr, check);
        	else if( ip_proto == IPPROTO_UDP) skb->csum_offset = offsetof(struct udphdr, check);
        }

}

/*
 * The hardware data rate on 'collective' is 6 bits/cycle, i.e. 5100Mb/s .
 * We carry 240 bytes of payload in each 256 byte packet, and there are some bytes of 'overhead' as well
 * (CRC, opcode, and a few others); giving a 'peak performance' TCP/IP data rate of a little under 4781 Mb/s .
 * The 'collective' hardware should be able to do this in both directions simultaneously.
 *
 * Driving data into the compute fabric from the 10gE link can achieve more or less this, by using one core as
 * interrupt handler for the 10gE and another core as interrupt handler for the collective, if you run (say)
 * 16 TCP/IP sessions through the 10gE and the IO node, one to each compute node in the PSET.
 *
 * Driving data out of the compute fabric and into the 10gE in the normal way for linux device drivers causes
 * the core handling the collective interrupt to go 100% busy; there are not enough cycles to drain the collective
 * FIFO and also go through the linux networking stack. I have seen about 4Gb/s this way.
 * To get the last 15% or so, it seems necessary to have more than one core helping with this work.
 *
 * I'm trying to do this by having one core handle the 'collective' interrupt and drain the FIFO, and then
 * hand the sk_buff off to another core via a 'work queue', so that this second core can drive the linux
 * network stack.
 *
 * I haven't measured the simultaneous-bidirectional data rate capability.
 *
 */
static int bgnet_receive(struct bg_col *bgcol, struct sk_buff *skb, struct bglink_hdr_col *lnkhdr, struct bglink_proto* proto)
{
  TRACE("(>) skb=%p lnkhdr=%p proto=%p", skb,lnkhdr,proto) ;
  if( skb != NULL && lnkhdr != NULL && proto != NULL && -1 != (int) proto )
  {
    struct net_device *dev = (struct net_device*)proto->private;
    struct bgnet_dev *bgnet = netdev_priv(dev);
/*     struct net_device *dev = (struct net_device*)((void *)bgnet - */
/*                                                   netdev_priv(NULL)); */

    TRACE("bgnet rcvd pkt: data=%p, len=%d, head=%d, tail=%d, res len=%d [%s:%d]",
          skb->data, skb->len, lnkhdr->opt.opt_net.pad_head,
           lnkhdr->opt.opt_net.pad_tail, skb->len - lnkhdr->opt.opt_net.pad_head - lnkhdr->opt.opt_net.pad_tail, __func__, __LINE__);

/*     if (skb->len % BGNET_FRAG_MTU != 0) */
/*       printk("bgnet: received packet size not multiple of %d\n", BGNET_FRAG_MTU); */

    /* skb_pull and trim check for over/underruns. For 0 size the
     * add/subtract is the same as a test */
    __skb_pull(skb, lnkhdr->opt.opt_net.pad_head);
    __skb_trim(skb, skb->len - lnkhdr->opt.opt_net.pad_tail);

    if (lnkhdr->src_key == bgnet->bgcol_vector) {
        /* drop ether packets that are from ourselves */
        /* bg tree device sends packets to itself when broadcasting */
        kfree_skb(skb);
        return 0;
    }

     /* dump_skb(skb); */

    dumpmem(skb->data,skb->len,"Frame delivered via collective") ;

    skb->dev = dev;
    skb->protocol = eth_type_trans(skb, dev);

    if ( k_trust_collective_crc) skb->ip_summed = CHECKSUM_PARTIAL ;
    stamp_checksum_place_in_skb(skb) ;

/* #if defined(TRUST_TREE_CRC) */
/*     skb->ip_summed = CHECKSUM_PARTIAL ; // Frame was checked by CRC, but we would need a checksum if it is being forwarded off the BGP fabric */
/* //    // Packets from tree-local addresses have been verified by tree hardware */
/* //      { */
/* //        struct ethhdr *eth = (struct ethhdr *)skb->data; */
/* //        if (bgnet->eth_mask == 0 || */
/* //            ((bgnet->eth_mask & *(unsigned int *)(&eth->h_source[0])) == */
/* //             (bgnet->eth_local))) */
/* //          { */
/* //               skb->ip_summed = CHECKSUM_UNNECESSARY ; */
/* //        } */
/* //        else */
/* //          { */
/* //            skb->ip_summed = CHECKSUM_NONE ; */
/* //          } */
/* //      } */
/* #endif */

    TRACE("Delivering skb->dev=%p skb->protocol=%d skb->pkt_type=%d skb->ip_summed=%d ",
        skb->dev, skb->protocol, skb->pkt_type, skb->ip_summed ) ;
    dumpmem(skb->data,skb->len,"Frame after stripping header") ;
    dev->last_rx = jiffies;
    bgnet->stats.rx_packets++;
    bgnet->stats.rx_bytes += skb->len;

    TRACE("bgnet_receive before-netif-rx bgnet->stats.rx_packets=%lu bgnet->stats.tx_packets=%lu bgnet->stats.rx_bytes=%lu bgnet->stats.tx_bytes=%lu bgnet->stats.rx_frame_errors=%lu",
        bgnet->stats.rx_packets, bgnet->stats.tx_packets, bgnet->stats.rx_bytes, bgnet->stats.tx_bytes, bgnet->stats.rx_frame_errors) ;
/*     TRACEN(k_t_napi,"netif_rx(skb=%p)",skb) ; // Only tracing the torus ... */
/*     if( k_deliver_via_workqueue &&  bgnet->bgcol->deliver_via_workqueue ) */
/* 	    { */
/* 		  bgnet_deliver_via_workqueue(skb) ; */
/* 	    } */
/*     else */
/* 	    { */
#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
    if( bgcol_diagnostic_use_napi)
	    {
			    {
				    TRACEN(k_t_napi|k_t_request,"netif_receive_skb(%p)",skb) ;
				    netif_receive_skb(skb) ;
			    }
	    }
    else
	    {
		    netif_rx(skb);
	    }
#else
    netif_rx(skb);
#endif
/* 	    } */
    TRACE("bgnet_receive after-netif-rx  bgnet->stats.rx_packets=%lu bgnet->stats.rx_bytes=%lu bgnet->stats.rx_frame_errors=%lu",
        bgnet->stats.rx_packets, bgnet->stats.rx_bytes, bgnet->stats.rx_frame_errors) ;

  }
  TRACE("(<)") ;

    return 0;
}

static int bgnet_receive_trimmed(struct bg_col *bgcol, struct sk_buff *skb,  struct bglink_proto* proto, unsigned int src_key )
{
  TRACE("(>) skb=%p proto=%p", skb,proto) ;
  if( skb != NULL && proto != NULL && -1)
  {
    struct net_device *dev = (struct net_device*)proto->private;
    struct bgnet_dev *bgnet = netdev_priv(dev);
/*     struct net_device *dev = (struct net_device*)((void *)bgnet - */
/*                                                   netdev_priv(NULL)); */

    TRACE("bgnet rcvd pkt: data=%p, len=%d",
          skb->data, skb->len);
    if( src_key != bgnet->bgcol_vector)
	    {
		    dumpmem(skb->data,skb->len,"Frame delivered via collective") ;

		    skb->dev = dev;
		    skb->protocol = eth_type_trans(skb, dev);

		    if ( k_trust_collective_crc) skb->ip_summed = CHECKSUM_PARTIAL ;
		    stamp_checksum_place_in_skb(skb) ;


		    TRACE("Delivering skb->dev=%p skb->protocol=%d skb->pkt_type=%d skb->ip_summed=%d ",
			skb->dev, skb->protocol, skb->pkt_type, skb->ip_summed ) ;
		    dumpmem(skb->data,skb->len,"Frame after stripping header") ;
		    dev->last_rx = jiffies;
		    bgnet->stats.rx_packets++;
		    bgnet->stats.rx_bytes += skb->len;

		    TRACE("bgnet_receive before-netif-rx bgnet->stats.rx_packets=%lu bgnet->stats.tx_packets=%lu bgnet->stats.rx_bytes=%lu bgnet->stats.tx_bytes=%lu bgnet->stats.rx_frame_errors=%lu",
			bgnet->stats.rx_packets, bgnet->stats.tx_packets, bgnet->stats.rx_bytes, bgnet->stats.tx_bytes, bgnet->stats.rx_frame_errors) ;
		/*     TRACEN(k_t_napi,"netif_rx(skb=%p)",skb) ; // Only tracing the torus ... */
		#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
		    if( bgcol_diagnostic_use_napi)
			    {
					    {
						    TRACEN(k_t_napi|k_t_request,"netif_receive_skb(%p)",skb) ;
						    netif_receive_skb(skb) ;
					    }
			    }
		    else
			    {
				    netif_rx(skb);
			    }
		#else
		    netif_rx_ni(skb); // In a workqueue handler ...
		#endif
		    TRACE("bgnet_receive after-netif-rx  bgnet->stats.rx_packets=%lu bgnet->stats.rx_bytes=%lu bgnet->stats.rx_frame_errors=%lu",
			bgnet->stats.rx_packets, bgnet->stats.rx_bytes, bgnet->stats.rx_frame_errors) ;
	  }
    else
  	  {
  		   /*   a discardable self-send */
  		  dev_kfree_skb(skb) ;
  	  }

  }
  TRACE("(<)") ;

    return 0;
}


/*  A packet gets to the IO node, and needs 'reflecting' to the compute node(s) that want it. */
static int col_reflect(struct bg_col *bgcol, struct sk_buff *skb, struct bglink_hdr_col *lnkhdr,
       struct bglink_proto* proto)
{
  TRACE("(>) col_reflect skb=%p lnkhdr=%p proto=%p", skb,lnkhdr,proto) ;
  if( skb != NULL && lnkhdr != NULL && proto != NULL && -1 != (int) proto )
  {
    struct net_device *dev = (struct net_device*)proto->private;
    struct bgnet_dev *bgnet = netdev_priv(dev);


    TRACE("bgnet rcvd pkt for reflection: data=%p, len=%d, head=%d, tail=%d, res len=%d [%s:%d]",
    skb->data, skb->len, lnkhdr->opt.opt_net.pad_head,
     lnkhdr->opt.opt_net.pad_tail, skb->len - lnkhdr->opt.opt_net.pad_head - lnkhdr->opt.opt_net.pad_tail, __func__, __LINE__);

/*     if (skb->len % BGNET_FRAG_MTU != 0) */
/*   printk("bgnet: received packet size not multiple of %d\n", BGNET_FRAG_MTU); */

    /* skb_pull and trim check for over/underruns. For 0 size the
     * add/subtract is the same as a test */
    __skb_pull(skb, lnkhdr->opt.opt_net.pad_head);
    __skb_trim(skb, skb->len - lnkhdr->opt.opt_net.pad_tail);
     /*  A 'broadcast' packet needs delivering locally as well as reflecting */
      {
        struct ethhdr *eth = (struct ethhdr *)skb->data;
        if (is_broadcast_ether_addr(eth->h_dest)) {
          struct sk_buff *localskb = skb_clone(skb, GFP_KERNEL);
          if( localskb )
            {
              dumpmem(localskb->data,localskb->len,"Frame delivered via tree (broadcast reflection)") ;
              localskb->dev = dev;
              localskb->protocol = eth_type_trans(localskb, dev);

              localskb->ip_summed = CHECKSUM_UNNECESSARY ;  /*  Packet was from tree, h/w verified it */

              TRACE("Delivering localskb->dev=%p localskb->protocol=%d localskb->pkt_type=%d localskb->ip_summed=%d ",
                  localskb->dev, localskb->protocol, localskb->pkt_type, localskb->ip_summed ) ;
              dumpmem(localskb->data,localskb->len,"Frame after stripping header") ;
              dev->last_rx = jiffies;
              bgnet->stats.rx_packets++;
              bgnet->stats.rx_bytes += localskb->len;
              TRACE("col_reflect before-netif-rx bgnet->stats.rx_packets=%lu bgnet->stats.rx_bytes=%lu bgnet->stats.rx_frame_errors=%lu",
                  bgnet->stats.rx_packets, bgnet->stats.rx_bytes, bgnet->stats.rx_frame_errors) ;
/*               TRACEN(k_t_napi,"netif_rx(skb=%p)",localskb) ; // Only tracing the torus ... */
#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
		    if( bgcol_diagnostic_use_napi)
			    {
				    TRACEN(k_t_napi,"netif_receive_skb(%p)",localskb) ;
				    netif_receive_skb(localskb) ;
			    }
		    else
			    {
				    netif_rx(localskb);
			    }
#else
              netif_rx(localskb) ;
#endif
             TRACE("col_reflect after-netif-rx  bgnet->stats.rx_packets=%lu bgnet->stats.rx_bytes=%lu bgnet->stats.rx_frame_errors=%lu",
                  bgnet->stats.rx_packets, bgnet->stats.rx_bytes, bgnet->stats.rx_frame_errors) ;
          }
        }
      }


     /* dump_skb(skb); */
    col_start_xmit(skb, dev) ;
  }

  TRACE("(<) col_reflect") ;

    return 0;
}

/*  A packet gets to the IO node, and needs 'reflecting' to the compute node(s) that want it. */
static int col_reflect_trimmed(struct bg_col *bgcol, struct sk_buff *skb,
       struct bglink_proto* proto, unsigned int src_key )
{
  TRACE("(>) col_reflect skb=%p proto=%p", skb,proto) ;
  if( skb != NULL && proto != NULL && -1 != (int) proto )
  {
    struct net_device *dev = (struct net_device*)proto->private;
    struct bgnet_dev *bgnet = netdev_priv(dev);


    TRACE("bgnet rcvd pkt for reflection: data=%p, len=%d",
    skb->data, skb->len);


     /*  A 'broadcast' packet needs delivering locally as well as reflecting */
      {
        struct ethhdr *eth = (struct ethhdr *)skb->data;
        if (is_broadcast_ether_addr(eth->h_dest)) {
          struct sk_buff *localskb = skb_clone(skb, GFP_KERNEL);
          if( localskb )
            {
              dumpmem(localskb->data,localskb->len,"Frame delivered via tree (broadcast reflection)") ;
              localskb->dev = dev;
              localskb->protocol = eth_type_trans(localskb, dev);

              localskb->ip_summed = CHECKSUM_UNNECESSARY ;  /*  Packet was from tree, h/w verified it */

              TRACE("Delivering localskb->dev=%p localskb->protocol=%d localskb->pkt_type=%d localskb->ip_summed=%d ",
                  localskb->dev, localskb->protocol, localskb->pkt_type, localskb->ip_summed ) ;
              dumpmem(localskb->data,localskb->len,"Frame after stripping header") ;
              dev->last_rx = jiffies;
              bgnet->stats.rx_packets++;
              bgnet->stats.rx_bytes += localskb->len;
              TRACE("col_reflect before-netif-rx bgnet->stats.rx_packets=%lu bgnet->stats.rx_bytes=%lu bgnet->stats.rx_frame_errors=%lu",
                  bgnet->stats.rx_packets, bgnet->stats.rx_bytes, bgnet->stats.rx_frame_errors) ;
/*               TRACEN(k_t_napi,"netif_rx(skb=%p)",localskb) ; // Only tracing the torus ... */
#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
		    if( bgcol_diagnostic_use_napi)
			    {
				    TRACEN(k_t_napi,"netif_receive_skb(%p)",localskb) ;
				    netif_receive_skb(localskb) ;
			    }
		    else
			    {
				    netif_rx(localskb);
			    }
#else
              netif_rx(localskb) ;
#endif
             TRACE("col_reflect after-netif-rx  bgnet->stats.rx_packets=%lu bgnet->stats.rx_bytes=%lu bgnet->stats.rx_frame_errors=%lu",
                  bgnet->stats.rx_packets, bgnet->stats.rx_bytes, bgnet->stats.rx_frame_errors) ;
          }
        }
      }


     /* dump_skb(skb); */
    col_start_xmit(skb, dev) ;
  }

  TRACE("(<) col_reflect") ;

    return 0;
}


#ifdef CONFIG_NET_POLL_CONTROLLER
static void bgnet_poll(struct net_device *dev)
{
    /* no-op; packets are fed by the col device */
}
#endif

static inline int is_torus_ether_addr(const u8 *addr)
{
    return ((addr[0] & 0x7) == 0x6);
}


unsigned int find_xyz_address(unsigned int ip) ;


static int bgnet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
  col_start_xmit(skb, dev) ;
  return 0 ;
}

static void bgnet_uninit(struct net_device *dev)
{
    struct bgnet_dev *bgnet = netdev_priv(dev);

	bglink_unregister_proto(&bgnet->lnk);
	bglink_unregister_proto(&bgnet->lnkreflect);

}

static struct net_device_stats *bgnet_get_stats(struct net_device *dev)
{
    struct bgnet_dev* bgnet = netdev_priv(dev);

    return &bgnet->stats;
}


static int bgnet_set_mac_addr(struct net_device* netDev,
			      void* p)
{
       struct sockaddr* addr = p;

        if (!is_valid_ether_addr(addr->sa_data))
                return -EADDRNOTAVAIL;

        memcpy(netDev->dev_addr, addr->sa_data, netDev->addr_len);

	return 0;
}


static int bgnet_set_config(struct net_device* netDev,
			    struct ifmap* map)
{
	int rc = 0;
	struct bgnet_dev* bgnet = netdev_priv(netDev);

	 /*  Set this with ifconfig <interface> port <collective virtual channel> */
	if (map->port)
		bgnet->bgcol_channel = map->port;

	 /*  Set this with ifconfig <interface> io_addr <collective route> */
	if (map->base_addr)
		bgnet->bgcol_route = map->base_addr;

	return rc;
}


static int  bgnet_init(struct net_device *dev)
{
    struct bgnet_dev *bgnet = netdev_priv(dev);
    TRACE("(>) bgnet_init") ;
    bgnet->bgcol_route = 0 /*15*/;
#define ETH_COL_CHANNEL 0
    bgnet->bgcol_channel = 0 ;
/*     bgnet->bgcol_channel = (bgnet_personality.Block_Config & BGP_PERS_BLKCFG_IPOverCollectiveVC) ? 1 : 0; */
/*     bgnet->eth_bridge_vector = -1; */
/*     bgnet->link_protocol = BGLINK_P_NET; */
/*     bgnet->net_device = dev; */

    bgnet->bgcol = bgcol_get_dev();
    TRACE("(=) bgnet->bgcol=%p",bgnet->bgcol) ;

    if (!bgnet->bgcol)
	return -1;

    bgnet->bgcol->bgnet_channel = bgnet->bgcol_channel ;
/*     bgnet->phandle_tree = 3; */
/*     bgnet->phandle_torus = 0; */
/* //    bgnet->tree_route = 15;  // 15 is 'partition flood' */
/*     bgnet->tree_route = 0 ;    // 0 is 'compute to IO' or 'IO to compute' */
/*     bgnet->tree_channel = BGNET_TREE_CHANNEL ; */
/*     bgnet->eth_mask = 0; */
/* //    bgnet->eth_bridge_vector = 0; // route through the I/O node? (personality.Network_Config.IONodeRank) */
/*     bgnet->eth_bridge_vector = personality.Network_Config.IOnodeRank; // route through the I/O node? (personality.Network_Config.IONodeRank) */
    bgnet->eth_bridge_vector = bgnet_personality.Network_Config.IOnodeRank;  /*  route through the I/O node? (personality.Network_Config.IONodeRank) */
    bgnet->bgcol_protocol = 1;
    bgnet->bgcol_reflector_protocol = 2 ;  /*  CN requests reflection from ION */

    if( bgnet_personality.Network_Config.Rank != bgnet_personality.Network_Config.IOnodeRank)
      {
        // On compute nodes, run a global interrupt barrier here with a view to aligning the printk timestamps
        bgcol_align_timebase() ;
      }

/*     bgnet->i_am_ionode = ( personality.Network_Config.IOnodeRank == personality.Network_Config.Rank) ; */
#if 0
    p = get_property(np, "local-mac-address", NULL);
    if (p == NULL) {
        printk(KERN_ERR "%s: Can't find local-mac-address property\n",
               np->full_name);
        goto err;
    }
    memcpy(dev->dev_addr, p, 6);
#endif
    dev->dev_addr[0] = 0x00;
    dev->dev_addr[1] = 0x80;
    *((unsigned*)(&dev->dev_addr[2])) = 0x46000000u | bgnet_personality.Network_Config.Rank;  /*  why 0x46yyyyyy ??? */

    bgnet->bgcol_vector = *(unsigned int *)(&dev->dev_addr[2]);
    bgnet->eth_local = bgnet->eth_mask & *(unsigned int *)&dev->dev_addr[0];

/*     spin_lock(&bgnet_lock); */
    if (list_empty(&bgnet_list)) {
	 /*  register with col */
/* 	bgnet_lnk.lnk_proto = bgnet->link_protocol; */
/* 	bgnet_lnk.receive_from_self = 0; */
/* 	bgnet_lnk.col_rcv = col_receive; */
/* 	bgnet_lnk.col_flush = col_flush; */
/* 	bgnet_lnk.torus_rcv = torus_receive; */
/* 	bglink_register_proto(&bgnet_lnk); */
	    bgnet->lnk.lnk_proto = bgnet->bgcol_protocol;
	    bgnet->lnk.col_rcv = bgnet_receive;
	    bgnet->lnk.col_rcv_trimmed = bgnet_receive_trimmed;
	    bgnet->lnk.private = dev;
	    bglink_register_proto(&bgnet->lnk);

	    bgnet->lnkreflect.lnk_proto = bgnet->bgcol_reflector_protocol;
	    bgnet->lnkreflect.col_rcv = col_reflect;
	    bgnet->lnkreflect.col_rcv_trimmed = col_reflect_trimmed;
	    bgnet->lnkreflect.private = dev;
	    bglink_register_proto(&bgnet->lnkreflect);

	 /*  Hook for the tree interrupt handler to find the 'bgnet' */
	    bgnet->bgcol->bgnet = bgnet ;
    }
/*     list_add_rcu(&bgnet->list, &bgnet_list); */
/*  */
/*     spin_unlock(&bgnet_lock); */
/*  */
/*     skb_queue_head_init(&bgnet->pending_skb_list); */
    bgcol_enable_interrupts(bgnet->bgcol) ;   /*  Should be able to run tree interrupts now */


    TRACE("(<) bgnet_init") ;
    return 0;
}

#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
static int bgnet_poll_napi(struct napi_struct * napi, int budget)
{
	TRACEN(k_t_napi,"(>) napi=%p budget%d",napi,budget) ;
	bgcol_duplex_slih(0) ;
	TRACEN(k_t_napi,"(<)") ;
	return 0 ;
}
#endif

#if defined(HAVE_NET_DEVICE_OPS)
static const struct net_device_ops netdev_ops = {
    .ndo_change_mtu = bgnet_change_mtu ,
    .ndo_get_stats = bgnet_get_stats ,
    .ndo_start_xmit = bgnet_start_xmit ,
    .ndo_init = bgnet_init ,
    .ndo_uninit = bgnet_uninit ,
    .ndo_open = bgnet_open ,
    .ndo_stop = bgnet_stop ,
    .ndo_set_config = bgnet_set_config ,
    .ndo_set_mac_address = bgnet_set_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
    .ndo_poll_controller  = bgnet_poll,
#endif
};
#endif
static int __init
bgnet_module_init(void)
{
    struct bgnet_dev *bgnet;
    struct net_device *dev;

    TRACEN(k_t_general, "(>) bgnet_module_init") ;
    dev = alloc_etherdev(sizeof(struct bgnet_dev));
    TRACEN(k_t_general, "(=) bgnet_module_init dev=%p", dev) ;
    if (!dev)
	return -ENOMEM;

/*     SET_MODULE_OWNER(dev); // Anachronism */

     /*  Read personality. */
    bluegene_getPersonality((void*) &bgnet_personality, sizeof(bgnet_personality));
    bgnet = (struct bgnet_dev*) netdev_priv(dev);
    memset(bgnet, 0, sizeof(*bgnet));
    bgcol_module_init() ;
/*     bgnet_init(dev); */

/*     // Set the MAC address for this interface. */
/*     if (bluegene_isIONode()) { */
/* 	unsigned char ipOctet2 = (bgnet_personality.Ethernet_Config.IPAddress.octet[13] + 1) & 0xfc; */
/*  */
/* 	dev->dev_addr[0] = ipOctet2 | 2; */
/* 	dev->dev_addr[1] = bgnet_personality.Ethernet_Config.IPAddress.octet[14]; */
/* 	dev->dev_addr[2] = bgnet_personality.Ethernet_Config.IPAddress.octet[15]; */
/* 	dev->dev_addr[3] = ((bgnet_personality.Network_Config.Rank >> 16) & 0x3f) | (ipOctet2 << 6); */
/* 	dev->dev_addr[4] = (unsigned char) ((bgnet_personality.Network_Config.Rank >> 8)); */
/* 	dev->dev_addr[5] = (unsigned char) bgnet_personality.Network_Config.Rank; */
/*     } else */
/* 	memcpy(dev->dev_addr, bgnet_personality.Ethernet_Config.EmacID, sizeof(dev->dev_addr)); */

#if defined(HAVE_NET_DEVICE_OPS)
    dev->netdev_ops = &netdev_ops ;
#else
    dev->init			= bgnet_init;
    dev->uninit			= bgnet_uninit;
    dev->get_stats	        = bgnet_get_stats;
    dev->hard_start_xmit	= bgnet_start_xmit;
    dev->change_mtu		= bgnet_change_mtu;
    dev->open			= bgnet_open;
    dev->stop			= bgnet_stop;
    dev->set_config		= bgnet_set_config;
    dev->set_mac_address	= bgnet_set_mac_addr;
#ifdef CONFIG_NET_POLL_CONTROLLER
    dev->poll_controller	= bgnet_poll;
#endif
#endif
    dev->mtu      = BGNET_DEFAULT_MTU;

/*  Tried turning checksum generation off, but this resulted in packets routed off the BGP not having checksums */
/*  and lack of interoperability with front-end nodes */
/*  (try CHECKSUM_PARTIAL above to see if the TOMAL will generate an IP checksum in this circumstance) */
    dev->features  = k_trust_collective_crc
                   ? (NETIF_F_HIGHDMA | NETIF_F_NO_CSUM)
                   :  NETIF_F_HIGHDMA ;
/*     if( k_trust_collective_crc) */
/* 	    { */
/* 		    dev->features  = NETIF_F_HIGHDMA | NETIF_F_IP_CSUM | NETIF_F_NO_CSUM | NETIF_F_HW_CSUM | NETIF_F_IPV6_CSUM ; */
/* 	    } */
/*     else */
/* 	    { */
/* 		    dev->features = NETIF_F_HIGHDMA ; */
/* 	    } */

/* #if defined(TRUST_TREE_CRC) */
/*     dev->features               = NETIF_F_IP_CSUM | NETIF_F_NO_CSUM | NETIF_F_HW_CSUM | NETIF_F_IPV6_CSUM | NETIF_F_HIGHDMA ; */
/* #endif */
/*     dev->features |= NETIF_F_NO_CSUM; */

    TRACEN(k_t_general,"(=) dev->name=%s",
        dev->name
        ) ;
    {
      int rc = register_netdev(dev) ;
      TRACEN(k_t_general, "(=) bgnet_module_init register_netdev rc=%d", rc) ;
      if( rc != 0 )
	goto err;
    }

#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
    netif_napi_add(dev,&bgnet->napi, bgnet_poll_napi, k_collective_budget) ;
    napi_enable(&bgnet->napi) ;
#endif
     /* increase header size to fit torus hardware header */
/*     if (bgnet->torus) */
/* 	dev->hard_header_len	+= 16; */

    if (bgnet->eth_bridge_vector != -1)
        printk(KERN_INFO "      bridge 0x%06x\n", bgnet->eth_bridge_vector);

    TRACEN(k_t_general, "(<) bgnet_module_init rc=0") ;
    return 0;

 err:
    free_netdev(dev);
    TRACEN(k_t_general, "(<) bgnet_module_init err rc=-1") ;
    return -1;
}


/* static void __exit */
/* bgnet_module_exit (void) */
/* { */
/* 	return; */
/* } */

module_init(bgnet_module_init);
/* module_exit(bgnet_module_exit); */
