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
 ********************************************************************/


#ifndef _BGP_DMA_MEMMAP_H_
#define _BGP_DMA_MEMMAP_H_

#define _BGP_DMA_NUM_INJ_FIFO_GROUPS		   4
#define _BGP_DMA_NUM_INJ_FIFOS_PER_GROUP	   32
#define _BGP_DMA_NUM_INJ_FIFOS			   (_BGP_DMA_NUM_INJ_FIFO_GROUPS * _BGP_DMA_NUM_INJ_FIFOS_PER_GROUP)

#define _BGP_DMA_NUM_REC_FIFO_GROUPS		   4
#define _BGP_DMA_NUM_REC_FIFOS_PER_GROUP	   9
#define _BGP_DMA_NUM_REC_FIFOS			   (_BGP_DMA_NUM_REC_FIFO_GROUPS * _BGP_DMA_NUM_REC_FIFOS_PER_GROUP)

/*   size = end - start - BGP_FIFO_SAFETY_MARGIN  */
/*   so you can disinguish between full and empty, in 16 byte units */
#define _BGP_DMA_FIFO_SAFETY_MARGIN		   1
#define _BGP_DMA_QUADS_PER_PACKET		   16

#define _BGP_DMA_NUM_COUNTER_GROUPS		   4
#define _BGP_DMA_NUM_COUNTERS_PER_GROUP		   64
#define _BGP_DMA_NUM_COUNTERS			   (_BGP_DMA_NUM_COUNTER_GROUPS * _BGP_DMA_NUM_COUNTERS_PER_GROUP)

/*   these are the lower 12 bits */
/* #define  _BGP_DMA_GROUP_A(g)                    ((g)*0x1000) */

/*  ------------------------------------------------ */
/*     Macros defining absolute virtual address */
/*  ------------------------------------------------ */
#define _BGP_VA_DMA_GROUP_A(g)			   (_BGP_VA_DMA + ((g)*0x1000))

/*  offset start of iDMA */
#define _BGP_VA_iDMA_GROUP_START(g)		   (_BGP_VA_DMA_GROUP_A(g) + 0x0 )

/*  repeated 32 times i=0 to 31 */
#define _BGP_VA_iDMA_START(g,i)                    (_BGP_VA_DMA_GROUP_A(g) + ((i)*0x0010) )
#define _BGP_VA_iDMA_END(g,i)                      (_BGP_VA_DMA_GROUP_A(g) + (0x0004+(i)*0x0010) )
#define _BGP_VA_iDMA_HEAD(g,i)                     (_BGP_VA_DMA_GROUP_A(g) + (0x0008+(i)*0x0010) )
#define _BGP_VA_iDMA_TAIL(g,i)                     (_BGP_VA_DMA_GROUP_A(g) + (0x000C+(i)*0x0010) )
#define _BGP_VA_iDMA_NOT_EMPTY(g)                  (_BGP_VA_DMA_GROUP_A(g) + 0x0200)
						    /* HOLE:	   ( _BGP_VA_DMA_GROUP_A(g)+0x0204)    */
#define _BGP_VA_iDMA_AVAILABLE(g)                  (_BGP_VA_DMA_GROUP_A(g) + 0x0208)
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x020C) */
#define _BGP_VA_iDMA_THRESHOLD_CROSSED(g)          (_BGP_VA_DMA_GROUP_A(g) + 0x0210)
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x0214) */
#define _BGP_VA_iDMA_CLEAR_THRESHOLD_CROSSED(g)    (_BGP_VA_DMA_GROUP_A(g) + 0x0218)
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x021C)  */
#define _BGP_VA_iDMA_ACTIVATED(g)                  (_BGP_VA_DMA_GROUP_A(g) + 0x220)
#define _BGP_VA_iDMA_ACTIVATE(g)                   (_BGP_VA_DMA_GROUP_A(g) + 0x224)
#define _BGP_VA_iDMA_DEACTIVATE(g)                 (_BGP_VA_DMA_GROUP_A(g) + 0x228)
						    /* HOLE:	   ( _BGP_VA_DMA_GROUP_A(g)+0x022C) to ( _BGP_VA_DMA_GROUP_A(g)+0x02FF) */
