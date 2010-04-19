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
 *		'ioctl' and 'procfs' support
 *
 ********************************************************************/
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

#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/bootmem.h>


#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/time.h>
#include <linux/vmalloc.h>

#include <linux/dma-mapping.h>

#include <net/inet_connection_sock.h>
#include <net/inet_sock.h>
#include <net/inet_hashtables.h>
#include <net/tcp.h>
#include <net/tcp_hiatus.h>

#include <spi/linux_kernel_spi.h>

#include "bgp_dma_tcp.h"

#include "bgp_bic_diagnosis.h"
#include "../bgp_network/bgdiagnose.h"

/* #define TRUST_TORUS_CRC */

#define SEND_SHORT_FRAMES_INLINE
#define ENABLE_TUNING

#define ENABLE_LEARNING_ADDRESSES

#if !defined(CONFIG_BLUEGENE_TCP_WITHOUT_NAPI)
/*  Select operation with linux 'dev->poll' */
#define TORNIC_DEV_POLL

/* #if defined(CONFIG_SMP) && !defined(CONFIG_BLUEGENE_UNIPROCESSOR) && !defined(CONFIG_BGP_VRNIC) */
/* #define TORNIC_STEAL_POLL_CORE */
/* #endif */

#endif

#if defined(CONFIG_TCP_CONGESTION_OVERRIDES)
extern int sysctl_tcp_force_nodelay ;
extern int sysctl_tcp_permit_cwnd ;
extern int sysctl_tcp_max_cwnd ;
#endif

int sysctl_bgp_torus_backlog_floor ;
int bgp_dma_sockproto ;  /*  Used elsewhere to control whether we try accelerated sockets */

extern int bgtornic_driverparm ;  /*  Parametrisation for bringup of 'tornic' device */

static int proc_dodcr(struct ctl_table *ctl, int write, struct file * filp,
               void __user *buffer, size_t *lenp, loff_t *ppos) ;

static int proc_dodcr_c8b(struct ctl_table *ctl, int write, struct file * filp,
               void __user *buffer, size_t *lenp, loff_t *ppos) ;

static int proc_dodcr(struct ctl_table *ctl, int write, struct file * filp,
               void __user *buffer, size_t *lenp, loff_t *ppos)
  {
    int rc ;
    TRACE("(>)ctl=%p write=%d len=%d", ctl,write,*lenp) ;
    dma_tcp_state.tuning_recfifo_threshold=mfdcrx(0xd3a) ;
    rc = proc_dointvec(ctl,write,filp,buffer,lenp,ppos) ;
    mtdcrx(0xd3a,dma_tcp_state.tuning_recfifo_threshold) ;
    TRACE("(<)") ;
    return rc ;
  }

static int proc_dodcr_c8b(struct ctl_table *ctl, int write, struct file * filp,
               void __user *buffer, size_t *lenp, loff_t *ppos)
  {
    int rc ;
    dumptorusdcrs() ;
    TRACE("(>)ctl=%p write=%d len=%d", ctl,write,*lenp) ;
    dma_tcp_state.tuning_dcr_c8b=mfdcrx(0xc8b) ;
    rc = proc_dointvec(ctl,write,filp,buffer,lenp,ppos) ;
    mtdcrx(0xc8b,dma_tcp_state.tuning_dcr_c8b) ;
    TRACE("(<)") ;
    return rc ;
  }



static struct ctl_path bgp_torus_ctl_path[] = {
	{ .procname = "bgp", .ctl_name = 0, },
	{ .procname = "torus", .ctl_name = 0, },
	{ },
};

#define CTL_PARAM(Name,Var)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .data           = &dma_tcp_state.Var , \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_dointvec       \
  }

#define CTL_PARAM_DCR(Name,Var)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .data           = &dma_tcp_state.Var , \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_dodcr       \
  }

#define CTL_PARAM_DCR_C8B(Name,Var)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .data           = &dma_tcp_state.Var , \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_dodcr_c8b       \
  }

#define CTL_PARAM_HWFIFO(Name,Var)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .data           = &dma_tcp_state.Var , \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_dohwfifo       \
  }

