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

#ifndef __ZEPTO_DEBUG__
#define __ZEPTO_DEBUG__

#ifdef CONFIG_ZEPTO_DEBUG 

extern int zepto_debug_level; /* defined in arch/powerpc/kernel/setup_32.c */

#define zepto_debug(level,format,...)  if(level<=zepto_debug_level) { printk("Z: " format,##__VA_ARGS__); }

#else

#define zepto_debug(a,...)

#endif

#endif
