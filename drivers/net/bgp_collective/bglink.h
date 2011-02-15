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
 *          Volkmar Uhlig <vuhlig@us.ibm.com>
 *
 * Description:   Link layer definitions
 *
 *
 ********************************************************************/
#ifndef __DRIVERS__BLUEGENE__LINK_H__
#define __DRIVERS__BLUEGENE__LINK_H__

#include <linux/skbuff.h>

#include <asm/atomic.h>

/* link layer protocol IDs */
#define BGLINK_P_NET	0x01
#define BGLINK_P_CON	0x10

union link_proto_opt {
    u16 raw;
    struct {
	u16 option	: 4;
	u16 pad_head	: 4;
	u16 pad_tail	: 8;
    } opt_net;
    struct {
	u16 len;
    } opt_con;
} __attribute__((packed));

struct bglink_hdr_col {
    u32 dst_key;
    u32 src_key;
    u16 conn_id;
    u8 this_pkt;
    u8 total_pkt;
    u16 lnk_proto;   /*  net, con, ... */
    union link_proto_opt opt;
} ;  /*  __attribute__((packed)); */

struct bglink_hdr_col_map {
    u32 dst_key;
    u32 src_key;
    u32 conn_this_total;
    u32 proto_option_head_tail ;
} ;

struct bglink_hdr_torus {
    u32 dst_key;
    u32 len;
    u16 lnk_proto;   /*  net, con, ... */
    union link_proto_opt opt;
} ;  /*  __attribute__((packed)); */

/* link protocol callbacks
 * rcv is called when new packet arrives
 * flush is called when the device was busy and becomes idle
 *     again (flow control)
 */
struct bgnet_dev ;
struct bg_col ;
struct bglink_proto {
    u16 lnk_proto;
    int receive_from_self;
    int (*col_rcv)(struct bg_col*, struct sk_buff*, struct bglink_hdr_col *, struct bglink_proto *proto);
    int (*col_rcv_trimmed)(struct bg_col*, struct sk_buff*, struct bglink_proto *proto, unsigned int src_key);
    int (*col_flush)(int chn);
    int (*torus_rcv)(struct sk_buff*, struct bglink_hdr_torus *);
    void *private;
    struct list_head list;
};

extern struct list_head linkproto_list;

static void bglink_register_proto(struct bglink_proto *proto) __attribute__ ((unused)) ;
static void bglink_unregister_proto(struct bglink_proto *proto) __attribute__ ((unused)) ;;
static struct bglink_proto* bglink_find_proto(u16 proto)__attribute__ ((unused)) ;

enum {
  k_link_protocol_limit = 8   /*  we only actually have 'eth' and 'eth_reflector' at the moment, but we might get 'con' and more */
};
extern struct bglink_proto * proto_array[k_link_protocol_limit] ;
static void bglink_register_proto(struct bglink_proto *proto)
{
  if( proto->lnk_proto < k_link_protocol_limit)
    {
      proto_array[proto->lnk_proto] = proto ;
    }
}

static void bglink_unregister_proto(struct bglink_proto *proto)
{
  if( proto->lnk_proto < k_link_protocol_limit)
    {
      proto_array[proto->lnk_proto] = NULL ;
    }
}

static struct bglink_proto* bglink_find_proto(u16 proto)
{
    return proto_array[proto & (k_link_protocol_limit-1)] ;
}


#if 0
/*
 * Here are some thoughts on how we might better consolidate link headers
 * for the col and torus.  The idea is that there's an 8-byte packet header
 * that must be sent (at least) once per packet, and an 8-byte fragment header
 * that has to be included with every fragment.  For the col we can include
 * both headers in every fragment.  For the torus, there's not room to send
 * the packet header in every fragment, so we'd have to send it once as part
 * of the payload in the first fragment (as we're doing now anyway).
 * The various structures might look something like:
 */

struct pkt_hdr {
    u32 lnk_proto    : 8;
    u32 dst_key      : 24;
    u16 len;
    u16 private;
} __attribute__((packed));

struct frag_hdr {
    u32 offset;
    u32 conn_id      : 8;
    u32 src_key      : 24;
} __attribute__((packed));

struct frag_hdr_col {
    struct pkt_hdr pkt;
    struct frag_hdr frag;
} __attribute__((packed));

struct frag_hdr_torus {
    union torus_fifo_hw_header fifo;
    struct frag_hdr frag;
} __attribute__((packed));
#endif

#endif /* !__DRIVERS__BLUEGENE__LINK_H__ */
