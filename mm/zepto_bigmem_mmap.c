//****************************************************************************/
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


#ifdef __KERNEL__


#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/semaphore.h>

#include <linux/zepto_task.h>

#define BIGMEM_MMAP_ALIGNMENT   (PAGE_SIZE)

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h> 
#include "rbtree.h"
#include "zepto_bigmem_mmap.h"

#define BIGMEM_MMAP_ALIGNMENT   (64*1024)

#endif

#define MAX_BIGMEM_CORE (4)


/*
  This function is used at _find_free_bigmem_mmap_region_no_lock()
*/
static unsigned next_bigmem_mmap_alignment(unsigned addr)
{
    unsigned mask = BIGMEM_MMAP_ALIGNMENT-1;
    return ( addr & ~mask ) + ((addr&mask)!=0)*BIGMEM_MMAP_ALIGNMENT;
}


#undef BIGMEM_MMAP_DEBUG  
//#define BIGMEM_MMAP_DEBUG  

static struct rb_root bigmem_mmap_rb_root[MAX_BIGMEM_CORE] = { RB_ROOT, RB_ROOT,RB_ROOT,RB_ROOT };
static int            bigmem_mmap_initialized[MAX_BIGMEM_CORE] = { 0,0,0,0 };

static unsigned bigmem_mmap_start[MAX_BIGMEM_CORE];
static unsigned bigmem_mmap_end[MAX_BIGMEM_CORE];
static unsigned bigmem_mmap_section_allocated_bytes[MAX_BIGMEM_CORE];
static unsigned bigmem_mmap_n_sections_allocated[MAX_BIGMEM_CORE];


#ifdef __KERNEL__


static void* __allocatememory(unsigned size) {  return kmalloc(size, GFP_KERNEL); }
static void  __freememory(void* ptr) { kfree(ptr); }

#ifdef BIGMEM_MMAP_DEBUG 
static void  _bigmem_mmap_debug_print(char* fmt,...) 
{
    va_list ap;
    if( 2 <= zetp_debug_level  ) {
	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
    }
}
#else
#define _bigmem_mmap_debug_print(fmt,...)
#endif


static void  _bigmem_mmap_error_print(char* fmt,...) 
{
    va_list ap;
    va_start(ap, fmt);
    vprintk(fmt,ap);
    va_end(ap);
}

struct semaphore  _bigmem_mmap_sem;


void _bigmem_mmap_init_lock(int cid)
{
	sema_init(&_bigmem_mmap_sem,1);
}

void _bigmem_mmap_finalize_lock(int cid)
{
}

void  _bigmem_mmap_lock(int cid) 
{
	down(&_bigmem_mmap_sem);
}

void  _bigmem_mmap_unlock(int cid)
{
	up(&_bigmem_mmap_sem);
}


#else
/* for userspace test */

static int bigmem_process_cid(void) {return 0;}


static void* __allocatememory(unsigned size) {  return malloc(size); }
static void  __freememory(void* ptr) { free(ptr); }

#ifdef BIGMEM_MMAP_DEBUG 
static void  _bigmem_mmap_debug_print(char* fmt,...) 
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
#else
#define _bigmem_mmap_debug_print(fmt,...)
#endif

static void  _bigmem_mmap_error_print(char* fmt,...) 
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout,fmt,ap);
    va_end(ap);
}

static int _bigmem_mmap_lock_depth[MAX_BIGMEM_CORE];

void _bigmem_mmap_init_lock(int cid)
{
    _bigmem_mmap_lock_depth[cid] = 0;
}

void _bigmem_mmap_finalize_lock(int cid)
{
    _bigmem_mmap_lock_depth[cid] = 0;
}


#define  _bigmem_mmap_lock(cid)   \
  do { \
    if(_bigmem_mmap_lock_depth[cid]<0) {  \
	printf("lock is already taken %s(%d)\n",__FILE__,__LINE__); exit(1); \
    } else { \
        _bigmem_mmap_lock_depth[cid]--; \
    } \
  } while(0)

#define  _bigmem_mmap_unlock(cid) \
  do { \
    if(_bigmem_mmap_lock_depth[cid]==0 ) {  \
	printf("lock is not held yet %s(%d)\n",__FILE__,__LINE__); exit(1); \
    } else { \
        _bigmem_mmap_lock_depth[cid]++; \
    } \
  } while(0)

