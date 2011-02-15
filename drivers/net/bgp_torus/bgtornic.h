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
 * Authors: Volkmar uhlig
 *          Chris Ward <tjcw@uk.ibm.com>
 *
 * Description:   definitions for BG networks
 *
 *
 ********************************************************************/

#ifndef __DRIVERS__NET__BLUEGENE__BGNIC_H__
#define __DRIVERS__NET__BLUEGENE__BGNIC_H__

/* #define BG_IRQ(group, irq)	((group) << 5 | (irq)) */


/**********************************************************************
 * link layer
 **********************************************************************/

/* #define BGNET_P_ETH0		1 */
/* #define BGNET_P_ETH1            2 */
/* #define BGNET_P_ETH2            3 */
/* #define BGNET_P_ETH3            4 */
/* #define BGNET_P_ETH4            5 */
/* #define BGNET_P_ETH5            6 */
/* #define BGNET_P_ETH6            7 */
/* #define BGNET_P_ETH7            8 */
/* #define BGNET_P_ETH8            9 */
/* #define BGNET_P_LAST_ETH        BGNET_P_ETH8 */
/*  */
/* #define BGNET_P_CONSOLE		20 */

/* #define BGNET_FRAG_MTU		240 */
/*  When running 'dma_tcp_frames', we can have an MTU as large as we like. IP limits to 64k, though. */
enum {
	BGTORNET_DEFAULT_MTU = ETH_DATA_LEN ,
	BGTORNET_MAX_MTU = 65536
};
#define BGNET_MAX_MTU		65536
/* #define BGNET_MAX_MTU		(BGNET_FRAG_MTU * 128) */
/* #define BGNET_DEFAULT_MTU	(BGNET_FRAG_MTU * 30 - 12) */
/* #define BGNET_DEFAULT_MTU	ETH_DATA_LEN */

enum {
	k_trust_torus_crc =
#if defined(BGP_TORUS_IP_CHECKSUM)
		0
#else
		1
#endif
		,
/* #if defined(CONFIG_BGP_TORUS_ADAPTIVE_ROUTING) */
//	k_trust_torus_crc = 1 ,  /*  Whether the IP layer should trust the BGP hardware CRC on the torus network */
/* #else */
//	k_trust_torus_crc = 1 ,  /*  Whether the IP layer should trust the BGP hardware CRC on the torus network */
/* #endif */
	k_asf_frame_verifier = 0  /*  Whether to try a frame verifier in the bgtornic layer */
};


struct bglink_hdr
{
    unsigned int dst_key;
    unsigned int src_key;
    unsigned short conn_id;
    unsigned char this_pkt;
    unsigned char total_pkt;
    unsigned short lnk_proto;   /*  1 eth, 2 con, 3... */
    union {
        unsigned short optional;  /*  for encapsulated protocol use */
        struct {
            u8 pad_head;
            u8 pad_tail;
        } opt_eth;
    };
} __attribute__((packed));


struct bglink_proto
{
    unsigned short lnk_proto;
    int (*rcv)(struct sk_buff*, struct bglink_hdr*, struct bglink_proto*);
    void *private;
    struct list_head list;
};

struct bgtornet_dev
{
    unsigned short tor_protocol;
    unsigned int eth_mask;
    unsigned int eth_local;
    struct bglink_proto lnk;
    struct net_device_stats stats;
    u32 phandle_torus;
    struct napi_struct napi ; /* 2.6.27-ism for NAPI poll */
    struct sk_buff_head xmit_list ;   /* List of skb's to be sent */
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


#endif /* !__DRIVERS__NET__BLUEGENE__BGNIC_H__ */
