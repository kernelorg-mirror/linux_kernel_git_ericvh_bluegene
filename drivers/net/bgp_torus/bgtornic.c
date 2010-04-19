/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2007,2010
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
 * Authors: Chris Ward <tjcw@uk.ibm.com>
 *	    Volkmar Uhlig <vuhlig@us.ibm.com>
 *
 * Description: Blue Gene driver exposing tree and torus as a NIC
 *
 *
 ********************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
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
#include <linux/ip.h>

#include <net/arp.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/bgp_personality.h>
#include <asm/bluegene.h>
#include <linux/KernelFxLog.h>


#include "bgtornic.h"

int col_start_xmit(struct sk_buff *skb, struct net_device *dev);

/* #define TRUST_TORUS_CRC */

#if !defined(CONFIG_BLUEGENE_TCP_WITHOUT_NAPI)
/*  Select operation with linux 'dev->poll' */
#define TORNIC_DEV_POLL
#endif

/* #define TORNIC_TASKLET_BGNET */

/* #define TORNIC_TRANSMIT_TREE_TASKLET */

#include "../bgp_network/bgp_net_traceflags.h"

#define ENABLE_TRACE

/* #define REQUIRE_TRACE */

#if defined(ENABLE_TRACE)
extern int bgp_dma_tcp_tracemask ;
/* extern int bgtorus_debug_tracemask ; */
#define bgtornic_debug_tracemask bgp_dma_tcp_tracemask
/* static int bgtornic_debug_tracemask=k_t_general|k_t_lowvol|k_t_irqflow|k_t_irqflow_rcv|k_t_protocol ; */
#endif

#if defined(REQUIRE_TRACE)
#define TRACE(x...)    KernelFxLog(1,x)
#define TRACE1(x...)   KernelFxLog(1,x)
#define TRACE2(x...)   KernelFxLog(1,x)
#define TRACEN(i,x...) KernelFxLog(1,x)
#define TRACED(x...)   KernelFxLog(1,x)
#define TRACES(x...)   KernelFxLog(1,x)
#elif defined(ENABLE_TRACE)
#define TRACE(x...)    KernelFxLog(bgtornic_debug_tracemask & k_t_general,x)
#define TRACE1(x...)   KernelFxLog(bgtornic_debug_tracemask & k_t_lowvol,x)
#define TRACE2(x...)   KernelFxLog(bgtornic_debug_tracemask & k_t_detail,x)
#define TRACEN(i,x...) KernelFxLog(bgtornic_debug_tracemask & (i),x)
#define TRACED(x...)   KernelFxLog(1,x)
#define TRACES(x...)   KernelFxLog(1,x)
#else
#define TRACE(x...)
#define TRACE1(x...)
#define TRACE2(x...)
#define TRACEN(i,x...)
#define TRACED(x...)
#endif

/* #define TORNIC_FORCE_BROADCAST 1 */
/**********************************************************************
 *                           defines
 **********************************************************************/

static const char version[] = "Bgtornet: Version 1.0, (c) 2008,2010 IBM Corporation, GPL";

/**********************************************************************
 *                         Linux module
 **********************************************************************/

MODULE_DESCRIPTION("BlueGene Torus Ethernet driver");
MODULE_LICENSE("GPL");


int bgtornic_driverparm = 0 ;
int bgnet_receive_torus(struct sk_buff * skb) ;
void dma_tcp_poll_handler(void) ;
void dma_tcp_rx_enable(void) ;

/*  Diagnostic options */
enum {
	k_inhibit_scattergather = 0 , /*  Whether to tell linux we cannot do 'scattergather' DMA TODO: test whether scattergathers actually work, using (e.g.) NFS */
	k_inhibit_gso = 1 /* Whether to tell linux not to try Generic Segmentation Offload ; not useful until I can get s-g working with multiple frags in a skb */
};


static void dumpmem(const void *address, unsigned int length, const char * label) __attribute__((unused)) ;
static void dumpmem(const void *address, unsigned int length, const char * label)
  {
    int x ;
    TRACEN(k_t_fifocontents|k_t_scattergather,"Memory dump, length=0x%08x: %s",length,label) ;
    if( length > 20*32 ) {
      length = 20*32 ;
    }
    for (x=0;x<length;x+=32)
      {
        int *v = (int *)(address+x) ;
        TRACEN(k_t_fifocontents|k_t_scattergather,"%p: %08x %08x %08x %08x %08x %08x %08x %08x",
            v,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]
            ) ;
      }
  }