#endif


static int bigmem_mmap_section_alloc_count[MAX_BIGMEM_CORE];

static struct bigmem_mmap_section_struct* _alloc_bigmem_mmap_section(int cid, unsigned start, unsigned end )
{
    struct bigmem_mmap_section_struct* new_section;

    new_section = __allocatememory(sizeof(struct bigmem_mmap_section_struct));
    if( !new_section ) return NULL;
    bigmem_mmap_section_alloc_count[cid] ++;

    memset( new_section, 0, sizeof(struct bigmem_mmap_section_struct) );
    new_section->start = start;
    new_section->end   = end;

    return new_section;
}
static void _free_bigmem_mmap_elem(int cid, struct bigmem_mmap_section_struct*  ptr ) 
{
    if( ptr ) {
	__freememory(ptr);
	bigmem_mmap_section_alloc_count[cid]--;
	// _bigmem_mmap_debug_print("* bigmem_mmap is free'ed  0x%08x\n",(unsigned)ptr );
    }
}


/* ============================================================
   functions operate rb nodes. expect bigmem_mmap lock is hold.
   internal use only
   ============================================================ */
static void _insert_bigmem_mmap_secion_no_lock(int cid, struct rb_root *root, struct bigmem_mmap_section_struct *new)
{
    struct rb_node **link = &root->rb_node;
    struct rb_node *parent = NULL;
    int value = new->start;

    while(*link) {
	struct bigmem_mmap_section_struct *bigmem_mmap_tmp;
	parent = *link;
	bigmem_mmap_tmp = rb_entry(parent, struct bigmem_mmap_section_struct, rb_node);

	if(bigmem_mmap_tmp->start > value) 
	    link = &(*link)->rb_left;
	else
	    link = &(*link)->rb_right;
    }
    rb_link_node(&new->rb_node, parent, link);
    rb_insert_color(&new->rb_node, root);
}

static void _remove_all_bigmem_mmap_sections_no_lock(int cid, struct rb_root *root)
{
    struct rb_node *node = rb_last(root);

    while( node ) {
	struct rb_node *node_tmp = node;
	struct bigmem_mmap_section_struct *bigmem_mmap_tmp = rb_entry(node, struct bigmem_mmap_section_struct, rb_node );
	// _bigmem_mmap_debug_print("free %p / node_tmp=%p / start=0x%08x\n", bigmem_mmap_tmp, node_tmp, bigmem_mmap_tmp->start);
	rb_erase(node_tmp, root);
	node = rb_prev(node);
	_free_bigmem_mmap_elem(cid, bigmem_mmap_tmp);
    }
}


static struct bigmem_mmap_section_struct*  _find_bigmem_mmap_section_no_lock(int cid, struct rb_root *root, unsigned addr)
{
    struct rb_node *node = root->rb_node;  /* top of the tree */
    struct bigmem_mmap_section_struct *ret = 0;

    if(!node) {
	/* _bigmem_mmap_debug_print("_find_bigmem_mmap_section_no_lock() addr=0x%08x  empty rbtree!\n",addr); */
	return 0;
    }

    while(node)   {
	struct bigmem_mmap_section_struct *bigmem_mmap_tmp = rb_entry(node, struct bigmem_mmap_section_struct, rb_node);
	/*
	_bigmem_mmap_debug_print("_find_bigmem_mmap_section_no_lock() addr=0x%08x  [0x%08x,0x%08x)\n",
	       addr, bigmem_mmap_tmp->start,  bigmem_mmap_tmp->end );
	*/
	if( bigmem_mmap_tmp->end > addr ) {
	    ret = bigmem_mmap_tmp;
	    if( bigmem_mmap_tmp->start == addr )  {
		break;
	    }
	    node = node->rb_left;
	} else {
	    node = node->rb_right;
	}
    }

    if( !node ) {
	_bigmem_mmap_error_print("_find_bigmem_mmap_section_no_lock() didn't find 0x%08x\n", addr);
	ret = 0;
    }

    return ret;
}

/*
  find a region satisfies its start > addr 
*/
struct bigmem_mmap_section_struct*  _find_bigmem_mmap_section_any_bigger_no_lock(int cid, struct rb_root *root, unsigned addr)
{
    struct rb_node *node = root->rb_node;  /* top of the tree */
    struct bigmem_mmap_section_struct *ret = 0;

