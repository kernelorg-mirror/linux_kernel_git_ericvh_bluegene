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
 * Description: Blue Gene low-level driver for tree
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
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/tcp.h>
#include <linux/KernelFxLog.h>

#include <net/arp.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/irq.h>
#ifdef CONFIG_PPC_MERGE
#include <asm/prom.h>
#include <asm/of_platform.h>
#endif

#include <asm/bgp_personality.h>
#include <asm/bluegene.h>


/* #include "bgnic.h" */
/* #include "bgcol.h" */

#define TORUS_DEV_NAME "bgtorus"
#include "../bgp_network/bgp_net_traceflags.h"

int __init
bgtornet_module_init(void) ;
int __init
bgtornet_module_exit(void) ;
int __exit
dma_tcp_module_init(void) ;
int __exit
dma_tcp_module_cleanup(void) ;

typedef struct {
  struct sk_buff_head skb_list_xmit ;   /* List of skb's being passed to the tasklet for sending */
} bg_tcptorus ;

static bg_tcptorus static_torus ;

typedef struct {
  unsigned char x ;
  unsigned char y ;
  unsigned char z ;
} torusTarget_t ;

/* #define CONFIG_BLUEGENE_TORUS_TRACE */

#if defined(CONFIG_BLUEGENE_TORUS_TRACE)
/* int bgtorus_debug_tracemask=k_t_general|k_t_lowvol|k_t_irqflow|k_t_irqflow_rcv|k_t_protocol ; */
/* int bgtorus_debug_tracemask=k_t_protocol; */
int bgtorus_debug_tracemask=k_t_init ;
#endif

#if defined(REQUIRE_TRACE)
#define TRACE(x...)    KernelFxLog(1,x)
#define TRACE1(x...)   KernelFxLog(1,x)
#define TRACE2(x...)   KernelFxLog(1,x)
#define TRACEN(i,x...) KernelFxLog(1,x)
#define TRACED(x...)   KernelFxLog(1,x)
#define TRACES(x...)   KernelFxLog(1,x)
#elif  defined(CONFIG_BLUEGENE_TORUS_TRACE)
#define TRACE(x...)    KernelFxLog(bgtorus_debug_tracemask & k_t_general,x)
#define TRACE1(x...)   KernelFxLog(bgtorus_debug_tracemask & k_t_lowvol,x)
#define TRACE2(x...)   KernelFxLog(bgtorus_debug_tracemask & k_t_detail,x)
#define TRACEN(i,x...) KernelFxLog(bgtorus_debug_tracemask & (i),x)
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

/* #define HAS_HOSTS */
/* #define HAS_NICPARM */
/* #define HAS_DRIVERPARM */
#define HAS_TORUSDIAG

/*  If you need settable parameters for the tree or the NIC (for debugging), enable them here */
#if defined(HAS_DRIVERPARM)
static int bgtorus_driverparm ;
#endif

#if defined(HAS_NICPARM)
extern int bgnic_driverparm ;
#endif

/* void torus_learn_host(const char *cp) ; */

int bgp_dma_ethem ;  /*  Set externally if we want to try 'eth-em' on torus */

/* #define SENDS_WITH_TASKLET */

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
 * device management
 */
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


#define BGP_MAX_DEVICES 8
static struct bgpnet_dev bgpnet_devices[BGP_MAX_DEVICES];
static unsigned int bgpnet_num_devices = 0;


static int bgtorus_mappable_module_init(void) ;

static int bgpnet_add_device(int major, int minor, const char* name,
                             unsigned long long base, int irq,
                             irqreturn_t (*irq_handler)(int, void*));
static int bgpnet_device_open(struct inode *inode, struct file *filp);
static int bgpnet_device_mmap(struct file *filp,  struct vm_area_struct *);
static int bgpnet_device_release(struct inode *inode, struct file * filp);
static int bgpnet_device_ioctl(struct inode *inode, struct file * filp,
                               unsigned int  cmd,   unsigned long arg);


