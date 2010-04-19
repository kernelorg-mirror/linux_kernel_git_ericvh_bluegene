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
 *          Andrew Tauferner <ataufer@us.ibm.com>
 *
 * Description:   Header file for col device
 *
 *
 ********************************************************************/
#ifndef __DRIVERS__NET__BLUEGENE__COL_H__
#define __DRIVERS__NET__BLUEGENE__COL_H__

#define KEEP_BG_COL_STATISTICS
#define EXTRA_TUNING
/* #define KEEP_RECV_TOTAL */
#define HAS_MISSED_INTERRUPT_TIMER

#define _BGP_COL_BASE		(0x610000000ULL)
#define _BGP_COL_OFFSET	(0x001000000ULL)
#define _BGP_COL_SIZE		(0x400)

#define _BGP_TORUS_BASE		(0x601140000ULL)
#define _BGP_TORUS_OFFSET	(0x000010000ULL)

#define BGP_MAX_CHANNEL		2
#define BGP_COL_CHANNEL	0
#define BGP_COL_ADDR_BITS	24

#define COL_CHANNEL_PADDR(c)	(_BGP_COL_BASE + ((c)*_BGP_COL_OFFSET))
#define COL_CHANNEL_DCROFF(c)	(0x20 + ((c) * 8))
#define COL_DCR_BASE		(0xc00)
#define COL_DCR_SIZE		(0x80)

#define COL_IRQMASK_INJ	(_TR_INJ_PIX_APAR0  | _TR_INJ_PIX_APAR1  |\
                                 _TR_INJ_PIX_ALIGN0 | _TR_INJ_PIX_ALIGN1 |\
                                 _TR_INJ_PIX_ADDR0  | _TR_INJ_PIX_ADDR1  |\
                                 _TR_INJ_PIX_DPAR0  | _TR_INJ_PIX_DPAR1  |\
                                 _TR_INJ_PIX_COLL   | _TR_INJ_PIX_UE     |\
                                 _TR_INJ_PIX_PFO0   | _TR_INJ_PIX_PFO1   |\
                                 _TR_INJ_PIX_HFO0   | _TR_INJ_PIX_HFO1)

#define COL_IRQMASK_REC	(_TR_REC_PRX_APAR0  | _TR_REC_PRX_APAR1  |\
                                 _TR_REC_PRX_ALIGN0 | _TR_REC_PRX_ALIGN1 |\
                                 _TR_REC_PRX_ADDR0  | _TR_REC_PRX_ADDR1  |\
                                 _TR_REC_PRX_COLL   | _TR_REC_PRX_UE     |\
                                 _TR_REC_PRX_PFU0   | _TR_REC_PRX_PFU1   |\
                                 _TR_REC_PRX_HFU0   | _TR_REC_PRX_HFU1   |\
				 _TR_REC_PRX_WM0    | _TR_REC_PRX_WM1 )

#define COL_IRQ_RCV_PENDING_MASK(idx) (1U << (1 - idx))
#define COL_IRQ_INJ_PENDING_MASK(idx) (1U << (2 - idx))


#define COL_IRQ_GROUP		5
#define COL_IRQ_BASE		20
#define COL_IRQ_NONCRIT_NUM	20
#define COL_NONCRIT_BASE	0
#define COL_FIFO_SIZE		8


union bgcol_header {
	unsigned int raw;
	struct {
		unsigned int pclass	: 4;
		unsigned int p2p	: 1;
		unsigned int irq	: 1;
		unsigned vector		: 24;
		unsigned int csum_mode	: 2;
	} p2p;
	struct {
		unsigned int pclass	: 4;
		unsigned int p2p	: 1;
		unsigned int irq	: 1;
		unsigned int op		: 3;
		unsigned int opsize	: 7;
		unsigned int tag	: 14;
		unsigned int csum_mode	: 2;
	} bcast;
} __attribute__((packed));