struct ctl_table bgp_dma_table[] = {
#if defined(USE_SKB_TO_SKB)
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "dma_rec_counters",
	                .data           = bgp_dma_tcp_counter_copies,
	                .maxlen         = DMA_NUM_COUNTERS_PER_GROUP*sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_do_dma_rec_counters
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "flow_counter",
	                .data           = dma_tcp_state.flow_counter,
	                .maxlen         = k_flow_counters*sizeof(int),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
#endif
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "tracemask",
                .data           = &bgp_dma_tcp_tracemask,
                .maxlen         = sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "backlog_floor",
                .data           = &sysctl_bgp_torus_backlog_floor,
                .maxlen         = sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "sockproto",
                .data           = &bgp_dma_sockproto,
                .maxlen         = sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "ethem",
                .data           = &bgp_dma_ethem,
                .maxlen         = sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "tornic_driverparm",
                .data           = &bgtornic_driverparm,
                .maxlen         = sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
/*         { */
/*                 .ctl_name       = CTL_UNNUMBERED, */
/*                 .procname       = "tornic_count", */
/*                 .data           = &bgp_tornic_count, */
/*                 .maxlen         = sizeof(int), */
/*                 .mode           = 0644, */
/*                 .proc_handler   = &proc_dointvec */
/*         }, */
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "tx_by_core",
                .data           = dma_tcp_state.tx_by_core,
                .maxlen         = 4*sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "tx_in_use_count",
                .data           = dma_tcp_state.tx_in_use_count,
                .maxlen         = (k_injecting_directions+1)*sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
#if defined(TRACK_LIFETIME_IN_FIFO)
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "max_lifetime_by_direction",
                .data           = max_lifetime_by_direction,
                .maxlen         = (k_injecting_directions)*sizeof(unsigned long long),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
#endif
        CTL_PARAM("bluegene_tcp_is_built",bluegene_tcp_is_built) ,
        CTL_PARAM("count_no_skbuff",count_no_skbuff) ,
#if defined(USE_SKB_TO_SKB)
        CTL_PARAM("eager_limit",eager_limit) ,
#endif
#if defined(CONFIG_BGP_STATISTICS)
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "reception_fifo_histogram",
                .data           = reception_fifo_histogram,
                .maxlen         = 33*sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "reception_fifo_histogram",
                .data           = reception_fifo_histogram,
                .maxlen         = 33*sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "reception_hi_watermark",
                .data           = &reception_hi_watermark,
                .maxlen         = sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "rtt_histogram",
                .data           = rtt_histogram,
                .maxlen         = 33*sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "transit_histogram",
                .data           = transit_histogram,
                .maxlen         = 33*sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "resequence_histogram",
                .data           = dma_tcp_state.resequence_histogram,
                .maxlen         = k_concurrent_receives*sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "bytes_sent",
                .data           = &dma_tcp_state.bytes_sent,
                .maxlen         = sizeof(unsigned long long),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "bytes_received",
                .data           = &dma_tcp_state.bytes_received,
                .maxlen         = sizeof(unsigned long long),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
#endif

#if defined(CONFIG_TCP_HIATUS_COUNTS)
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "tcp_hiatus_counts",
		.data		= tcp_hiatus_counts,
		.maxlen		= k_tcp_hiatus_reasons*sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "tcp_force_nodelay",
		.data		= &sysctl_tcp_force_nodelay,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "tcp_permit_cwnd",
		.data		= &sysctl_tcp_permit_cwnd,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "tcp_max_cwnd",
		.data		= &sysctl_tcp_max_cwnd,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#endif

#if defined(ENABLE_TUNING)
        CTL_PARAM("tuning_num_packets",tuning_num_packets) ,
        CTL_PARAM("tuning_num_empty_passes",tuning_num_empty_passes) ,
        CTL_PARAM("tuning_non_empty_poll_delay",tuning_non_empty_poll_delay) ,
        CTL_PARAM("tuning_poll_after_enabling",tuning_poll_after_enabling) ,
        CTL_PARAM("tuning_run_handler_on_hwi",tuning_run_handler_on_hwi) ,
        CTL_PARAM("tuning_clearthresh_slih",tuning_clearthresh_slih) ,
        CTL_PARAM("tuning_clearthresh_flih",tuning_clearthresh_flih) ,
        CTL_PARAM("tuning_disable_in_dcr",tuning_disable_in_dcr) ,

        CTL_PARAM("tuning_injection_hashmask",tuning_injection_hashmask) ,

        CTL_PARAM_DCR("tuning_recfifo_threshold",tuning_recfifo_threshold) ,

        CTL_PARAM("tuning_exploit_reversepropose",tuning_exploit_reversepropose) ,
        CTL_PARAM("tuning_counters_per_source",tuning_counters_per_source) ,
        CTL_PARAM("tuning_defer_skb_until_counter",tuning_defer_skb_until_counter) ,
        CTL_PARAM("tuning_deliver_eagerly",tuning_deliver_eagerly) ,
        CTL_PARAM("tuning_diagnose_rst",tuning_diagnose_rst) ,
        CTL_PARAM("tuning_select_fifo_algorithm",tuning_select_fifo_algorithm) ,
        CTL_PARAM("tuning_min_icsk_timeout",tuning_min_icsk_timeout) ,
        CTL_PARAM("tuning_virtual_channel",tuning_virtual_channel) ,

