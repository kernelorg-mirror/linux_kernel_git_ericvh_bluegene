/*********************************************************************
 *
 * Description: Blue Gene low-level driver for collective network
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
 * The protocol implemented here will send a 'jumbo' (9000 byte) frame
 * in 38 packets, i.e. 240 bytes payload + 16 bytes link header per packet.
 * The measured throughput was 4325 Mbit/sec on one IO link
 *
 * It is logically possible to send a 'jumbo' frame in 36 packets; to
 * do this you need to pack 255 bytes of payload + 1 byte of link
 * header per packet (you need to at least indicate which node has sent
 * the packet); you probably want to do this by 'trampling' the first
 * byte of each packet, sending a 'correction' byte sequence at the
 * end of the frame, and having the receiver demultiplex and correct
 * the frames.
 * This should achieve 4565 Mbit/sec
 *
 * If you were to drive the link with an MTU of close to 65535, you
 * could send a 65270-byte frame in 256 packets, which should achieve
 * 4655 Mbit/sec.
 *
 ********************************************************************/

#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <net/arp.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/prom.h>


#include "bglink.h"
#include "bgcol.h"
#include "bgnet.h"
#include "bgp_dcr.h"
#include "ppc450.h"

#include <asm/bluegene.h>

#define DRV_NAME	"bgcol"
#define DRV_VERSION	"1.0"
#define DRV_DESC	"IBM Blue Gene Collective Driver"

MODULE_DESCRIPTION(DRV_DESC);
MODULE_AUTHOR("IBM");
MODULE_LICENSE("GPL");

/*  SA_ONSTACK is deprecated, but its replacement has not made it into MCP yet. Compatibility ... */
#if !defined(IRQF_DISABLED)
#define IRQF_DISABLED SA_ONSTACK
#endif

/*  configuration selector macros */
#define COLLECTIVE_RECEIVE_WITH_SLIH
/* #define COLLECTIVE_DELIVER_VIA_TASKLET */
/* #define COLLECTIVE_BREAK_ON_FRAME */
#define COLLECTIVE_TRANSMIT_WITH_SLIH
#define COLLECTIVE_TRANSMIT_WITH_FLIH
#define COLLECTIVE_XMITTER_FREES
#define COLLECTIVE_DUPLEX_SLIH
#define COLLECTIVE_ONEPASS_TXRX
#define BGP_COL_STATUS_VISIBILITY


extern void bic_set_cpu_for_irq(unsigned int irq, unsigned int cpu) ;

/*  For diagnosis of certain link sequencing problems, it can be useful to keep a trail of */
/*  recently-arrived link headers. Set this macro if you want a trail kept */
/* #define KEEP_LNKHDR_TRAIL */
enum {
  k_lnkhdr_trail_display_length = 50,  /*  Link header amount of trail to display */
  k_lnkhdr_trail_length = 64,  /*  Link header ring buffer length, next power of 2 above k_lnkhdr_trail_display_length. */
  k_lnhhdr_ffdc_limit = 20  /*  First-failure-data-capture limit, we want to catch first failures and not saturate the logging system */
};

/*  For diagnostics, track the last thing that we knew happened to the bgcol in interrupt mode */
enum {
  k_bgcolaction_none ,
  k_bgcolaction_xmit ,
  k_bgcolaction_xmit_enable ,
  k_bgcolaction_xmit_irq ,
  k_bgcolaction_xmit_irq_disable

};

struct bglink_proto * proto_array[k_link_protocol_limit] ;

/* static int bgcolaction ; */

extern int e10000_diag_count ;


/* #define CONFIG_BLUEGENE_COLLECTIVE_TRACE */

/* #define REQUIRE_TRACE */

#include <linux/KernelFxLog.h>

#include "../bgp_network/bgp_net_traceflags.h"

/* #if defined(CONFIG_BLUEGENE_COLLECTIVE_TRACE) */
/* static int bgcol_debug_tracemask=k_t_general|k_t_lowvol|k_t_irqflow|k_t_irqflow_rcv|k_t_protocol ; */
int bgcol_debug_tracemask  = k_t_init | k_t_request | k_t_protocol ;
/* int bgcol_debug_tracemask  = 0xffffffff ; */
/* #endif */

/*  Can drop bits out of COMPILED_TRACEMASK if we want to selectively compile out trace */
#define COMPILED_TRACEMASK (0xffffffff-k_t_detail-k_t_fifocontents)
/* #define COMPILED_TRACEMASK (k_t_error) */

#define XTRACEN(i,x...)
#if defined(REQUIRE_TRACE)
#define TRACE(x...)    KernelFxLog(1,x)
#define TRACE1(x...)   KernelFxLog(1,x)
#define TRACE2(x...)   KernelFxLog(1,x)
#define TRACEN(i,x...) KernelFxLog(1,x)
#define TRACED(x...)   KernelFxLog(1,x)
#define TRACES(x...)   KernelFxLog(1,x)
#elif  defined(CONFIG_BLUEGENE_COLLECTIVE_TRACE)
#define TRACE(x...)    KernelFxLog(bgcol_debug_tracemask & k_t_general,x)
#define TRACE1(x...)   KernelFxLog(bgcol_debug_tracemask & k_t_lowvol,x)
#define TRACE2(x...)   KernelFxLog(bgcol_debug_tracemask & k_t_detail,x)
#define TRACEN(i,x...) KernelFxLog(bgcol_debug_tracemask & (COMPILED_TRACEMASK & (i)),x)
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

#define _BGP_DCR_COL 0

#define FRAGMENT_TIMEOUT	(HZ/10)

#define COL_LNKHDRLEN		(sizeof(struct bglink_hdr_col))
#define COL_FRAGPAYLOAD	(COL_PAYLOAD - COL_LNKHDRLEN)
#define COL_SKB_ALIGN		16


#define BGP_COL_MAJOR_NUM  120
#define BGP_TORUS_MAJOR_NUM 121
#define BGP_GI_MAJOR_NUM    122
#define BGP_COL_MINOR_NUMS  2
#define BGP_TORUS_MINOR_NUMS 2
#define BGP_GI_MINOR_NUMS   4
#define _BGP_UA_COL0  (0x6)
#define _BGP_PA_COL0  (0x10000000)
#define _BGP_UA_COL1  (0x6)
#define _BGP_PA_COL1  (0x11000000)
#define _BGP_UA_TORUS0 (0x6)
#define _BGP_PA_TORUS0 (0x01140000)
#define _BGP_UA_TORUS1 (0x6)
#define _BGP_PA_TORUS1 (0x01150000)

/*
 * 'Oversized' skbuffs are an attempt to increase throughput on the collective interface by arranging for
 * 2 cores to work together on pulling data and distributing it. See commentary in bgnet.c as to what needs
 * to be done to get it to work.
 * Having an skbuff at 64K rather than 9K (to match etherhet 'jumbo' frames) doesn't really cost much memory;
 * we are only likely to have a few MB of skbuffs in each IO node, and less in each compute node.
 */
enum {
	k_use_plentiful_skb = 1 , /* Whether to use an oversized sk_buff to receive in to */
	k_plentiful_skb_size = 256*COL_FRAGPAYLOAD
};

static void bgcol_prefill(struct sk_buff_head * skb_list, unsigned int count)
{
	unsigned int x ;
	for(x=0;x<count;x+=1)
		{
			struct sk_buff *skb=alloc_skb(k_plentiful_skb_size,GFP_KERNEL) ;
			if(skb)
				{
					skb_queue_tail(skb_list,skb) ;
				}

		}
}

static struct sk_buff * take_skb_from_list_for_filling(struct bg_col *col)
{
	return skb_dequeue (&col->skb_list_for_filling) ;
}

static void replenish_list_for_filling(struct bg_col *col)
{
	struct sk_buff *skb=alloc_skb(k_plentiful_skb_size,GFP_KERNEL) ;
	if(skb)
		{
			skb_queue_tail(&col->skb_list_for_filling,skb) ;
		}

}
/* int bgcol_diagnostic_use_napi ; */
/*
 * device management
 */

#define BGP_MAX_DEVICES 8
static struct bgpnet_dev bgpnet_devices[BGP_MAX_DEVICES];
/* static unsigned int bgpnet_num_devices = 0; */


static struct proc_dir_entry* bgpnetDir;
/* static struct proc_dir_entry* barrierEntry; */
static struct proc_dir_entry* statisticsEntry;
static struct proc_dir_entry* statusEntry;
/* static struct proc_dir_entry* tracemaskEntry; */
struct bg_col static_col;

static struct bg_col *__bgcol = &static_col ;

/* static int bgpnet_add_device(int major, int minor, const char* name, */
/*                              unsigned long long base, int irq, */
/*                              irqreturn_t (*irq_handler)(int, void*)); */
/* static int bgpnet_device_open(struct inode *inode, struct file *filp); */
/* static int bgpnet_device_mmap(struct file *filp,  struct vm_area_struct *); */
/* static int bgpnet_device_release(struct inode *inode, struct file * filp); */
/* static int bgpnet_device_ioctl(struct inode *inode, struct file * filp, */
/*                                unsigned int  cmd,   unsigned long arg); */
/* static ssize_t bgpnet_device_read(struct file *filp, char __user *buf, size_t count, */
/* 				  loff_t *f_pos); */
/* static unsigned int bgpnet_device_poll(struct file *file, poll_table * wait); */


/* static struct file_operations bgpnet_device_fops = */
/* { */
/*   .owner=   THIS_MODULE, */
/*   .open=    bgpnet_device_open, */
/*   .read=    bgpnet_device_read, */
/*   .write=   NULL, */
/*   .poll=    bgpnet_device_poll, */
/*   .ioctl=   bgpnet_device_ioctl, */
/*   .release= bgpnet_device_release, */
/*   .mmap=    bgpnet_device_mmap, */
/* }; */

struct bg_col *bgcol_get_dev()
{
    return __bgcol;
}

unsigned int bgcol_get_nodeid(struct bg_col* col)
{
    return col->nodeid;
}

/**********************************************************************
 * IRQs
 **********************************************************************/

/* static irqreturn_t bgcol_unhandled_interrupt(int irq, void *dev, struct pt_regs* regs) */
/* { */
/*     panic("col: unhandled irq %d\n", irq); */
/* } */

static irqreturn_t bgcol_duplex_interrupt(int irq, void *dev);

#define IRQ_IDX_INJECT	0
#define IRQ_IDX_RECEIVE	1

#define DEF_IRQ(_irq, _name, _handler) \
{ .irq = _irq, .name = _name, .handler = _handler }

#define BG_COL_IRQ_INJ 180
#define BG_COL_IRQ_RCV 181

#define BG_COL_IRQ_GROUP 5
#define BG_COL_IRQ_INJ_GINT 20
#define BG_COL_IRQ_RCV_GINT 21

/*  Linux 'virtual interrupt' numbers corresponding to how the collective is wired to the BIC */
enum {
	k_inject_irq = (5*32 + 20) + 32 ,
	k_receive_irq = (5*32 + 21) + 32
} ;

static struct {
    unsigned irq;
    char *name;
    irqreturn_t (*handler)(int irq, void *dev);
} bgcol_irqs [] = {
    DEF_IRQ(k_inject_irq, "Tree inject", bgcol_duplex_interrupt),	/* IRQ_IDX_INJECT */
    DEF_IRQ(k_receive_irq, "Tree receive", bgcol_duplex_interrupt),	/* IRQ_IDX_RECEIVE */
#if 0
    DEF_IRQ("Tree VC0", bgcol_receive_interrupt),
    DEF_IRQ("Tree VC1", bgcol_receive_interrupt),
    DEF_IRQ("Tree CRNI timeout", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree no-target", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree ALU overflow", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree local client inject", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree local client receive", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree write send CH0", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree ECC send CH0", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree link CRC send CH0", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree write send CH1", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree ECC send CH1", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree link CRC send CH1", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree write send CH2", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree ECC send CH2", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree link CRC send CH2", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree ECC rcv CH0", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree link CRC rcv CH0", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree ECC rcv CH1", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree link CRC rcv CH1", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree ECC rcv CH2", bgcol_unhandled_interrupt),
    DEF_IRQ("Tree link CRC rcv CH2", bgcol_unhandled_interrupt),
#endif
    { -1,NULL, NULL }
};


/**********************************************************************
 *                                 Debug
 **********************************************************************/

static inline void dump_skb(struct sk_buff *skb)
{
  TRACEN(k_t_general,"sk_buff at %p, data=%p, len=%d", skb,skb->data, skb->len) ;
#if defined(CONFIG_BLUEGENE_COLLECTIVE_TRACE)
  if( bgcol_debug_tracemask & k_t_detail )
    {
      int i;
      for (i = 0; i < skb->len / 4 + 1; i++)
            printk("%08x%c", ((u32*)skb->data)[i], (i + 1) % 8 ? ' ' : '\n');
      printk("\n");
    }
#endif
}

static inline void dump_skb_partial(struct sk_buff *skb, int maxlength)
{
  TRACEN(k_t_general,"sk_buff at %p, data=%p, len=%d", skb,skb->data, skb->len) ;
#if defined(CONFIG_BLUEGENE_COLLECTIVE_TRACE)
  if( bgcol_debug_tracemask & k_t_detail )
    {
      int j = (maxlength > skb->len) ? skb->len : maxlength ;
      int i;
      for (i = 0; i < j / 4 + 1; i++)
        printk("%08x%c", ((u32*)skb->data)[i], (i + 1) % 8 ? ' ' : '\n');
      printk("\n");
    }
#endif
}

static inline void dump_bgcol_packet(struct bglink_hdr_col *lnkhdr, void * payload)
  {
    TRACEN(k_t_general,"bgcol_packet: hdr: conn=%x, this_pkt=%x, tot_pkt=%x, dst=%x, src=%x",
        lnkhdr->conn_id, lnkhdr->this_pkt, lnkhdr->total_pkt, lnkhdr->dst_key, lnkhdr->src_key);
#if defined(CONFIG_BLUEGENE_COLLECTIVE_TRACE)
    if( bgcol_debug_tracemask & k_t_detail )
      {
        int i ;
        int * pi = (int *) payload ;
        for( i=0; i<COL_FRAGPAYLOAD/sizeof(int); i += 8)
          {
            TRACEN(k_t_bgcolpkt," %04x %08x %08x %08x %08x %08x %08x %08x %08x",
                4*i, pi[i+0], pi[i+1], pi[i+2], pi[i+3], pi[i+4], pi[i+5], pi[i+6], pi[i+7]
                 ) ;
          }
  }
#endif
  }

/* Delivery of skbuffs to linux networking layer */
/* Deliver an 'sk_buff' via a work queue, so that 'this' core can spend its time draining the collective hardware */
struct bgcol_workqueue_item
{
	struct work_struct work ;
	struct bglink_proto *proto ;
	unsigned int src_key ;
};
static void bgcol_workqueue_actor(struct work_struct * work)
{
	char * cb = (char *) work ;
	struct sk_buff *skb = (struct sk_buff *) (cb - offsetof(struct sk_buff, cb)) ;
	struct bgcol_workqueue_item * bgcol_work =(struct bgcol_workqueue_item *) work ;
	TRACEN(k_t_napi,"(>) work=%p skb=%p", work, skb) ;
	bgcol_work->proto->col_rcv_trimmed(&static_col,skb,bgcol_work->proto,bgcol_work->src_key) ;
	replenish_list_for_filling(&static_col) ;
	TRACEN(k_t_napi,"(<)") ;
}
static void bgcol_deliver_via_workqueue(struct sk_buff *skb, struct bglink_hdr_col *lnkhdr, struct bglink_proto *proto )
{
	struct bgcol_workqueue_item * bgcol_work = (struct bgcol_workqueue_item *)(skb->cb) ;
	int rc ;
	TRACEN(k_t_napi,"(>)skb=%p", skb) ;
	    __skb_pull(skb, lnkhdr->opt.opt_net.pad_head);
	    __skb_trim(skb, skb->len - lnkhdr->opt.opt_net.pad_tail);
	INIT_WORK(&bgcol_work->work,bgcol_workqueue_actor) ;
	bgcol_work->proto = proto ;
	bgcol_work->src_key = lnkhdr->src_key ;
	rc=schedule_work_on(k_WorkqueueDeliveryCPU,&bgcol_work->work) ;
	TRACEN(k_t_napi,"(<) rc=%d",rc) ;
}
/**********************************************************************
 *                          Interrupt handling
 **********************************************************************/

/*  Enable receive interrupts */
void bgcol_enable_interrupts(struct bg_col *bgcol)
{
    unsigned rec_enable;
    unsigned long flags ;
    TRACE( "(>) bgcol=%p", bgcol);
    printk(KERN_NOTICE "enable ints \n");

    spin_lock_irqsave(&bgcol->lock, flags);

     /*  set watermarks */
    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_GLOB_VCFG0, _TR_GLOB_VCFG_RWM(0) );
    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_GLOB_VCFG1, _TR_GLOB_VCFG_RWM(0) );
     /*  set watermarks */
    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_GLOB_VCFG0, _TR_GLOB_VCFG_IWM(4) );  /*  let transmit fifos get half empty before interrupting */

    rec_enable = mfdcrx(bgcol->dcrbase + _BGP_DCR_TR_REC_PRXEN);
    rec_enable |= COL_IRQMASK_REC;
    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_REC_PRXEN, rec_enable );

     /*  clear exception flags */
    mfdcrx( bgcol->dcrbase + _BGP_DCR_TR_INJ_PIXF );
    mfdcrx( bgcol->dcrbase + _BGP_DCR_TR_REC_PRXF );

    spin_unlock_irqrestore(&bgcol->lock, flags);
    TRACE( "(<)  rec_enable:0x%08x", rec_enable);
}

static inline void bgcol_enable_interrupts_rcv(struct bg_col *bgcol)
{
    unsigned rec_enable;
    TRACE( "(>) bgcol=%p", bgcol);
    rec_enable = COL_IRQMASK_REC ;
    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_REC_PRXEN, rec_enable );

    TRACE( "(<)  rec_enable:0x%08x", rec_enable);
}

static inline void bgcol_enable_interrupts_xmit(struct bg_col *bgcol)
{
    TRACE( "bgcol=%p", bgcol);

    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_INJ_PIXEN, (_TR_INJ_PIX_ENABLE | _TR_INJ_PIX_WM0 ) );

}


static inline void bgcol_disable_interrupts(struct bg_col *bgcol)
{
  TRACEN(k_t_irqflow,"bgcol=%p", bgcol);

    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_INJ_PIXEN, _TR_INJ_PIX_ENABLE );
    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_REC_PRXEN, 0 );

}

static inline void bgcol_disable_interrupts_rcv(struct bg_col *bgcol)
{
  TRACEN(k_t_irqflow,"bgcol=%p", bgcol);

    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_REC_PRXEN, 0 );

}

static inline void bgcol_disable_interrupts_xmit(struct bg_col *bgcol)
{
  TRACEN(k_t_irqflow, "bgcol=%p", bgcol);

    mtdcrx( bgcol->dcrbase + _BGP_DCR_TR_INJ_PIXEN, _TR_INJ_PIX_ENABLE );
}
void bgcol_enable_rcv_wm_interrupt(struct bgcol_channel* chn)
{
    unsigned long flags;
    unsigned long prxen;

    spin_lock_irqsave(&chn->col->lock, flags);
    chn->irq_rcv_pending_mask = COL_IRQ_RCV_PENDING_MASK(chn->idx);
    prxen = mfdcrx(chn->col->dcrbase + _BGP_DCR_TR_REC_PRXEN);
    if (chn->idx)
	mtdcrx(chn->col->dcrbase + _BGP_DCR_TR_REC_PRXEN, prxen | _TR_REC_PRX_WM1);
    else
	mtdcrx(chn->col->dcrbase + _BGP_DCR_TR_REC_PRXEN, prxen | _TR_REC_PRX_WM0);
    spin_unlock_irqrestore(&chn->col->lock, flags);

    return;
}
static void inj_timeout(unsigned long colArg)
{
    printk(KERN_INFO "bgcol: inject fifo timed out!\n");
}