    if(!node) {
	return 0;
    }

    while(node)   {
	struct bigmem_mmap_section_struct *bigmem_mmap_tmp = rb_entry(node, struct bigmem_mmap_section_struct, rb_node);
	if( bigmem_mmap_tmp->start > addr ) {
	    ret = bigmem_mmap_tmp;
	    break;
	} else {
	    node = node->rb_right;
	}
    }
    if( !node ) {
	_bigmem_mmap_error_print("_find_bigmem_mmap_section_no_lock() didn't find 0x%08x\n", addr);
	ret = 0;
    }
    return ret;
}




static unsigned _bigmem_mmap_allocatedmemory_no_lock(int cid)
{
    struct rb_node *node = rb_first(&bigmem_mmap_rb_root[cid]);
    unsigned total=0;

    while( node ) {
	struct bigmem_mmap_section_struct *bigmem_mmap_tmp = rb_entry(node, struct bigmem_mmap_section_struct, rb_node );
	total += (bigmem_mmap_tmp->end - bigmem_mmap_tmp->start);
	node = rb_next(node);
    }
    return total;
}




static unsigned _bigmem_mmap_freememory_no_lock(int cid)
{
    return (bigmem_mmap_end[cid] - bigmem_mmap_start[cid]) - bigmem_mmap_section_allocated_bytes[cid];
}


/* The start address of a free region is aligned by BIGMEM_MMAP_ALIGNEMENT.
   We assume that this function is only called to allocate a region on bigmem_mmap
   via anonymous mmap().
*/
unsigned _find_free_bigmem_mmap_region_no_lock(int cid,unsigned len )
{
    struct rb_node *node = rb_first(&bigmem_mmap_rb_root[cid]);
    unsigned prev_end = next_bigmem_mmap_alignment(bigmem_mmap_start[cid]);
    struct bigmem_mmap_section_struct  region_found;
    int     found=0;
    int idx =0;

    memset(&region_found, 0, sizeof(region_found));

    if( !node ) {
	if( len < (next_bigmem_mmap_alignment(bigmem_mmap_end[cid])-bigmem_mmap_start[cid]) ) {
	    return bigmem_mmap_start[cid];
	} else {
	    _bigmem_mmap_error_print( "Not enough memory. len=0x%08x [0x%08x,0x%08x) free=0x%08x allocated=0x%08x\n", 
			       len, bigmem_mmap_start[cid], bigmem_mmap_end[cid], _bigmem_mmap_freememory_no_lock(cid), _bigmem_mmap_allocatedmemory_no_lock(cid) );
	    return BIGMEM_MMAP_ALLOCATION_FAILURE;
	}
    }
    
    _bigmem_mmap_debug_print("finding a region that can hold 0x%08x bytes\n", len);

    while( node ) {
	struct bigmem_mmap_section_struct *bigmem_mmap_tmp = rb_entry(node, struct bigmem_mmap_section_struct, rb_node );

	if( prev_end != bigmem_mmap_tmp->start ) {
	    if( len < bigmem_mmap_tmp->start - prev_end ) { 
		region_found.start = prev_end;
		region_found.end   = bigmem_mmap_tmp->start;
		found++;
	    }
	}
	prev_end = next_bigmem_mmap_alignment(bigmem_mmap_tmp->end);
	    
	node = rb_next(node);
	idx++;
    }
    if( found==0 && prev_end != bigmem_mmap_end[cid] ) {
	if( len < bigmem_mmap_end[cid]-prev_end ) {
	    region_found.start = prev_end;
	    region_found.end   = bigmem_mmap_end[cid];
	    found ++;
	}
    }

    if( found == 0 ) {
	_bigmem_mmap_error_print( "Not enough memory. len=0x%08x [0x%08x,0x%08x) free=0x%08x allocated=0x%08x\n", 
			   len, bigmem_mmap_start[cid], bigmem_mmap_end[cid], _bigmem_mmap_freememory_no_lock(cid), _bigmem_mmap_allocatedmemory_no_lock(cid) );
	return BIGMEM_MMAP_ALLOCATION_FAILURE;
    }

    _bigmem_mmap_debug_print("found a region[0x%08x,%08x) that can hold 0x%08x bytes\n", 
		      region_found.start, region_found.end,     len);

    return region_found.start;
}