        CTL_PARAM_DCR_C8B("tuning_dcr_c8b",tuning_dcr_c8b) ,
#endif
#if defined(CONFIG_BGP_TORUS_DIAGNOSTICS)
        {
                .ctl_name       = CTL_UNNUMBERED,
                .procname       = "tcp_scattergather_frag_limit",
                .data           = &tcp_scattergather_frag_limit,
                .maxlen         = sizeof(int),
                .mode           = 0644,
                .proc_handler   = &proc_dointvec
        },
#endif

#if defined(KEEP_TCP_FLAG_STATS)
        CTL_PARAM("tcp_count_fin",tcp_received_flag_count[7]) ,
        CTL_PARAM("tcp_count_syn",tcp_received_flag_count[6]) ,
        CTL_PARAM("tcp_count_rst",tcp_received_flag_count[5]) ,
        CTL_PARAM("tcp_count_psh",tcp_received_flag_count[4]) ,
        CTL_PARAM("tcp_count_ack",tcp_received_flag_count[3]) ,
        CTL_PARAM("tcp_count_urg",tcp_received_flag_count[2]) ,
        CTL_PARAM("tcp_count_ece",tcp_received_flag_count[1]) ,
        CTL_PARAM("tcp_count_cwr",tcp_received_flag_count[0]) ,
#endif
        { 0 },
};

static void __init
register_torus_sysctl(dma_tcp_t *dma_tcp)
{
	dma_tcp->sysctl_table_header=register_sysctl_paths(bgp_torus_ctl_path,bgp_dma_table) ;
	TRACEN(k_t_init, "sysctl_table_header=%p",dma_tcp->sysctl_table_header) ;

}

/*  feature for exploring all-to-all performance with a device in /dev */
static int bgpdmatcp_add_device(int major, int minor, const char* name);
static int bgpdmatcp_device_open(struct inode *inode, struct file *filp);
static int bgpdmatcp_device_release(struct inode *inode, struct file * filp);
static long bgpdmatcp_device_ioctl( struct file * filp,
                               unsigned int  cmd,   unsigned long arg);
enum {
	k_bgpdmatcp_major = 126 ,
	k_bgpdmatcp_minor_nums = 1
} ;

struct bgpdmatcp_dev
{
  int                  major,minor;        /* device major, minor */
  struct task_struct* current;            /* process holding device */
  int                  signum;             /* signal to send holding process */
  wait_queue_head_t    read_wq;
  int                  read_complete;
  struct semaphore     sem;                /* interruptible semaphore */
  struct cdev          cdev;               /* container device? */
};


static struct bgpdmatcp_dev bgpdmatcp_device;


static struct file_operations bgpdmatcp_device_fops =
	{
	  .owner=   THIS_MODULE,
	  .open=    bgpdmatcp_device_open,
	  .read =   NULL,
	  .write=   NULL,
	  .poll=    NULL,
	  .unlocked_ioctl=   bgpdmatcp_device_ioctl,
	  .release= bgpdmatcp_device_release,
	  .mmap=    NULL,
	};


static int bgpdmatcp_add_device(int major,
			     int minor,
			     const char* devname
			     )
{
  int ret;
  dev_t devno;
  struct bgpdmatcp_dev* dev = &bgpdmatcp_device;

  /* initilize struct */
  init_MUTEX (&dev->sem);
  dev->major  = major;
  dev->minor  = minor;
  init_waitqueue_head(&dev->read_wq);
  dev->read_complete = 0;
  devno=MKDEV(major,minor);

  /* register i.e., /proc/devices */
  ret=register_chrdev_region(devno,1,(char *)devname);

  if (ret) {
	printk (KERN_WARNING "bgpdmatcp: couldn't register device (%d,%d) err=%d\n",
	      major,minor,ret);
	return ret;
  }

  /* add cdev */
  cdev_init(&dev->cdev,&bgpdmatcp_device_fops);
  dev->cdev.owner=THIS_MODULE;
  dev->cdev.ops=&bgpdmatcp_device_fops;
  ret=cdev_add(&dev->cdev,devno,1);
  if (ret) {
      printk(KERN_WARNING "bgpdmatcp: couldn't register device (%d,%d), err=%d\n",
	     major,minor,ret);
      return ret;
  }

  /* signul to pass to owning process, should be altered using ioctl */
  dev->signum=-1;


  return 0;
}


static int bgpdmatcp_device_open (struct inode *inode, struct file *filp)
{
  struct bgpdmatcp_dev *dev=container_of(inode->i_cdev,struct bgpdmatcp_dev,cdev);

  if(down_interruptible(&dev->sem)) return -ERESTARTSYS;
  up(&dev->sem);

  dev->current=current;
  filp->private_data = (void*) dev;

  return 0;
}