static BGP_Personality_t personality;
static struct net_device *static_dev ;


/* int bgtorus_start_xmit(struct sk_buff *skb, struct net_device *dev) ; */
int bgtorus_start_xmit(struct sk_buff *skb, struct net_device *dev
/* 		,unsigned int x, unsigned int y, unsigned int z */
		) ;

/**********************************************************************
 *                   Linux' packet and skb management
 **********************************************************************/

static int bgtornet_change_mtu(struct net_device *dev, int new_mtu)
{
/*   struct bgtornet_dev *bgtornet = netdev_priv(dev); */
    if (new_mtu < 60 || new_mtu > BGTORNET_MAX_MTU )
	return -EINVAL;
    dev->mtu = new_mtu;
/*     bgtree_set_mtu(bgtornet->tree, new_mtu) ; */
    return 0;
}


/*  Take 2 bytes from every 16 to form a frame verifier */
static unsigned int asf_frame_verifier(const char * data, unsigned int length)
{
	const unsigned int * data_int = (unsigned int *) data ;
	unsigned int result = 0 ;
	unsigned int index ;
	for(index=0; index<length/sizeof(unsigned int);index += 4)
		{
			result += data_int[index] ;
		}
	return result & 0xffff ;
}

static int bgtornet_receive(struct sk_buff *skb, struct bglink_hdr *lnkhdr,
			 struct bglink_proto* proto)
{
    struct net_device *dev = (struct net_device*)proto->private;
    struct bgtornet_dev *bgtornet = netdev_priv(dev);

    TRACE("bgtornet rcvd pkt: data=%p, len=%d, head=%d, tail=%d, res len=%d",
	  skb->data, skb->len, lnkhdr->opt_eth.pad_head,
	   lnkhdr->opt_eth.pad_tail, skb->len - lnkhdr->opt_eth.pad_head - lnkhdr->opt_eth.pad_tail);


    /* skb_pull and trim check for over/underruns. For 0 size the
     * add/subtract is the same as a test */
    __skb_pull(skb, lnkhdr->opt_eth.pad_head);
    __skb_trim(skb, skb->len - lnkhdr->opt_eth.pad_tail);



/*     dumpmem(skb->data,skb->len,"Frame delivered via torus") ; */

    skb->dev = dev;
    skb->protocol = eth_type_trans(skb, dev);

    TRACEN(k_t_napi,"netif_rx(skb=%p)",skb) ;
    netif_rx(skb);


    dev->last_rx = jiffies;
    bgtornet->stats.rx_packets++;
    bgtornet->stats.rx_bytes += skb->len;

    return 0;
}

void bgtornet_rx_schedule(void)
  {
    TRACEN(k_t_general,"(>) bgtornet_rx_schedule") ;
    {
    struct net_device *dev = static_dev;
    struct bgtornet_dev *bgtornet = netdev_priv(dev);
    TRACEN(k_t_napi,"netif_rx_schedule(dev=%p,napi=%p)",dev,&bgtornet->napi) ;
    napi_schedule(&bgtornet->napi) ;
    }
    TRACEN(k_t_general,"(<) bgtornet_rx_schedule") ;
  }

struct net_device_stats *bgtornet_stats(void)
  {
    struct net_device *dev = static_dev;
    struct bgtornet_dev *bgtornet = netdev_priv(dev);
    return   &bgtornet->stats ;
  }

static int frame_passes_verification(struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
        unsigned int eth_proto = eth->h_proto ;
        struct iphdr *iph = (struct iphdr *)((skb->data)+sizeof(struct ethhdr)) ;
        unsigned int iphlen = 4*iph->ihl ;
        struct tcphdr *tcph = (struct tcphdr *) ( ((char *)(iph)) + (iphlen) );
        unsigned int ip_proto = iph->protocol ;
        if( eth_proto == ETH_P_IP && ip_proto == IPPROTO_TCP )
		{
			unsigned int tcphlen = 4*tcph->doff ;
			char * payload = ((char *)(tcph)) + (tcphlen) ;
			unsigned int payload_len=iph->tot_len-iphlen-tcphlen ;
			unsigned int framecheck = asf_frame_verifier(payload,payload_len) ;
			unsigned int rcvcheck = tcph->check ;
			TRACEN(k_t_general, "framecheck=0x%08x rcvcheck=0x%08x",
					framecheck, rcvcheck
					) ;
			if( framecheck != rcvcheck)
				{
					TRACEN(k_t_request,"(!!!) frame verify fails, framecheck=0x%08x rcvcheck=0x%08x payload_len=%d",
								framecheck,
								rcvcheck,
								payload_len) ;
					return 0 ;
				}
		}
        return 1 ;
}