void bgcol_set_mtu(struct bg_col *bgcol, unsigned int mtu)
  {
    unsigned int max_packets_per_frame=(mtu+COL_FRAGPAYLOAD-1) / COL_FRAGPAYLOAD ;
    bgcol->max_packets_per_frame = max_packets_per_frame ;
    bgcol->mtu = max_packets_per_frame * COL_FRAGPAYLOAD + COL_SKB_ALIGN ;
  }

/*  Inject a 16-byte header and a COL_FRAGPAYLOAD-byte payload */
static inline void bgcol_payload_inject(void *port, void* first_quad, void *remaining_quads)
  {
/*     BUG_ON((((int)first_quad) & 0xf) != 0) ; */
/*     BUG_ON((((int)remaining_quads) & 0xf) != 0) ; */
    asm volatile(
                     "lfpdx   0,0,%[first_quad]        \n\t"  /* F0=Q0 load */
                     "li      3,16                    \n\t"  /* Indexing values */
                     "lfpdx   1,0,%[remaining_quads]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
                     "li      4,32                    \n\t"  /* Indexing values */
                     "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q2 load */
                     "li      3,48                    \n\t"  /* Indexing values */
                     "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q3 load */
                     "li      4,64                    \n\t"  /* Indexing values */
                     "stfpdx  0,0,%[port]        \n\t"  /* Q0 store to TR0_DI */
                     "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q4 load */
                     "li      3,80                    \n\t"  /* Indexing values */
                     "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q5 load */
                     "li      4,96                    \n\t"  /* Indexing values */
                     "lfpdx  6,3,%[remaining_quads]       \n\t"  /* F6=Q6 load */
                     "li      3,112                   \n\t"  /* Indexing values */
                     "stfpdx  1,0,%[port]        \n\t"  /* Q1 store */
                     "stfpdx  2,0,%[port]        \n\t"  /* Q2 store */
                     "stfpdx  3,0,%[port]        \n\t"  /* Q3 store */
                     "lfpdx  7,4,%[remaining_quads]       \n\t"  /* F7=Q7 load */
                     "li      4,128                    \n\t"  /* Indexing values */
                     "lfpdx  8,3,%[remaining_quads]       \n\t"  /* F8=Q8 load */
                     "li      3,144                    \n\t"  /* Indexing values */
                     "lfpdx  9,4,%[remaining_quads]       \n\t"  /* F9=Q9 load */
                     "li      4,160                    \n\t"  /* Indexing values */
                     "stfpdx  4,0,%[port]        \n\t"  /* Q4 store */
                     "stfpdx  5,0,%[port]        \n\t"  /* Q5 store */
                     "stfpdx  6,0,%[port]        \n\t"  /* Q6 store */
                     "lfpdx  0,3,%[remaining_quads]       \n\t"  /* F0=Q10 load */
                     "li      3,176                   \n\t"  /* Indexing values */
                     "lfpdx  1,4,%[remaining_quads]       \n\t"  /* F1=Q11 load */
                     "li      4,192                    \n\t"  /* Indexing values */
                     "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q12 load */
                     "li      3,208                   \n\t"  /* Indexing values */
                     "stfpdx  7,0,%[port]        \n\t"  /* Q7 store */
                     "stfpdx  8,0,%[port]        \n\t"  /* Q8 store */
                     "stfpdx  9,0,%[port]        \n\t"  /* Q9 store */
                     "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q13 load */
                     "li      4,224                    \n\t"  /* Indexing values */
                     "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q14 load */
                     "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q15 load */
                     "stfpdx  0,0,%[port]        \n\t"  /* Q10 store */
                     "stfpdx  1,0,%[port]        \n\t"  /* Q11 store */
                     "stfpdx  2,0,%[port]        \n\t"  /* Q12 store */
                     "stfpdx  3,0,%[port]        \n\t"  /* Q13 store */
                     "stfpdx  4,0,%[port]        \n\t"  /* Q14 store */
                     "stfpdx  5,0,%[port]        \n\t"  /* Q15 store */
                     :
                     : [first_quad]      "b" (first_quad) ,           /* Inputs */
                       [remaining_quads] "b" (remaining_quads),
                       [port]            "b" (port)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9",
                       "r3" , "r4"  );
  }
/*  Inject a 16-byte header and a COL_FRAGPAYLOAD-byte payload */
static inline void bgcol_payload_inject2(void *port, double* first_quad_0, double* first_quad_1, void *remaining_quads)
  {
/*     BUG_ON((((int)first_quad) & 0xf) != 0) ; */
/*     BUG_ON((((int)remaining_quads) & 0xf) != 0) ; */
    asm volatile(
                     "lfdx   0,0,%[first_quad_0]        \n\t"  /* F0=Q0 load */
                     "lfsdx   0,0,%[first_quad_1]        \n\t"  /* F0=Q0 load */
                     "li      3,16                    \n\t"  /* Indexing values */
                     "lfpdx   1,0,%[remaining_quads]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
                     "li      4,32                    \n\t"  /* Indexing values */
                     "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q2 load */
                     "li      3,48                    \n\t"  /* Indexing values */
                     "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q3 load */
                     "li      4,64                    \n\t"  /* Indexing values */
                     "stfpdx  0,0,%[port]        \n\t"  /* Q0 store to TR0_DI */
                     "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q4 load */
                     "li      3,80                    \n\t"  /* Indexing values */
                     "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q5 load */
                     "li      4,96                    \n\t"  /* Indexing values */
                     "lfpdx  6,3,%[remaining_quads]       \n\t"  /* F6=Q6 load */
                     "li      3,112                   \n\t"  /* Indexing values */
                     "stfpdx  1,0,%[port]        \n\t"  /* Q1 store */
                     "stfpdx  2,0,%[port]        \n\t"  /* Q2 store */
                     "stfpdx  3,0,%[port]        \n\t"  /* Q3 store */
                     "lfpdx  7,4,%[remaining_quads]       \n\t"  /* F7=Q7 load */
                     "li      4,128                    \n\t"  /* Indexing values */
                     "lfpdx  8,3,%[remaining_quads]       \n\t"  /* F8=Q8 load */
                     "li      3,144                    \n\t"  /* Indexing values */
                     "lfpdx  9,4,%[remaining_quads]       \n\t"  /* F9=Q9 load */
                     "li      4,160                    \n\t"  /* Indexing values */
                     "stfpdx  4,0,%[port]        \n\t"  /* Q4 store */
                     "stfpdx  5,0,%[port]        \n\t"  /* Q5 store */
                     "stfpdx  6,0,%[port]        \n\t"  /* Q6 store */
                     "lfpdx  0,3,%[remaining_quads]       \n\t"  /* F0=Q10 load */
                     "li      3,176                   \n\t"  /* Indexing values */
                     "lfpdx  1,4,%[remaining_quads]       \n\t"  /* F1=Q11 load */
                     "li      4,192                    \n\t"  /* Indexing values */
                     "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q12 load */
                     "li      3,208                   \n\t"  /* Indexing values */
                     "stfpdx  7,0,%[port]        \n\t"  /* Q7 store */
                     "stfpdx  8,0,%[port]        \n\t"  /* Q8 store */
                     "stfpdx  9,0,%[port]        \n\t"  /* Q9 store */
                     "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q13 load */
                     "li      4,224                    \n\t"  /* Indexing values */
                     "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q14 load */
                     "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q15 load */
                     "stfpdx  0,0,%[port]        \n\t"  /* Q10 store */
                     "stfpdx  1,0,%[port]        \n\t"  /* Q11 store */
                     "stfpdx  2,0,%[port]        \n\t"  /* Q12 store */
                     "stfpdx  3,0,%[port]        \n\t"  /* Q13 store */
                     "stfpdx  4,0,%[port]        \n\t"  /* Q14 store */
                     "stfpdx  5,0,%[port]        \n\t"  /* Q15 store */
                     :
                     : [first_quad_0]      "b" (first_quad_0) ,           /* Inputs */
                       [first_quad_1]      "b" (first_quad_1) ,           /* Inputs */
                       [remaining_quads] "b" (remaining_quads),
                       [port]            "b" (port)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9",
                       "r3" , "r4"  );
  }