/*  repeated twice, i=0 to 1 */
#define _BGP_VA_iDMA_COUNTER_ENABLED(g,i)          (_BGP_VA_DMA_GROUP_A(g) + (0x0300 +(i)*0x0004) )
#define _BGP_VA_iDMA_COUNTER_ENABLE(g,i)           (_BGP_VA_DMA_GROUP_A(g) + (0x0308 +(i)*0x0004) )
#define _BGP_VA_iDMA_COUNTER_DISABLE(g,i)          (_BGP_VA_DMA_GROUP_A(g) + (0x0310 +(i)*0x0004) )
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x0318) to ( _BGP_VA_DMA_GROUP_A(g)+0x031C) */
/*  repeated twice, i=0 to 1 */
#define _BGP_VA_iDMA_COUNTER_HIT_ZERO(g,i)         (_BGP_VA_DMA_GROUP_A(g) + (0x0320 +(i)*0x0004) )
#define _BGP_VA_iDMA_COUNTER_CLEAR_HIT_ZERO(g,i)	(_BGP_VA_DMA_GROUP_A(g) + (0x0328 +(i)*0x0004) )
#define _BGP_VA_iDMA_COUNTER_GRP_STATUS(g)         (_BGP_VA_DMA_GROUP_A(g) + 0x0330)
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x0334) to ( _BGP_VA_DMA_GROUP_A(g)+0x03FC) */
/*  repeated 64 times  i=0 to 63 */
#define _BGP_VA_iDMA_COUNTER(g,i)                  ( _BGP_VA_DMA_GROUP_A(g) + (0x0400 +(i)*0x0010) )
#define _BGP_VA_iDMA_COUNTER_INCREMENT(g,i)        ( _BGP_VA_DMA_GROUP_A(g) + (0x0404 +(i)*0x0010) )
#define _BGP_VA_iDMA_COUNTER_BASE(g,i)             ( _BGP_VA_DMA_GROUP_A(g) + (0x0408 +(i)*0x0010) )
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x040C) to ( _BGP_VA_DMA_GROUP_A(g)+0x07FC) */

/*  offset start of rDMA  */
#define  _BGP_VA_rDMA_GROUP_START(g)               ( _BGP_VA_DMA_GROUP_A(g) + 0x0800 )

/*  repeated 8 times  i=0 to 7 */
#define _BGP_VA_rDMA_START(g,i)                    ( _BGP_VA_DMA_GROUP_A(g) + (0x0800 + (i)*0x0010) )
#define _BGP_VA_rDMA_END(g,i)                      ( _BGP_VA_DMA_GROUP_A(g) + (0x0804 + (i)*0x0010) )
#define _BGP_VA_rDMA_HEAD(g,i)                     ( _BGP_VA_DMA_GROUP_A(g) + (0x0808 + (i)*0x0010) )
#define _BGP_VA_rDMA_TAIL(g,i)                     ( _BGP_VA_DMA_GROUP_A(g) + (0x080C + (i)*0x0010) )
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x0890) to ( _BGP_VA_DMA_GROUP_A(g)+0x08FC) */
/*  repeated 16 times, 0 to 15 */
/*   below addresses have storage backing them, but are not used by the DMA */
#define _BGP_NUM_rDMA_UNUSED                       16
#define _BGP_VA_rDMA_UNUSED(g,i)                   ( _BGP_VA_DMA_GROUP_A(g) + (0x0900 + (i)*0x0004) )
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x0940) to ( _BGP_VA_DMA_GROUP_A(g)+0x09FC) */

#define _BGP_VA_rDMA_TAIL(g,i)                     ( _BGP_VA_DMA_GROUP_A(g) + (0x080C + (i)*0x0010) )
/* / repeated 2 times  i=0 to 1 */
#define _BGP_VA_rDMA_NOT_EMPTY(g,i)                ( _BGP_VA_DMA_GROUP_A(g) + (0x0A00 + (i)*0x0004) )
#define _BGP_VA_rDMA_AVAILABLE(g,i)                ( _BGP_VA_DMA_GROUP_A(g) + (0x0A08 + (i)*0x0004) )
#define _BGP_VA_rDMA_THRESHOLD_CROSSED(g,i)        ( _BGP_VA_DMA_GROUP_A(g) + (0x0A10 + (i)*0x0004) )
#define _BGP_VA_rDMA_CLEAR_THRESHOLD_CROSSED(g,i)  ( _BGP_VA_DMA_GROUP_A(g) + (0x0A18 + (i)*0x0004) )
						    /* HOLE:         ( _BGP_DMA_GROUP_A(g)+0x0A1C) to ( _BGP_VA_DMA_GROUP_A(g)+0x0AFC) */