static int bgpdmatcp_device_release (struct inode *inode, struct file * filp)
{
  struct bgpdmatcp_dev *dev=(struct bgpdmatcp_dev *)filp->private_data;

  /*Ensure exclusive access*/
  if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

  dev->current = NULL;
  up(&dev->sem);

  return 0;
}

/* Report the counts of how often a TCP write has stalled, by stall reason */
static void bgp_dma_diag_report_hiatus_counts(int __user * report)
{
	copy_to_user(report,tcp_hiatus_counts,k_tcp_hiatus_reasons*sizeof(int)) ;
}

/* Report bytes read and bytes written over the torus */
static void bgp_dma_diag_report_transfer_counts(int __user * report)
{
	copy_to_user(report,&dma_tcp_state.bytes_received,sizeof(unsigned long long)) ;
	copy_to_user(report+sizeof(unsigned long long)/sizeof(int),&dma_tcp_state.bytes_sent,sizeof(unsigned long long)) ;
}


enum {
	k_ioctl_activate = 0 ,
	k_ioctl_wait = 1 ,
	k_ioctl_clearcount = 2 ,
	k_ioctl_activate_minicube = 3 ,
	k_ioctl_wait_sync = 4 ,
	k_ioctl_activate_to_one = 5 ,
	k_ioctl_report_tx_queue = 6 ,
	k_ioctl_report_hiatus_counts = 7 ,
	k_ioctl_report_bytes_transferred = 8
};
static long bgpdmatcp_device_ioctl (
				struct file * filp,
				unsigned int cmd,
				unsigned long arg)
{
	TRACEN(k_t_detail, "cmd=%d arg=0x%08lx",cmd,arg) ;

	switch (cmd) {
		case k_ioctl_activate :
			{
				int sendBytes ;
				if( get_user(sendBytes,(int __user *)arg) )
					{
						return -EFAULT ;
					}
				if( sendBytes <= k_injection_packet_size)
					{
						dma_tcp_transfer_activate_sync(sendBytes) ;
					}
/* 				else */
/* 					{ */
/* 						dma_tcp_transfer_activate(sendBytes) ; */
/* 					} */
			}
			break ;
/* #if 0 */
/* 		case k_ioctl_wait : */
/* 			{ */
/* 				int demandCount ; */
/* 				int rc ; */
/* 				if( get_user(demandCount,(int __user *)arg) ) */
/* 					{ */
/* 						return -EFAULT ; */
/* 					} */
/* 				rc = dma_tcp_transfer_wait(demandCount) ; */
/* 				return rc ? 0 : (-EAGAIN) ; */
/* 			} */
/* 			break ; */
/* #endif */
		case k_ioctl_wait_sync :
			{
				int demandCount ;
				int rc ;
				if( get_user(demandCount,(int __user *)arg) )
					{
						return -EFAULT ;
					}
				rc = dma_tcp_transfer_wait_sync(demandCount) ;
				return rc ? 0 : (-EAGAIN) ;
			}
			break ;
		case k_ioctl_clearcount :
			dma_tcp_transfer_clearcount() ;
			break ;
/* #if 0 */
/* 		case k_ioctl_activate_minicube : */
/* 			{ */
/* 				int sendBytes ; */
/* 				if( get_user(sendBytes,(int __user *)arg) ) */
/* 					{ */
/* 						return -EFAULT ; */
/* 					} */
/* 				dma_tcp_transfer_activate_minicube(sendBytes) ; */
/* 			} */
/* 			break ; */
/* 		case k_ioctl_activate_to_one : */
/* 			{ */
/* 				int sendBytes ; */
/* 				unsigned int tg ; */
/* 				if( get_user(sendBytes,(int __user *)arg) ) */
/* 					{ */
/* 						return -EFAULT ; */
/* 					} */
/* 				if( get_user(tg,(int __user *)(arg+sizeof(int))) ) */
/* 					{ */
/* 						return -EFAULT ; */
/* 					} */
/* 				dma_tcp_transfer_activate_to_one(sendBytes,tg) ; */
/* 			} */
/* 			break ; */
/* #endif */
		case k_ioctl_report_tx_queue :
			bgp_dma_diag_report_transmission_queue((int __user *)arg) ;
			break ;
		case k_ioctl_report_hiatus_counts :
			bgp_dma_diag_report_hiatus_counts((int __user *)arg) ;
			break ;
		case k_ioctl_report_bytes_transferred :
			bgp_dma_diag_report_transfer_counts((int __user *)arg) ;
			break ;
	}
  return 0;
}

void __init
dma_tcp_devfs_procfs_init(dma_tcp_t * dma_tcp)
{
    bgpdmatcp_add_device(k_bgpdmatcp_major,0,"bgpdmatcp") ;
    register_torus_sysctl(dma_tcp) ;
}