/*  load a bgcol payload's worth from memory into registers */
static inline void bgcol_payload_inject_load(void* first_quad, void *remaining_quads)
  {
/*     BUG_ON((((int)first_quad) & 0xf) != 0) ; */
/*     BUG_ON((((int)remaining_quads) & 0xf) != 0) ; */
           asm volatile(
                     "lfpdx   0,0,%[first_quad]        \n\t"  /* F0=Q0 load */
                     "li      3,16                    \n\t"  /* Indexing values */
                     "lfpdx   1,0,%[remaining_quads]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
                     "li      4,32                    \n\t"  /* Indexing values */
                     "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q2 load */
                     "li      3,48                    \n\t"  /* Indexing values */
                     "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q3 load */
                     "li      4,64                    \n\t"  /* Indexing values */
                     "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q4 load */
                     "li      3,80                    \n\t"  /* Indexing values */
                     "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q5 load */
                     "li      4,96                    \n\t"  /* Indexing values */
                     "lfpdx  6,3,%[remaining_quads]       \n\t"  /* F6=Q6 load */
                     "li      3,112                   \n\t"  /* Indexing values */
                     "lfpdx  7,4,%[remaining_quads]       \n\t"  /* F7=Q7 load */
                     "li      4,128                    \n\t"  /* Indexing values */
                     "lfpdx  8,3,%[remaining_quads]       \n\t"  /* F8=Q8 load */
                     "li      3,144                    \n\t"  /* Indexing values */
                     "lfpdx  9,4,%[remaining_quads]       \n\t"  /* F9=Q9 load */
                     "li      4,160                    \n\t"  /* Indexing values */
                     "lfpdx  10,3,%[remaining_quads]       \n\t"  /* F0=Q10 load */
                     "li      3,176                   \n\t"  /* Indexing values */
                     "lfpdx  11,4,%[remaining_quads]       \n\t"  /* F1=Q11 load */
                     "li      4,192                    \n\t"  /* Indexing values */
                     "lfpdx  12,3,%[remaining_quads]       \n\t"  /* F2=Q12 load */
                     "li      3,208                   \n\t"  /* Indexing values */
                     "lfpdx  13,4,%[remaining_quads]       \n\t"  /* F3=Q13 load */
                     "li      4,224                    \n\t"  /* Indexing values */
                     "lfpdx  14,3,%[remaining_quads]       \n\t"  /* F4=Q14 load */
                     "lfpdx  15,4,%[remaining_quads]       \n\t"  /* F5=Q15 load */
                     :
                     : [first_quad]      "b" (first_quad) ,           /* Inputs */
                       [remaining_quads] "b" (remaining_quads)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15","r3" , "r4"  );
  }
static inline void bgcol_payload_inject_load2(double* first_quad_0, double* first_quad_1, void *remaining_quads)
  {
/*     BUG_ON((((int)first_quad) & 0xf) != 0) ; */
/*     BUG_ON((((int)remaining_quads) & 0xf) != 0) ; */
           asm volatile(
                     "lfdx   0,0,%[first_quad_0]        \n\t"  /* F0=Q0 load */
                     "lfsdx   0,0,%[first_quad_1]        \n\t"  /* F0=Q0 load */
                     "li      3,16                    \n\t"  /* Indexing values */
                     "lfpdx   1,0,%[remaining_quads]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
                     "li      4,32                    \n\t"  /* Indexing values */
                     "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q2 load */
                     "li      3,48                    \n\t"  /* Indexing values */
                     "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q3 load */
                     "li      4,64                    \n\t"  /* Indexing values */
                     "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q4 load */
                     "li      3,80                    \n\t"  /* Indexing values */
                     "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q5 load */
                     "li      4,96                    \n\t"  /* Indexing values */
                     "lfpdx  6,3,%[remaining_quads]       \n\t"  /* F6=Q6 load */
                     "li      3,112                   \n\t"  /* Indexing values */
                     "lfpdx  7,4,%[remaining_quads]       \n\t"  /* F7=Q7 load */
                     "li      4,128                    \n\t"  /* Indexing values */
                     "lfpdx  8,3,%[remaining_quads]       \n\t"  /* F8=Q8 load */
                     "li      3,144                    \n\t"  /* Indexing values */
                     "lfpdx  9,4,%[remaining_quads]       \n\t"  /* F9=Q9 load */
                     "li      4,160                    \n\t"  /* Indexing values */
                     "lfpdx  10,3,%[remaining_quads]       \n\t"  /* F0=Q10 load */
                     "li      3,176                   \n\t"  /* Indexing values */
                     "lfpdx  11,4,%[remaining_quads]       \n\t"  /* F1=Q11 load */
                     "li      4,192                    \n\t"  /* Indexing values */
                     "lfpdx  12,3,%[remaining_quads]       \n\t"  /* F2=Q12 load */
                     "li      3,208                   \n\t"  /* Indexing values */
                     "lfpdx  13,4,%[remaining_quads]       \n\t"  /* F3=Q13 load */
                     "li      4,224                    \n\t"  /* Indexing values */
                     "lfpdx  14,3,%[remaining_quads]       \n\t"  /* F4=Q14 load */
                     "lfpdx  15,4,%[remaining_quads]       \n\t"  /* F5=Q15 load */
                     :
                     : [first_quad_0]      "b" (first_quad_0) ,           /* Inputs */
                       [first_quad_1]      "b" (first_quad_1) ,           /* Inputs */
                       [remaining_quads] "b" (remaining_quads)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15","r3" , "r4"  );
  }
static inline void bgcol_payload_inject_load2partial(double* first_quad_0, double* first_quad_1, void *remaining_quads, int quadcount )
  {
/*     BUG_ON((((int)first_quad) & 0xf) != 0) ; */
/*     BUG_ON((((int)remaining_quads) & 0xf) != 0) ; */
           asm volatile(
                     "mtctr  %[quadcount]        \n\t"
                     "lfdx   0,0,%[first_quad_0]        \n\t"  /* F0=Q0 load */
                     "lfsdx   0,0,%[first_quad_1]        \n\t"  /* F0=Q0 load */
                     "li      3,16                    \n\t"  /* Indexing values */
                     "lfpdx   1,0,%[remaining_quads]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
                      "bdz    1                           \n\t"  /* Skip out if done */
                     "li      4,32                    \n\t"  /* Indexing values */
                     "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q2 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      3,48                    \n\t"  /* Indexing values */
                     "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q3 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      4,64                    \n\t"  /* Indexing values */
                     "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q4 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      3,80                    \n\t"  /* Indexing values */
                     "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q5 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      4,96                    \n\t"  /* Indexing values */
                     "lfpdx  6,3,%[remaining_quads]       \n\t"  /* F6=Q6 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      3,112                   \n\t"  /* Indexing values */
                     "lfpdx  7,4,%[remaining_quads]       \n\t"  /* F7=Q7 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      4,128                    \n\t"  /* Indexing values */
                     "lfpdx  8,3,%[remaining_quads]       \n\t"  /* F8=Q8 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      3,144                    \n\t"  /* Indexing values */
                     "lfpdx  9,4,%[remaining_quads]       \n\t"  /* F9=Q9 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      4,160                    \n\t"  /* Indexing values */
                     "lfpdx  10,3,%[remaining_quads]       \n\t"  /* F0=Q10 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      3,176                   \n\t"  /* Indexing values */
                     "lfpdx  11,4,%[remaining_quads]       \n\t"  /* F1=Q11 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      4,192                    \n\t"  /* Indexing values */
                     "lfpdx  12,3,%[remaining_quads]       \n\t"  /* F2=Q12 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      3,208                   \n\t"  /* Indexing values */
                     "lfpdx  13,4,%[remaining_quads]       \n\t"  /* F3=Q13 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "li      4,224                    \n\t"  /* Indexing values */
                     "lfpdx  14,3,%[remaining_quads]       \n\t"  /* F4=Q14 load */
                     "bdz    1                           \n\t"  /* Skip out if done */
                     "lfpdx  15,4,%[remaining_quads]       \n"  /* F5=Q15 load */
                     "1:                                   \n\t"  /* Jump-out label */
                     :
                     : [first_quad_0]      "b" (first_quad_0) ,           /* Inputs */
                       [first_quad_1]      "b" (first_quad_1) ,           /* Inputs */
                       [remaining_quads] "b" (remaining_quads) ,
                       [quadcount] "r" (quadcount)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15","r3" , "r4"  );
  }
static inline void bgcol_payload_inject_storeload(void *port, void* first_quad, void *remaining_quads)
  {
/*     BUG_ON((((int)first_quad) & 0xf) != 0) ; */
/*     BUG_ON((((int)remaining_quads) & 0xf) != 0) ; */
           asm volatile(
                     "stfpdx  0,0,%[port]        \n\t"  /* Q0 store to TR0_DI */
               "lfpdx   0,0,%[first_quad]        \n\t"  /* F0=Q0 load */
                     "stfpdx  1,0,%[port]        \n\t"  /* Q1 store */
               "li      3,16                    \n\t"  /* Indexing values */
               "lfpdx   1,0,%[remaining_quads]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
                     "stfpdx  2,0,%[port]        \n\t"  /* Q2 store */
               "li      4,32                    \n\t"  /* Indexing values */
               "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q2 load */
                     "stfpdx  3,0,%[port]        \n\t"  /* Q3 store */
               "li      3,48                    \n\t"  /* Indexing values */
               "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q3 load */
                     "stfpdx  4,0,%[port]        \n\t"  /* Q4 store */
               "li      4,64                    \n\t"  /* Indexing values */
               "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q4 load */
                     "stfpdx  5,0,%[port]        \n\t"  /* Q5 store */
               "li      3,80                    \n\t"  /* Indexing values */
               "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q5 load */
                     "stfpdx  6,0,%[port]        \n\t"  /* Q6 store */
               "li      4,96                    \n\t"  /* Indexing values */
               "lfpdx  6,3,%[remaining_quads]       \n\t"  /* F6=Q6 load */
                     "stfpdx  7,0,%[port]        \n\t"  /* Q7 store */
               "li      3,112                   \n\t"  /* Indexing values */
               "lfpdx  7,4,%[remaining_quads]       \n\t"  /* F7=Q7 load */
                     "stfpdx  8,0,%[port]        \n\t"  /* Q8 store */
               "li      4,128                    \n\t"  /* Indexing values */
               "lfpdx  8,3,%[remaining_quads]       \n\t"  /* F8=Q8 load */
                     "stfpdx  9,0,%[port]        \n\t"  /* Q9 store */
               "li      3,144                    \n\t"  /* Indexing values */
               "lfpdx  9,4,%[remaining_quads]       \n\t"  /* F9=Q9 load */
                     "stfpdx  10,0,%[port]        \n\t"  /* Q10 store */
               "li      4,160                    \n\t"  /* Indexing values */
               "lfpdx  10,3,%[remaining_quads]       \n\t"  /* F0=Q10 load */
                     "stfpdx  11,0,%[port]        \n\t"  /* Q11 store */
               "li      3,176                   \n\t"  /* Indexing values */
               "lfpdx  11,4,%[remaining_quads]       \n\t"  /* F1=Q11 load */
                     "stfpdx  12,0,%[port]        \n\t"  /* Q12 store */
               "li      4,192                    \n\t"  /* Indexing values */
               "lfpdx  12,3,%[remaining_quads]       \n\t"  /* F2=Q12 load */
                     "stfpdx  13,0,%[port]        \n\t"  /* Q13 store */
               "li      3,208                   \n\t"  /* Indexing values */
               "lfpdx  13,4,%[remaining_quads]       \n\t"  /* F3=Q13 load */
                    "stfpdx  14,0,%[port]        \n\t"  /* Q14 store */
               "li      4,224                    \n\t"  /* Indexing values */
               "lfpdx  14,3,%[remaining_quads]       \n\t"  /* F4=Q14 load */
                     "stfpdx  15,0,%[port]        \n\t"  /* Q15 store */
               "lfpdx  15,4,%[remaining_quads]       \n\t"  /* F5=Q15 load */
                    :
                     : [first_quad]      "b" (first_quad) ,           /* Inputs */
                       [remaining_quads] "b" (remaining_quads),
                       [port]            "b" (port)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15","r3" , "r4"  );
  }
static inline void bgcol_payload_inject_storeload2(void *port, double* first_quad_0, double* first_quad_1, void *remaining_quads)
  {
/*     BUG_ON((((int)first_quad) & 0xf) != 0) ; */
/*     BUG_ON((((int)remaining_quads) & 0xf) != 0) ; */
           asm volatile(
                     "stfpdx  0,0,%[port]        \n\t"  /* Q0 store to TR0_DI */
               "lfdx   0,0,%[first_quad_0]        \n\t"  /* F0=Q0 load */
               "lfsdx   0,0,%[first_quad_1]        \n\t"  /* F0=Q0 load */
                     "stfpdx  1,0,%[port]        \n\t"  /* Q1 store */
               "li      3,16                    \n\t"  /* Indexing values */
               "lfpdx   1,0,%[remaining_quads]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
                     "stfpdx  2,0,%[port]        \n\t"  /* Q2 store */
               "li      4,32                    \n\t"  /* Indexing values */
               "lfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q2 load */
                     "stfpdx  3,0,%[port]        \n\t"  /* Q3 store */
               "li      3,48                    \n\t"  /* Indexing values */
               "lfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q3 load */
                     "stfpdx  4,0,%[port]        \n\t"  /* Q4 store */
               "li      4,64                    \n\t"  /* Indexing values */
               "lfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q4 load */
                     "stfpdx  5,0,%[port]        \n\t"  /* Q5 store */
               "li      3,80                    \n\t"  /* Indexing values */
               "lfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q5 load */
                     "stfpdx  6,0,%[port]        \n\t"  /* Q6 store */
               "li      4,96                    \n\t"  /* Indexing values */
               "lfpdx  6,3,%[remaining_quads]       \n\t"  /* F6=Q6 load */
                     "stfpdx  7,0,%[port]        \n\t"  /* Q7 store */
               "li      3,112                   \n\t"  /* Indexing values */
               "lfpdx  7,4,%[remaining_quads]       \n\t"  /* F7=Q7 load */
                     "stfpdx  8,0,%[port]        \n\t"  /* Q8 store */
               "li      4,128                    \n\t"  /* Indexing values */
               "lfpdx  8,3,%[remaining_quads]       \n\t"  /* F8=Q8 load */
                     "stfpdx  9,0,%[port]        \n\t"  /* Q9 store */
               "li      3,144                    \n\t"  /* Indexing values */
               "lfpdx  9,4,%[remaining_quads]       \n\t"  /* F9=Q9 load */
                     "stfpdx  10,0,%[port]        \n\t"  /* Q10 store */
               "li      4,160                    \n\t"  /* Indexing values */
               "lfpdx  10,3,%[remaining_quads]       \n\t"  /* F0=Q10 load */
                     "stfpdx  11,0,%[port]        \n\t"  /* Q11 store */
               "li      3,176                   \n\t"  /* Indexing values */
               "lfpdx  11,4,%[remaining_quads]       \n\t"  /* F1=Q11 load */
                     "stfpdx  12,0,%[port]        \n\t"  /* Q12 store */
               "li      4,192                    \n\t"  /* Indexing values */
               "lfpdx  12,3,%[remaining_quads]       \n\t"  /* F2=Q12 load */
                     "stfpdx  13,0,%[port]        \n\t"  /* Q13 store */
               "li      3,208                   \n\t"  /* Indexing values */
               "lfpdx  13,4,%[remaining_quads]       \n\t"  /* F3=Q13 load */
                    "stfpdx  14,0,%[port]        \n\t"  /* Q14 store */
               "li      4,224                    \n\t"  /* Indexing values */
               "lfpdx  14,3,%[remaining_quads]       \n\t"  /* F4=Q14 load */
                     "stfpdx  15,0,%[port]        \n\t"  /* Q15 store */
               "lfpdx  15,4,%[remaining_quads]       \n\t"  /* F5=Q15 load */
                    :
                     : [first_quad_0]      "b" (first_quad_0) ,           /* Inputs */
                       [first_quad_1]      "b" (first_quad_1) ,           /* Inputs */
                       [remaining_quads] "b" (remaining_quads),
                       [port]            "b" (port)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15","r3" , "r4"  );
  }
static inline void bgcol_payload_inject_store(void *port)
  {
           asm volatile(
                     "stfpdx  0,0,%[port]        \n\t"  /* Q0 store to TR0_DI */
                     "stfpdx  1,0,%[port]        \n\t"  /* Q1 store */
                     "stfpdx  2,0,%[port]        \n\t"  /* Q2 store */
                     "stfpdx  3,0,%[port]        \n\t"  /* Q3 store */
                     "stfpdx  4,0,%[port]        \n\t"  /* Q4 store */
                     "stfpdx  5,0,%[port]        \n\t"  /* Q5 store */
                     "stfpdx  6,0,%[port]        \n\t"  /* Q6 store */
                     "stfpdx  7,0,%[port]        \n\t"  /* Q7 store */
                     "stfpdx  8,0,%[port]        \n\t"  /* Q8 store */
                     "stfpdx  9,0,%[port]        \n\t"  /* Q9 store */
                     "stfpdx  10,0,%[port]        \n\t"  /* Q10 store */
                     "stfpdx  11,0,%[port]        \n\t"  /* Q11 store */
                     "stfpdx  12,0,%[port]        \n\t"  /* Q12 store */
                     "stfpdx  13,0,%[port]        \n\t"  /* Q13 store */
                     "stfpdx  14,0,%[port]        \n\t"  /* Q14 store */
                     "stfpdx  15,0,%[port]        \n\t"  /* Q15 store */
                     :
                     : /* inputs */
                       [port]            "b" (port)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15"  );
  }

/*  receive a COL_FRAGPAYLOAD-byte payload */
static inline void bgcol_payload_receive240(void *port, void *remaining_quads)
  {
/*     BUG_ON((((int)remaining_quads) & 0xf) != 0) ; */
           asm volatile(
               "lfpdx  1,0,%[port]        \n\t"  /* Q1 store */
               "lfpdx  2,0,%[port]        \n\t"  /* Q2 store */
               "lfpdx  3,0,%[port]        \n\t"  /* Q3 store */
               "lfpdx  4,0,%[port]        \n\t"  /* Q4 store */
               "lfpdx  5,0,%[port]        \n\t"  /* Q5 store */
               "lfpdx  6,0,%[port]        \n\t"  /* Q6 store */
               "lfpdx  7,0,%[port]        \n\t"  /* Q7 store */
               "lfpdx  8,0,%[port]        \n\t"  /* Q8 store */
               "lfpdx  9,0,%[port]        \n\t"  /* Q9 store */
               "lfpdx  0,0,%[port]        \n\t"  /* Q10 store */
               "li      3,16                    \n\t"  /* Indexing values */
               "stfpdx   1,0,%[remaining_quads]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
               "li      4,32                    \n\t"  /* Indexing values */
               "stfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q2 load */
               "lfpdx  1,0,%[port]        \n\t"  /* Q11 store */
               "li      3,48                    \n\t"  /* Indexing values */
               "stfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q3 load */
               "lfpdx  2,0,%[port]        \n\t"  /* Q12 store */
               "li      4,64                    \n\t"  /* Indexing values */
               "stfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q4 load */
               "lfpdx  3,0,%[port]        \n\t"  /* Q13 store */
               "li      3,80                    \n\t"  /* Indexing values */
               "stfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q5 load */
               "lfpdx  4,0,%[port]        \n\t"  /* Q14 store */
               "li      4,96                    \n\t"  /* Indexing values */
               "stfpdx  6,3,%[remaining_quads]       \n\t"  /* F6=Q6 load */
               "lfpdx  5,0,%[port]        \n\t"  /* Q15 store */
               "li      3,112                   \n\t"  /* Indexing values */
               "stfpdx  7,4,%[remaining_quads]       \n\t"  /* F7=Q7 load */
               "li      4,128                    \n\t"  /* Indexing values */
               "stfpdx  8,3,%[remaining_quads]       \n\t"  /* F8=Q8 load */
               "li      3,144                    \n\t"  /* Indexing values */
               "stfpdx  9,4,%[remaining_quads]       \n\t"  /* F9=Q9 load */
               "li      4,160                    \n\t"  /* Indexing values */
               "stfpdx  0,3,%[remaining_quads]       \n\t"  /* F0=Q10 load */
               "li      3,176                   \n\t"  /* Indexing values */
               "stfpdx  1,4,%[remaining_quads]       \n\t"  /* F1=Q11 load */
               "li      4,192                    \n\t"  /* Indexing values */
               "stfpdx  2,3,%[remaining_quads]       \n\t"  /* F2=Q12 load */
               "li      3,208                   \n\t"  /* Indexing values */
               "stfpdx  3,4,%[remaining_quads]       \n\t"  /* F3=Q13 load */
               "li      4,224                    \n\t"  /* Indexing values */
               "stfpdx  4,3,%[remaining_quads]       \n\t"  /* F4=Q14 load */
               "stfpdx  5,4,%[remaining_quads]       \n\t"  /* F5=Q15 load */
                     :
                     :            /* Inputs */
                       [remaining_quads] "b" (remaining_quads),
                       [port]            "b" (port)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "r3" , "r4"  );
  }


/*  Load a full bgcol payload into 16 parallel floating point registers */
/*  Caution ... the compiler doesn't know that we want the regs later on */
static inline unsigned int bgcol_payload_load(
    void *port,  /*  The FIFO port */
    void *lnkhdr,  /*  Where to put the first 16 bytes of the payload */
    void *destport  /*  Which address to tap to ask for the next packet */
    )
  {
    unsigned int src_key ;
    unsigned int dummy ;
    struct { unsigned char c [16]  ; } *lnkhdrc =  lnkhdr ;
/*     BUG_ON((((int)lnkhdr) & 0xf) != 0) ; */

           asm  (
               "lfpdx  0,0,%[port]        \n\t"  /* lnkhdr */
               "lfpdx  1,0,%[port]        \n\t"  /* Q1 store */
               "lfpdx  2,0,%[port]        \n\t"  /* Q2 store */
               "lfpdx  3,0,%[port]        \n\t"  /* Q3 store */
               "lfpdx  4,0,%[port]        \n\t"  /* Q4 store */
               "lfpdx  5,0,%[port]        \n\t"  /* Q5 store */
               "lfpdx  6,0,%[port]        \n\t"  /* Q6 store */
               "stfpdx 0,0,%[lnkhdr]      \n\t"
               "lfpdx  7,0,%[port]        \n\t"  /* Q7 store */
               "lfpdx  8,0,%[port]        \n\t"  /* Q8 store */
               "lfpdx  9,0,%[port]        \n\t"  /* Q9 store */
               "lfpdx  10,0,%[port]        \n\t"  /* Q10 store */
               "lfpdx  11,0,%[port]        \n\t"  /* Q11 store */
               "lfpdx  12,0,%[port]        \n\t"  /* Q12 store */
               "lwz      %[src_key],4(%[lnkhdr])        \n\t"
               "lfpdx  13,0,%[port]        \n\t"  /* Q13 store */
               "lfpdx  14,0,%[port]        \n\t"  /* Q14 store */
               "lfpdx  15,0,%[port]        \n\t"  /* Q15 store */
               "lwz     %[dummy],0(%[destport])   \n\t"  /* trigger to pull the next packet in */
                     :          /* outputs */
                       [dummy] "=r" (dummy),
                       [src_key] "=b" (src_key),
                        "=m" (*lnkhdrc)
                     :            /* Inputs */
                       [port]            "b" (port) ,
                       [lnkhdr] "b" (lnkhdrc) ,
                       [destport] "b" (destport)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15"
                       );
           TRACEN(k_t_fifocontents, "bgcol_payload_load src_key=%08x",src_key) ;
    return src_key ;
  }

static inline unsigned int bgcol_payload_load2(
    void *port,  /*  The FIFO port */
    double *lnkhdr0,  /*  Where to put the first 8 bytes of the payload */
    double *lnkhdr1,  /*  Where to put the second 8 bytes of the payload */
    void *destport  /*  Which address to tap to ask for the next packet */
    )
  {
    unsigned int src_key ;
    unsigned int dummy ;
/*     BUG_ON((((int)lnkhdr0) & 0x07) != 0) ; */
/*     BUG_ON((((int)lnkhdr1) & 0x07) != 0) ; */

           asm  (
               "lfpdx  0,0,%[port]        \n\t"  /* lnkhdr */
               "lfpdx  1,0,%[port]        \n\t"  /* Q1 store */
               "lfpdx  2,0,%[port]        \n\t"  /* Q2 store */
               "lfpdx  3,0,%[port]        \n\t"  /* Q3 store */
               "lfpdx  4,0,%[port]        \n\t"  /* Q4 store */
               "lfpdx  5,0,%[port]        \n\t"  /* Q5 store */
               "lfpdx  6,0,%[port]        \n\t"  /* Q6 store */
               "stfdx 0,0,%[lnkhdr0]      \n\t"
               "lfpdx  7,0,%[port]        \n\t"  /* Q7 store */
               "stfsdx 0,0,%[lnkhdr1]      \n\t"
               "lfpdx  8,0,%[port]        \n\t"  /* Q8 store */
               "lfpdx  9,0,%[port]        \n\t"  /* Q9 store */
               "lfpdx  10,0,%[port]        \n\t"  /* Q10 store */
               "lfpdx  11,0,%[port]        \n\t"  /* Q11 store */
               "lfpdx  12,0,%[port]        \n\t"  /* Q12 store */
               "lwz      %[src_key],4(%[lnkhdr0])        \n\t"
               "lfpdx  13,0,%[port]        \n\t"  /* Q13 store */
               "lfpdx  14,0,%[port]        \n\t"  /* Q14 store */
               "lfpdx  15,0,%[port]        \n\t"  /* Q15 store */
               "lwz     %[dummy],0(%[destport])   \n\t"  /* trigger to pull the next packet in */
                     :          /* outputs */
                       [dummy] "=r" (dummy),
                       [src_key] "=b" (src_key),
                        "=m" (*lnkhdr0),
                        "=m" (*lnkhdr1)
                     :            /* Inputs */
                       [port]            "b" (port) ,
                       [lnkhdr0] "b" (lnkhdr0) ,
                       [lnkhdr1] "b" (lnkhdr1) ,
                       [destport] "b" (destport)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15"
                       );
           TRACEN(k_t_fifocontents, "bgcol_payload_load src_key=%08x",src_key) ;
    return src_key ;
  }

/*  Save the previous payload to store, and load the next payload from FIFO */
static inline unsigned int bgcol_payload_storeload(
    void *port,
    void *lnkhdr,
    void * payloadptr,
    void *destport )
  {
    unsigned int index1 ;
    unsigned int index2 ;
    unsigned int src_key ;
    struct { unsigned char c [COL_FRAGPAYLOAD] ; } *payload ;
    struct { unsigned char c [16]  ; } *lnkhdrc  ;
/*     BUG_ON((((int)lnkhdr) & 0xf) != 0) ; */
/*     BUG_ON((((int)payloadptr) & 0xf) != 0) ; */

    lnkhdrc =  lnkhdr ;

    payload = payloadptr;
    TRACEN(k_t_fifocontents, "bgcol_payload_storeload payload=%p",payloadptr) ;

           asm  (
               "lfpdx   0,0,%[port]        \n\t"  /* lnkhdr */
               "li      %[index1],16                    \n\t"  /* Indexing values */
               "stfpdx  1,0,%[payload]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
               "li      %[index2],32                    \n\t"  /* Indexing values */
               "lfpdx   1,0,%[port]        \n\t"  /* Q1 store */
               "stfpdx  2,%[index1],%[payload]       \n\t"  /* F2=Q2 load */
               "li      %[index1],48                    \n\t"  /* Indexing values */
               "lfpdx   2,0,%[port]        \n\t"  /* Q2 store */
               "stfpdx  3,%[index2],%[payload]       \n\t"  /* F3=Q3 load */
               "li      %[index2],64                    \n\t"  /* Indexing values */
               "lfpdx   3,0,%[port]        \n\t"  /* Q3 store */
               "stfpdx  4,%[index1],%[payload]       \n\t"  /* F4=Q4 load */
               "li      %[index1],80                    \n\t"  /* Indexing values */
               "lfpdx   4,0,%[port]        \n\t"  /* Q4 store */
               "stfpdx  5,%[index2],%[payload]       \n\t"  /* F5=Q5 load */
               "li      %[index2],96                    \n\t"  /* Indexing values */
               "lfpdx   5,0,%[port]        \n\t"  /* Q5 store */
               "stfpdx  6,%[index1],%[payload]       \n\t"  /* F6=Q6 load */
               "li      %[index1],112                   \n\t"  /* Indexing values */
               "lfpdx   6,0,%[port]        \n\t"  /* Q6 store */
               "stfpdx  7,%[index2],%[payload]       \n\t"  /* F7=Q7 load */
               "li      %[index2],128                    \n\t"  /* Indexing values */
               "lfpdx   7,0,%[port]        \n\t"  /* Q7 store */
               "stfpdx  8,%[index1],%[payload]       \n\t"  /* F8=Q8 load */
               "li      %[index1],144                    \n\t"  /* Indexing values */
               "lfpdx   8,0,%[port]        \n\t"  /* Q8 store */
               "stfpdx  0,0,%[lnkhdr]      \n\t"
               "stfpdx  9,%[index2],%[payload]       \n\t"  /* F9=Q9 load */
               "li      %[index2],160                    \n\t"  /* Indexing values */
               "lfpdx   9,0,%[port]        \n\t"  /* Q9 store */
               "stfpdx  10,%[index1],%[payload]       \n\t"  /* F0=Q10 load */
               "li      %[index1],176                   \n\t"  /* Indexing values */
               "lfpdx   10,0,%[port]        \n\t"  /* Q10 store */
               "stfpdx  11,%[index2],%[payload]       \n\t"  /* F1=Q11 load */
               "li      %[index2],192                    \n\t"  /* Indexing values */
               "lfpdx   11,0,%[port]        \n\t"  /* Q11 store */
               "stfpdx  12,%[index1],%[payload]       \n\t"  /* F2=Q12 load */
               "li      %[index1],208                   \n\t"  /* Indexing values */
               "lfpdx   12,0,%[port]        \n\t"  /* Q12 store */
               "stfpdx  13,%[index2],%[payload]       \n\t"  /* F3=Q13 load */
               "li      %[index2],224                    \n\t"  /* Indexing values */
               "lfpdx   13,0,%[port]        \n\t"  /* Q13 store */
               "lwz     %[src_key],4(%[lnkhdr])        \n\t"
               "stfpdx  14,%[index1],%[payload]       \n\t"  /* F4=Q14 load */
               "lfpdx   14,0,%[port]        \n\t"  /* Q14 store */
               "stfpdx  15,%[index2],%[payload]       \n\t"  /* F5=Q15 load */
               "lfpdx   15,0,%[port]        \n\t"  /* Q15 store */
               "lwz     %[index1],0(%[destport])   \n\t"  /* trigger to pull the next packet in */
                     :          /* outputs */
                       [src_key] "=b" (src_key),
                       "=m" (*payload),
                       "=m" (*lnkhdrc),
                       [index1] "=b" (index1),
                       [index2] "=b" (index2)
                     :            /* Inputs */
                       [port]            "b" (port) ,
                       [payload] "b" (payload),
                       [lnkhdr] "b" (lnkhdrc) ,
                       [destport] "b" (destport)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15"
                       );

           TRACEN(k_t_fifocontents, "bgcol_payload_storeload src_key=%08x",src_key) ;
    return src_key ;
  }

static inline unsigned int bgcol_payload_storeload2(
    void *port,
    double *lnkhdr0,
    double *lnkhdr1,
    void * payloadptr,
    void *destport )
  {
    unsigned int index1 ;
    unsigned int index2 ;
    unsigned int src_key ;
    struct { unsigned char c [COL_FRAGPAYLOAD] ; } *payload ;
     /*     BUG_ON((((int)lnkhdr0) & 0x07) != 0) ; */
     /*     BUG_ON((((int)lnkhdr1) & 0x07) != 0) ; */
/*     BUG_ON((((int)payloadptr) & 0xf) != 0) ; */


    payload = payloadptr;
    TRACEN(k_t_fifocontents, "bgcol_payload_storeload payload=%p",payloadptr) ;

           asm  (
               "lfpdx   0,0,%[port]        \n\t"  /* lnkhdr */
               "li      %[index1],16                    \n\t"  /* Indexing values */
               "stfpdx  1,0,%[payload]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
               "li      %[index2],32                    \n\t"  /* Indexing values */
               "lfpdx   1,0,%[port]        \n\t"  /* Q1 store */
               "stfpdx  2,%[index1],%[payload]       \n\t"  /* F2=Q2 load */
               "li      %[index1],48                    \n\t"  /* Indexing values */
               "lfpdx   2,0,%[port]        \n\t"  /* Q2 store */
               "stfpdx  3,%[index2],%[payload]       \n\t"  /* F3=Q3 load */
               "li      %[index2],64                    \n\t"  /* Indexing values */
               "lfpdx   3,0,%[port]        \n\t"  /* Q3 store */
               "stfpdx  4,%[index1],%[payload]       \n\t"  /* F4=Q4 load */
               "li      %[index1],80                    \n\t"  /* Indexing values */
               "lfpdx   4,0,%[port]        \n\t"  /* Q4 store */
               "stfpdx  5,%[index2],%[payload]       \n\t"  /* F5=Q5 load */
               "li      %[index2],96                    \n\t"  /* Indexing values */
               "lfpdx   5,0,%[port]        \n\t"  /* Q5 store */
               "stfpdx  6,%[index1],%[payload]       \n\t"  /* F6=Q6 load */
               "li      %[index1],112                   \n\t"  /* Indexing values */
               "lfpdx   6,0,%[port]        \n\t"  /* Q6 store */
               "stfpdx  7,%[index2],%[payload]       \n\t"  /* F7=Q7 load */
               "li      %[index2],128                    \n\t"  /* Indexing values */
               "lfpdx   7,0,%[port]        \n\t"  /* Q7 store */
               "stfpdx  8,%[index1],%[payload]       \n\t"  /* F8=Q8 load */
               "li      %[index1],144                    \n\t"  /* Indexing values */
               "lfpdx   8,0,%[port]        \n\t"  /* Q8 store */
               "stfdx  0,0,%[lnkhdr0]      \n\t"
               "stfpdx  9,%[index2],%[payload]       \n\t"  /* F9=Q9 load */
               "li      %[index2],160                    \n\t"  /* Indexing values */
               "lfpdx   9,0,%[port]        \n\t"  /* Q9 store */
               "stfsdx  0,0,%[lnkhdr1]      \n\t"
               "stfpdx  10,%[index1],%[payload]       \n\t"  /* F0=Q10 load */
               "li      %[index1],176                   \n\t"  /* Indexing values */
               "lfpdx   10,0,%[port]        \n\t"  /* Q10 store */
               "stfpdx  11,%[index2],%[payload]       \n\t"  /* F1=Q11 load */
               "li      %[index2],192                    \n\t"  /* Indexing values */
               "lfpdx   11,0,%[port]        \n\t"  /* Q11 store */
               "stfpdx  12,%[index1],%[payload]       \n\t"  /* F2=Q12 load */
               "li      %[index1],208                   \n\t"  /* Indexing values */
               "lfpdx   12,0,%[port]        \n\t"  /* Q12 store */
               "stfpdx  13,%[index2],%[payload]       \n\t"  /* F3=Q13 load */
               "li      %[index2],224                    \n\t"  /* Indexing values */
               "lfpdx   13,0,%[port]        \n\t"  /* Q13 store */
               "lwz     %[src_key],4(%[lnkhdr0])        \n\t"
               "stfpdx  14,%[index1],%[payload]       \n\t"  /* F4=Q14 load */
               "lfpdx   14,0,%[port]        \n\t"  /* Q14 store */
               "stfpdx  15,%[index2],%[payload]       \n\t"  /* F5=Q15 load */
               "lfpdx   15,0,%[port]        \n\t"  /* Q15 store */
               "lwz     %[index1],0(%[destport])   \n\t"  /* trigger to pull the next packet in */
                     :          /* outputs */
                       [src_key] "=b" (src_key),
                       "=m" (*payload),
                       "=m" (*lnkhdr0),
                       "=m" (*lnkhdr1),
                       [index1] "=b" (index1),
                       [index2] "=b" (index2)
                     :            /* Inputs */
                       [port]            "b" (port) ,
                       [payload] "b" (payload),
                       [lnkhdr0] "b" (lnkhdr0) ,
                       [lnkhdr1] "b" (lnkhdr1) ,
                       [destport] "b" (destport)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15"
                       );

           TRACEN(k_t_fifocontents, "bgcol_payload_storeload src_key=%08x",src_key) ;
    return src_key ;
  }

/*  Save the previous payload to store */
static inline void bgcol_payload_store(
    void * payloadptr)
  {
    unsigned int index1 ;
    unsigned int index2 ;
    struct { unsigned char c [COL_FRAGPAYLOAD] ; } *payload=payloadptr ;
/*     BUG_ON((((int)payloadptr) & 0xf) != 0) ; */

    TRACEN(k_t_fifocontents, "bgcol_payload_store payload=%p",payload) ;
           asm  (
               "li      %[index1],16                    \n\t"  /* Indexing values */
               "stfpdx  1,0,%[payload]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
               "li      %[index2],32                    \n\t"  /* Indexing values */
               "stfpdx  2,%[index1],%[payload]       \n\t"  /* F2=Q2 load */
               "li      %[index1],48                    \n\t"  /* Indexing values */
               "stfpdx  3,%[index2],%[payload]       \n\t"  /* F3=Q3 load */
               "li      %[index2],64                    \n\t"  /* Indexing values */
               "stfpdx  4,%[index1],%[payload]       \n\t"  /* F4=Q4 load */
               "li      %[index1],80                    \n\t"  /* Indexing values */
               "stfpdx  5,%[index2],%[payload]       \n\t"  /* F5=Q5 load */
               "li      %[index2],96                    \n\t"  /* Indexing values */
               "stfpdx  6,%[index1],%[payload]       \n\t"  /* F6=Q6 load */
               "li      %[index1],112                   \n\t"  /* Indexing values */
               "stfpdx  7,%[index2],%[payload]       \n\t"  /* F7=Q7 load */
               "li      %[index2],128                    \n\t"  /* Indexing values */
               "stfpdx  8,%[index1],%[payload]       \n\t"  /* F8=Q8 load */
               "li      %[index1],144                    \n\t"  /* Indexing values */
               "stfpdx  9,%[index2],%[payload]       \n\t"  /* F9=Q9 load */
               "li      %[index2],160                    \n\t"  /* Indexing values */
               "stfpdx  10,%[index1],%[payload]       \n\t"  /* F0=Q10 load */
               "li      %[index1],176                   \n\t"  /* Indexing values */
               "stfpdx  11,%[index2],%[payload]       \n\t"  /* F1=Q11 load */
               "li      %[index2],192                    \n\t"  /* Indexing values */
               "stfpdx  12,%[index1],%[payload]       \n\t"  /* F2=Q12 load */
               "li      %[index1],208                   \n\t"  /* Indexing values */
               "stfpdx  13,%[index2],%[payload]       \n\t"  /* F3=Q13 load */
               "li      %[index2],224                    \n\t"  /* Indexing values */
               "stfpdx  14,%[index1],%[payload]       \n\t"  /* F4=Q14 load */
               "stfpdx  15,%[index2],%[payload]       \n\t"  /* F5=Q15 load */
                     :          /* outputs */
                       "=m" (*payload),
                       [index1] "=b" (index1),
                       [index2] "=b" (index2)
                     :            /* Inputs */
                       [payload] "b" (payload)         /* inputs */
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15"
                       );
  }

/*  receive 256 bytes, a 16-byte header and a 240-byte payload */
/*  returns the 'source key', the key of the node which sent the data */

static inline int bgcol_payload_receive256(void *port,
    void *lnkhdr,
    unsigned char * payload_table[],
    unsigned int table_index_mask,
    void *destport )
  {
    int table_offset ;
    int src_key ;
    struct { unsigned char c [COL_FRAGPAYLOAD] ; } *payload ;
    struct { unsigned char c [16]  ; } *lnkhdrc =  lnkhdr ;

           asm  (
               "lfpdx  0,0,%[port]        \n\t"  /* lnkhdr */
               "lfpdx  1,0,%[port]        \n\t"  /* Q1 store */
               "lfpdx  2,0,%[port]        \n\t"  /* Q2 store */
               "lfpdx  3,0,%[port]        \n\t"  /* Q3 store */
               "lfpdx  4,0,%[port]        \n\t"  /* Q4 store */
               "lfpdx  5,0,%[port]        \n\t"  /* Q5 store */
               "lfpdx  6,0,%[port]        \n\t"  /* Q6 store */
               "stfpdx 0,0,%[lnkhdr]      \n\t"
               "lfpdx  7,0,%[port]        \n\t"  /* Q7 store */
               "lfpdx  8,0,%[port]        \n\t"  /* Q8 store */
               "lfpdx  9,0,%[port]        \n\t"  /* Q9 store */
               "lwz      %[src_key],4(%[lnkhdr])        \n\t"
               "lfpdx  10,0,%[port]        \n\t"  /* Q10 store */
               "lfpdx  11,0,%[port]        \n\t"  /* Q11 store */
               "lfpdx  12,0,%[port]        \n\t"  /* Q12 store */
               "and    3,%[src_key],%[table_index_mask] \n\t"
               "lfpdx  13,0,%[port]        \n\t"  /* Q13 store */
               "slwi   %[table_offset],3,2              \n\t"
               "lfpdx  14,0,%[port]        \n\t"  /* Q14 store */
               "lwzx     %[payload],%[table_offset],%[payload_table] \n\t"
               "lfpdx  15,0,%[port]        \n\t"  /* Q15 store */
               "lwz       5,0(%[destport])   \n\t"  /* trigger to pull the next packet in */
               "li      3,16                    \n\t"  /* Indexing values */
               "stfpdx  1,0,%[payload]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
               "li      4,32                    \n\t"  /* Indexing values */
               "stfpdx  2,3,%[payload]       \n\t"  /* F2=Q2 load */
               "li      3,48                    \n\t"  /* Indexing values */
               "stfpdx  3,4,%[payload]       \n\t"  /* F3=Q3 load */
               "li      4,64                    \n\t"  /* Indexing values */
               "stfpdx  4,3,%[payload]       \n\t"  /* F4=Q4 load */
               "li      3,80                    \n\t"  /* Indexing values */
               "stfpdx  5,4,%[payload]       \n\t"  /* F5=Q5 load */
               "li      4,96                    \n\t"  /* Indexing values */
               "stfpdx  6,3,%[payload]       \n\t"  /* F6=Q6 load */
               "li      3,112                   \n\t"  /* Indexing values */
               "stfpdx  7,4,%[payload]       \n\t"  /* F7=Q7 load */
               "li      4,128                    \n\t"  /* Indexing values */
               "stfpdx  8,3,%[payload]       \n\t"  /* F8=Q8 load */
               "li      3,144                    \n\t"  /* Indexing values */
               "stfpdx  9,4,%[payload]       \n\t"  /* F9=Q9 load */
               "li      4,160                    \n\t"  /* Indexing values */
               "stfpdx  10,3,%[payload]       \n\t"  /* F0=Q10 load */
               "li      3,176                   \n\t"  /* Indexing values */
               "stfpdx  11,4,%[payload]       \n\t"  /* F1=Q11 load */
               "li      4,192                    \n\t"  /* Indexing values */
               "stfpdx  12,3,%[payload]       \n\t"  /* F2=Q12 load */
               "li      3,208                   \n\t"  /* Indexing values */
               "stfpdx  13,4,%[payload]       \n\t"  /* F3=Q13 load */
               "li      4,224                    \n\t"  /* Indexing values */
               "stfpdx  14,3,%[payload]       \n\t"  /* F4=Q14 load */
               "stfpdx  15,4,%[payload]       \n\t"  /* F5=Q15 load */
                     : [payload] "=b" (payload),         /* outputs */
                       [src_key] "=b" (src_key),
                       [table_offset] "=b" (table_offset),
                       "=m" (*payload),
                       "=m" (*lnkhdrc)
                     :            /* Inputs */
                       [port]            "b" (port) ,
                       [lnkhdr] "b" (lnkhdrc) ,
                       [payload_table] "b" (payload_table) ,
                       [table_index_mask] "b" (table_index_mask) ,
                       [destport] "b" (destport)
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14",
                       "fr15",
                       "r3" , "r4", "r5"
                       );
           TRACEN(k_t_fifocontents, "bgcol_payload_receive256 table_offset=%08x payload=%p\n src_key=%08x",table_offset,payload,src_key) ;
    return src_key ;
  }
/**********************************************************************
 * Receive and transmit
 **********************************************************************/

/* #if defined(COLLECTIVE_DELIVER_VIA_TASKLET) */
/* static void bgcol_receive_proto_tasklet_handler(unsigned long dummy) */
/*   { */
/*     struct bg_col *bgcol = __bgcol; */
/*     struct sk_buff *skb = skb_dequeue(&bgcol->fragskb_list_rcv); */
/*  */
/*     TRACE("bgnet: (>)bgcol_receive_proto_tasklet_handler"); */
/*  */
/*     while( skb ) */
/*       { */
//         /*  deliver to upper protocol layers */
/*         struct bglink_hdr_col *lnkhdrp = (struct bglink_hdr_col *)&(skb->cb) ; */
/*         struct bglink_proto *proto; */
/*         proto = bgcol_find_linkproto(lnkhdrp->lnk_proto); */
/*         if (proto) */
/*           { */
/*  */
/*             TRACE("Handed to proto rcv=%p", proto->rcv) ; */
/*             TRACE("hdr: conn=%x, this_pkt=%x, tot_pkt=%x, dst=%x, src=%x proto=%x", lnkhdrp->conn_id, lnkhdrp->this_pkt, lnkhdrp->total_pkt, lnkhdrp->dst_key, lnkhdrp->src_key, lnkhdrp->lnk_proto); */
/*             dump_skb_partial(skb,64) ; */
/*             TRACE("proto->rcv=%p skb=%p lnkhdrp=%p proto=%p", */
/*                 proto->rcv,skb, lnkhdrp, proto */
/*                 ) ; */
/*             (void) proto->rcv(skb, lnkhdrp, proto); */
/*           } */
/*         else */
/*           { */
/*               dump_skb_partial(skb,64); */
/*               TRACE("bgcol: unsupported link protocol (%p) %x", proto, lnkhdrp->lnk_proto); */
/*               dev_kfree_skb(skb); */
/*           } */
/*         skb = skb_dequeue(&bgcol->fragskb_list_rcv) ; */
/*       } */
/*  */
/*     TRACE("bgnet: (<)bgcol_receive_proto_tasklet_handler"); */
/*  */
/*   } */
/*  */
/* static DECLARE_TASKLET(bgcol_receive_proto_tasklet,bgcol_receive_proto_tasklet_handler,0); */
/* #endif */

static inline void bgcol_vacate_slot(struct bg_col *bgcol, unsigned int slot)
  {
    bgcol->per_eth_table[slot].payload = (void *)0xffffffff ;  /*  so we get a trap if we try to store through it */
    bgcol->per_eth_table[slot].expect = 0xffffffff ;
    bgcol->skb_rcv_table[slot] = NULL ;
    TRACEN(k_t_general,"Slot %d vacated",slot );
  }


static void init_ethkey_table(struct bg_col *bgcol)
  {
  int x ;
  for( x = 0 ; x < k_ethkey_table_size ; x += 1)
    {
      bgcol_vacate_slot(bgcol,x) ;
    }
  }

#if defined(KEEP_LNKHDR_TRAIL)
static struct bglink_hdr_col lnkhdr_trail[k_lnkhdr_trail_length] ;
static unsigned int lnkhdr_trail_index ;
static unsigned int lnkhdr_trail_shown_index ;
static int trail_shown_count ;

static void record_lnkhdr_trail(struct bglink_hdr_col *lnkhdr)
  {
    lnkhdr_trail[lnkhdr_trail_index & (k_lnkhdr_trail_length-1)] = *lnkhdr ;
    lnkhdr_trail_index += 1 ;
  }

static void show_lnkhdr_trail(const char * reason)
  {
    if( trail_shown_count < k_lnhhdr_ffdc_limit )
      {
        unsigned int trail_count = (k_lnkhdr_trail_display_length > lnkhdr_trail_index) ? lnkhdr_trail_index : k_lnkhdr_trail_display_length ;
        unsigned int current_index = lnkhdr_trail_index - trail_count ;
        printk(KERN_INFO "lnkhdr trail to packet %d, reason <%s>:\n", lnkhdr_trail_index, reason) ;
        while( current_index != lnkhdr_trail_index)
          {
            unsigned int x = ( current_index & (k_lnkhdr_trail_length-1)) ;
            if( current_index >= lnkhdr_trail_shown_index )
		    {
		    printk(KERN_INFO "lnkhdr_trail[%02x] dst_key=%08x src_key=%08x conn_id=%04x this_pkt=%02x total_pkt=%02x lnk_proto=%04x opt=[%02x:%02x:%02x]\n",
			(current_index-lnkhdr_trail_index) & 0xff,
			lnkhdr_trail[x].dst_key,
			lnkhdr_trail[x].src_key,
			lnkhdr_trail[x].conn_id,
			lnkhdr_trail[x].this_pkt,
			lnkhdr_trail[x].total_pkt,
			lnkhdr_trail[x].lnk_proto,
			lnkhdr_trail[x].opt.opt_net.option,
			lnkhdr_trail[x].opt.opt_net.pad_head,
			lnkhdr_trail[x].opt.opt_net.pad_tail
		    ) ;
		    }
            current_index += 1 ;

          }
        trail_shown_count += 1 ;
        lnkhdr_trail_shown_index = lnkhdr_trail_index ;
      }
  }

static void show_payload(void * payload, unsigned int mioaddr)
{
	    if( trail_shown_count < k_lnhhdr_ffdc_limit )
		    {
          unsigned int *pi=(unsigned int *) payload ;
          unsigned int x ;
          for(x=0; x<240/sizeof(unsigned int)-9; x+=8)
            {
              printk(KERN_INFO "payload [%08x %08x %08x %08x %08x %08x %08x %08x]\n",
                  pi[x],pi[x+1],pi[x+2],pi[x+3],pi[x+4],pi[x+5],pi[x+6],pi[x+7]
                                                                           ) ;
            } ;
          printk(KERN_INFO "payload [%08x %08x %08x %08x]\n",
              pi[x],pi[x+1],pi[x+2],pi[x+3]
                                       ) ;
		    }
}
#else
static inline void record_lnkhdr_trail(struct bglink_hdr_col *lnkhdr)
  {

  }
static inline void show_lnkhdr_trail(const char * reason)
  {
    TRACE("%s", reason);
  }
static void show_payload(void * payload, unsigned int mioaddr)
{
	    TRACE("payload=%p mioaddr=0x%08x", payload, mioaddr);
}

#endif

#if !defined(COLLECTIVE_DELIVER_VIA_TASKLET)
static inline void bgcol_deliver_directly(struct bg_col *bgcol,struct bglink_hdr_col *lnkhdr, struct sk_buff *skb)
  {
	    struct bglink_proto *proto;

	     /*  deliver to upper protocol layers */
	    proto = bglink_find_proto(lnkhdr->lnk_proto);
	  if(!bgcol->deliver_without_workqueue)
		  {
			  TRACEN(k_t_general,"Delivering skb=%p via work queue",skb) ;
			  bgcol_deliver_via_workqueue(skb, lnkhdr,proto) ;

		  }
	  else
		  {
		    if (proto)
		      {
			TRACE("Handed to proto=%p", proto) ;
			TRACE("hdr: conn=%x, this_pkt=%x, tot_pkt=%x, dst=%x, src=%x proto=%x", lnkhdr->conn_id, lnkhdr->this_pkt, lnkhdr->total_pkt, lnkhdr->dst_key, lnkhdr->src_key, lnkhdr->lnk_proto);
			dump_skb_partial(skb,64) ;
			TRACE("proto->col_rcv=%p skb=%p lnkhdr=%p proto=%p",
			    proto->col_rcv,skb, lnkhdr, proto
			    ) ;
			(void) proto->col_rcv(bgcol,skb, lnkhdr, proto);
		/*         enable_kernel_fp() ; */
		      }
		    else
		      {
			  dump_skb_partial(skb,64);
			  TRACE("bgcol: unsupported link protocol (%p) %x", proto, lnkhdr->lnk_proto);
			  dev_kfree_skb(skb);
		      }
		    replenish_list_for_filling(bgcol) ;
		  }
  }
#endif

static char scratch_payload[COL_FRAGPAYLOAD] __attribute__((aligned(16)));
static inline int bgcol_receive_mark3(struct bg_col *bgcol, unsigned channel,unsigned int status_in, unsigned int mioaddr)
{
    void *payloadptr;
/*     union bgcol_status status; */
    unsigned int unload_count ;
    unsigned int unload_index ;
    struct bglink_hdr_col lnkhdr __attribute__((aligned(8)));
    double *lnkhdrd = (double *)&lnkhdr ;
    unsigned int total_unload_count = 0 ;
    unsigned int end_frame_hint = 0 ;
#if defined(KEEP_RECV_TOTAL)
    unsigned int recv_total = bgcol->recv_total ;
#endif
/*     bgcol->recv_total += total_unload_count ; */

/*     status.raw = status_in ; */
/*     unload_count = status.x.rcv_hdr ; */
    unload_count = bgcol_status_rcv_hdr(status_in) ;
/*     bgcol->recv_fifo_histogram2[unload_count & 0x0f ] += 1; */
    TRACE("status=%08x", status_in);

#if defined(KEEP_RECV_TOTAL)
    bgcol->recv_total = recv_total + unload_count ;  /*  Not exact, for the case where we exit the loop early, but good enought for statistics */
#endif
#if defined(COLLECTIVE_ONEPASS_TXRX)
    if(unload_count > 0)
#else
    while(unload_count > 0)
#endif
      {
        unsigned int received_src_key ;
        unsigned int slot ;
        unsigned int received_seq ;
        unsigned int expected_seq ;

        unsigned int seq_next_packet ;
        unsigned int seq_tot_packet ;
        unsigned char * deposited_payload  ;
        unsigned char * next_payload  ;
        unsigned int received ;
        unsigned int expected ;

        /* Load up the FP regs with the first packet from the FIFO, and get ready to analyze it */
        received_src_key=bgcol_payload_load2((void*)mioaddr + _BGP_TRx_DR,lnkhdrd,lnkhdrd+1,(void*)(mioaddr + _BGP_TRx_HR)) ;
#if defined(KEEP_LNKHDR_TRAIL)
        record_lnkhdr_trail(&lnkhdr) ;
#endif
        slot = received_src_key & (k_ethkey_table_size-1) ;
        received = ((unsigned int *)&lnkhdr)[2] ;
        expected = bgcol->per_eth_table[slot].expect ;
        /* Find if it was an 'expected' packet in context of previous packets from this source */
        received_seq = ( received >> 8 ) & 0xff ;
        expected_seq = ( expected >> 8 ) & 0xff ;
        seq_tot_packet = expected & 0xff ;
        seq_next_packet = expected_seq + 1 ;

        bgcol->per_eth_table[slot].expect = expected + 0x0100 ;

        deposited_payload = bgcol->per_eth_table[slot].payload ;
        next_payload = deposited_payload + COL_FRAGPAYLOAD ;

        TRACEN(k_t_detail,"slot=%08x seq(%x,%x) re(%08x,%08x)",
             slot,
             received_seq, expected_seq,
             received, expected
             ) ;

          if( ( received == expected ) && (seq_next_packet < seq_tot_packet) )
          {
            bgcol->per_eth_table[slot].payload = next_payload ;
            for(unload_index=1;unload_index<unload_count;unload_index+=1)
            {
               /*  This is the busiest loop. Keep it simple .... */
               /*  save the payload to store, and load up the next one */
              received_src_key=bgcol_payload_storeload2(
                  (void*)mioaddr + _BGP_TRx_DR,
                  lnkhdrd,
                  lnkhdrd+1,
                  deposited_payload,
                  (void*)(mioaddr + _BGP_TRx_HR)) ;
#if defined(KEEP_LNKHDR_TRAIL)
              record_lnkhdr_trail(&lnkhdr) ;
#endif
              slot = received_src_key & (k_ethkey_table_size-1) ;
              received = ((unsigned int *)&lnkhdr)[2] ;
              expected = bgcol->per_eth_table[slot].expect ;
              /* Find if it was an 'expected' packet in context of previous packets from this source */
              expected_seq = ( expected >> 8 ) & 0xff ;
              seq_tot_packet = expected & 0xff ;
              deposited_payload = bgcol->per_eth_table[slot].payload ;

              seq_next_packet = expected_seq + 1 ;


              next_payload = deposited_payload + COL_FRAGPAYLOAD ;

              TRACEN(k_t_detail,"slot=%08x seq(%x,%x) re(%08x,%08x)",
                   slot,
                   received_seq, expected_seq,
                   received, expected
                   ) ;
              if( received != expected ) break ;
              bgcol->per_eth_table[slot].payload = next_payload ;
              bgcol->per_eth_table[slot].expect = expected + 0x0100 ;
              if( seq_next_packet >= seq_tot_packet ) break ;
            }
            total_unload_count += unload_index ;
          }
        else
          {
            total_unload_count += 1 ;
          }

        TRACE("slot=%08x seq(%x,%x) re(%08x,%08x)",
             slot,
             received_seq, expected_seq,
             received, expected
             ) ;

/* We have registers loaded, and we have exited the busy loop for one of a number of reasons
 * 1) This is the last packet of a frame
 * 2) We have unloaded everything that the status word said was in the FIFO
 * 3) This packet doesn't continue the previous frame from this source properly
 *   a) This is the first packet of a frame, and there was no frame in progress
 *   b) The sender has sent packets in a sequence that we do not understand
 *
 * Diagnose which, and handle appropriately
 */
        end_frame_hint = 0 ;
        if( received == expected && ((unsigned int)deposited_payload) != 0xffffffff )
          {
             /*  Things are going well, put the payload down into memory, and work out what to do with it */
            TRACE("Putting payload down at %p", deposited_payload);

            bgcol_payload_store(deposited_payload) ;
            if( seq_next_packet >= seq_tot_packet)
              {
                 /*  Frame is complete. Deliver it up a layer */
                struct sk_buff *skb = bgcol->skb_rcv_table[slot] ;
                if( seq_next_packet > seq_tot_packet)
			{
				TRACEN(k_t_request,"(!!!) seq_next_packet=%d seq_tot_packet=%d",
						seq_next_packet,	seq_tot_packet) ;
			}
/*                 BUG_ON(seq_next_packet > seq_tot_packet) ; // we think we checked this as we went along; firewall report here */
                TRACEN(k_t_general,"Frame is complete");
    #if defined(COLLECTIVE_DELIVER_VIA_TASKLET)
                skb_queue_tail(&bgcol->fragskb_list_rcv, skb) ;
                TRACEN(k_t_general,"scheduling proto tasklet");
                tasklet_schedule(&bgcol_receive_proto_tasklet);
    #else
                bgcol_deliver_directly(bgcol,&lnkhdr, skb) ;
    #endif
                 /*  and tag the slot as vacant */
                bgcol_vacate_slot(bgcol,slot) ;
                 /*  'break' here should cause the interrupt handler to return */
                 /*  this CPU can then deliver the frame and the next CPU can take up */
                 /*  draining the bgcol */
#if defined(COLLECTIVE_BREAK_ON_FRAME)
                break ;
#endif
                end_frame_hint = 1 ;
              }
            }
        else
          {
            if( received == expected )
		    {
			     /*  Packet looked good, but destination address was 0xffffffff. Diagnose it ... */
			TRACEN(k_t_protocol,"Unexpected dest address 0xffffffff, received=0x%08x", received) ;
                        TRACEN(k_t_protocol,"slot=%d hdr: conn=%x, this_pkt=%x, tot_pkt=%x, dst=%x, src=%x", slot, lnkhdr.conn_id, lnkhdr.this_pkt, lnkhdr.total_pkt, lnkhdr.dst_key, lnkhdr.src_key);
		    }
             /*  The packet wasn't in sequence with previous packets from the source. Look to see if we can handle it */
            if( 0 == lnkhdr.this_pkt )
              {
                if ( lnkhdr.total_pkt * COL_FRAGPAYLOAD + COL_SKB_ALIGN <= bgcol->mtu)
                  {
                    if( 1 == lnkhdr.total_pkt )
                      {
                        struct sk_buff *skb = bgcol->skb_mini ;
                         /*  We have a single-packet frame. Use 'skb_mini' and send it on */
                        if( skb )
                          {
                            skb_reserve(skb, COL_SKB_ALIGN - ((unsigned int)(skb->data)) % COL_SKB_ALIGN);
                            payloadptr = skb_put(skb, COL_FRAGPAYLOAD);
                            TRACE("Putting payload in mini slot at %p", payloadptr);
                            bgcol_payload_store(payloadptr) ;
                #if defined(COLLECTIVE_DELIVER_VIA_TASKLET)
                            kept_lnkhdrp = (struct bglink_hdr_col *)(&(skb->cb)) ;
                            *kept_lnkhdrp = lnkhdr ;
                            skb_queue_tail(&bgcol->fragskb_list_rcv, skb) ;
                            TRACE("scheduling proto tasklet");
                            tasklet_schedule(&bgcol_receive_proto_tasklet);
                #else
                            bgcol_deliver_directly(bgcol,&lnkhdr, skb) ;
                #endif
                          }
/*                         bgcol->skb_mini = alloc_skb(COL_FRAGPAYLOAD + COL_SKB_ALIGN , GFP_KERNEL | GFP_ATOMIC ) ; */
                        bgcol->skb_mini = take_skb_from_list_for_filling(bgcol) ;
                        end_frame_hint = 1 ;
                         /*  If there was a partial frame in the underneath skbuff, it can be left for */
                         /*  completion later. This doesn't seem likely; but the receive logic will work for it. */
                      }
                    else
                      {
                             /*  Put the payload down at the beginning of the skb we had up our sleeve */
                            struct sk_buff *skb = bgcol->skb_in_waiting ;
                            if( skb && (skb_tailroom(skb) >= lnkhdr.total_pkt * COL_FRAGPAYLOAD + COL_SKB_ALIGN ) )
                              {
                                struct bglink_hdr_col *kept_lnkhdrp ;
                                int size = lnkhdr.total_pkt * COL_FRAGPAYLOAD ;
                                skb_reserve(skb, COL_SKB_ALIGN - ((unsigned int)(skb->data)) % COL_SKB_ALIGN);
                                payloadptr = skb_put(skb, size);
                                kept_lnkhdrp = (struct bglink_hdr_col *)(&(skb->cb)) ;
                                *kept_lnkhdrp = lnkhdr ;
                                TRACE("Putting payload in waiting slot at %p", payloadptr);
                                bgcol_payload_store(payloadptr) ;
                              }
                            else
                              {
                                if( skb ) dev_kfree_skb(skb) ;  /*  Maybe someone upped the MTU on us */
                                skb = NULL ;
                              }
/*                             bgcol->skb_in_waiting = alloc_skb( */
/*                         		    k_use_plentiful_skb ? k_plentiful_skb_size :  bgcol->mtu */
//                        		    , GFP_KERNEL | GFP_ATOMIC);  /*  And grab a new one */
                            bgcol->skb_in_waiting = take_skb_from_list_for_filling(bgcol) ;
                            if( skb )
                              {
                             /*  If there's a part-arrived frame, trample it */
                            if( bgcol->skb_rcv_table[slot] )
                              {
                                TRACEN(k_t_protocol,"Dropping previous partial frame");
                                TRACEN(k_t_protocol,"slot=%d hdr: conn=%x, this_pkt=%x, tot_pkt=%x, dst=%x, src=%x", slot, lnkhdr.conn_id, lnkhdr.this_pkt, lnkhdr.total_pkt, lnkhdr.dst_key, lnkhdr.src_key);
                                TRACEN(k_t_protocol,"expected slot=%d re=(%08x,%08x)", slot, received, expected);
                                show_lnkhdr_trail("partial frame") ;
                                {
                                  struct bgnet_dev *bgnet = bgcol->bgnet ;
                                  bgnet->stats.rx_errors += 1;
                                  bgnet->stats.rx_missed_errors += 1;
                                }

                                dev_kfree_skb(bgcol->skb_rcv_table[slot]) ;
                              }



                              /*  Set things up for the fast loop */
                             bgcol->skb_rcv_table[slot]=skb ;
                             bgcol->per_eth_table[slot].payload = payloadptr+COL_FRAGPAYLOAD ;
                             bgcol->per_eth_table[slot].expect = (lnkhdr.conn_id << 16) | (1 << 8) | (lnkhdr.total_pkt) ;
                             TRACE("Saved first packet of new frame, next bgcol->per_eth_table[%d]={%p,%08x}", slot, bgcol->per_eth_table[slot].payload,bgcol->per_eth_table[slot].expect);

                           }

                    else
                      {
                        TRACEN(k_t_protocol,"No skbuff memory available, dropping packet");
                        TRACEN(k_t_protocol,"slot=%d hdr: conn=%x, this_pkt=%x, tot_pkt=%x, dst=%x, src=%x", slot, lnkhdr.conn_id, lnkhdr.this_pkt, lnkhdr.total_pkt, lnkhdr.dst_key, lnkhdr.src_key);
                        bgcol->recv_no_skbuff += 1 ;
                        bgcol->bgnet->stats.rx_dropped += 1;
                        bgcol->bgnet->stats.rx_errors += 1;
                      }
                  }
                  }
                else
                  {
                    bgcol_payload_store(scratch_payload) ;
                    TRACEN(k_t_protocol,"Frame larger than MTU, dropping");
                    show_lnkhdr_trail("Frame larger than MTU") ;
                    show_payload(scratch_payload,mioaddr) ;
                    bgcol->bgnet->stats.rx_errors += 1;
                    bgcol->bgnet->stats.rx_over_errors += 1;
                  }
              }

            else
              {
                 /*  Unexpected mid-frame packet */
                bgcol_payload_store(scratch_payload) ;
                TRACEN(k_t_protocol,"Unexpected packet from middle of frame, dropping");
                show_lnkhdr_trail("Unexpected packet from middle of frame") ;
                show_payload(scratch_payload,mioaddr) ;
                bgcol->bgnet->stats.rx_errors += 1;
                bgcol->bgnet->stats.rx_fifo_errors += 1;
              }
              }


         /*  We have handled the reason why the 'fast loop' dropped out. Refresh the status */
#if !defined(COLLECTIVE_ONEPASS_TXRX)
           /*  and redrive the 'fast loop' if there is anything in the fifo. */
/*           status.raw = in_be32_nosync((unsigned*)(mioaddr + _BGP_TRx_Sx)); */
/*           unload_count = status.x.rcv_hdr ; */
          unload_count = bgcol_status_rcv_hdr(*(unsigned*)(mioaddr + _BGP_TRx_Sx)) ;
/*           bgcol->recv_fifo_histogram3[unload_count & 0x0f ] += 1; */
#endif
      }
/*     bgcol->recv_total += total_unload_count ; */
/*  Return the number of packets we unloaded, and set the high bit if we have */
/*  reason to think there's nothing coming in any time soon */
    return total_unload_count
         | ( ( end_frame_hint && (unload_count == total_unload_count ) )
              ? 0x80000000 : 0
           ) ;

}



/*  Attempting to free skbuffs in an interrupt handler doesn't work well, some 'destructor' callbacks */
/*  protest if they are driven at interrupt level. So we queue them to be freed later. */
#ifndef COLLECTIVE_TRANSMIT_WITH_SLIH
static void bgcol_completed_buffer_handler(unsigned long dummy)
  {
    struct bg_col* bgcol=__bgcol ;
    TRACE("(>)[%s:%d]",__func__, __LINE__) ;
     /*  Free any skbufs the transmit interrupt handler has finished with */
      {
        struct sk_buff *freeskb = skb_dequeue(&(bgcol->skb_list_free) ) ;
        while (freeskb)
          {
            TRACEN(k_t_irqflow,"Freeing skb=%p", freeskb) ;
            dump_skb_partial(freeskb,64) ;
            dev_kfree_skb(freeskb) ;
            freeskb = skb_dequeue(&(bgcol->skb_list_free) ) ;
          }
      }
    TRACE("(<)[%s:%d]",__func__, __LINE__) ;
  }
static DECLARE_TASKLET(bgcol_completed_buffer_tasklet,bgcol_completed_buffer_handler,0) ;
#endif

/* static char local_payload[COL_FRAGPAYLOAD] __attribute__((aligned(16))) ; */
static void bgcol_xmit_next_skb(struct bg_col* bgcol)
  {
	  if(! skb_queue_empty(&(bgcol->skb_list_xmit)))
		  {
			    struct sk_buff *skb = skb_dequeue(&(bgcol->skb_list_xmit) ) ;
			    struct bgnet_dev *bgnet = bgcol->bgnet ;
			    unsigned int i_am_compute_node = (bgnet->bgcol_vector ^ bgnet->eth_bridge_vector) & 0x00ffffff ;
			    TRACE("bgcol_xmit_next_skb bgcol_vector=0x%08x eth_bridge_vector=0x%08x i_am_compute_node=%08x",
				bgnet->bgcol_vector,bgnet->eth_bridge_vector,i_am_compute_node
				) ;
			    bgcol->skb_current_xmit=skb ;
			    if( skb )
			      {
				unsigned long offset;
				union bgcol_header dest ;
				struct ethhdr *eth = (struct ethhdr *)skb->data;
				 /*  Work out what bgcol header to use for the new skb */

				TRACEN(k_t_irqflow,"%s: skb=%p, eth=%p, bgnet=%p, len=%d", __FUNCTION__, skb, eth, bgnet, skb->len);
				dump_skb_partial(skb, 64) ;
				dest.raw = 0 ;
				dest.p2p.pclass = bgnet->bgcol_route;

				if (is_broadcast_ether_addr(eth->h_dest)) {
					     /*  May have to go to the IO node for broadcasting */
					    if(0 == i_am_compute_node)
					      {
						TRACE("broadcasting from IO node") ;
						dest.bcast.tag = 0;
						bgcol->lnkhdr_xmit.lnk_proto = bgnet->bgcol_protocol;
					      }
					    else
					      {
						TRACE("sending to IO node for broadcast") ;
						dest.p2p.vector = bgnet->eth_bridge_vector;
						dest.p2p.p2p = 1;
						bgcol->lnkhdr_xmit.lnk_proto = bgnet->bgcol_reflector_protocol;
					      }
				} else {
				      TRACE("bgcol_xmit_next_skb bgnet->bgcol_vector=%08x bgnet->eth_bridge_vector=%08x",bgnet->bgcol_vector,bgnet->eth_bridge_vector) ;
				      if (bgnet->eth_mask == 0 ||
					  ((bgnet->eth_mask & *(unsigned int *)(&eth->h_dest[0])) ==
					   (bgnet->eth_local))) {
					     if(0 == i_am_compute_node)
					       {
						 TRACE("sending to compute node") ;
						 dest.p2p.vector = *(unsigned int *)(&eth->h_dest[2]);
						 bgcol->lnkhdr_xmit.lnk_proto = bgnet->bgcol_protocol;
					       }
					     else
					       {
						 dest.p2p.vector = bgnet->eth_bridge_vector;
						 if(( bgnet->eth_bridge_vector ^ (*(unsigned int *)(&eth->h_dest[2]))) & 0x00ffffff)
						   {
						     TRACE("sending to IO node for reflection") ;
						     bgcol->lnkhdr_xmit.lnk_proto = bgnet->bgcol_reflector_protocol;
						   }
						 else
						   {
						     TRACE("sending to IO node as final destination") ;
						     bgcol->lnkhdr_xmit.lnk_proto = bgnet->bgcol_protocol;
						   }
					       }
				      } else {
					  TRACE("sending to IO node for onward transmission") ;
					  dest.p2p.vector = bgnet->eth_bridge_vector;
					  bgcol->lnkhdr_xmit.lnk_proto = bgnet->bgcol_protocol;
				      }
				    dest.p2p.p2p = 1;
				}

				/* initialize link layer */
				bgcol->lnkhdr_xmit.dst_key = eth_to_key(eth->h_dest);
				bgcol->lnkhdr_xmit.src_key = bgnet->bgcol_vector;

				/* pad out head of packet so it starts at a 16 Byte boundary */
				offset = ((unsigned long)skb->data) & 0xf;
				bgcol->lnkhdr_xmit.opt.opt_net.pad_head = offset;
				bgcol->lnkhdr_xmit.opt.opt_net.pad_tail = (COL_FRAGPAYLOAD - ((skb->len + offset) % COL_FRAGPAYLOAD)) % COL_FRAGPAYLOAD;
				bgcol->current_xmit_data=skb->data - offset ;
				bgcol->current_xmit_len=skb->len + offset ;
				 /*  prepare link header */
				bgcol->lnkhdr_xmit.conn_id = bgcol->curr_conn++;
				bgcol->lnkhdr_xmit.total_pkt = ((skb->len + offset - 1) / COL_FRAGPAYLOAD) + 1;
				bgcol->lnkhdr_xmit.this_pkt = 0;
				TRACE("%s: dst_key=%08x src_key=%08x lnk_proto=%d conn_id=%d total_pkt=%d pad_head=%d pad_tail=%d", __FUNCTION__,
				    bgcol->lnkhdr_xmit.dst_key, bgcol->lnkhdr_xmit.src_key, bgcol->lnkhdr_xmit.lnk_proto, bgcol->lnkhdr_xmit.conn_id, bgcol->lnkhdr_xmit.total_pkt, bgcol->lnkhdr_xmit.opt.opt_net.pad_head, bgcol->lnkhdr_xmit.opt.opt_net.pad_tail );
				bgcol->fragidx_xmit = 0 ;
				bgcol->dest_xmit = dest ;
				    TRACEN(k_t_lowvol,"bgnet xmit: dst=%08x, src=%08x, ldst=%08x, head=%d, tail=%d",
					  bgcol->lnkhdr_xmit.dst_key, bgcol->lnkhdr_xmit.src_key, dest.raw, bgcol->lnkhdr_xmit.opt.opt_net.pad_head, bgcol->lnkhdr_xmit.opt.opt_net.pad_tail);
			      }
		  }
  }

/*  Push packets in until we finish the skb or the fifo fills */
/*  Returns 2 if we would like to push something into the fifo but cannot because it is full */
/*  Returns 1 if we pushed something into the fifo */
static inline int bgcol_xmit_push_packets(struct bg_col* bgcol,
/*     struct bgcol_channel *chn, */
    unsigned int status_in, unsigned int mioaddr)
  {
    unsigned int fragidx ;
    struct bgnet_dev *bgnet = bgcol->bgnet ;
    union bgcol_status status;
    union bgcol_header dest ;
    struct sk_buff *skb = bgcol->skb_current_xmit ;
    void *payloadptr = bgcol->current_xmit_data ;
    int len = bgcol->current_xmit_len ;
    int fullness ;
    int initial_fragidx ;
    double *lnkhdrxd = (double *) &(bgcol->lnkhdr_xmit) ;

    dest = bgcol->dest_xmit ;
    fragidx = bgcol->fragidx_xmit ;
    TRACE("bgnet xmit: dst=%08x, src=%08x, ldst=%08x, head=%d, tail=%d, fragidx=%d",
          bgcol->lnkhdr_xmit.dst_key, bgcol->lnkhdr_xmit.src_key, dest.raw, bgcol->lnkhdr_xmit.opt.opt_net.pad_head, bgcol->lnkhdr_xmit.opt.opt_net.pad_tail, fragidx);
    dump_skb_partial(skb,64) ;
    if( 0 != ( ((unsigned)(payloadptr) ) & 0x0f ) )
	    {
		    TRACEN(k_t_request, "Misaligned payloadptr=%p", payloadptr) ;
	    }
/*     BUG_ON(0 != ( ((unsigned)(payloadptr) ) & 0x0f ) ) ; */
    if( 0 == ( ((unsigned)payloadptr) & 0x0f ) )
      {
       /*  Have we got space in the FIFO ? */
      status.raw = status_in ;
      fullness = status.x.inj_hdr ;
/*       bgcol->send_fifo_histogram[fullness] += 1 ; // fullness statistics */
      TRACE("bgnet xmit: status=%08x",status.raw);
      if (fullness >= COL_FIFO_SIZE )
      {
         /*  No room. Upper routines will retry when appropriate */
        TRACEN(k_t_irqflow,"Send FIFO full");
        TRACEN(k_t_irqflow,"bgnet xmit: dst=%08x, src=%08x, ldst=%08x, head=%d, tail=%d, fragidx=%d",
              bgcol->lnkhdr_xmit.dst_key, bgcol->lnkhdr_xmit.src_key, dest.raw, bgcol->lnkhdr_xmit.opt.opt_net.pad_head, bgcol->lnkhdr_xmit.opt.opt_net.pad_tail, fragidx);
        return 2 ;
      }
       /*  update fragment index */
      bgcol->lnkhdr_xmit.this_pkt = fragidx;
      initial_fragidx = fragidx ;
#if defined(COLLECTIVE_ONEPASS_TXRX)
      if( len >= COL_FRAGPAYLOAD )
#else
      while( len >= COL_FRAGPAYLOAD && fullness < COL_FIFO_SIZE)
#endif
        {
          bgcol_payload_inject_load2(lnkhdrxd,lnkhdrxd+1, payloadptr) ;
          dump_bgcol_packet(&bgcol->lnkhdr_xmit, payloadptr) ;
          fragidx += 1 ;
          bgcol->lnkhdr_xmit.this_pkt = fragidx;
          *(volatile unsigned*)(mioaddr + _BGP_TRx_HI) =  dest.raw;
          len -= COL_FRAGPAYLOAD;
          payloadptr += COL_FRAGPAYLOAD;
          fullness += 1;
          while( len >= COL_FRAGPAYLOAD && fullness < COL_FIFO_SIZE)
              {
                 /*  We have full packets, and space in the fifo for them */
                TRACE("bgcol: ptr=%p, len=%d", payloadptr, len);
                bgcol_payload_inject_storeload2((void*)(mioaddr + _BGP_TRx_DI),lnkhdrxd,lnkhdrxd+1, payloadptr) ;
                dump_bgcol_packet(&bgcol->lnkhdr_xmit, payloadptr) ;
                fragidx += 1 ;
                bgcol->lnkhdr_xmit.this_pkt = fragidx;
                 /*  write destination header */
                *(volatile unsigned*)(mioaddr + _BGP_TRx_HI) =  dest.raw ;
                len -= COL_FRAGPAYLOAD;
                payloadptr += COL_FRAGPAYLOAD;
                fullness += 1;
             }
          bgcol_payload_inject_store((void*)(mioaddr + _BGP_TRx_DI)) ;
#if !defined(COLLECTIVE_ONEPASS_TXRX)
          status.raw = in_be32_nosync((unsigned*)(mioaddr + _BGP_TRx_Sx)) ;
          fullness = status.x.inj_hdr ;
#endif
        }
      bgnet->stats.tx_bytes += COL_FRAGPAYLOAD*(fragidx-initial_fragidx) ;

       /*  Either the FIFO is full, or we are near (or at) the end of the skb-worth of data */
       /*  Stuff one packet in. */


      if( len > 0 && fullness < COL_FIFO_SIZE )
          {
               /*  If the last packet doesn't cross a page boundary, we can send it with */
               /*  whatever is in memory after it, and we won't get a SEGV. */
              TRACE("bgcol: ptr=%p, len=%d", payloadptr, len);
              bgnet->stats.tx_bytes += len;

                   /*  write destination header */
/*               enable_kernel_fp() ; */
                  *(volatile unsigned*)(mioaddr + _BGP_TRx_HI) =  dest.raw;
/*                   bgcol_payload_inject_load2partial(lnkhdrxd,lnkhdrxd+1, payloadptr,(len+15)/16) ; */
                      bgcol_payload_inject_load2(lnkhdrxd,lnkhdrxd+1, payloadptr) ;
                      bgcol_payload_inject_store((void*)(mioaddr + _BGP_TRx_DI)) ;

              len=0 ;
          }

      }
    else
      {
         /*  The packet was misaligned. This will cause the skb to be flushed and we will get a */
         /*  fresh one next time. */
        len=0 ;
      }
    TRACE("bgcol: bgcol->skb_current_xmit=%p", bgcol->skb_current_xmit);

    TRACE("bgcol: bgcol->skb_current_xmit=%p", bgcol->skb_current_xmit);
     /*  Did we complete the skb ? */
    if( 0 == len )
      {
          /*  Yes, we can free this one and upper layers will cue the next one */
        TRACEN(k_t_irqflow,"bgcol: finished skb=%p", skb);
        bgnet->stats.tx_packets++;
        dump_skb_partial(skb,64);
/*  Linux seems unhappy freeing skb's in an interrupt handler */
#if defined(COLLECTIVE_TRANSMIT_WITH_SLIH)
#if defined(COLLECTIVE_XMITTER_FREES)
        skb_queue_tail(&bgcol->skb_list_free,skb) ;
#else
        dev_kfree_skb(skb) ;
#endif
#else
        skb_queue_tail(&bgcol->skb_list_free,skb) ;
        tasklet_schedule(&bgcol_completed_buffer_tasklet) ;
#endif
        bgcol->skb_current_xmit=NULL ;
      }
    else
      {
         /*  No, Remember the link header for next time */
        TRACE("bgcol: bgcol->skb_current_xmit=%p", bgcol->skb_current_xmit);
        TRACE("bgcol: more to go for skb=%p , fragidx=%d, len=%d", skb, fragidx, skb->len);
        bgcol->fragidx_xmit = fragidx ;
        bgcol->current_xmit_len=len ;
        bgcol->current_xmit_data=payloadptr ;
      }
    TRACE("bgcol: bgcol->skb_current_xmit=%p", bgcol->skb_current_xmit);
    return 1 ;  /* Indicate that a redrive might be productive */
  }


/*  One pass at filling the transmit FIFO. */
/*  Returns 2 if we would like to push something into the fifo but cannot because it is full */
/*  Returns 1 if we pushed something into the fifo (and we would like a redrive because we finished a frame) */
/*  Returns 0 if all the data has been put in the FIFO (and a redrive would be unproductive unless someone queues a frame for sending) */
/*   An upper layer must redrive or enable interrupts if it gets a non-zero. */
static inline int bgcol_xmit_onepass(struct bg_col *bgcol, unsigned int status_in, unsigned int mioaddr)
  {
/*     unsigned chnidx = bgcol->bgnet_channel ; */
    struct sk_buff *skb = bgcol->skb_current_xmit ;
    if( NULL == skb)
      {
	struct bgnet_dev *bgnet = bgcol->bgnet ;
        if( bgnet)
          {
	    bgcol_xmit_next_skb(bgcol) ;
	    skb = bgcol->skb_current_xmit ;
	    if( NULL == skb )
	      {
		TRACEN(k_t_irqflow,"bgcol: no more to send");
		return 0 ;
	      }
          }
        else
          {
            TRACEN(k_t_irqflow,"bgcol: bgnet is not ready");
            return 0 ;
          }
      }
     /*  By this stage we should have a viable skb and a viable link header */
    return bgcol_xmit_push_packets(bgcol,
        status_in,
        mioaddr) ;
  }

/*  'full duplex' SLIH, receiving and sending */
/*  Number of times to spin before concluding there isn't anything on the bgcol */
enum {
  k_unproductive_receive_threshold = 10 ,
  k_unproductive_transmit_threshold = 10
};

void bgcol_duplex_slih(unsigned long dummy)
  {
    struct bg_col *bgcol = __bgcol ;
    struct bgcol_channel *chn = &bgcol->chn[bgcol->bgnet_channel];
    unsigned int mioaddr=chn->mioaddr ;
    unsigned int status=*((volatile unsigned*)(mioaddr + _BGP_TRx_Sx)) ;
    unsigned int rcr ;
    unsigned int rcx ;
    unsigned int productive=0 ;
    unsigned int unproductive_receive_count=0 ;
    unsigned int unproductive_transmit_count=0 ;
    unsigned int rcrset = 0 ;

    enable_kernel_fp() ;

#if defined(KEEP_BG_COL_STATISTICS)
    bgcol->send_fifo_histogram0[(status >> 16) & 0x0f] += 1 ;
    bgcol->recv_fifo_histogram0[(status      ) & 0x0f] += 1 ;
#endif
    for(;;)
      {
        TRACEN(k_t_irqflow,"status=%08x", status);
        rcr = bgcol_receive_mark3(bgcol, bgcol->bgnet_channel, status, mioaddr) ;
#if defined(KEEP_BG_COL_STATISTICS) && defined(EXTRA_TUNING)
        {
		unsigned int extra_status=*((volatile unsigned*)(mioaddr + _BGP_TRx_Sx)) ;
		    bgcol->send_fifo_histogram2[(extra_status >> 16) & 0x0f] += 1 ;
		    bgcol->recv_fifo_histogram2[(extra_status      ) & 0x0f] += 1 ;

        }
#endif
        rcx = bgcol_xmit_onepass(bgcol, status, mioaddr) ;
        TRACEN(k_t_irqflow,"rcr=0x%08x rcx=0x%08x", rcr, rcx);
        status=*((volatile unsigned*)(mioaddr + _BGP_TRx_Sx)) ;
#if defined(KEEP_BG_COL_STATISTICS)
    bgcol->send_fifo_histogram1[(status >> 16) & 0x0f] += 1 ;
    bgcol->recv_fifo_histogram1[(status      ) & 0x0f] += 1 ;
#endif
         /*  What we do now depends on whether the slihs were 'productive' ... */
        unproductive_receive_count = rcr ? 0 : (unproductive_receive_count+1) ;
        unproductive_transmit_count = (rcx==1) ? 0 : (unproductive_transmit_count+1) ;
        productive += ( 0 != rcr || 1 == rcx ) ;
        rcrset = ( rcr > 0 ) ? 0 : rcrset ;
        rcrset |= rcr ;
        if( 0 == productive )
          {
#if defined(KEEP_BG_COL_STATISTICS)
            bgcol->spurious_interrupts += 1 ;
#endif
            break ;  /*  a spurious interrupt */
          }
        if( ( unproductive_receive_count > k_unproductive_receive_threshold
              || (rcrset & 0x80000000)
            )
            &&
            ( unproductive_transmit_count > k_unproductive_transmit_threshold
               || (rcx == 0 )
            )
          ) break ;  /*  Neither transmit not receive are likely to progress */
      }

#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
    if( bgcol_diagnostic_use_napi )
	    {
		TRACEN(k_t_napi,"napi_complete(%p)",&(bgcol->bgnet->napi)) ;
		napi_complete(&(bgcol->bgnet->napi)) ;
	    }
#endif
    bgcol->handler_running = 0 ;
    if( 0 != rcx )
      {
         /*  Filled the TX FIFO, need an interrupt when it has room */
        TRACEN(k_t_irqflow,"Enabling TX interrupts");
        bgcol_enable_interrupts_xmit(bgcol) ;  /* Ask for an interrupt when there is space */
      }

#if defined(HAS_MISSED_INTERRUPT_TIMER)
    mod_timer(&bgcol->missed_interrupt_timer, jiffies+200) ;  /*  Cause timer interrupt after 2000ms if things don't stay alive ... temp while diagnosing problem ... */
#endif
    bgcol_enable_interrupts_rcv(bgcol) ;
  }


static DECLARE_TASKLET(bgcol_duplex_slih_tasklet,bgcol_duplex_slih,0);

static irqreturn_t bgcol_duplex_interrupt(int irq, void *dev)
  {
    struct bg_col *bgcol = (struct bg_col*)dev;

    TRACE("bgnet: (>)interrupt %d", irq);
    bgcol->handler_running = 1 ;
    bgcol_disable_interrupts_xmit(bgcol) ;
    bgcol_disable_interrupts_rcv(bgcol) ;
    (void) mfdcrx(bgcol->dcrbase +_BGP_DCR_TR_REC_PRXF);
#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
    if( bgcol_diagnostic_use_napi)
	    {
		    TRACEN(k_t_napi,"napi_schedule(%p)",&bgcol->bgnet->napi) ;
		    napi_schedule(&bgcol->bgnet->napi) ;
	    }
    else
	    {
		    tasklet_schedule(&bgcol_duplex_slih_tasklet);

	    }
#else
    tasklet_schedule(&bgcol_duplex_slih_tasklet);
#endif
    TRACE("bgnet: (<)interrupt %d", irq);
    return IRQ_HANDLED ;
  }


#if defined(HAS_MISSED_INTERRUPT_TIMER)
static void bgcol_missed_interrupt(unsigned long dummy)
{
	    struct bg_col *bgcol = (struct bg_col*)&static_col;
	    TRACEN(k_t_irqflow,"(>)") ;

	    bgcol->handler_running = 1 ;
	    bgcol_disable_interrupts_xmit(bgcol) ;
	    bgcol_disable_interrupts_rcv(bgcol) ;
	    (void) mfdcrx(bgcol->dcrbase +_BGP_DCR_TR_REC_PRXF);
	#if defined(CONFIG_BGP_COLLECTIVE_NAPI)
	    if( bgcol_diagnostic_use_napi)
		    {
			    TRACEN(k_t_napi,"napi_schedule(%p)",&bgcol->bgnet->napi) ;
			    napi_schedule(&bgcol->bgnet->napi) ;
		    }
	    else
		    {
			    tasklet_schedule(&bgcol_duplex_slih_tasklet);

		    }
	#else
	    tasklet_schedule(&bgcol_duplex_slih_tasklet);
	#endif
	mod_timer(&bgcol->missed_interrupt_timer, jiffies+10) ;  /*  Cause timer interrupt after 100ms if things don't stay alive ... temp while diagnosing problem ... */
	TRACEN(k_t_irqflow,"(<)") ;
}
#endif
int col_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
  struct bg_col *bgcol=__bgcol ;
  TRACEN(k_t_irqflow|k_t_startxmit,"%s: Enq skb=%p, dev=%p, len=%d", __FUNCTION__, skb, dev, skb->len);
#if defined(COLLECTIVE_TRANSMIT_WITH_SLIH)
  skb_queue_tail(&(bgcol->skb_list_xmit),skb) ;
#if defined(COLLECTIVE_TRANSMIT_WITH_FLIH)
  if( ! bgcol->handler_running)
	  {
		  TRACEN(k_t_irqflow,"Enabling TX interrupts");
		  bgcol_enable_interrupts_xmit(bgcol) ;  /* Ask for an interrupt when there is space */
	  }
#else
      tasklet_schedule(&bgcol_duplex_slih_tasklet);
#endif
#else
    {
      unsigned int flags ;

      dump_skb_partial(skb,64) ;
      spin_lock_irqsave(&bgcol->irq_lock_xmit, flags) ;
        {
          struct sk_buff *xskb = bgcol->skb_current_xmit ;
          if( NULL == xskb && skb_queue_empty(&(bgcol->skb_list_xmit)))
            {
              int rc ;
              TRACEN(k_t_irqflow,"%s: Enq+en skb=%p, len=%d", __FUNCTION__, skb, skb->len);
              skb_queue_tail(&(bgcol->skb_list_xmit),skb) ;
              enable_kernel_fp();
              rc = bgcol_xmit_handle(bgcol) ;
             if( 0 == rc )
                {
                   /*  No room in fifo */
                  TRACEN(k_t_irqflow,"Enabling TX interrupts");
                  bgcol_enable_interrupts_xmit(bgcol) ;  /* Ask for an interrupt when there is space */
                }
            }
          else
            {
              TRACEN(k_t_irqflow,"%s: Enq skb=%p, dev=%p, len=%d", __FUNCTION__, skb, dev, skb->len);
              skb_queue_tail(&(bgcol->skb_list_xmit),skb) ;
            }
        }
      spin_unlock_irqrestore(&bgcol->irq_lock_xmit, flags);
    }
#endif
/*     } */
#if defined(COLLECTIVE_XMITTER_FREES)
    {
	    struct sk_buff *skb = skb_dequeue(&(bgcol->skb_list_free) ) ;
	    while(skb)
		    {
			    TRACEN(k_t_irqflow,"Freeing sent skb=%p",skb);
			    dev_kfree_skb(skb) ;
			    skb = skb_dequeue(&(bgcol->skb_list_free) ) ;
		    }

    }
#endif
  return 0 ;
}


/* static int bgpnet_add_device(int major, */
/*                              int minor, */
/*                              const char* devname, */
/*                              unsigned long long physaddr, */
/*                              int irq, */
/*                              irqreturn_t (*irq_handler)(int, void *)) */
/* { */
/*   int ret; */
/*   dev_t devno; */
/*   struct bgpnet_dev* dev = &bgpnet_devices[bgpnet_num_devices]; */
/*  */
//  /* initilize struct */
/*   init_MUTEX (&dev->sem); */
/*   dev->major  = major; */
/*   dev->minor  = minor; */
/*   dev->physaddr = physaddr; */
/*   init_waitqueue_head(&dev->read_wq); */
/*   dev->read_complete = 0; */
/*   if (physaddr) { */
/*           dev->regs = ioremap(physaddr, 4096); */
/*   } */
/*   devno=MKDEV(major,minor); */
/*  */
//  /* register i.e., /proc/devices */
/*   ret=register_chrdev_region(devno,1,(char *)devname); */
/*  */
/*   if (ret) */
/*     { */
/*       printk (KERN_WARNING "bgpnet: couldn't register device (%d,%d) register_chrdev_region err=%d\n", */
/*               major,minor,ret); */
/*       return ret; */
/*     } */
/*  */
//  /* add cdev */
/*   cdev_init(&dev->cdev,&bgpnet_device_fops); */
/*   dev->cdev.owner=THIS_MODULE; */
/*   dev->cdev.ops=&bgpnet_device_fops; */
/*   ret=cdev_add(&dev->cdev,devno,1); */
/*   if (ret) */
/*     { */
/*       printk(KERN_WARNING "bgpnet: couldn't register device (%d,%d) cdev_add err=%d\n", */
/*              major,minor,ret); */
/*       return ret; */
/*     } */
/*  */
//  /* signul to pass to owning process, should be altered using ioctl */
/*   dev->signum=-1; */
/*  */
/*   bgpnet_num_devices++; */
/*  */
/*   return 0; */
/* } */

/* static int bgpnet_device_open (struct inode *inode, struct file *filp) */
/* { */
/*   struct bgpnet_dev *dev=container_of(inode->i_cdev,struct bgpnet_dev,cdev); */
/*  */
/*   if(down_interruptible(&dev->sem)) return -ERESTARTSYS; */
/*   up(&dev->sem); */
/*  */
/*   dev->current=current; */
/*   filp->private_data = (void*) dev; */
/*  */
/*   TRACE("bgpnet: device (%d,%d) opened by process \"%s\" pid %i", */
/*         MAJOR(inode->i_rdev), MINOR(inode->i_rdev), current->comm, current->pid); */
/*  */
/*   return 0; */
/* } */


/*
 * Read doesn't actually read anything.   It simply blocks if the fifo is empty.
 */
/* static ssize_t bgpnet_device_read(struct file *filp, char __user *buf, size_t count, */
/* 				 loff_t *f_pos) */
/* { */
/*     struct bgpnet_dev* dev = (struct bgpnet_dev *)filp->private_data; */
/*     union bgcol_status status; */
/*     int chn = dev->minor; */
/*  */
/*     if (dev->major == BGP_COL_MAJOR_NUM && (chn == 0 || chn == 1)) { */
/*         status.raw = in_be32((unsigned *)((char*)dev->regs + _BGP_TRx_Sx)); */
/*         if (!status.x.rcv_hdr) { */
/*                 TRACE("bgpnet: read found status not ready status=0x%08x", status.raw); */
//                /* enable interrupt when packets come in. */
/*                 bgcol_enable_rcv_wm_interrupt(&__bgcol->chn[chn]); */
/*                 wait_event_interruptible(dev->read_wq, dev->read_complete); */
/*                 dev->read_complete = 0; */
/*                 TRACE("bgpnet: read wakes up"); */
/*         } */
//        /* Ok if we give a false positive -- we tried.
/*          * Note that we never actually copy out some data.  The status might be a useful */
/*          * thing to write in the buffer, but the caller only cares to block until */
/*          * something is there. */
//        */
/*     } */
/*  */
/*     return 0; */
/* } */


/* Don't think this will work on the 'bgnet' channel. What is the intent ? CIOD ? */
/* If for CIOD, it may have suffered in the 'revised interrupt handler' integrataion */
/*
 * Note that poll only waits for data to be available in the read fifo.
 * We do this by enabling an interrupt while we wait.  The interrupt is disabled
 * when it fires.  The poll may complete before it fires (timeout), but that is ok.
 */
/* static unsigned int bgpnet_device_poll(struct file *filp, poll_table * wait) */
/* { */
/*     struct bgpnet_dev* dev = (struct bgpnet_dev*) filp->private_data; */
/*     unsigned int rc; */
/*     union bgcol_status status; */
/*     unsigned int chn = dev->minor; */
/*  */
/*     if (dev->major == BGP_COL_MAJOR_NUM && (chn == 0 || chn == 1)) { */
/*         poll_wait(filp, &dev->read_wq, wait); */
/*  */
//        /* Return current col status. */
//        rc = POLLOUT|POLLWRNORM; /* For now implement read poll only */
/*         status.raw = in_be32((unsigned *)((char*)dev->regs + _BGP_TRx_Sx)); */
/*         if (status.x.rcv_hdr) { */
/*                 TRACE("bgpnet: poll found status ready status=0x%08x", status.raw); */
//                /* got something already */
/*                 rc |= POLLIN|POLLRDNORM; */
/*         } else { */
/*                 TRACE("bgpnet: poll found status not ready status=0x%08x", status.raw); */
//                /* enable interrupt when packets come in. */
/*                 mtdcrx(_BGP_DCR_TR_REC_PRXEN, (chn ? _TR_REC_PRX_WM1 : _TR_REC_PRX_WM0)); */
/*         } */
/*     } else */
/* 	rc = POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM; */
/*  */
/*     return rc; */
/* } */


/* static int bgpnet_device_mmap(struct file *filp, struct vm_area_struct *vma) */
/* { */
/*   unsigned long vsize = vma->vm_end - vma->vm_start; */
/*   struct bgpnet_dev * device = (struct bgpnet_dev *)filp->private_data; */
/*   int ret = -1; */
/*  */
//  /* ------------------------------------------------------- */
//  /* set up page protection.                                 */
//  /* ------------------------------------------------------- */
/*  */
/*   vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot); */
/*   vma->vm_flags     |= VM_IO; */
/*   vma->vm_flags     |= VM_RESERVED; */
/*  */
//  /* ------------------------------------------------------- */
//  /*                  do the mapping                         */
//  /* ------------------------------------------------------- */
/*  */
/*   if (device->physaddr != 0) */
/*     ret = remap_pfn_range(vma, */
/*                           vma->vm_start, */
/*                           device->physaddr >> PAGE_SHIFT, */
/*                           vsize, */
/*                           vma->vm_page_prot); */
/*  */
/*   if (ret) { */
/*       printk (KERN_WARNING "bgpnet: mapping of device (%d,%d) failed\n", */
/*                    device->major, device->minor); */
/*   } else { */
/*       TRACE("bgpnet: mapped (%d,%d) to vm=%lx", */
/*              device->major, device->minor, vma->vm_start); */
/*   } */
/*   return ret? -EAGAIN :0; */
/* } */

/* ************************************************************************* */
/*                  BG/P network: release device                             */
/* ************************************************************************* */

/* static int bgpnet_device_release (struct inode *inode, struct file * filp) */
/* { */
/*   struct bgpnet_dev *dev=(struct bgpnet_dev *)filp->private_data; */
/*  */
//  /*Ensure exclusive access*/
/*   if(down_interruptible(&dev->sem)) return -ERESTARTSYS; */
/*  */
/*   dev->current = NULL; */
/*   up(&dev->sem); */
/*  */
/*   TRACE("bgpnet: device (%d,%d) successfully released", */
/*          MAJOR(inode->i_rdev), MINOR(inode->i_rdev)); */
/*   return 0; */
/* } */


/* static int bgpnet_device_ioctl (struct inode *inode, */
/*                                 struct file * filp, */
/*                                 unsigned int cmd, */
/*                                 unsigned long arg) */
/* { */
/*   return 0; */
/* } */



/*  Base 10 is assumed.  Hexadecimal numbers must begin with 0x or 0X (ie. 0xabadcafe). */
/*  Binary numbers must begin with the letter b in lowercase (ie. b01101001). */
#define LOWER(c) ((c) < 'a' ? (c) + ('a' - 'A') : (c))
static inline unsigned long atol(char *str)
{
        unsigned long value = 0;
        unsigned char base = 10;

        if ((*str == '0') && (LOWER(*(str+1)) == 'x')) {
                base = 16;  /*  hexadecimal */
                str += 2;
        } else if (*str == 'b') {
                base = 2;  /*  binary */
                str++;
        }

        for (; *str; str++) {
                unsigned char digit = (*str > '9') ? (10 + LOWER(*str) - 'a') : (*str - '0');
                if (digit >= base) {
                        value = 0;
                        break;
                }
                value = value * base + digit;
        }

        return value;
}





/**********************************************************************
 * Initialization and shut-down
 **********************************************************************/

static inline void bgcol_reset_channel(struct bgcol_channel *chn)
{
    mtdcrx(chn->dcrbase + _BGP_DCR_TR_RCTRL, _TR_RCTRL_RST);
    mtdcrx(chn->dcrbase + _BGP_DCR_TR_SCTRL, _TR_RCTRL_RST);
}


static int bgcol_init_channel(unsigned long idx, struct bg_col *col)
{
    struct bgcol_channel* chn = &col->chn[idx];
    int i;

    chn->paddr = COL_CHANNEL_PADDR(idx);
    chn->dcrbase = col->dcrbase + COL_CHANNEL_DCROFF(idx);
    chn->irq_rcv_pending_mask = COL_IRQ_RCV_PENDING_MASK(idx);
    chn->irq_inj_pending_mask = COL_IRQ_INJ_PENDING_MASK(idx);
    init_timer(&chn->inj_timer);
    chn->inj_timer.function = inj_timeout;
    chn->inj_timer.data = (unsigned long) col;
    chn->inj_timer.expires = 0;
    for (i = 0; i < BGP_MAX_DEVICES; i++)
	if (bgpnet_devices[i].major == BGP_COL_MAJOR_NUM &&
	    bgpnet_devices[i].minor == idx) {
		chn->chrdev = &bgpnet_devices[i];
		break;
	}
    if (i >= BGP_MAX_DEVICES)
	chn->chrdev = NULL;
    chn->col = col;
    chn->idx = idx;

    if (!request_mem_region(chn->paddr, _BGP_COL_SIZE, COL_DEV_NAME))
	return -1;

    chn->mioaddr = (unsigned long)ioremap(chn->paddr, _BGP_COL_SIZE);
    if (!chn->mioaddr)
	goto err_remap;

    if (chn)
	mtdcrx(col->dcrbase + _BGP_DCR_TR_GLOB_VCFG1,
                 _TR_GLOB_VCFG_RWM(0) | _TR_GLOB_VCFG_IWM(4));
    else
	mtdcrx(col->dcrbase + _BGP_DCR_TR_GLOB_VCFG0,
                _TR_GLOB_VCFG_RWM(0) | _TR_GLOB_VCFG_IWM(4));
    mtdcrx(chn->col->dcrbase + _BGP_DCR_TR_REC_PRXEN, COL_IRQMASK_REC);
    mtdcrx(chn->col->dcrbase + _BGP_DCR_TR_INJ_PIXEN, COL_IRQMASK_INJ);

    return 0;

 err_remap:
    printk("error mapping col\n");
    release_mem_region(chn->mioaddr, _BGP_COL_SIZE);

    return -1;
}

static int bgcol_uninit_channel(struct bgcol_channel *chn,
				 struct bg_col *col)
{
    if (chn->mioaddr)
    {
	iounmap((void*)chn->mioaddr);
	chn->mioaddr = 0;

	 /*  unconditionally... */
	release_mem_region(chn->paddr, _BGP_COL_SIZE);
    }
    return 0;
}

static int bgcol_init (struct bg_col *col)
{
    int cidx, rc, idx;

/*     skb_queue_head_init(&skb_delivery_queue) ; */
    if( 0 == col->mtu)
      {
        bgcol_set_mtu(col,60960+sizeof(struct ethhdr) ) ;  /*  It's possible that the 'bgnet' might have won a race to set MTU ... */
      }
    col->skb_in_waiting = alloc_skb(
			    k_use_plentiful_skb ? k_plentiful_skb_size :  col->mtu
			    , GFP_KERNEL );
    col->skb_mini = alloc_skb(BGNET_FRAG_MTU + COL_SKB_ALIGN , GFP_KERNEL ) ;

    spin_lock_init(&col->lock);
    spin_lock_init(&col->irq_lock);

    skb_queue_head_init(&col->skb_list_for_filling) ;
    skb_queue_head_init(&col->skb_list_for_delivering) ;
    skb_queue_head_init(&col->skb_list_for_freeing) ;

    bgcol_prefill(&col->skb_list_for_filling, 100)  ;


    col->dcrbase = COL_DCR_BASE;

    skb_queue_head_init(&col->skb_list_xmit) ;
    skb_queue_head_init(&col->skb_list_free) ;
    col->skb_current_xmit = NULL ;

    skb_queue_head_init(&col->fragskb_list_rcv) ;
    init_ethkey_table(col) ;

     /*  abuse IO port structure for DCRs */
    if (!request_region(col->dcrbase, COL_DCR_SIZE, COL_DEV_NAME))
	return -1;

     /*  disable device IRQs before we attach them */
    bgcol_disable_interrupts(col);

#if defined(HAS_MISSED_INTERRUPT_TIMER)
    setup_timer(&col->missed_interrupt_timer,bgcol_missed_interrupt,0) ;
#endif
    col->nodeid = mfdcrx(col->dcrbase + _BGP_DCR_TR_GLOB_NADDR);

    for (cidx = 0; cidx < BGP_MAX_CHANNEL; cidx++) {
	if (bgcol_init_channel(cidx, col) != 0)
	    goto err_channel;
    }

     /*  clear exception flags */
    mfdcrx(col->dcrbase + _BGP_DCR_TR_INJ_PIXF);
    mfdcrx(col->dcrbase + _BGP_DCR_TR_REC_PRXF);

     /*  allocate IRQs last; otherwise, if an IRQ is still pending, we */
     /*  get kernel segfaults */
    for (idx = 0; bgcol_irqs[idx].irq != -1; idx++)
    {
#if defined(COLLECTIVE_TREE_AFFINITY)
	  bic_set_cpu_for_irq(bgcol_irqs[idx].irq,k_TreeAffinityCPU) ;
	  TRACEN(k_t_general,"setting affinity irq=%d affinity=%d",bgcol_irqs[idx].irq, k_TreeAffinityCPU );
#endif
	rc = request_irq(bgcol_irqs[idx].irq, bgcol_irqs[idx].handler,
			 IRQF_DISABLED, bgcol_irqs[idx].name, col);
	if (rc)
	    goto err_irq_alloc;
    }


    return 0;

 err_irq_alloc:
    for (idx = 0; bgcol_irqs[idx].irq != -1; idx++)
	free_irq(bgcol_irqs[idx].irq, col);

 err_channel:
    for (cidx = 0; cidx < BGP_MAX_CHANNEL; cidx++)
	bgcol_uninit_channel(&col->chn[cidx], col);

    release_region(col->dcrbase, COL_DCR_SIZE);

    return -1;
}

/**********************************************************************
 *                      /proc filesystem
 **********************************************************************/

#define TGREAD(r, d) \
        rc = snprintf(page, remaining, "%.30s (%03x): %08x\n", d, \
                      bgcol->dcrbase + r, mfdcrx(bgcol->dcrbase + r)); \
        if (rc < 0) goto out; \
        if (rc > remaining) { remaining = 0; goto out; } \
        page += rc;  \
        remaining -= rc;

#define TGSHOW(r) \
  rc = snprintf(page, remaining, "%.60s : %08x\n", #r, (unsigned int)(r) );\
  if (rc < 0) goto out; \
  if (rc > remaining) { remaining = 0; goto out; } \
  page += rc;  \
  remaining -= rc;


static int bgpnet_statistics_read (char *page, char **start, off_t off,
                            int count, int *eof, void *data)
{
    struct bg_col *bgcol = data;
    int rc, remaining = count;
    *eof = 1;
    TGREAD(_BGP_DCR_TR_REC_PRXEN, "Receive Exception Enable");
    TGREAD(_BGP_DCR_TR_REC_PRXF,  "Receive Exception Flag  ");
    TGREAD(_BGP_DCR_TR_INJ_PIXEN, "Injection Exception Enable");
    TGREAD(_BGP_DCR_TR_INJ_PIXF,  "Injection Exception Flag  ");

    TGSHOW(*((unsigned*)(bgcol->chn[0].mioaddr + _BGP_TRx_Sx))) ;
    TGSHOW(bgcol->curr_conn) ;
#if !defined(COLLECTIVE_TRANSMIT_WITH_SLIH)
    TGSHOW(spin_is_locked(&bgcol->irq_lock_xmit)) ;
#endif
    TGSHOW(skb_queue_len(&bgcol->skb_list_xmit)) ;
    TGSHOW(skb_queue_len(&bgcol->skb_list_free)) ;
    TGSHOW(skb_queue_len(&bgcol->fragskb_list_rcv)) ;
    TGSHOW(bgcol->skb_current_xmit) ;
    TGSHOW(bgcol->current_xmit_len) ;
    TGSHOW(bgcol->fragidx_xmit) ;
    TGSHOW(bgcol->recv_total) ;
    TGSHOW(bgcol->recv_guess_miss) ;
    TGSHOW(bgcol->recv_no_skbuff) ;
    TGSHOW(bgcol->recv_no_first_packet) ;
    TGSHOW(bgcol->spurious_interrupts) ;
    TGSHOW(irq_desc[BG_COL_IRQ_INJ].status) ;
    TGSHOW(irq_desc[BG_COL_IRQ_INJ].irq_count) ;
    TGSHOW(irq_desc[BG_COL_IRQ_INJ].irqs_unhandled) ;
    TGSHOW(irq_desc[BG_COL_IRQ_RCV].status) ;
    TGSHOW(irq_desc[BG_COL_IRQ_RCV].irq_count) ;
    TGSHOW(irq_desc[BG_COL_IRQ_RCV].irqs_unhandled) ;

     out:

        return count - remaining;
}

static int bgpnet_status_read (char *page, char **start, off_t off,
                            int count, int *eof, void *data)
{
    struct bg_col *bgcol = data;
    int rc, remaining = count;
    *eof = 1;


    TGREAD(_BGP_DCR_TR_GLOB_FPTR, "Fifo Pointer");
    TGREAD(_BGP_DCR_TR_GLOB_NADDR, "Node Address");
    TGREAD(_BGP_DCR_TR_GLOB_VCFG0, "VC0 Configuration");
    TGREAD(_BGP_DCR_TR_GLOB_VCFG1, "VC1 Configuration");
    TGREAD(_BGP_DCR_TR_REC_PRXEN, "Receive Exception Enable");
    TGREAD(_BGP_DCR_TR_REC_PRXF,  "Receive Exception Flag  ");
    TGREAD(_BGP_DCR_TR_REC_PRDA, "Receive Diagnostic Address");
    TGREAD(_BGP_DCR_TR_REC_PRDD, "Receive Diagnostic Data");
    TGREAD(_BGP_DCR_TR_INJ_PIXEN, "Injection Exception Enable");
    TGREAD(_BGP_DCR_TR_INJ_PIXF,  "Injection Exception Flag  ");
    TGREAD(_BGP_DCR_TR_INJ_PIDA, "Injection Diagnostic Address");
    TGREAD(_BGP_DCR_TR_INJ_PIDD, "Injection Diagnostic Data");
    TGREAD(_BGP_DCR_TR_INJ_CSPY0, "VC0 payload checksum");
    TGREAD(_BGP_DCR_TR_INJ_CSHD0, "VC0 header checksum");
    TGREAD(_BGP_DCR_TR_INJ_CSPY1, "VC1 payload checksum");
    TGREAD(_BGP_DCR_TR_INJ_CSHD1, "VC1 header checksum");

    TGREAD(_BGP_DCR_TR_CLASS_RDR0, "Route Desc 0, 1");
    TGREAD(_BGP_DCR_TR_CLASS_RDR1, "Route Desc 2, 3");
    TGREAD(_BGP_DCR_TR_CLASS_RDR2, "Route Desc 4, 5");
    TGREAD(_BGP_DCR_TR_CLASS_RDR3, "Route Desc 6, 7");
    TGREAD(_BGP_DCR_TR_CLASS_RDR4, "Route Desc 8, 9");
    TGREAD(_BGP_DCR_TR_CLASS_RDR5, "Route Desc 10, 11");
    TGREAD(_BGP_DCR_TR_CLASS_RDR6, "Route Desc 12, 13");
    TGREAD(_BGP_DCR_TR_CLASS_RDR7, "Route Desc 14, 15");
    TGREAD(_BGP_DCR_TR_CLASS_ISRA, "Idle pattern low");
    TGREAD(_BGP_DCR_TR_CLASS_ISRB, "Idle pattern high");

    TGREAD(_BGP_DCR_TR_DMA_DMAA, "SRAM diagnostic addr");
    TGREAD(_BGP_DCR_TR_DMA_DMAD, "SRAM diagnostic data");
    TGREAD(_BGP_DCR_TR_DMA_DMADI, "SRAM diagnostic data inc");
    TGREAD(_BGP_DCR_TR_DMA_DMAH, "SRAM diagnostic header");

    TGREAD(_BGP_DCR_TR_ERR_R0_CRC, "CH0: Receiver link CRC errors");
    TGREAD(_BGP_DCR_TR_ERR_R0_CE, "CH0: Receiver SRAM errors corrected");
    TGREAD(_BGP_DCR_TR_ERR_S0_RETRY, "CH0: Sender link retransmissions");
    TGREAD(_BGP_DCR_TR_ERR_S0_CE, "CH0: Sender SRAM errors corrected");

    TGREAD(_BGP_DCR_TR_ERR_R1_CRC, "CH1: Receiver link CRC errors");
    TGREAD(_BGP_DCR_TR_ERR_R1_CE, "CH1: Receiver SRAM errors corrected");
    TGREAD(_BGP_DCR_TR_ERR_S1_RETRY, "CH1: Sender link retransmissions");
    TGREAD(_BGP_DCR_TR_ERR_S1_CE, "CH1: Sender SRAM errors corrected");

    TGREAD(_BGP_DCR_TR_ERR_R2_CRC, "CH2: Receiver link CRC errors");
    TGREAD(_BGP_DCR_TR_ERR_R2_CE, "CH2: Receiver SRAM errors corrected");
    TGREAD(_BGP_DCR_TR_ERR_S2_RETRY, "CH2: Sender link retransmissions");
    TGREAD(_BGP_DCR_TR_ERR_S2_CE, "CH2: Sender SRAM errors corrected");

    TGREAD(_BGP_DCR_TR_ARB_RCFG, "ARB: General router config");
    TGREAD(_BGP_DCR_TR_ARB_RSTAT, "ARB: General router status");
    TGREAD(_BGP_DCR_TR_ARB_HD00, "ARB: Next hdr, CH0, VC0");
    TGREAD(_BGP_DCR_TR_ARB_HD01, "ARB: Next hdr, CH0, VC1");
    TGREAD(_BGP_DCR_TR_ARB_HD10, "ARB: Next hdr, CH1, VC0");
    TGREAD(_BGP_DCR_TR_ARB_HD11, "ARB: Next hdr, CH1, VC1");
    TGREAD(_BGP_DCR_TR_ARB_HD20, "ARB: Next hdr, CH2, VC0");
    TGREAD(_BGP_DCR_TR_ARB_HD21, "ARB: Next hdr, CH2, VC1");

   rc = snprintf(page, remaining, "CH0: status=%08x\n",
                  in_be32((unsigned*)(bgcol->chn[0].mioaddr + _BGP_TRx_Sx)));
   if (rc < 0) goto out;
   if (rc > remaining) { remaining = 0; goto out; }
    page += rc; remaining -= rc;

    rc = snprintf(page, remaining, "CH1: status=%08x\n",
                  in_be32((unsigned*)(bgcol->chn[1].mioaddr + _BGP_TRx_Sx)));
    if (rc < 0) goto out;
    if (rc > remaining) { remaining = 0; goto out; }
    page += rc; remaining -= rc;

    rc = snprintf(page, remaining, "Data placement total=%d guess wrong=%d\n",
                  bgcol->recv_total, bgcol->recv_guess_miss) ;
    if (rc < 0) goto out;
    if (rc > remaining) { remaining = 0; goto out; }
    page += rc; remaining -= rc;
    rc = snprintf(page,remaining, "Receive no_skbuff=%d no_first_packet=%d\n",
                  bgcol->recv_no_skbuff, bgcol->recv_no_first_packet) ;
    if (rc < 0) goto out;
    if (rc > remaining) { remaining = 0; goto out; }
    page += rc; remaining -= rc;

#if defined(KEEP_BG_COL_STATISTICS)
      {
/*         int x ; */
/*         for( x=0; x<=COL_FIFO_SIZE;x+=1) */
/*           { */
/*             rc = snprintf(page, remaining, "sf_h0[%d]=%d\n", x, bgcol->send_fifo_histogram0[x]) ; */
/*             if (rc < 0) goto out; */
/*             if (rc > remaining) { remaining = 0; goto out; } */
/*              page += rc; remaining -= rc; */
/*           } */
/*         for( x=0; x<=COL_FIFO_SIZE;x+=1) */
/*           { */
/*             rc = snprintf(page, remaining, "sf_h1[%d]=%d\n", x, bgcol->send_fifo_histogram1[x]) ; */
/*             if (rc < 0) goto out; */
/*             if (rc > remaining) { remaining = 0; goto out; } */
/*              page += rc; remaining -= rc; */
/*           } */
/*         for( x=0; x<=COL_FIFO_SIZE;x+=1) */
/*           { */
/*             rc = snprintf(page, remaining, "rf_h0[%d]=%d\n", x, bgcol->recv_fifo_histogram0[x]) ; */
/*             if (rc < 0) goto out; */
/*             if (rc > remaining) { remaining = 0; goto out; } */
/*              page += rc; remaining -= rc; */
/*           } */
/*         for( x=0; x<=COL_FIFO_SIZE;x+=1) */
/*           { */
/*             rc = snprintf(page, remaining, "rf_h1[%d]=%d\n", x, bgcol->recv_fifo_histogram1[x]) ; */
/*             if (rc < 0) goto out; */
/*             if (rc > remaining) { remaining = 0; goto out; } */
/*              page += rc; remaining -= rc; */
/*           } */
        rc=snprintf(page, remaining, "spurious interrupts=%d\n", bgcol->spurious_interrupts) ;
        if (rc < 0) goto out;
        if (rc > remaining) { remaining = 0; goto out; }
         page += rc; remaining -= rc;
      }
#endif

 out:

    return count - remaining;
}


static int bgcol_proc_write(struct file *filp, const char __user *buff, unsigned long len, void *data)
  {
    char proc_write_buffer[256] ;
    unsigned long actual_len=(len<255) ? len : 255 ;
    int rc = copy_from_user( proc_write_buffer, buff, actual_len ) ;
    if( rc != 0 ) return -EFAULT ;
    proc_write_buffer[actual_len] = 0 ;
    return actual_len ;
  }

/* static unsigned char xtable[256] = */
/*     { */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, */
/*     }; */
/*  */
/* static int bgcol_atoix(const unsigned char *cp) */
/*   { */
/*     int result = 0 ; */
/*     unsigned char ecp = xtable[*cp] ; */
/*     while (ecp < 0x10) */
/*       { */
/*         result = (result << 4 ) | ecp ; */
/*         cp += 1 ; */
/*         ecp = xtable[*cp] ; */
/*       } */
/*     return result ; */
/*   } */

static int dcrcopy ;
static int proc_docoldcr(struct ctl_table *ctl, int write, struct file * filp,
               void __user *buffer, size_t *lenp, loff_t *ppos)
  {
    int rc ;
    TRACE("(>)ctl=%p write=%d len=%d", ctl,write,*lenp) ;

    dcrcopy=mfdcrx((unsigned int)(ctl->extra1)) ;
    rc = proc_dointvec(ctl,write,filp,buffer,lenp,ppos) ;
    TRACE("(<)") ;
    return rc ;
  }

static int proc_docolmio_0(struct ctl_table *ctl, int write, struct file * filp,
               void __user *buffer, size_t *lenp, loff_t *ppos)
  {
    int rc ;
    TRACE("(>)ctl=%p write=%d len=%d", ctl,write,*lenp) ;
    ctl->data=(unsigned*)(static_col.chn[0].mioaddr + (unsigned int)(ctl->extra1)) ;
    rc = proc_dointvec(ctl,write,filp,buffer,lenp,ppos) ;
    TRACE("(<)") ;
    return rc ;
  }

static int proc_docolmio_1(struct ctl_table *ctl, int write, struct file * filp,
               void __user *buffer, size_t *lenp, loff_t *ppos)
  {
    int rc ;
    TRACE("(>)ctl=%p write=%d len=%d", ctl,write,*lenp) ;
    ctl->data=(unsigned*)(static_col.chn[1].mioaddr + (unsigned int)(ctl->extra1)) ;
    rc = proc_dointvec(ctl,write,filp,buffer,lenp,ppos) ;
    TRACE("(<)") ;
    return rc ;
  }

static struct ctl_path bgp_col_ctl_path[] = {
	{ .procname = "bgp", .ctl_name = 0, },
	{ .procname = "collective", .ctl_name = 0, },
	{ },
};

#define CTL_PARAM_ADDR(Name,Addr)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .data           = (int *)Addr,              \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_dointvec       \
  }

#define CTL_PARAM_MIO_0(Name,Offset)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_docolmio_0,       \
          .extra1	  = (void *)Offset             \
  }

#define CTL_PARAM_MIO_1(Name,Offset)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_docolmio_1,       \
          .extra1	  = (void *)Offset             \
  }

#define CTL_PARAM_COLDCR(Name,DCRNumber)        \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .data           = &dcrcopy , \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_docoldcr  ,     \
          .extra1          = (void *) DCRNumber   \
  }

static unsigned int static_pagesize = 1 << PAGE_SHIFT ;

static struct ctl_table bgp_col_ctl_table[] = {
/* 		CTL_PARAM_ADDR("napi",&bgcol_diagnostic_use_napi) , */
		CTL_PARAM_ADDR("pagesize",&static_pagesize) ,
		CTL_PARAM_ADDR("tracemask",&bgcol_debug_tracemask) ,
/* 		CTL_PARAM_ADDR("e10000_diag_count",&e10000_diag_count) , */
		CTL_PARAM_COLDCR("Receive-Exception-Enable",_BGP_DCR_TR_REC_PRXEN),
		CTL_PARAM_COLDCR("Receive-Exception-Flag",_BGP_DCR_TR_REC_PRXF),
		CTL_PARAM_COLDCR("Injection-Exception-Enable",_BGP_DCR_TR_INJ_PIXEN),
		CTL_PARAM_COLDCR("Injection-Exception-Flag  ",_BGP_DCR_TR_INJ_PIXF),
		CTL_PARAM_MIO_0("BGP_TR0_S0",_BGP_TRx_Sx) ,
		CTL_PARAM_MIO_1("BGP_TR1_S1",_BGP_TRx_Sx) ,
		CTL_PARAM_ADDR("curr_conn",&static_col.curr_conn) ,
		CTL_PARAM_ADDR("current_xmit_len",&static_col.current_xmit_len) ,
		CTL_PARAM_ADDR("fragidx_xmit",&static_col.fragidx_xmit) ,
		CTL_PARAM_ADDR("recv_total",&static_col.recv_total) ,
		CTL_PARAM_ADDR("recv_guess_miss",&static_col.recv_guess_miss) ,
		CTL_PARAM_ADDR("recv_no_skbuff",&static_col.recv_no_skbuff) ,
		CTL_PARAM_ADDR("recv_no_first_packet",&static_col.recv_no_first_packet) ,
		CTL_PARAM_ADDR("deliver_without_workqueue",&static_col.deliver_without_workqueue) ,
#if defined(KEEP_BG_COL_STATISTICS)
		  {
		          .ctl_name       = CTL_UNNUMBERED,
		          .procname       = "sf_h0" ,
		          .data           = static_col.send_fifo_histogram0,
		          .maxlen         = COL_FIFO_SIZE*sizeof(int),
		          .mode           = 0644,
		          .proc_handler   = &proc_dointvec
		  } ,
		  {
		          .ctl_name       = CTL_UNNUMBERED,
		          .procname       = "sf_h1" ,
		          .data           = static_col.send_fifo_histogram1,
		          .maxlen         = COL_FIFO_SIZE*sizeof(int),
		          .mode           = 0644,
		          .proc_handler   = &proc_dointvec
		  } ,
		  {
		          .ctl_name       = CTL_UNNUMBERED,
		          .procname       = "rf_h0" ,
		          .data           = static_col.recv_fifo_histogram0,
		          .maxlen         = COL_FIFO_SIZE*sizeof(int),
		          .mode           = 0644,
		          .proc_handler   = &proc_dointvec
		  } ,
		  {
		          .ctl_name       = CTL_UNNUMBERED,
		          .procname       = "rf_h1" ,
		          .data           = static_col.recv_fifo_histogram1,
		          .maxlen         = COL_FIFO_SIZE*sizeof(int),
		          .mode           = 0644,
		          .proc_handler   = &proc_dointvec
		  } ,
#if defined(EXTRA_TUNING)
		  {
		          .ctl_name       = CTL_UNNUMBERED,
		          .procname       = "sf_h2" ,
		          .data           = static_col.send_fifo_histogram2,
		          .maxlen         = COL_FIFO_SIZE*sizeof(int),
		          .mode           = 0644,
		          .proc_handler   = &proc_dointvec
		  } ,
		  {
		          .ctl_name       = CTL_UNNUMBERED,
		          .procname       = "rf_h2" ,
		          .data           = static_col.recv_fifo_histogram2,
		          .maxlen         = COL_FIFO_SIZE*sizeof(int),
		          .mode           = 0644,
		          .proc_handler   = &proc_dointvec
		  } ,

#endif
#endif
		  { 0 }


} ;

static void register_collective_sysctl(struct bg_col *col)
{
	col->sysctl_table_header=register_sysctl_paths(bgp_col_ctl_path,bgp_col_ctl_table) ;
	TRACEN(k_t_init, "sysctl_table_header=%p",col->sysctl_table_header) ;

}

int __init
bgcol_module_init(void)
{
    struct bg_col *col = &static_col ;
    int rc;
    unsigned long long tr0, tr1, ts0, ts1;

    register_collective_sysctl(&static_col) ;

    tr0=((unsigned long long)_BGP_UA_COL0<<32)  + _BGP_PA_COL0;
    tr1=((unsigned long long)_BGP_UA_COL1<<32)  + _BGP_PA_COL1;
    ts0=((unsigned long long)_BGP_UA_TORUS0<<32) + _BGP_PA_TORUS0;
    ts1=((unsigned long long)_BGP_UA_TORUS1<<32) + _BGP_PA_TORUS1;

#if defined(KEEP_BG_COL_STATISTICS) || defined(BGP_COL_STATUS_VISIBILITY)
    bgpnetDir = proc_mkdir("bgpcol", NULL);
    if (bgpnetDir) {
#if defined(KEEP_BG_COL_STATISTICS)
        statisticsEntry = create_proc_entry("statistics", S_IRUGO, bgpnetDir);
        if (statisticsEntry) {
          statisticsEntry->nlink = 1;
          statisticsEntry->read_proc = (void*) bgpnet_statistics_read;
          statisticsEntry->write_proc = (void*) bgcol_proc_write;
          statisticsEntry->data = col ;
        }
#endif
#if defined(BGP_COL_STATUS_VISIBILITY)
        statusEntry = create_proc_entry("status", S_IRUGO, bgpnetDir);
        if (statusEntry) {
          statusEntry->nlink = 1;
          statusEntry->read_proc = (void*) bgpnet_status_read;
          statusEntry->write_proc = (void*) bgcol_proc_write;
          statusEntry->data = col ;
        }
#endif
/* #if defined(CONFIG_BLUEGENE_COLLECTIVE_TRACE) */
/*         tracemaskEntry = create_proc_entry("tracemask", S_IRUGO, bgpnetDir); */
/*         if (tracemaskEntry) { */
/*           tracemaskEntry->nlink = 1; */
/*           tracemaskEntry->read_proc = (void*) bgpnet_tracemask_read; */
/*           tracemaskEntry->write_proc = (void*) bgpnet_tracemask_write; */
/*         } */
/* #endif */
   }
#endif

    rc = bgcol_init(col);
    if (rc)
	goto err_col_init;

    mb();


    return 0;

 err_col_init:
    /* XXX: unmap IRQs */
    return rc;
}
