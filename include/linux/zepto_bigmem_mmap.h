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


#ifndef __ZEPTO_BIGMEM_MMAP_H_DEFINED__
#define __ZEPTO_BIGMEM_MMAP_H_DEFINED__

typedef enum {
    BIGMEM_MMAP_SUCCESS,
    BIGMEM_MMAP_FAILURE,
} BIGMEM_MMAP_status;

#define  BIGMEM_MMAP_ALLOCATION_FAILURE   ((unsigned)-1)

unsigned    allocate_bigmem_mmap_section(unsigned len);
BIGMEM_MMAP_status remove_bigmem_mmap_section(unsigned addr);

unsigned    get_bigmem_mmap_start(void);
unsigned    get_bigmem_mmap_end(void);

BIGMEM_MMAP_status update_bigmem_mmap_start(unsigned addr);
BIGMEM_MMAP_status update_bigmem_mmap_end(unsigned addr);

BIGMEM_MMAP_status bigmem_mmap_init(unsigned start, unsigned end);
BIGMEM_MMAP_status bigmem_mmap_finalize(void);

struct bigmem_mmap_section_struct            /* bigmem mapping area */
{ 
    struct rb_node  rb_node;
    unsigned start,end;       /* section [start,end) */
};

#endif