static inline void deliver_frame(struct sk_buff *skb)
{
        struct net_device *dev = static_dev;
        struct bgtornet_dev *bgtornet = netdev_priv(dev);


/*         dumpmem(skb->data,skb->len,"Frame delivered via torus") ; */

        skb->dev = dev;
        skb->protocol = eth_type_trans(skb, dev);
/*         skb->pkt_type = PACKET_HOST ; */
        if( k_trust_torus_crc) skb->ip_summed = CHECKSUM_PARTIAL ;

#if defined(TORNIC_DEV_POLL)
        TRACEN(k_t_napi,"netif_receive_skb(skb=%p)",skb) ;
        netif_receive_skb(skb) ;
#else
        TRACEN(k_t_napi,"netif_rx(skb=%p)",skb) ;
        netif_rx(skb);
#endif

        dev->last_rx = jiffies;
        bgtornet->stats.rx_packets++;
        bgtornet->stats.rx_bytes += skb->len;
}

int bgtornet_receive_torus(struct sk_buff *skb)
{

    TRACE("bgtornet rcvd pkt: data=%p, len=%d",
          skb->data, skb->len);

    if( k_asf_frame_verifier )
	    {
		    if (frame_passes_verification(skb))
			    {
				    deliver_frame(skb) ;
			    }
		    else
			    {
					dev_kfree_skb(skb) ;
			    }
	    }
    else
	    {
		    deliver_frame(skb) ;
	    }

    TRACE("(<)");
    return 0;
}


static void inject_verifier(struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	unsigned int eth_proto = eth->h_proto ;
        struct iphdr *iph = (struct iphdr *)((skb->data)+sizeof(struct ethhdr)) ;
	unsigned int iphlen = 4*iph->ihl ;
	struct tcphdr *tcph = (struct tcphdr *) ( ((char *)(iph)) + (iphlen) );
	unsigned int ip_proto = iph->protocol ;
	if( eth_proto == ETH_P_IP && ip_proto == IPPROTO_TCP )
		{
			unsigned int tcphlen = 4*tcph->doff ;
			char * payload = ((char *)(tcph)) + (tcphlen) ;
			unsigned int payload_len=iph->tot_len-iphlen-tcphlen ;
			unsigned int framecheck = asf_frame_verifier(payload,payload_len) ;
			tcph->check = framecheck ;
			TRACEN(k_t_general,"framecheck set to 0x%08x",framecheck) ;
		}

}

static int bgtornet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
#if defined(CONFIG_BLUEGENE_TCP)
	struct ethhdr *eth = (struct ethhdr *)skb->data;
        struct iphdr *iph = (struct iphdr *)((skb->data)+sizeof(struct ethhdr)) ;
        struct bgtornet_dev *bgtornet = netdev_priv(dev);
        unsigned int h_proto =  eth->h_proto ;
        unsigned int daddr = iph->daddr ;
	TRACEN(k_t_general,"(>) skb=%p skb->sk=%p h_dest[%02x:%02x:%02x:%02x:%02x:%02x] daddr=0x%08x", skb, skb->sk,
			eth->h_dest[0],eth->h_dest[1],eth->h_dest[2],eth->h_dest[3],eth->h_dest[4],eth->h_dest[5], daddr) ;
	if( eth->h_dest[0] == 0x00 && eth->h_dest[1] == 0x80 && eth->h_dest[2] == 0x47)
		{

			if( h_proto == ETH_P_IP && (daddr >> 24) == 12)
				{
					eth->h_dest[3]=(daddr >> 16) & 0xff ;
					eth->h_dest[4]=(daddr >> 8) & 0xff ;
					eth->h_dest[5]=(daddr& 0xff) - 1 ;
				}

			      if( eth->h_dest[3] == personality.Network_Config.Xcoord
			          && eth->h_dest[4] == personality.Network_Config.Ycoord
			          && eth->h_dest[5] == personality.Network_Config.Zcoord
			          )
				      {
					          netif_rx(skb) ;  /*  Try to feed the skb to the local networking layer */
				      }
			      else
				{
				        if( k_asf_frame_verifier ) inject_verifier(skb) ;
					bgtorus_start_xmit(skb, dev
/* 							, eth->h_dest[3],eth->h_dest[4],eth->h_dest[5] */
							                                            ) ;
				}
			bgtornet->stats.tx_packets += 1 ;
			bgtornet->stats.tx_bytes += skb->len ;
		}
	else
		{
			 /*  Request to send a frame over the torus, but not to a torus MAC address. Trace and discard. */
			TRACEN(k_t_protocol,"skb=%p skb->sk=%p h_dest[%02x:%02x:%02x:%02x:%02x:%02x] not torus-mac", skb, skb->sk,
					eth->h_dest[0],eth->h_dest[1],eth->h_dest[2],eth->h_dest[3],eth->h_dest[4],eth->h_dest[5]) ;
/* 			bgtornet->stats.tx_errors += 1; */
/* 			bgtornet->stats.tx_aborted_errors += 1; */
			dev_kfree_skb(skb) ;

		}
	TRACEN(k_t_general,"(<)") ;
