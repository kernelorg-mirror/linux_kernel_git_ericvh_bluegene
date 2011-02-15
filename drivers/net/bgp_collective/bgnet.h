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
 * Authors: Volkmar Uhlig <vuhlig@us.ibm.com>
 *          Chris Ward <tjcw@uk.ibm.com>
 *
 * Description:   definitions for BG networks
 *
 *
 ********************************************************************/

#ifndef __DRIVERS__NET__BLUEGENE__BGNET_H__
#define __DRIVERS__NET__BLUEGENE__BGNET_H__

/* static inline unsigned int BG_IRQ(unsigned int group, unsigned int irq) */
/* { */
/* 	return ((group) << 5 | (irq)) ; */
/* } */
/* #define BG_IRQ(group, irq)	((group) << 5 | (irq)) */


/**********************************************************************
 * link layer
 **********************************************************************/

/* enum { */
/* 	BGNET_P_ETH0 = 1 , */
/* 	BGNET_P_ETH1 = 2 , */
/* 	BGNET_P_ETH2 = 3 , */
/* 	BGNET_P_ETH3 = 4 , */
/* 	BGNET_P_ETH4 = 5 , */
/* 	BGNET_P_ETH5 = 6 , */
/* 	BGNET_P_ETH6 = 7 , */
/* 	BGNET_P_ETH7 = 8 , */
/* 	BGNET_P_ETH8 = 9 , */
/* 	BGNET_P_LAST_ETH = BGNET_P_ETH8 , */
/* 	BGNET_P_CONSOLE = 20 */
/* }; */
/* //#define BGNET_P_ETH0		1 */
/* //#define BGNET_P_ETH1            2 */
/* //#define BGNET_P_ETH2            3 */
/* //#define BGNET_P_ETH3            4 */
/* //#define BGNET_P_ETH4            5 */
/* //#define BGNET_P_ETH5            6 */
/* //#define BGNET_P_ETH6            7 */
/* //#define BGNET_P_ETH7            8 */
/* //#define BGNET_P_ETH8            9 */
/* //#define BGNET_P_LAST_ETH        BGNET_P_ETH8 */
/* // */
/* //#define BGNET_P_CONSOLE		20 */

/* Facility for using multiple cores in support of 'collective', only make it happen if multiple cores are available ... */
#if defined(CONFIG_SMP) && !defined(CONFIG_BLUEGENE_UNIPROCESSOR) && !defined(CONFIG_BGP_VRNIC)
#define COLLECTIVE_TREE_AFFINITY
#endif

#if defined(COLLECTIVE_TREE_AFFINITY)
/* On IO nodes, 10gE will be using core 0. On Compute nodes, torus will be using core 2. So exploit cores 1 and 3 for collective ... */
enum {
	k_TreeAffinityCPU = 1 ,
	k_WorkqueueDeliveryCPU = 3
};
#else
enum {
	k_TreeAffinityCPU = 0 ,
	k_WorkqueueDeliveryCPU = 0
};
#endif

enum {
	BGNET_FRAG_MTU = 240 ,
/* 	BGNET_MAX_MTU = BGNET_FRAG_MTU * 128 , */
	BGNET_DEFAULT_MTU = ETH_DATA_LEN
};
/* #define BGNET_FRAG_MTU		240 */
/* #define BGNET_MAX_MTU		(BGNET_FRAG_MTU * 128) */
/* //#define BGNET_DEFAULT_MTU	(BGNET_FRAG_MTU * 30 - 12) */
/* #define BGNET_DEFAULT_MTU	ETH_DATA_LEN */

/* // Which bgcol channel to use for the driver */
/* #define BGNET_TREE_CHANNEL 0 */

enum {
	k_trust_collective_crc =
#if defined(BGP_COLLECTIVE_IP_CHECKSUM)
		0
#else
		1
#endif
		/*  Whether the IP layer should trust the BGP hardware CRC on the collective network */
};

enum {
	k_collective_budget = 1000  /*  Number of frames we are willing to collect from the tree before we 'yield' */
};

enum {
	k_deliver_via_workqueue = 1 /* Whether to deliver via a work queue (on another core) */
};
struct bgnet_dev
{
    struct bg_col *bgcol;
    unsigned int bgcol_route;
    unsigned int bgcol_channel;
    unsigned short bgcol_protocol;
    unsigned short bgcol_reflector_protocol ;
    unsigned int bgcol_vector;
    unsigned int eth_mask;
    unsigned int eth_local;
    unsigned int eth_bridge_vector;
    struct bglink_proto lnk;
    struct bglink_proto lnkreflect;
    struct net_device_stats stats;
    u32 phandle_bgcol;
    u32 phandle_torus;
    struct sk_buff_head xmit_list ;   /* List of skb's to be sent */
#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
    struct napi_struct napi ;
#endif
/*     unsigned int i_am_ionode ; */
};

extern inline unsigned int eth_to_key(char *addr)
{
    unsigned int key;
    if (is_broadcast_ether_addr(addr))
        key = ~0U;
    else
        key = (addr[3] << 16) | (addr[4] << 8) | (addr[5] << 0);
    return key;
}


/* extern struct list_head bglink_proto; */
/* extern struct bglink_proto bgnet_eth; */

#endif /* !__DRIVERS__NET__BLUEGENE__BGNIC_H__ */
