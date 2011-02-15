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

#ifndef __ZCL_TRACE_H_DEFINED__
#define __ZCL_TRACE_H_DEFINED__

#ifdef __cplusplus
extern "C" {
#endif

    /* debuggin functions, macros */

    typedef void  (*ZCL_TRACE_FUNC)(char* msg,...);

    void   zcl_register_trace(ZCL_TRACE_FUNC func);
    void   zcl_trace_start(unsigned level);
    void   zcl_trace_stop(void);
    char*  zcl_format(const char* fmt,...);
    void   zcl_trace(unsigned level, char* fmt,...);
    const char* zcl_basename(const char* p);
    void   zcl_error(char* fmt,...);

#define ZCL__FILE__  zcl_basename(__FILE__)

#define ZCL_DEBUG  1
#ifdef ZCL_DEBUG
#define ZCL_TRACE(LEVEL,STRING)   zcl_trace((LEVEL),"%s@%s(%d): %s\n",__func__,ZCL__FILE__,__LINE__,(STRING))
#else
#define ZCL_TRACE(LEVEL,STRING)  do {} while(0);
#endif


#ifdef __cplusplus
};
#endif


#endif

