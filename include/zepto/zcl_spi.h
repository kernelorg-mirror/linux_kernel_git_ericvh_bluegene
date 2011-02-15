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

#ifndef __ZCL_SPI_H_DEFINED__
#define __ZCL_SPI_H_DEFINED__

#include <common/namespace.h>

__BEGIN_DECLS

#ifdef __ZCL_KERNEL__
#error "This should not be included from Linux kernel!"
#endif

#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <bpcore/ppc450_inlines.h>

#ifndef __INLINE__
#define __INLINE__ extern inline
#endif

extern uint32_t zcl_PhysicalProcessorID(void);
extern uint32_t zcl_ProcessCount(void);
extern int zcl_ProcessorCount(void);

extern unsigned  zcl_get_bigmemory_va_start(void);
extern unsigned  zcl_get_bigmemory_pa_start(void);
extern unsigned  zcl_get_bigmemory_len(void);
extern int       zcl_virt2phy(unsigned long va, unsigned long *pa);

extern int zcl_AllocateLockBox(uint32_t lockid, uint32_t numlocks,
				  uint32_t** ptr, uint32_t flags);


extern uint32_t  zcl_spi_CounterGroupQueryFree(uint32_t type, uint32_t group,
					       uint32_t* n_subgroups,
					       uint32_t* subgroups);

extern uint32_t zcl_spi_CounterGroupAllocate(uint32_t type,
                                      uint32_t group,
                                      uint32_t num_subgroups,
                                      uint32_t* subgroups,
                                      uint32_t target,
                                      uint32_t handler,
                                      uint32_t* handler_parm,
                                      uint32_t interruptGroup,
					     uint32_t* _cg_ptr);


extern uint32_t zcl_spi_InjFifoGroupQueryFree(
    uint32_t group, 
    uint32_t* num_fifos, 
    uint32_t* fifo_ids);

extern uint32_t zcl_spi_InjFifoGroupAllocate(   uint32_t group,
						uint32_t num_fifos,
						uint32_t* fifo_ids,
						uint16_t* priorities,
						uint16_t* locals,
						uint8_t* ts_inj_maps,
						uint32_t* fg_ptr );



extern uint32_t zcl_spi_InjFifoInitById(uint32_t* fg_ptr,
					int  fifo_id,
					uint32_t* va_start,
					uint32_t* va_head,
					uint32_t* va_end);


extern  uint32_t zcl_spi_RecFifoSetMap(uint32_t* rec_map);

extern uint32_t zcl_spi_RecFifoGetFifoGroup(
    uint32_t*                         fifogroup,
    int                               group,
    int                               target);

extern uint32_t zcl_spi_RecFifoInitByID(
    uint32_t*          fg_ptr,
    int                fifo_id,
    void               *va_start,
    void               *va_head,
    void               *va_end  );


extern uint32_t zcl_spi_ChgCounterInterruptEnables(uint32_t enable);

extern uint32_t zcl_spi_globalBarrier(unsigned msec);

extern uint32_t zcl_spi_debug_tag(unsigned long tag);

extern uint32_t zcl_spi_donothing(void);

extern int zcl_getpersonality(char* personality, size_t size);

extern int zcl_Coord2Rank(uint32_t xcoord, uint32_t ycoord, uint32_t zcoord, uint32_t tcoord, uint32_t* rank, uint32_t* numnodes);
extern int zcl_Rank2Coord(uint32_t rank, uint32_t* xcoord, uint32_t* ycoord, uint32_t* zcoord, uint32_t* tcoord);

#ifndef kernel_coords_t_defined
typedef struct _Kernel_Coordinates {
    unsigned char x;
    unsigned char y;
    unsigned char z;
    unsigned char t;
} kernel_coords_t;
#define kernel_coords_t_defined
#endif

extern int zcl_Rank2Coords(kernel_coords_t* coordinates, uint32_t len);

extern int zcl_rank(void);
extern int zcl_size(void);


__END_DECLS


#endif /* #ifndef __ZCL_SPI_H_DEFINED__ */
