/****************************************************************************/
/* ZEPTOOS:zepto-info */
/*     This file is part of ZeptoOS: The Small Linux for Big Computers.
 *     See www.mcs.anl.gov/zeptoos for more information.
 */
/* ZEPTOOS:zepto-info */
/* */
/* ZEPTOOS:zepto-fillin */
/*     $Id:  $
 *     ZeptoOS_Version: 2.0
 *     ZeptoOS_Heredity: FOSS_ORIG
 *     ZeptoOS_License: GPL
 */
/* ZEPTOOS:zepto-fillin */
/* */
/* ZEPTOOS:zepto-gpl */
/*      Copyright: Argonne National Laboratory, Department of Energy,
 *                 and UChicago Argonne, LLC.  2004, 2005, 2006, 2007, 2008
 *      ZeptoOS License: GPL
 * 
 *      This software is free.  See the file ZeptoOS/misc/license.GPL
 *      for complete details on your rights to copy, modify, and use this
 *      software.
 */
/* ZEPTOOS:zepto-gpl */
/****************************************************************************/
/* 
   bigmem explicit mmap is only available for ION node
*/

#ifndef CONFIG_ZEPTO_COMPUTENODE

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>

#include <linux/zepto_debug.h>

#define __ZCL_KERNEL__

#include <linux/zepto_task.h>

static int zeptobigmem_open( struct inode* inode, struct file* filp )
{
    zepto_debug(2,"zeptobigmem_open()\n");

    if( !enable_bigmem ) {
	printk(KERN_ERR "bigmem is not enabled\n");
	return -ENOMEM;
    }
    if( get_bigmem_region_start() < 0xffffffff ) { 
	printk(KERN_WARNING "bigmem is in use\n");
	return -ENOMEM;
    }
    return 0;
}

static int zeptobigmem_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned size = vma->vm_end - vma->vm_start;

    zepto_debug(2,"zeptobigmem_mmap [%08lx, %08lx)\n",  vma->vm_start, vma->vm_end );

    if( (vma->vm_start&0x0fffffff) != 0 ) {
	printk(KERN_ERR "[Z] bigmem start address should be 256MB-aligned.  vma_start=%08lx\n",
	       vma->vm_start );
	return -EAGAIN;
    }
    if( ((size&0x00ffffff)!=0)  || size > get_bigmem_size() ) {
	printk(KERN_ERR "[Z] invalid bigmem size.  size=%08x\n", size);
	return -EAGAIN;
    }

    /* set bigmem start (virtual) address */
    if( init_bigmem_tlb(vma->vm_start) == -1 ) {
	return -EBUSY;
    }

    /* just set flags ( no PTEs ) */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    vma->vm_flags |= (MAP_FIXED|MAP_PRIVATE|VM_IO|VM_DONTEXPAND|VM_RESERVED|VM_PFNMAP);

    if( bigmem_mmap_init(get_bigmem_region_start(),
			 get_bigmem_region_end() )!=BIGMEM_MMAP_SUCCESS ) { 
	printk(KERN_ERR "[Z] bigmem_mmap_init() failed\n");
	free_bigmem_tlb();
	return -EAGAIN;
    }

    /* don't call  bigmem_process_new for explit mmap usage */
    SET_ZEPTO_TASK(current, 1);
		
    zepto_debug(2,"bigmem VA:[%08x,%08x)  PA:[%08x,%08x)  size=%08x\n",
		get_bigmem_region_start(),
		get_bigmem_region_end(),
		get_bigmem_pa_start(),
		get_bigmem_pa_end(),
		get_bigmem_size() );

    return 0;
}




static const struct file_operations zeptobigmem_fops = {
    .open = zeptobigmem_open , 
    .mmap = zeptobigmem_mmap,
};

static struct cdev  zeptobigmem_cdev;

int __init zeptobigmem_init(void)
{
    int rc;
    static int zeptobigmem_maj = 127;
    static int zeptobigmem_min = 0;
    dev_t devnum;

    /*  registering zeptobigmem char device */
    devnum = MKDEV(zeptobigmem_maj, zeptobigmem_min);

    rc = register_chrdev_region( devnum, 1, "zeptobigmem" );
    if( rc ) {
	printk(KERN_WARNING "register_chrdev_region() failed. zeptobigmem(%d:%d) rc=%d\n",
	       zeptobigmem_maj, zeptobigmem_min, rc );
	return -1;
    }

    /* connecting up the device */
    cdev_init(&zeptobigmem_cdev, &zeptobigmem_fops);
    kobject_set_name(&zeptobigmem_cdev.kobj, "zeptobigmem%d",devnum);

    rc = cdev_add(&zeptobigmem_cdev, devnum, 1 );
    if (rc)   {
	printk(KERN_WARNING "cdev_add() failed. zeptobigmem(%d:%d) rc=%d\n",
	       zeptobigmem_maj, zeptobigmem_min, rc );
	return -1;
    }

    zepto_debug(2,"zeptobigmem mmap driver is registered\n");

    return 0;
}
__initcall(zeptobigmem_init);

#endif
