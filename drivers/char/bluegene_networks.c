#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/mm.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/pgtable.h>


static int bgpnet_add_device(int major, int minor, const char* name, unsigned long long base);
static int bgpnet_device_open(struct inode *inode, struct file *filp);
static int bgpnet_device_mmap(struct file *filp,  struct vm_area_struct *);
static int bgpnet_device_release(struct inode *inode, struct file * filp);
static int bgpnet_device_ioctl(struct inode *inode, struct file * filp,
                               unsigned int  cmd,   unsigned long arg);


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


static struct file_operations bgpnet_device_fops =
{
  .owner=   THIS_MODULE,
  .open=    bgpnet_device_open,
  .read =   NULL,
  .write=   NULL,
  .poll=    NULL,
  .ioctl=   bgpnet_device_ioctl,
  .release= bgpnet_device_release,
  .mmap=    bgpnet_device_mmap,
};


static int bgpnet_add_device(int major,
                             int minor,
                             const char* devname,
                             unsigned long long physaddr)
{
  int ret;
  dev_t devno;
  struct bgpnet_dev* dev = &bgpnet_devices[bgpnet_num_devices];

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

  if (ret) {
	printk (KERN_WARNING "bgpnet: couldn't register device (%d,%d) err=%d\n",
              major,minor,ret);
	return ret;
  }

  /* add cdev */
  cdev_init(&dev->cdev,&bgpnet_device_fops);
  dev->cdev.owner=THIS_MODULE;
  dev->cdev.ops=&bgpnet_device_fops;
  ret=cdev_add(&dev->cdev,devno,1);
  if (ret) {
      printk(KERN_WARNING "bgpnet: couldn't register device (%d,%d), err=%d\n",
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

  return 0;
}



static int bgpnet_device_mmap(struct file *filp, struct vm_area_struct *vma)
{
  unsigned long vsize = vma->vm_end - vma->vm_start;
  struct bgpnet_dev * device = (struct bgpnet_dev *)filp->private_data;
  int ret = -1;

  vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
  vma->vm_flags     |= VM_IO;
  vma->vm_flags     |= VM_RESERVED;

  if (device->physaddr != 0)
    ret = remap_pfn_range(vma,
                          vma->vm_start,
                          device->physaddr >> PAGE_SHIFT,
                          vsize,
                          vma->vm_page_prot);

  if (ret)
      printk (KERN_WARNING "bgpnet: mapping of device (%d,%d) failed\n",
                   device->major, device->minor);

  return ret? -EAGAIN :0;
}


static int bgpnet_device_release (struct inode *inode, struct file * filp)
{
  struct bgpnet_dev *dev=(struct bgpnet_dev *)filp->private_data;

  /*Ensure exclusive access*/
  if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

  dev->current = NULL;
  up(&dev->sem);

  return 0;
}


static int bgpnet_device_ioctl (struct inode *inode,
                                struct file * filp,
                                unsigned int cmd,
                                unsigned long arg)
{
  return 0;
}


static int __init bgpnet_module_init(void)
{
	int rc = 0;
	unsigned long long tr0, tr1, ts0, ts1;

	tr0=((unsigned long long) _BGP_UA_COL0 << 32)  + _BGP_PA_COL0;
	tr1=((unsigned long long) _BGP_UA_COL1 << 32)  + _BGP_PA_COL1;
	ts0=((unsigned long long) _BGP_UA_TORUS0 << 32) + _BGP_PA_TORUS0;
	ts1=((unsigned long long) _BGP_UA_TORUS1 << 32) + _BGP_PA_TORUS1;

	bgpnet_add_device(BGP_COL_MAJOR_NUM,  0,"bgptree_vc0", tr0);
	bgpnet_add_device(BGP_COL_MAJOR_NUM,  1, "bgptree_vc1", tr1);
	bgpnet_add_device(BGP_TORUS_MAJOR_NUM, 0, "bgptorus_g0", ts0);
	bgpnet_add_device(BGP_TORUS_MAJOR_NUM, 1, "bgptorus_g1", ts1);

	return rc;
}


module_init(bgpnet_module_init);