/* ============================================================
   The following function are called from other code.
   ============================================================ */

/* this function is called when brk is updated */
BIGMEM_MMAP_status update_bigmem_mmap_start(unsigned addr)
{
    BIGMEM_MMAP_status ret = BIGMEM_MMAP_FAILURE;
    struct bigmem_mmap_section_struct* bigmem_mmap_tmp;
    int cid = bigmem_process_cid();

    _bigmem_mmap_lock(cid);
    bigmem_mmap_tmp = _find_bigmem_mmap_section_no_lock(cid, &bigmem_mmap_rb_root[cid], addr);
    if( bigmem_mmap_tmp ) {
	_bigmem_mmap_error_print("update_bigmem_mmap_start(0x%08x) failed.  request address conflicts with region[0x%08x,0x%08x)\n",
			  addr, bigmem_mmap_tmp->start, bigmem_mmap_tmp->end);
	goto out;
    }
    bigmem_mmap_start[cid] = addr;
    ret = BIGMEM_MMAP_SUCCESS;
 out:
    _bigmem_mmap_unlock(cid);
    return ret;
}

/* this function is called when start is updated */
BIGMEM_MMAP_status update_bigmem_mmap_end(unsigned addr)
{
    BIGMEM_MMAP_status ret = BIGMEM_MMAP_FAILURE;
    struct bigmem_mmap_section_struct* bigmem_mmap_tmp;
    int cid = bigmem_process_cid();

    _bigmem_mmap_lock(cid);
    bigmem_mmap_tmp = _find_bigmem_mmap_section_no_lock(cid, &bigmem_mmap_rb_root[cid], addr);
    if( bigmem_mmap_tmp ) {
	_bigmem_mmap_error_print("update_bigmem_mmap_end(0x%08x) failed.  request address conflicts with region[0x%08x,0x%08x)\n",
			  addr, bigmem_mmap_tmp->start, bigmem_mmap_tmp->end);
	goto out;
    }
    bigmem_mmap_end[cid] = addr;
    ret = BIGMEM_MMAP_SUCCESS;
 out:
    _bigmem_mmap_unlock(cid);
    return ret;
}

unsigned get_bigmem_mmap_start(void) 
{ 
    unsigned ret=0;
    int cid = bigmem_process_cid();
    _bigmem_mmap_lock(cid);
    ret = bigmem_mmap_start[cid];
    _bigmem_mmap_unlock(cid);
    return ret;
}

unsigned get_bigmem_mmap_end(void) 
{ 
    unsigned ret=0;
    int cid = bigmem_process_cid();
    _bigmem_mmap_lock(cid);
    ret = bigmem_mmap_end[cid];
    _bigmem_mmap_unlock(cid);
    return ret;
}


unsigned create_bigmem_mmap_section( int cid, unsigned addr, unsigned len )
{
    struct bigmem_mmap_section_struct* bigmem_mmap_tmp;
    unsigned ret=BIGMEM_MMAP_ALLOCATION_FAILURE;
    unsigned addr_end = addr+len-4;
    struct bigmem_mmap_section_struct*  new_bigmem_mmap;

    _bigmem_mmap_lock(cid);
    bigmem_mmap_tmp = _find_bigmem_mmap_section_no_lock(cid, &bigmem_mmap_rb_root[cid], addr);
    if( bigmem_mmap_tmp ) {
	_bigmem_mmap_debug_print("addr(%08x) is within [%08x,%08x)\n",
			 addr,
			 bigmem_mmap_tmp->start, bigmem_mmap_tmp->end );
	goto out;
    }
    bigmem_mmap_tmp = _find_bigmem_mmap_section_no_lock(cid, &bigmem_mmap_rb_root[cid], addr_end);
    if( bigmem_mmap_tmp ) {
	_bigmem_mmap_debug_print("addr_end(%08x) is within [%08x,%08x)\n",
			 addr_end,
			 bigmem_mmap_tmp->start, bigmem_mmap_tmp->end);
	goto out;
    }

    new_bigmem_mmap = _alloc_bigmem_mmap_section(cid, addr, addr+len);
    if( !new_bigmem_mmap ) {
	goto out;
    }

    /* XXX: add extra error check, or proof rb never fail? */
    _insert_bigmem_mmap_secion_no_lock(cid, &bigmem_mmap_rb_root[cid], new_bigmem_mmap);
    ret = addr;
 out:
    _bigmem_mmap_unlock(cid);
    return ret;
}