static struct file_operations bgpnet_device_fops =
{
  .owner=   THIS_MODULE,
  .open=    bgpnet_device_open,
  .read=    NULL,
  .write=   NULL,
  .poll=    NULL,
  .ioctl=   bgpnet_device_ioctl,
  .release= bgpnet_device_release,
  .mmap=    bgpnet_device_mmap,
};



#if defined(HAS_TORUSDIAG)
void torus_diag(int param) ;  /*  So we can drive a function in the torus layer to poke at things */
#endif

void bgp_dma_tcp_send_and_free( struct sk_buff *skb ) ;

void bgp_dma_tcp_poll(void) ;


int col_start_xmit(struct sk_buff *skb, struct net_device *dev) ;
/*  We have a frame which should be routable via the torus. */
/*  For code path checkout, try it via the tree ... */
int bgtorus_start_xmit(struct sk_buff *skb, struct net_device *dev
/* 		, unsigned int x, unsigned int y, unsigned int z */
		)
{
/*   int ethem = bgp_dma_ethem ; */
/*   TRACEN(k_t_general,"(>) %s:%d", __func__, __LINE__) ; */
/*   if( 0 == ethem ) */
/*     { */
/*       col_start_xmit(skb, dev) ; */
/*     } */
/*   else */
/*     { */
/*       struct inet_connection_sock *icskp = inet_csk(skb->sk) ; */
/*       if( ethem & 4) */
/*         { */
/*           // Feature for duplicating the frame over the tree, so we can take the torus 'through the motions' */
/*           // as we bring up various drivers */
/*           struct sk_buff *cloneskb = skb_clone(skb, GFP_ATOMIC) ; */
/*           if( cloneskb) */
/*             { */
/*                col_start_xmit(cloneskb, dev) ; */
/*             } */
/*         } */
/*     #if defined(CONFIG_BLUEGENE_TCP) */
/*         if( 1 ) */
/*         { */
              bgp_dma_tcp_send_and_free(skb
/*         		      ,x,y,z */
			      ) ;
/*  */
/*           } */
/*       else */
/*         { */
/*           col_start_xmit(skb, dev) ; */
/*         } */
/*     #else */
/*       col_start_xmit(skb, dev) ; */
/*     #endif */
/*     } */
  TRACEN(k_t_general,"(<) %s:%d", __func__, __LINE__) ;
  return 0 ;
}

static int bgtorus_proc_read (char *page, char **start, off_t off,
          int count, int *eof, void *data)
{
    int remaining = count;
    *eof = 1;

    return count-remaining ;
}

#if defined(CONFIG_BLUEGENE_TORUS_TRACE) || defined(HAS_DRIVERPARM) || defined(HAS_NICPARM) || defined(HAS_TORUSDIAG)
static unsigned char xtable[256] =
    {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };

static int bgtorus_atoix(const unsigned char *cp)
  {
    int result = 0 ;
    unsigned char ecp = xtable[*cp] ;
    while (ecp < 0x10)
      {
        result = (result << 4 ) | ecp ;
        cp += 1 ;
        ecp = xtable[*cp] ;
      }
    return result ;
  }
#endif

static int bgtorus_proc_write(struct file *filp, const char __user *buff, unsigned long len, void *data)
  {
    char proc_write_buffer[256] ;
    unsigned long actual_len=(len<255) ? len : 255 ;
    int rc = copy_from_user( proc_write_buffer, buff, actual_len ) ;
    if( rc != 0 ) return -EFAULT ;
    proc_write_buffer[actual_len] = 0 ;
#if defined(HAS_DRIVERPARM)
    if( 0 == strncmp(proc_write_buffer,"driverparm=",11))
      {
        bgtorus_driverparm=bgtorus_atoix(proc_write_buffer+11) ;
      }
#endif
#if defined(HAS_NICPARM)
    if( 0 == strncmp(proc_write_buffer,"nicparm=",8))
      {
        bgnic_driverparm=bgtorus_atoix(proc_write_buffer+8) ;
      }
#endif
#if defined(CONFIG_BLUEGENE_TORUS_TRACE)
    if ( 0 == strncmp(proc_write_buffer,"tracemask=",10) )
      {
        bgtorus_debug_tracemask = bgtorus_atoix(proc_write_buffer+10) ;
      }
#endif
#if defined(HAS_TORUSDIAG)
    if ( 0 == strncmp(proc_write_buffer,"torusdiag=",10) )
      {
        int diag_opcode = bgtorus_atoix(proc_write_buffer+10) ;
        torus_diag(diag_opcode) ;
      }
#endif

    return actual_len ;
  }

