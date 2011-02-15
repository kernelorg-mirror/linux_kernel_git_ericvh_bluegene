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

#ifndef __ZEPTO_TASK_H_DEFINED__
#define __ZEPTO_TASK_H_DEFINED__

#include <linux/zepto_debug.h>

/*  This value may be used in the future version of the official Linux kernel.
    See <linux/personality.h>
*/
#define PER_ZEPTO_TASK 0x0020000

#define SET_ZEPTO_TASK(task, val)           \
           do {     \
                if (val)                                      \
                       task->personality |= PER_ZEPTO_TASK;    \
               else                                           \
                       task->personality &= ~PER_ZEPTO_TASK;   \
           } while(0)

#define IS_ZEPTO_TASK(task) ((task->personality&PER_ZEPTO_TASK)!=0)

#define  ZEPTO_ELF_HDR_FLAG   0x00000080    /* e_flags */

#ifdef CONFIG_ZEPTO_MEMORY
#include <linux/zepto_bigmem.h>
#include <linux/zepto_bigmem_mmap.h>
#endif

extern int zepto_task_error(const char* fmt,...);  
/* 
defined in arch/powerpc/syslib/bgdd/zepto_task.c 

This function is used to print a critical error from a zepto task.
*/

#endif