unsigned allocate_bigmem_mmap_section(unsigned len ) 
{
    unsigned ret = BIGMEM_MMAP_ALLOCATION_FAILURE;
    int cid = bigmem_process_cid();

    /* XXX: do we need to allow zero byte allocataion? */
    if( len==0 ) {
	_bigmem_mmap_error_print("zero byte allocation is not supported.\n");
	return BIGMEM_MMAP_ALLOCATION_FAILURE;
    }

    _bigmem_mmap_lock(cid);

    ret = _find_free_bigmem_mmap_region_no_lock(cid, len);
    if(ret == BIGMEM_MMAP_ALLOCATION_FAILURE ) {
	_bigmem_mmap_error_print("allocate_bigmem_mmap_section(0x%08x) No free region found.\n",
			  len);
	ret = BIGMEM_MMAP_ALLOCATION_FAILURE;
	goto out;
    }

    /* XXX: add extra error check, or proof rb never fail? */
    _insert_bigmem_mmap_secion_no_lock(cid, &bigmem_mmap_rb_root[cid], _alloc_bigmem_mmap_section(cid, ret,ret+len));

    bigmem_mmap_section_allocated_bytes[cid] +=  len;
    bigmem_mmap_n_sections_allocated[cid]++;
 out:
    _bigmem_mmap_unlock(cid);

    return ret;
}




BIGMEM_MMAP_status remove_bigmem_mmap_section(unsigned addr )
{
    struct bigmem_mmap_section_struct* bigmem_mmap_tmp;
    BIGMEM_MMAP_status ret = BIGMEM_MMAP_FAILURE ;
    int cid = bigmem_process_cid();

    _bigmem_mmap_lock(cid);
    bigmem_mmap_tmp = _find_bigmem_mmap_section_no_lock(cid, &bigmem_mmap_rb_root[cid], addr);
    if( !bigmem_mmap_tmp ) {
	_bigmem_mmap_error_print("remove_bigmem_mmap_section(): there is no region contains %08x\n", addr);
	ret = BIGMEM_MMAP_FAILURE ;
	goto out;
    }
    /* unlikely */
    if( bigmem_mmap_tmp->start != addr ) {
	_bigmem_mmap_error_print("Error: addr(%08x) does not match with region start [%08x,%08x)  bigmem_mmap_n_sections_allocated=%d\n",
			  addr, bigmem_mmap_tmp->start, bigmem_mmap_tmp->end,bigmem_mmap_n_sections_allocated[cid] );
	ret = BIGMEM_MMAP_FAILURE ;
        goto out;
    }

    /* XXX: rb_erase() and _free_bigmem_mmap_elem never fail? */
    rb_erase(&bigmem_mmap_tmp->rb_node, &bigmem_mmap_rb_root[cid]);
    _free_bigmem_mmap_elem(cid, bigmem_mmap_tmp);

    bigmem_mmap_section_allocated_bytes[cid] -= (bigmem_mmap_tmp->end - bigmem_mmap_tmp->start );
    bigmem_mmap_n_sections_allocated[cid]--;
    ret = BIGMEM_MMAP_SUCCESS;
 out:
    _bigmem_mmap_unlock(cid);
    return ret;
}

#ifdef __KERNEL__