#if defined(TCP_TORUS_AVAILABLE)
extern BGP_Personality_t tcp_bgp_personality;
#endif


static int __init
torus_module_init (void)
{
  struct proc_dir_entry *ent;
  TRACEN(k_t_init,"torus_module_init") ;
  /* ----------------------------------------------------- */
  /*        create /proc entry                             */
  /* ----------------------------------------------------- */
  printk(KERN_INFO "%s:%d create proc ent \n", __func__, __LINE__);
  ent = create_proc_entry("driver/" TORUS_DEV_NAME, S_IRUGO, NULL);
  if (ent)
  {
      ent->nlink = 1;
      ent->read_proc = (void *)bgtorus_proc_read;
      ent->write_proc = (void *)bgtorus_proc_write;
  }
#if defined(TCP_TORUS_AVAILABLE)
  bluegene_getPersonality( &tcp_bgp_personality, sizeof(tcp_bgp_personality) );
  printk(KERN_NOTICE "Network_Config.Rank=%08x Network_Config.IOnodeRank=%08x\n",
      tcp_bgp_personality.Network_Config.Rank,
      tcp_bgp_personality.Network_Config.IOnodeRank
      ) ;
#endif
  skb_queue_head_init(&static_torus.skb_list_xmit) ;
   /*  Bring up the memory-mappable version */
  bgtorus_mappable_module_init() ;
   /*  NIC and IP driver initialisation */
  bgtornet_module_init() ;
  dma_tcp_module_init() ;
  return 0 ;
}

static void __exit
torus_module_exit (void)
{
  TRACEN(k_t_init,"torus_module_exit") ;
  bgtornet_module_exit() ;
/*   dma_tcp_module_cleanup() ; */
}
/*  Code grabbed from Rch's driver so that we can map the torus for user-space access */


static int bgpnet_add_device(int major,
                             int minor,
                             const char* devname,
                             unsigned long long physaddr,
                             int irq,
                             irqreturn_t (*irq_handler)(int, void *))
{
  int ret;
  dev_t devno;
  struct bgpnet_dev* dev = &bgpnet_devices[bgpnet_num_devices];
  TRACEN(k_t_init,"bgpnet_add_device devname=%s",devname) ;
  /* initilize struct */
  init_MUTEX (&dev->sem);
  dev->major  = major;
  dev->minor  = minor;
  dev->physaddr = physaddr;
  init_waitqueue_head(&dev->read_wq);
  dev->read_complete = 0;
  if (physaddr) {
          dev->regs = ioremap(physaddr, 4096);
  }
  devno=MKDEV(major,minor);

  /* register i.e., /proc/devices */
  ret=register_chrdev_region(devno,1,(char *)devname);

  if (ret)
    {
      printk (KERN_WARNING "bgpnet: couldn't register device (%d,%d) register_chrdev_region err=%d\n",
              major,minor,ret);
      return ret;
    }

  /* add cdev */
  cdev_init(&dev->cdev,&bgpnet_device_fops);
  dev->cdev.owner=THIS_MODULE;
  dev->cdev.ops=&bgpnet_device_fops;
  ret=cdev_add(&dev->cdev,devno,1);
  if (ret)
    {
      printk(KERN_WARNING "bgpnet: couldn't register device (%d,%d) cdev_add err=%d\n",
             major,minor,ret);
      return ret;
    }

  /* signul to pass to owning process, should be altered using ioctl */
  dev->signum=-1;

  bgpnet_num_devices++;

  return 0;
}


