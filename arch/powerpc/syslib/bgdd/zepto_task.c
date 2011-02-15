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
  Support codes for zepto task
*/
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/uaccess.h>

#include <linux/zepto_task.h>

int  enable_bigmem;

static int bigmem_proc_read(char *page, char **start, off_t off,
				    int count, int *eof, void *data)
{
    int rc;
    rc = snprintf(page, count, "%d", enable_bigmem);
    *eof = 1;
    return (rc >= 0 ? rc : 0);
}

static int bigmem_proc_write(struct file * filp, const char __user *buf,
					  unsigned long len, void * data)
{
    if(buf[0]=='0') {
	enable_bigmem = 0;
    } else {
	enable_bigmem = 1;
    }
    zepto_debug(2,"set enable_bigmem %d\n",
	   enable_bigmem);
    return len;
}


#ifdef CONFIG_ZEPTO_EXPERIMENTAL
#include <linux/random.h>

static int rtest_proc_read(char *page, char **start, off_t off,
				    int count, int *eof, void *data)
{
    int rc;
    rc = snprintf(page, count, "%d\n", random32());
    *eof = 1;
    return (rc >= 0 ? rc : 0);
}

static int rtest_proc_write(struct file * filp, const char __user *buf,
					  unsigned long len, void * data)
{
    u32 entropy;
    entropy = simple_strtoul(buf,NULL,0);
    srandom32(entropy);
    zepto_debug(1,"rtest: entropy=%d\n", entropy);
    return len;
}
#endif


static int bigmem_reset_proc_write(struct file *file, const char *buffer,
			 unsigned long len, void *data)
{
    char tmp[2];
    int rc;

    if(copy_from_user(tmp, buffer, 1) == 0 ) {
	tmp[1] = 0;
	zepto_debug(1,"bigmem_reset_proc_write %s",tmp);
	rc = len;
    } else {
	rc = -EFAULT;
    }

    if( tmp[0] == '1' ) {
	if( enable_bigmem ) {
	    bigmem_process_reset();
	    if( bigmem_mmap_finalize() !=BIGMEM_MMAP_SUCCESS ) {
		printk( KERN_ALERT  "[Z] bigmem_mmap_finalize() failed.\n");
	    }
	    free_bigmem_tlb();
	}
	zepto_debug(1, "bigmem is hard-reset.\n");
    }

    return rc;
}


#ifdef CONFIG_ZEPTO_EXPERIMENTAL
static int bgprint_write(struct file *file, const char *buffer,
			 unsigned long len, void *data)
{
    char *tmp;
    int rc;

    tmp = kzalloc(len+1,GFP_KERNEL);
    if( !tmp ) return 0;

    if(copy_from_user(tmp, buffer, len) == 0 ) {
	tmp[len] = 0;
	zepto_debug(1,"%s",tmp);
	rc = len;
    } else {
	rc = -EFAULT;
    }

    kfree(tmp);
    return rc;
}
#endif

static int zeptonext_proc_read(char *page, char **start, off_t off,
				    int count, int *eof, void *data)
{
    int rc;
    rc = snprintf(page, count, "zepto next codes are compiled %s\n", __DATE__);
    *eof = 1;
    return (rc >= 0 ? rc : 0);
}

static int __init  zepto_task_init(void)
{
    struct proc_dir_entry *p;

    enable_bigmem = 1; /* bigmem is enabled by default */

    p = create_proc_entry("bigmem_ctrl", S_IRUGO|S_IWUGO, NULL);
    if(p) {
	p->nlink = 1;
	p->read_proc  = bigmem_proc_read;
	p->write_proc = bigmem_proc_write;
    }
    zepto_debug(2,"/proc/bigmem_ctrl is registered\n");

    /* bigmem hard reset interface */
    p = create_proc_entry("bigmem_reset", S_IFREG|S_IRUGO|S_IWUGO, NULL );
    if(p ) {
	p->nlink = 1;
	p->write_proc = bigmem_reset_proc_write; 
    } else {
	printk(KERN_WARNING "Failed to register /proc/bigmem_reset\n");
    }
    zepto_debug(2,"/proc/bigmem_reset is registered\n");

    p = create_proc_entry("zeptonext", S_IFREG|S_IRUGO|S_IWUGO, NULL );
    if(p ) {
	p->nlink = 1;
	p->read_proc = zeptonext_proc_read; 
    } else {
	printk(KERN_WARNING "Failed to register /proc/zeptonext\n");
    }
    zepto_debug(2,"/proc/zeptonext is registered\n");


#ifdef CONFIG_ZEPTO_EXPERIMENTAL
    p = create_proc_entry("rtest", S_IRUGO|S_IWUGO, NULL);
    if(p) {
	p->nlink = 1;
	p->read_proc  = rtest_proc_read;
	p->write_proc = rtest_proc_write;
    }
    zepto_debug(2,"/proc/rtest is registered\n");

    p = create_proc_entry("bgprint", S_IFREG|S_IRUGO|S_IWUGO, NULL );
    if(p ) {
	p->nlink = 1;
	p->write_proc = bgprint_write; 
    } else {
	printk(KERN_WARNING "Failed to register /proc/bgprint\n");
    }
    zepto_debug(2,"/proc/bgprint is registered\n");
#endif

    return 0;
}
__initcall(zepto_task_init);

int zepto_task_error(const char* fmt,...)
{
    extern int bgWriteConsoleBlockDirect(const char* fmt,...); /* ./drivers/char/bluegene_console.c */
    int rc;
    va_list  args;

    va_start(args,fmt);
    rc = bgWriteConsoleBlockDirect(fmt,args);
    va_end(args);
    return rc;
}


#ifdef CONFIG_ZEPTO_NOTUSED

/* code fragments for tick disabling */

static inline unsigned long long get_tb(void)
{
    unsigned long tbu0, tbu1, tbl;
    do {
	tbu0 = get_tbu();
	tbl  = get_tbl();
	tbu1 = get_tbu();
    } while (tbu0 != tbu1);
    
    return ((unsigned long long) tbu0 << 32) | tbl;
}

static inline void disable_decrementer(void)
{
    unsigned int tcr = mfspr(SPRN_TCR);
    mtspr(SPRN_TCR, tcr & (~TCR_DIE));
}

static inline void enable_decrementer(void)
{
    unsigned int tcr = mfspr(SPRN_TCR);
    mtspr(SPRN_TCR, tcr | TCR_DIE);
}


void zc_enter_computation(void)
{
    disable_decrementer();
}

void zc_exit_computation(void)
{
    /*unsigned int r1;
      __asm__ __volatile__ ("mr %0,1" : "=r"(r1));
      printk(KERN_CRIT "running zc_exit_computation, r1=%u\n", r1);*/
    enable_decrementer();
}

#endif //  CONFIG_ZEPTO_NOTUSED