static int proc_bigmem_mmap_show(struct seq_file *m, void *v)
{
    struct rb_node *node;
    int idx = 0;
    unsigned  bigmem_virt2phy(unsigned long va);  /* defined in arch/ppc/mm/44x_mmu.c */
    int cid=0;

#ifdef CONFIG_ZEPTO_COMPUTENODE
    for(cid=0; cid<bigmem_process_active_count(); cid++) {
#endif
	_bigmem_mmap_lock(cid);
	node = rb_first(&bigmem_mmap_rb_root[cid]);
#ifdef CONFIG_ZEPTO_COMPUTENODE
	seq_printf(m,"\n");
	seq_printf(m,"[BigMem Process ID=%d]\n",cid);
#endif
	seq_printf(m,"bigmem_mmap          [%08x,%08x)\n", bigmem_mmap_start[cid], bigmem_mmap_end[cid]);
	seq_printf(m,"No. of chunks  %d\n",  bigmem_mmap_n_sections_allocated[cid] );
	seq_printf(m,"allocated      %d kB\n", bigmem_mmap_section_allocated_bytes[cid]/1024) ;
	seq_printf(m,"free           %d kB\n", _bigmem_mmap_freememory_no_lock(cid)/1024 );
	seq_printf(m,"\n");

	while( node ) {
	    struct bigmem_mmap_section_struct *bigmem_mmap_tmp = rb_entry(node, struct bigmem_mmap_section_struct, rb_node );

	    seq_printf(m,"%3d  va:[%08x %08x)  pa:[%08x,%08x)\n", idx, 
		       bigmem_mmap_tmp->start, bigmem_mmap_tmp->end,
		       bigmem_virt2phy_cid(bigmem_mmap_tmp->start,cid),
		       bigmem_virt2phy_cid(bigmem_mmap_tmp->end,cid)  );
	    node = rb_next(node);
	    idx++;
	}
	_bigmem_mmap_unlock(cid);
#ifdef CONFIG_ZEPTO_COMPUTENODE
    }
#endif

    return 0;
}

static int proc_bigmem_mmap_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_bigmem_mmap_show, NULL);
}

static struct file_operations proc_bigmem_mmap_operations = {
    .open		= proc_bigmem_mmap_open,
    .read		= seq_read,
    .llseek		= seq_lseek,
    .release	= single_release,
};

static struct proc_dir_entry *bigmem_proc_entry = NULL;

static void register_bigmem_mmap_proc(void)
{
    if(bigmem_proc_entry) return;

    bigmem_proc_entry = create_proc_entry("bigmem_mmap", 0, NULL);
    if(bigmem_proc_entry) {
	bigmem_proc_entry->proc_fops = &proc_bigmem_mmap_operations;
	zepto_debug(2,"/proc/bigmem_mmap is registered\n");
    } else {
	printk(KERN_ERR "[Z] Failed to register /proc/bigmem_mmap\n");
    }
}

#if 0
/* we don't free /proc/bigmem_mmap once it is registered */
static void unregister_bigmem_mmap_proc(void)
{
    remove_proc_entry("bigmem_mmap",bigmem_proc_entry);
    bigmem_proc_entry=NULL;
}
#endif

#endif



BIGMEM_MMAP_status bigmem_mmap_init(unsigned start, unsigned end)
{
    BIGMEM_MMAP_status ret = BIGMEM_MMAP_FAILURE;
    int cid = bigmem_process_cid();

    _bigmem_mmap_init_lock(cid);

    _bigmem_mmap_lock(cid);
    if( bigmem_mmap_initialized[cid] ) {
	_bigmem_mmap_error_print("bigmem_mmap already intialized!\n");
	ret = BIGMEM_MMAP_FAILURE;
	goto out;
    }

    bigmem_mmap_start[cid] = start;
    bigmem_mmap_end[cid]   = end;

    bigmem_mmap_section_allocated_bytes[cid] = 0;
    bigmem_mmap_n_sections_allocated[cid] = 0;

    _bigmem_mmap_debug_print("bigmem_mmap_init start=%08x end=%08x\n", bigmem_mmap_start[cid], bigmem_mmap_end[cid]);

    ret = BIGMEM_MMAP_SUCCESS;
    bigmem_mmap_initialized[cid] = 1;
 out:
    _bigmem_mmap_unlock(cid);

#ifdef __KERNEL__
    if(cid==0)  register_bigmem_mmap_proc();
#endif

    return ret; 
}