#else
  col_start_xmit(skb, dev) ;
#endif
  return 0 ;
}

static int bgtornet_poll(struct napi_struct * napi, int budget)
  {
    struct net_device *dev = napi->dev ;
    struct bgtornet_dev *bgtornet = netdev_priv(dev);
    TRACEN(k_t_general,"(>) bgtornet_poll napi=%p dev=%p budget=%d", napi, dev, budget) ;
    TRACEN(k_t_napi,"napi polling starts") ;
    dma_tcp_poll_handler() ;
    TRACEN(k_t_napi,"netif_rx_complete(dev=%p,napi=%p)",dev,&bgtornet->napi) ;
    napi_complete(&bgtornet->napi);
    dma_tcp_rx_enable() ;
    TRACEN(k_t_general,"(<) bgtornet_poll dev=%p", dev) ;
    return 0 ;
  }

static void bgtornet_uninit(struct net_device *dev)
{
    struct bgtornet_dev *bgtornet = netdev_priv(dev);
    BUG_ON(bgtornet->lnk.private != dev);

}

static struct net_device_stats *bgtornet_get_stats(struct net_device *dev)
{
    struct bgtornet_dev *bgtornet = netdev_priv(dev);
    return &bgtornet->stats;
}


static int bgtornet_init (struct net_device *dev)
{
    struct bgtornet_dev *bgtornet = netdev_priv(dev);

    bgtornet = netdev_priv(dev);



     /*  register with tree */
    bgtornet->lnk.lnk_proto = bgtornet->tor_protocol;
    bgtornet->lnk.rcv = bgtornet_receive;
    bgtornet->lnk.private = dev;



    return 0;
}

void bgtornet_set_arp_table_entry(unsigned int x, unsigned int y, unsigned int z, unsigned int ip_address)
	{
	struct net_device *dev = static_dev ;
	__be32 ip = ip_address ;
	struct neighbour * neigh = neigh_create(&arp_tbl, &ip, dev);
	if (neigh) {
		u8 lladdr[6] ;
		lladdr[0] = 0x00 ;
		lladdr[1] = 0x80 ;
		lladdr[2] = 0x47 ;
		lladdr[3] = x ;
		lladdr[4] = y ;
		lladdr[5] = z ;
		neigh_update(neigh,  lladdr, NUD_PERMANENT, NEIGH_UPDATE_F_OVERRIDE);
		neigh_release(neigh);
	}
	}

#if defined(HAVE_NET_DEVICE_OPS)
static const struct net_device_ops netdev_ops = {
    .ndo_change_mtu = bgtornet_change_mtu ,
    .ndo_get_stats = bgtornet_get_stats ,
    .ndo_start_xmit = bgtornet_start_xmit ,
    .ndo_init = bgtornet_init ,
    .ndo_uninit = bgtornet_uninit ,
};
#endif

static unsigned int dummy_features ;

static struct ctl_table bgp_tornic_table[] = {
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "features",
	                .data           = &dummy_features,
	                .maxlen         = sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        { 0 },
} ;
static struct ctl_path tornic_ctl_path[] = {
	{ .procname = "bgp", .ctl_name = 0, },
	{ .procname = "torusdev", .ctl_name = 0, },
	{ },
};