static int bgpnet_device_open (struct inode *inode, struct file *filp)
{
  struct bgpnet_dev *dev=container_of(inode->i_cdev,struct bgpnet_dev,cdev);

  if(down_interruptible(&dev->sem)) return -ERESTARTSYS;
  up(&dev->sem);

  dev->current=current;
  filp->private_data = (void*) dev;

  TRACE("bgpnet: device (%d,%d) opened by process \"%s\" pid %i",
        MAJOR(inode->i_rdev), MINOR(inode->i_rdev), current->comm, current->pid);

  return 0;
}




static int bgpnet_device_mmap(struct file *filp, struct vm_area_struct *vma)
{
  unsigned long vsize = vma->vm_end - vma->vm_start;
  struct bgpnet_dev * device = (struct bgpnet_dev *)filp->private_data;
  int ret = -1;

  /* ------------------------------------------------------- */
  /* set up page protection.                                 */
  /* ------------------------------------------------------- */

  vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
  vma->vm_flags     |= VM_IO;
  vma->vm_flags     |= VM_RESERVED;

  /* ------------------------------------------------------- */
  /*                  do the mapping                         */
  /* ------------------------------------------------------- */

  if (device->physaddr != 0)
    ret = remap_pfn_range(vma,
                          vma->vm_start,
                          device->physaddr >> PAGE_SHIFT,
                          vsize,
                          vma->vm_page_prot);

  if (ret) {
      printk (KERN_WARNING "bgpnet: mapping of device (%d,%d) failed\n",
                   device->major, device->minor);
  } else {
      TRACE("bgpnet: mapped (%d,%d) to vm=%lx",
             device->major, device->minor, vma->vm_start);
  }
  return ret? -EAGAIN :0;
}

/* ************************************************************************* */
/*                  BG/P network: release device                             */
/* ************************************************************************* */

static int bgpnet_device_release (struct inode *inode, struct file * filp)
{
  struct bgpnet_dev *dev=(struct bgpnet_dev *)filp->private_data;

  /*Ensure exclusive access*/
  if(down_interruptible(&dev->sem)) return -ERESTARTSYS;

  dev->current = NULL;
  up(&dev->sem);

  TRACE("bgpnet: device (%d,%d) successfully released",
         MAJOR(inode->i_rdev), MINOR(inode->i_rdev));
  return 0;
}


static int bgpnet_device_ioctl (struct inode *inode,
                                struct file * filp,
                                unsigned int cmd,
                                unsigned long arg)
{
  return 0;
}

static int bgtorus_mappable_module_init(void)
{
/*     unsigned long long tr0, tr1; */
    unsigned long long ts0, ts1;

    TRACEN(k_t_init,"bgtorus_mappable_module_init") ;

/*     tr0=((unsigned long long)_BGP_UA_COL0<<32)  + _BGP_PA_COL0; */
/*     tr1=((unsigned long long)_BGP_UA_COL1<<32)  + _BGP_PA_COL1; */
    ts0=((unsigned long long)_BGP_UA_TORUS0<<32) + _BGP_PA_TORUS0;
    ts1=((unsigned long long)_BGP_UA_TORUS1<<32) + _BGP_PA_TORUS1;

    bgpnet_add_device(BGP_TORUS_MAJOR_NUM, 0, "bgptorus_g0", ts0, -1, NULL);
    bgpnet_add_device(BGP_TORUS_MAJOR_NUM, 1, "bgptorus_g1", ts1, -1, NULL);

    mb();

    return 0;

}


/* module_init(bgtorus_mappable_module_init); */

module_init(torus_module_init);
module_exit(torus_module_exit);