union bgcol_status {
	unsigned int raw;
	struct {
		unsigned int inj_pkt	: 4;
		unsigned int inj_qwords	: 4;
	        unsigned int __res0	: 4;
		unsigned int inj_hdr	: 4;
		unsigned int rcv_pkt	: 4;
		unsigned int rcv_qwords : 4;
		unsigned int __res1	: 3;
		unsigned int irq	: 1;
		unsigned int rcv_hdr	: 4;
	} x;
} __attribute__((packed));

static inline unsigned int bgcol_status_inj_pkt   (unsigned int status) { return status >> 28 ; }
static inline unsigned int bgcol_status_inj_qwords(unsigned int status) { return (status >> 24) & 0x0f ; }
static inline unsigned int bgcol_status_inj_hdr   (unsigned int status) { return (status >> 16) & 0x0f ; }
static inline unsigned int bgcol_status_rcv_pkt   (unsigned int status) { return (status >> 12) & 0x0f ; }
static inline unsigned int bgcol_status_rcv_qwords(unsigned int status) { return (status >> 8 ) & 0x0f ; }
static inline unsigned int bgcol_status_irq       (unsigned int status) { return (status >> 4 ) & 1 ; }
static inline unsigned int bgcol_status_rcv_hdr   (unsigned int status) { return status & 0x0f ; }


/* some device defined */
#define _BGP_DCR_TR_RCTRL	(_BGP_DCR_TR_CH0_RCTRL - _BGP_DCR_TR_CH0)
#define _BGP_DCR_TR_SCTRL	(_BGP_DCR_TR_CH0_SCTRL - _BGP_DCR_TR_CH0)
#define _BGP_DCR_TR_RSTAT	(_BGP_DCR_TR_CH0_RSTAT - _BGP_DCR_TR_CH0)

/*  hardware specification: 4 bytes address, 256 bytes payload */
#define COL_ALEN	4
#define COL_PAYLOAD	256

#define FRAGMENT_LISTS          256


struct bgpnet_dev
{
  int                  major,minor;        /* device major, minor */
  unsigned long long   physaddr;           /* physical address */
  struct task_struct* current;            /* process holding device */
  int                  signum;             /* signal to send holding process */
  wait_queue_head_t    read_wq;
  int                  read_complete;
  void                 *regs;              /* mapped regs (only used with col) */
  struct semaphore     sem;                /* interruptible semaphore */
  struct cdev          cdev;               /* container device? */
};


struct bgcol_channel {
    phys_addr_t paddr;
    unsigned long mioaddr;
    unsigned int dcrbase;
    unsigned long irq_rcv_pending_mask;
    unsigned long irq_inj_pending_mask;
    struct timer_list inj_timer;
    unsigned int injected;
    unsigned int partial_injections;
    unsigned int unaligned_hdr_injections;
    unsigned int unaligned_data_injections;
    unsigned int received;
    unsigned int inject_fail;
    unsigned int dropped;
    unsigned int delivered;
    unsigned int idx;
    struct bg_col* col;
    struct bgpnet_dev* chrdev;
};

enum {
  k_ethkey_table_size=256
};

struct bg_col_per_eth {
  unsigned char * payload ;
  unsigned int expect ;
};

struct bg_col {
    spinlock_t lock;
    spinlock_t irq_lock;
    struct bgcol_channel chn[BGP_MAX_CHANNEL];
    unsigned int dcrbase;
    unsigned int curr_conn;
    unsigned int nodeid;
    unsigned int inj_wm_mask;
    unsigned int bgnet_channel ;

    unsigned int max_packets_per_frame ;
    unsigned int mtu ;

    /* statistics */
    unsigned fragment_timeout;