int __init
bgtornet_module_init (void)
{

     struct bgtornet_dev *bgtornet;
     struct net_device *dev;
    printk (KERN_INFO "%s\n", version);

    bluegene_getPersonality( &personality, sizeof(personality) );

    dev = alloc_etherdev(sizeof(struct bgtornet_dev));
    if (!dev)
	return -ENOMEM;

    static_dev = dev ;


    bgtornet = netdev_priv(dev);
    memset(bgtornet, 0, sizeof(*bgtornet));
     /*  The following probably need to be configurable */

    bgtornet->phandle_torus = 0;
    bgtornet->eth_mask = 0;
    dev->dev_addr[0] = 0x00;
    dev->dev_addr[1] = 0x80;
    dev->dev_addr[2] = 0x47;
    dev->dev_addr[3] = personality.Network_Config.Xcoord ;
    dev->dev_addr[4] = personality.Network_Config.Ycoord ;
    dev->dev_addr[5] = personality.Network_Config.Zcoord ;

    bgtornet->eth_local = bgtornet->eth_mask & *(unsigned int *)&dev->dev_addr[0];

#if defined(HAVE_NET_DEVICE_OPS)
    dev->netdev_ops = &netdev_ops ;
#else
    dev->init			= bgtornet_init;
    dev->uninit			= bgtornet_uninit;
    dev->get_stats	        = bgtornet_get_stats;
    dev->hard_start_xmit        = bgtornet_start_xmit;
    dev->change_mtu		= bgtornet_change_mtu;
#endif
    dev->mtu      = BGTORNET_DEFAULT_MTU;


    TRACEN(k_t_napi,"netif_napi_add(dev=%p,napi=%p,poll=bgtornet_poll,weight=16)",dev,&bgtornet->napi) ;
    netif_napi_add(dev,&bgtornet->napi,bgtornet_poll,16) ;
    TRACEN(k_t_napi,"napi poll_list=(%p,%p) state=%lu weight=%d poll=%p dev=%p dev_list=(%p,%p)",
            bgtornet->napi.poll_list.next,bgtornet->napi.poll_list.prev,
            bgtornet->napi.state,bgtornet->napi.weight,bgtornet->napi.poll,
            bgtornet->napi.dev,
            bgtornet->napi.dev_list.next,bgtornet->napi.dev_list.prev ) ;
    TRACEN(k_t_napi,"napi_enable(napi=%p)",&bgtornet->napi) ;
    napi_enable(&bgtornet->napi) ;
    TRACEN(k_t_napi,"napi poll_list=(%p,%p) state=%lu weight=%d poll=%p dev=%p dev_list=(%p,%p)",
            bgtornet->napi.poll_list.next,bgtornet->napi.poll_list.prev,
            bgtornet->napi.state,bgtornet->napi.weight,bgtornet->napi.poll,
            bgtornet->napi.dev,
            bgtornet->napi.dev_list.next,bgtornet->napi.dev_list.prev ) ;


/*  If we're trusting the torus hardware, there is no point forming an IP checksum on the send side */
    dev->features = NETIF_F_HIGHDMA
                  | (k_trust_torus_crc ? (NETIF_F_IP_CSUM | NETIF_F_NO_CSUM | NETIF_F_HW_CSUM | NETIF_F_IPV6_CSUM) : 0 )
		  | (k_inhibit_scattergather ? 0 : NETIF_F_SG) ;

    skb_queue_head_init(&(bgtornet->xmit_list)) ;


    if (register_netdev(dev) != 0)
	goto err;
    if( k_inhibit_gso )
	    {
		    dev->features &= ~(NETIF_F_GSO) ; // scatter-gather sometimes does not get it right. Might be a problem with GSO or might be broken anyway
						       /*  TODO: Isolate whether GSO is broken or whether the torus driver is broken */
	    }

    bgp_tornic_table[0].data = &(dev->features) ;

	register_sysctl_paths(tornic_ctl_path,bgp_tornic_table) ;

    printk(KERN_INFO
	   "%s: BGNET %s, MAC %02x:%02x:%02x:%02x:%02x:%02x\n" "BGTORNET mask 0x%08x local 0x%08x\n",
	   dev->name, "np->full_name",
	   dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
	   dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5],
           bgtornet->eth_mask, bgtornet->eth_local
	   );

    return 0;

 err:
    free_netdev(dev);
    return -1;


    return 0;
}

void __exit bgtornet_module_exit (void)
{
}

/* module_init(bgtornet_module_init); */
/* module_exit(bgtornet_module_exit); */