BIGMEM_MMAP_status bigmem_mmap_finalize(void)
{
    BIGMEM_MMAP_status ret = BIGMEM_MMAP_FAILURE;
    int cid = bigmem_process_cid();

    if( !bigmem_mmap_initialized[cid] ) {
	_bigmem_mmap_error_print("bigmem_mmap not intialized yet?\n");
	ret =  BIGMEM_MMAP_FAILURE;
	goto out;
    }
    _bigmem_mmap_lock(cid);

    _remove_all_bigmem_mmap_sections_no_lock(cid, &bigmem_mmap_rb_root[cid]);
    
    if( bigmem_mmap_section_alloc_count[cid] != 0 ) {
	_bigmem_mmap_error_print("memory coruption: bigmem_mmap_section_alloc_count=%d\n",
			  bigmem_mmap_section_alloc_count[cid]);
	return BIGMEM_MMAP_FAILURE;
    }

    bigmem_mmap_section_allocated_bytes[cid] = 0;
    bigmem_mmap_n_sections_allocated[cid] = 0;

    ret = BIGMEM_MMAP_SUCCESS;
    bigmem_mmap_initialized[cid] = 0;
 out:
    _bigmem_mmap_unlock(cid);

    _bigmem_mmap_finalize_lock(cid);

    return ret;
}


void bigmem_mmap_traverse_print(int (*print_func)(const char*,...) )
{
    struct rb_node *node = rb_first(&bigmem_mmap_rb_root[bigmem_process_cid()]);
    int idx = 0;

    while( node ) {
	struct bigmem_mmap_section_struct *bigmem_mmap_tmp = rb_entry(node, struct bigmem_mmap_section_struct, rb_node );
	print_func("%2d: [%08x %08x)\n", idx, bigmem_mmap_tmp->start, bigmem_mmap_tmp->end );
	node = rb_next(node);
	idx++;
    }
    print_func("total %d entries\n", idx);
}




#ifndef __KERNEL__

unsigned do_alloc(unsigned len)
{
    unsigned addr;

    _bigmem_mmap_debug_print("@ allocate_bigmem_mmap_section(0x%08x) request.\n",len);

    addr = allocate_bigmem_mmap_section(len);
    if( addr == BIGMEM_MMAP_ALLOCATION_FAILURE ) {
	_bigmem_mmap_error_print( "allocate_bigmem_mmap_section() failed\n");
	return addr;
    }
    _bigmem_mmap_debug_print("@ allocate_bigmem_mmap_section(0x%08x) succeeded. addr=0x%08x\n", len, addr);
    return addr;
}

void do_remove(unsigned addr)
{
    _bigmem_mmap_debug_print("@ remove_bigmem_mmap_section(0x%08x) request.\n", addr);

    if( remove_bigmem_mmap_section(addr) != BIGMEM_MMAP_SUCCESS ) {
	_bigmem_mmap_error_print( "remove_bigmem_mmap_section(%08x) failed\n",  addr);
	bigmem_mmap_traverse_print(printf);

    }
    _bigmem_mmap_debug_print("@ remove_bigmem_mmap_section(0x%08x) succeeded.\n", addr);

}

void bigmem_mmap_test(void)
{
    unsigned region_start = 0x10000000;
    unsigned region_end   = 0x50000000;
    unsigned addr;


    if( bigmem_mmap_init(region_start, region_end)!=BIGMEM_MMAP_SUCCESS ) {
	_bigmem_mmap_error_print( "bigmem_mmap_init() failed.\n");
	exit(1);
    }
    printf("bigmem_mmap region [%08x,%08x)\n",  get_bigmem_mmap_start(), get_bigmem_mmap_end() );
    
    addr = do_alloc(0x00002000);
    addr = do_alloc(0x0000F000);
    addr = do_alloc(0x00200000);
    bigmem_mmap_traverse_print(printf);

    do_remove(addr);
    addr = do_alloc(0x21002000);
    addr = do_alloc(0x00102000);
    bigmem_mmap_traverse_print(printf);

    if( bigmem_mmap_finalize() !=BIGMEM_MMAP_SUCCESS ) {
	_bigmem_mmap_error_print( "bigmem_mmap_finalize() failed.\n");
	exit(1);
    }
    bigmem_mmap_traverse_print(printf);
}