     /*  Interrupt management */
    unsigned int handler_running ;
     /*  Transmission items */
      struct bglink_hdr_col lnkhdr_xmit __attribute__((aligned(8))); /* Link header being used for partially-sent skb */
      spinlock_t irq_lock_xmit ;
      struct sk_buff_head skb_list_xmit ;   /* List of skb's to be sent */
      struct sk_buff_head skb_list_free ;   /* Keep a list of skb's to free at user level */
      struct sk_buff * skb_current_xmit ;   /* Partially-sent skb, if any */
      void * current_xmit_data ;            /* Data from current skb adjusted for alignment */
      int current_xmit_len ;                /* Length of current skb data */
      union bgcol_header dest_xmit ;
      unsigned int fragidx_xmit ;

     /*  Reception items */
      struct bglink_hdr_col lnkhdr_rcv __attribute__((aligned(8))); /* Link header pulled out of reception FIFO */
      struct sk_buff_head fragskb_list_rcv  ; /* List of fully-received frames */
      struct sk_buff_head fragskb_list_discard  ; /* List of frames to discard */
      struct sk_buff * skb_in_waiting ;  /*  An skb ready to catch the start of a 'new' frame */
      struct sk_buff * skb_mini ;  /*  A 'miniature' skbuff just right for catching single-packet frames */

      /* Core-to-core items */
      struct sk_buff_head skb_list_for_filling ;
      struct sk_buff_head skb_list_for_delivering ;
      struct sk_buff_head skb_list_for_freeing ;

      unsigned int deliver_without_workqueue ; /* Whether to activate the 'deliver on other core' code for an skbuff */


        struct bgnet_dev *bgnet ;

     /*  Statistics */

         int recv_total ;
         int recv_guess_miss ;
         int recv_no_skbuff ;
         int recv_no_first_packet ;

     /*  'big' tables */
        struct bg_col_per_eth per_eth_table[k_ethkey_table_size] ;
        struct sk_buff * skb_rcv_table[k_ethkey_table_size] ;

     /*  Tuning statistics */
#if defined(KEEP_BG_COL_STATISTICS)
        unsigned int send_fifo_histogram0[16] ;
        unsigned int send_fifo_histogram1[16] ;
        unsigned int recv_fifo_histogram0[16] ;
        unsigned int recv_fifo_histogram1[16] ;
#if defined(EXTRA_TUNING)
        unsigned int send_fifo_histogram2[16] ;
        unsigned int recv_fifo_histogram2[16] ;
#endif
#endif
        unsigned int spurious_interrupts ;
     /*  Diagnostic controls */
        struct ctl_table_header * sysctl_table_header ;
#if defined(HAS_MISSED_INTERRUPT_TIMER)
    struct timer_list missed_interrupt_timer ;
#endif
};

/**********************************************************************
 * driver
 **********************************************************************/

#define COL_DEV_NAME "bgcol"

extern int bgcol_debug_tracemask ;
struct bg_col;

struct bg_col *bgcol_get_dev(void);
void bgcol_enable_interrupts(struct bg_col* col);
unsigned int bgcol_get_nodeid(struct bg_col* col);
void bgcol_link_hdr_init(struct bglink_hdr_col *lnkhdr);
int bgcol_xmit(struct bg_col *col, int chnidx, union bgcol_header dest,
		struct bglink_hdr_col *lnkhdr, void *data, int len);
int __bgcol_xmit(struct bg_col *col, int chnidx, union bgcol_header dest,
		  struct bglink_hdr_col *lnkhdr, void *data, int len);

void bgcol_set_mtu(struct bg_col* col, unsigned int mtu) ;
void bgcol_enable_inj_wm_interrupt(struct bgcol_channel* chn);
void bgcol_disable_inj_wm_interrupt(struct bgcol_channel* chn);
void bgcol_enable_rcv_wm_interrupt(struct bgcol_channel* chn);
void bgcol_disable_rcv_wm_interrupt(struct bgcol_channel* chn);

void bgcol_duplex_slih(unsigned long dummy) ;

int col_start_xmit(struct sk_buff *skb, struct net_device *dev);
int __init bgcol_module_init(void) ;
enum {
	bgcol_diagnostic_use_napi = 1
};
/* extern int bgcol_diagnostic_use_napi ; */

#endif