/*  repeat 2 times, i=0 to 1 */
#define _BGP_VA_rDMA_COUNTER_ENABLED(g,i)          ( _BGP_VA_DMA_GROUP_A(g) + (0x0B00 + (i)*0x0004) )
#define _BGP_VA_rDMA_COUNTER_ENABLE(g,i)           ( _BGP_VA_DMA_GROUP_A(g) + (0x0B08 + (i)*0x0004) )
#define _BGP_VA_rDMA_COUNTER_DISABLE(g,i)          ( _BGP_VA_DMA_GROUP_A(g) + (0x0B10 + (i)*0x0004) )
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x0B18) to ( _BGP_VA_DMA_GROUP_A(g)+0x0B1C) */
/*  repeat 2 times, i=0 to 1 */
#define _BGP_VA_rDMA_COUNTER_HIT_ZERO(g,i)         ( _BGP_VA_DMA_GROUP_A(g) + (0x0B20 + (i)*0x0004) )
#define _BGP_VA_rDMA_COUNTER_CLEAR_HIT_ZERO(g,i)   ( _BGP_VA_DMA_GROUP_A(g) + (0x0B28 + (i)*0x0004) )
#define _BGP_VA_rDMA_COUNTER_GRP_STATUS(g)         ( _BGP_VA_DMA_GROUP_A(g) + 0x0B30)
						    /* HOLE:         ( _BGP_VA_DMA_GROUP_A(g)+0x0B34) to ( _BGP_VA_DMA_GROUP_A(g)+0x0BFC) */
/*  repeat 64 times, i=0 to 63 */
#define _BGP_VA_rDMA_COUNTER(g,i)                  ( _BGP_VA_DMA_GROUP_A(g) + (0x0C00 + (i)*0x0010) )
#define _BGP_VA_rDMA_COUNTER_INCREMENT(g,i)        ( _BGP_VA_DMA_GROUP_A(g) + (0x0C04 + (i)*0x0010) )
#define _BGP_VA_rDMA_COUNTER_BASE(g,i)             ( _BGP_VA_DMA_GROUP_A(g) + (0x0C08 + (i)*0x0010) )
#define _BGP_VA_rDMA_COUNTER_MAX(g,i)              ( _BGP_VA_DMA_GROUP_A(g) + (0x0C0C + (i)*0x0010) )



/*  --------------------------------------- */
/*     Macros defining address offset  */
/*  --------------------------------------- */


/*   these are the lower 12 bits */
#define  _BGP_DMA_GROUP_A_OFFSET(g)                    ((g)*0x1000)

/*  ---------------------- */
/*  offset start of iDMA */
/*  ---------------------- */
#define  _BGP_iDMA_GROUP_START_OFFSET(g)               ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0 )

/*  repeated 32 times i=0 to 31 */
#define _BGP_iDMA_START_OFFSET(g,i)                    ( _BGP_DMA_GROUP_A_OFFSET(g)+(i)*0x0010)
#define _BGP_iDMA_END_OFFSET(g,i)                      ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0004+(i)*0x0010)
#define _BGP_iDMA_HEAD_OFFSET(g,i)                     ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0008+(i)*0x0010)
#define _BGP_iDMA_TAIL_OFFSET(g,i)                     ( _BGP_DMA_GROUP_A_OFFSET(g)+0x000C+(i)*0x0010)
#define _BGP_iDMA_NOT_EMPTY_OFFSET(g)                  ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0200)
							 /* HOLE     ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0204)    */