void bigmem_mmap_test_random()
{
    unsigned region_start, region_end;
    unsigned addr_keep[10];
    unsigned n_addr_keep=0;
    int cid = bigmem_process_cid();
    int i,j;

    for(i=0; i<10; i++ ) {
	region_start = 0x30000000 + ((rand()%3)-1)*0x10000000;
	region_end   = 0x70000000 + ((rand()%3)-1)*0x10000000;

	printf("\n\n==== test try %d ==============\n", i);

	if( bigmem_mmap_init(region_start, region_end)!=BIGMEM_MMAP_SUCCESS ) {
	    _bigmem_mmap_error_print( "bigmem_mmap_init() failed.\n");
	    exit(1);
	}
	printf("bigmem_mmap region [%08x,%08x)\n",  get_bigmem_mmap_start(), get_bigmem_mmap_end() );

	n_addr_keep = 0;
	
	for( j=0; j< 300; j++ ) {
	    int size;
	    unsigned addr;
	    if( (rand()%133)==0 ) {
		size = ( (rand()+1)%(region_end-region_start+0x10000000))&0xffff0000;
	    } else {
		size = ( (rand()+1)%((region_end-region_start)/0xfff))&0xffff0000;
	    }
	    addr = do_alloc(size);

	    if( (j%100)==0 ) {
		if( _bigmem_mmap_allocatedmemory_no_lock(cid) != bigmem_mmap_section_allocated_bytes[cid] ) {
		    printf("Found inconsitency: free=%08x bigmem_mmap_section_allocated_bytes=0x%08x  bigmem_mmap_n_sections_allocated=%d iter=%d\n",
			   _bigmem_mmap_freememory_no_lock(cid), bigmem_mmap_section_allocated_bytes[cid], bigmem_mmap_n_sections_allocated[cid],j );
		    exit(1);
		}

		printf("free=%08x bigmem_mmap_section_allocated_bytes=0x%08x  bigmem_mmap_n_sections_allocated=%d iter=%d\n",
		       _bigmem_mmap_freememory_no_lock(cid),      bigmem_mmap_section_allocated_bytes[cid], bigmem_mmap_n_sections_allocated[cid],j );

	    }


	    if( addr != BIGMEM_MMAP_ALLOCATION_FAILURE ) {
		addr_keep[n_addr_keep++] = addr;
		if(n_addr_keep>=10) {
		    int k;
		    if( (rand()%2)==0 ) {
			for(k=0;k<(rand()%10);k++ ) {
			    do_remove(addr_keep[k]);
			}
		    }
		    n_addr_keep=0;
		}
	    }
	}

	printf("_bigmem_mmap_allocatedmemory_no_lock()=0x%08x bigmem_mmap_section_allocated_bytes=0x%08x  bigmem_mmap_n_sections_allocated=%d\n",
	       _bigmem_mmap_allocatedmemory_no_lock(cid), bigmem_mmap_section_allocated_bytes[cid], bigmem_mmap_n_sections_allocated[cid] );

	printf("@ traverse print\n");
	bigmem_mmap_traverse_print(printf);

	if( bigmem_mmap_finalize() !=BIGMEM_MMAP_SUCCESS ) {
	    _bigmem_mmap_error_print( "bigmem_mmap_finalize() failed.\n");
	    exit(1);
	}

    }
    printf("done.\n");
}

void  bigmem_mmap_test_seq()
{
    unsigned region_start, region_end;
    int i,j;

    for(i=0; i<1; i++ ) {
	region_start = 0x30000000 + ((rand()%3)-1)*0x10000000;
	region_end   = 0x70000000 + ((rand()%3)-1)*0x10000000;

	printf("\n==== test try %d ==============\n", i);

	if( bigmem_mmap_init(region_start, region_end)!=BIGMEM_MMAP_SUCCESS ) {
	    _bigmem_mmap_error_print( "bigmem_mmap_init() failed.\n");
	    exit(1);
	}
	printf("bigmem_mmap region [%08x,%08x)\n",  get_bigmem_mmap_start(), get_bigmem_mmap_end() );
	for( j=region_start; j<region_end-0x100000;j+=0x100000 ) {
	    do_alloc(0x100000);
	}
	/* assume that alloc function returns the first found memory region */
	for( j=region_start; j<region_end-0x100000;j+=0x100000 ) {
	    do_remove(j);
	}
	
	printf("@ traverse print\n");
	bigmem_mmap_traverse_print(printf);

	if( bigmem_mmap_finalize() !=BIGMEM_MMAP_SUCCESS ) {
	    _bigmem_mmap_error_print( "bigmem_mmap_finalize() failed.\n");
	    exit(1);
	}

    }
}


int main(int argc,char* argv[])
{

    printf("[random test]\n");
    bigmem_mmap_test_random();

    printf("[seq test]\n");
    bigmem_mmap_test_seq();
    printf("done.\n");

    return 0;
}

#endif