#define _BGP_iDMA_AVAILABLE_OFFSET(g)                  ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0208)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x020C) */
#define _BGP_iDMA_THRESHOLD_CROSSED_OFFSET(g)          ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0210)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0214) */
#define _BGP_iDMA_CLEAR_THRESHOLD_CROSSED_OFFSET(g)    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0218)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x021C)  */
#define _BGP_iDMA_ACTIVATED_OFFSET(g)                  ( _BGP_DMA_GROUP_A_OFFSET(g)+0x220)
#define _BGP_iDMA_ACTIVATE_OFFSET(g)                   ( _BGP_DMA_GROUP_A_OFFSET(g)+0x224)
#define _BGP_iDMA_DEACTIVATE_OFFSET(g)                 ( _BGP_DMA_GROUP_A_OFFSET(g)+0x228)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x022C) to ( _BGP_DMA_GROUP_A_OFFSET(g)+0x02FF) */
/*  repeated twice, i=0 to 1 */
#define _BGP_iDMA_COUNTER_ENABLED_OFFSET(g,i)          ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0300 +(i)*0x0004)
#define _BGP_iDMA_COUNTER_ENABLE_OFFSET(g,i)           ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0308 +(i)*0x0004)
#define _BGP_iDMA_COUNTER_DISABLE_OFFSET(g,i)          ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0310 +(i)*0x0004)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0318) to ( _BGP_DMA_GROUP_A_OFFSET(g)+0x031C) */
/*  repeated twice, i=0 to 1 */
#define _BGP_iDMA_COUNTER_HIT_ZERO_OFFSET(g,i)         ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0320 +(i)*0x0004)
#define _BGP_iDMA_COUNTER_CLEAR_HIT_ZERO_OFFSET(g,i)   ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0328 +(i)*0x0004)
#define _BGP_iDMA_COUNTER_GRP_STATUS_OFFSET(g)         ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0330)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0334) to ( _BGP_DMA_GROUP_A_OFFSET(g)+0x03FC) */
/*  repeated 64 times  i=0 to 63 */
#define _BGP_iDMA_COUNTER_OFFSET(g,i)                  ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0400 +(i)*0x0010)
#define _BGP_iDMA_COUNTER_INCREMENT_OFFSET(g,i)        ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0404 +(i)*0x0010)
#define _BGP_iDMA_COUNTER_BASE_OFFSET(g,i)             ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0408 +(i)*0x0010)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x040C) to ( _BGP_DMA_GROUP_A_OFFSET(g)+0x07FC) */


/* ----------------------- */
/*  offset start of rDMA  */
/* ----------------------- */
#define  _BGP_rDMA_GROUP_START_OFFSET(g)               ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0800 )

/*  repeated 8 times  i=0 to 7 */
#define _BGP_rDMA_START_OFFSET(g,i)                    ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0800 + (i)*0x0010)
#define _BGP_rDMA_END_OFFSET(g,i)                      ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0804 + (i)*0x0010)
#define _BGP_rDMA_HEAD_OFFSET(g,i)                     ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x0808 + (i)*0x0010)
#define _BGP_rDMA_TAIL_OFFSET(g,i)                     ( _BGP_DMA_GROUP_A_OFFSET(g)+ 0x080C + (i)*0x0010)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0890) to ( _BGP_DMA_GROUP_A_OFFSET(g)+0x09FC) */
/* / repeated 2 times  i=0 to 1 */
#define _BGP_rDMA_NOT_EMPTY_OFFSET(g,i)                ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0A00 + (i)*0x0004)
#define _BGP_rDMA_AVAILABLE_OFFSET(g,i)                ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0A08 + (i)*0x0004)
#define _BGP_rDMA_THRESHOLD_CROSSED_OFFSET(g,i)        ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0A10 + (i)*0x0004)
#define _BGP_rDMA_CLEAR_THRESHOLD_CROSSED_OFFSET(g,i)  ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0A18 + (i)*0x0004)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0A1C) to ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0AFC) */
/*  repeat 2 times, i=0 to 1 */
#define _BGP_rDMA_COUNTER_ENABLED_OFFSET(g,i)          ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B00 + (i)*0x0004)
#define _BGP_rDMA_COUNTER_ENABLE_OFFSET(g,i)           ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B08 + (i)*0x0004)
#define _BGP_rDMA_COUNTER_DISABLE_OFFSET(g,i)          ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B10 + (i)*0x0004)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B18) to ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B1C) */
/*  repeat 2 times, i=0 to 1 */
#define _BGP_rDMA_COUNTER_HIT_ZERO_OFFSET(g,i)         ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B20 + (i)*0x0004)
#define _BGP_rDMA_COUNTER_CLEAR_HIT_ZERO_OFFSET(g,i)   ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B28 + (i)*0x0004)
#define _BGP_rDMA_COUNTER_GRP_STATUS_OFFSET(g)         ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B30)
							 /* HOLE:    ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0B34) to ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0BFC) */
/*  repeat 64 times, i=0 to 63 */
#define _BGP_rDMA_COUNTER_OFFSET(g,i)                  ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0C00 + (i)*0x0010)
#define _BGP_rDMA_COUNTER_INCREMENT_OFFSET(g,i)        ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0C04 + (i)*0x0010)
#define _BGP_rDMA_COUNTER_BASE_OFFSET(g,i)             ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0C08 + (i)*0x0010)
#define _BGP_rDMA_COUNTER_MAX_OFFSET(g,i)              ( _BGP_DMA_GROUP_A_OFFSET(g)+0x0C0C + (i)*0x0010)

#endif



