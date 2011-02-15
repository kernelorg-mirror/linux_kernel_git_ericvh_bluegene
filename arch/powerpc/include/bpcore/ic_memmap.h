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
/**
 * \file bpcore/ic_memmap.h
 */



/**
 * BGP Interrupt Controller Register mapping and bit definition.
 *
 * Note: preliminary register assignment.
 */


/* ************************************************************************* */
/*      Architected BGP Interrupt Controller Registers                       */
/* ************************************************************************* */
/* Authors: Jose R. Brunheroto, Martin Ohmacht                               */
/* Reflects the contents of the document http://w3vlsi.watson.ibm.com//      */
/*                                                                           */
/* ************************************************************************* */



/*

     BIC CRIT hierarchy register
     +------------------------------------+
     |0 1 2 3 4 5 6 7 8 9          ... 31 |
     +------------------------------------+
      | | | | | | | | | |
      | | | | | | |                                    BIC UNIT 6
      | | | | | | |                                  +-----+
      | | | | | | +--------------------------------  |0-31 | -
      | | | | | |                                    +-----+
      | | | | | |                                      BIC UNIT 5
      | | | | | |                                    +-----+
      | | | | | +----------------------------------  |0-31 | -.
      | | | | |                                      +-----+
      | | | | |                                        BIC GROUP 4
      | | | | |                                      +-----+
      | | | | +------------------------------------  |0-31 | -
      | | | |                                        +-----+
      | | | |                                          BIC GROUP3
      | | | |                                        +-----+
      | | | +--------------------------------------  |0-31 | -
      | | |                                          +-----+
      | | |                                            BIC GROUP 2
      | | |                                          +-----+
      | | +----------------------------------------  |0-31 | -
      | |                                            +-----+
      | |                                              BIC GROUP 1
      | |                                            +-----+
      | +------------------------------------------  |0-31 | -
      |                                              +-----+
      |                                                BIC GROUP 0
      |                                              +-----+
      +--------------------------------------------  |0-31 | -
                                                     +-----+


     BIC NCRIT hierarchy register
     +------------------------------------+
     |0 1 2 3 4 5 6 7 8 9          ... 31 |
     +------------------------------------+
      | | | | | | | | | |
      | | | | | | |                                    BIC UNIT 6
      | | | | | | |                                  +-----+
      | | | | | | +--------------------------------  |0-31 | -
      | | | | | |                                    +-----+
      | | | | | |                                      BIC UNIT 5
      | | | | | |                                    +-----+
      | | | | | +----------------------------------  |0-31 | -.
      | | | | |                                      +-----+
      | | | | |                                        BIC GROUP 4
      | | | | |                                      +-----+
      | | | | +------------------------------------  |0-31 | -
      | | | |                                        +-----+
      | | | |                                          BIC GROUP3
      | | | |                                        +-----+
      | | | +--------------------------------------  |0-31 | -
      | | |                                          +-----+
      | | |                                            BIC GROUP 2
      | | |                                          +-----+
      | | +----------------------------------------  |0-31 | -
      | |                                            +-----+
      | |                                              BIC GROUP 1
      | |                                            +-----+
      | +------------------------------------------  |0-31 | -
      |                                              +-----+
      |                                                BIC GROUP 0
      |                                              +-----+
      +--------------------------------------------  |0-31 | -
                                                     +-----+


     BIC MCCU hierarchy register
     +------------------------------------+
     |0 1 2 3 4 5 6 7 8 9          ... 31 |
     +------------------------------------+
      | | | | | | | | | |
      | | | | | | |                                    BIC UNIT 6
      | | | | | | |                                  +-----+
      | | | | | | +--------------------------------  |0-31 | -
      | | | | | |                                    +-----+
      | | | | | |                                      BIC UNIT 5
      | | | | | |                                    +-----+
      | | | | | +----------------------------------  |0-31 | -.
      | | | | |                                      +-----+
      | | | | |                                        BIC GROUP 4
      | | | | |                                      +-----+
      | | | | +------------------------------------  |0-31 | -
      | | | |                                        +-----+
      | | | |                                          BIC GROUP3
      | | | |                                        +-----+
      | | | +--------------------------------------  |0-31 | -
      | | |                                          +-----+
      | | |                                            BIC GROUP 2
      | | |                                          +-----+
      | | +----------------------------------------  |0-31 | -
      | |                                            +-----+
      | |                                              BIC GROUP 1
      | |                                            +-----+
      | +------------------------------------------  |0-31 | -
      |                                              +-----+
      |                                                BIC GROUP 0
      |                                              +-----+
      +--------------------------------------------  |0-31 | -
                                                     +-----+

*/


#ifndef _IC_MEMMAP_H_                     /*  Prevent multiple inclusion */
#define _IC_MEMMAP_H_



#define _BGP_IC_NUMBER_OF_GROUPS     (10)        /*  number of groups (0..9 inclusive) */



#define _BGP_IC_TARGET_DISABLED      0x00        /*  disabled */
#define _BGP_IC_TARGET_NCRIT_BCAST   0x01        /*  non-critical broadcast */
#define _BGP_IC_TARGET_CRIT_BCAST    0x02        /*  critical broadcast */
#define _BGP_IC_TARGET_MCHK_BCAST    0x03        /*  machine check */

#define _BGP_IC_TARGET_NCRIT_CORE0   0x04        /*  non-critical core 0 */
#define _BGP_IC_TARGET_NCRIT_CORE1   0x05        /*  non-critical core 1 */
#define _BGP_IC_TARGET_NCRIT_CORE2   0x06        /*  non-critical core 2 */
#define _BGP_IC_TARGET_NCRIT_CORE3   0x07        /*  non-critical core 3 */

#define _BGP_IC_TARGET_CRIT_CORE0    0x08        /*  critical core 0 */
#define _BGP_IC_TARGET_CRIT_CORE1    0x09        /*  critical core 1 */
#define _BGP_IC_TARGET_CRIT_CORE2    0x0A        /*  critical core 2 */
#define _BGP_IC_TARGET_CRIT_CORE3    0x0B        /*  critical core 3 */

#define _BGP_IC_TARGET_MCHK_CORE0    0x0C        /*  machine check core 0 */
#define _BGP_IC_TARGET_MCHK_CORE1    0x0D        /*  machine check core 1 */
#define _BGP_IC_TARGET_MCHK_CORE2    0x0E        /*  machine check core 2 */
#define _BGP_IC_TARGET_MCHK_CORE3    0x0F        /*  machine check core 3 */


typedef struct _BGP_IC_Group_t
{
    volatile unsigned int status;                        /*  status (read and write) */
    volatile unsigned int rd_clr_status;                 /*  status (read and clear) */
    volatile unsigned int status_clr;                    /*  status (write and clear) */
    volatile unsigned int status_set;                    /*  status (write and set) */

    volatile unsigned int target_irq0_7;                 /*  target selector (IRQ 0:7) */
    volatile unsigned int target_irq8_15;                /*  target selector (IRQ 8:15) */
    volatile unsigned int target_irq16_23;               /*  target selector (IRQ 16:23) */
    volatile unsigned int target_irq24_31;               /*  target selector (IRQ 24:31) */

    union {
          volatile unsigned int ncrit_masked_irq[ 4 ];        /*  array for easier access */
          struct {
                 volatile unsigned int ncrit_0_masked_irq;    /*  non-critical core 0 masked irq (RO) */
                 volatile unsigned int ncrit_1_masked_irq;    /*  non-critical core 1 masked irq */
                 volatile unsigned int ncrit_2_masked_irq;    /*  non-critical core 2 masked irq */
                 volatile unsigned int ncrit_3_masked_irq;    /*  non-critical core 3 masked irq */
                 };
          };

    union {
          volatile unsigned int crit_masked_irq[ 4 ];         /*  array for easier access */
          struct {
                 volatile unsigned int crit_0_masked_irq;     /*  critical core 0 masked irq (RO) */
                 volatile unsigned int crit_1_masked_irq;     /*  critical core 1 masked irq */
                 volatile unsigned int crit_2_masked_irq;     /*  critical core 2 masked irq */
                 volatile unsigned int crit_3_masked_irq;     /*  critical core 3 masked irq */
                 };
           };

    union {
          volatile unsigned int mchk_masked_irq[ 4 ];         /*  array for easier access */
          struct {
                 volatile unsigned int mchk_0_masked_irq;     /*  machine check core 0 masked irq (RO) */
                 volatile unsigned int mchk_1_masked_irq;     /*  machine check core 1 masked irq */
                 volatile unsigned int mchk_2_masked_irq;     /*  machine check core 2 masked irq */
                 volatile unsigned int mchk_3_masked_irq;     /*  machine check core 3 masked irq */
                 };
           };

    volatile unsigned int ti_mchk_mask;                       /*  (RW) TestInt MachineCheck Mask */
    volatile unsigned int upc_time_stamp_mask;                /*  (RW) UPC Time Stamp Mask */
    volatile unsigned int clock_sync_stop_mask;               /*  (RW) Clock Sync-Stop Mask */

    volatile unsigned int ti_mchk_wof;                        /*  (RW) TestInt Mchk Who's on First */
    volatile unsigned int upc_time_stamp_wof;                 /*  (RW) UPC Time Stamp Who's on First */
    volatile unsigned int clock_sync_stop_wof;                /*  (RW) Clock Sync-Stop Who's on First */

    volatile unsigned int ti_mchk;                            /*  (RO) TestInt Mchk */
    volatile unsigned int upc_time_stamp;                     /*  (RO) UPC Time Stamp */
    volatile unsigned int clock_sync_stop;                    /*  (RO) Clock Sync-Stop */


} _BGP_IC_Group_t;



#define _BGP_IC_MEM_GROUP_SIZE      (0x80)         /*  group size in bytes */

/*  macros for indexed access to grouups */
#define _BGP_IC_MEM_GROUP_OFFSET(_grp)          ( _BGP_IC_MEM_GROUP0_OFFSET + (_grp)*_BGP_IC_MEM_GROUP_SIZE )


/*  Defines BGP Interrupt Controller Register Offset (memory mapped access) */
#define _BGP_IC_MEM_GROUP0_OFFSET  (0x0000)        /*  Group 0 offset */
#define _BGP_IC_MEM_GROUP1_OFFSET  (0x0080)        /*  Group 1 offset */
#define _BGP_IC_MEM_GROUP2_OFFSET  (0x0100)        /*  Group 2 offset */
#define _BGP_IC_MEM_GROUP3_OFFSET  (0x0180)        /*  Group 3 offset */
#define _BGP_IC_MEM_GROUP4_OFFSET  (0x0200)        /*  Group 4 offset */
#define _BGP_IC_MEM_GROUP5_OFFSET  (0x0280)        /*  Group 5 offset */
#define _BGP_IC_MEM_GROUP6_OFFSET  (0x0300)        /*  Group 6 offset */
#define _BGP_IC_MEM_GROUP7_OFFSET  (0x0380)        /*  Group 7 offset */
#define _BGP_IC_MEM_GROUP8_OFFSET  (0x0400)        /*  Group 8 offset */
#define _BGP_IC_MEM_GROUP9_OFFSET  (0x0480)        /*  Group 9 offset */

/*  reserved group offset */
#define _BGP_IC_MEM_GROUP10_OFFSET (0x0500)        /*  Group 10 offset */
#define _BGP_IC_MEM_GROUP11_OFFSET (0x0580)        /*  Group 11 offset */
#define _BGP_IC_MEM_GROUP12_OFFSET (0x0600)        /*  Group 12 offset */
#define _BGP_IC_MEM_GROUP13_OFFSET (0x0680)        /*  Group 13 offset */
#define _BGP_IC_MEM_GROUP14_OFFSET (0x0700)        /*  Group 14 offset */




/*  Hierarchy Registers offsets */
#define _BGP_IC_MEM_HNCR_OFFSET   (0x0780)         /*  Hierarchy Non-Critical Register */
#define _BGP_IC_MEM_HNCR0_OFFSET  (0x0780)         /*  Hierarchy Non-Critical Register (core 0) */
#define _BGP_IC_MEM_HNCR1_OFFSET  (0x0784)         /*  Hierarchy Non-Critical Register (core 1) */
#define _BGP_IC_MEM_HNCR2_OFFSET  (0x0788)         /*  Hierarchy Non-Critical Register (core 2) */
#define _BGP_IC_MEM_HNCR3_OFFSET  (0x078C)         /*  Hierarchy Non-Critical Register (core 3) */


#define _BGP_IC_MEM_HCR_OFFSET    (0x0790)         /*  Hierarchy Critical Register */
#define _BGP_IC_MEM_HCR0_OFFSET   (0x0790)         /*  Hierarchy Critical Register (core 0) */
#define _BGP_IC_MEM_HCR1_OFFSET   (0x0794)         /*  Hierarchy Critical Register (core 1) */
#define _BGP_IC_MEM_HCR2_OFFSET   (0x0798)         /*  Hierarchy Critical Register (core 2) */
#define _BGP_IC_MEM_HCR3_OFFSET   (0x079C)         /*  Hierarchy Critical Register (core 3) */


#define _BGP_IC_MEM_HMCHKR_OFFSET  (0x07A0)        /*  Hierarchy Machine Check Register */
#define _BGP_IC_MEM_HMCHKR0_OFFSET (0x07A0)        /*  Hierarchy Machine Check Register (core 0) */
#define _BGP_IC_MEM_HMCHKR1_OFFSET (0x07A4)        /*  Hierarchy Machine Check Register (core 1) */
#define _BGP_IC_MEM_HMCHKR2_OFFSET (0x07A8)        /*  Hierarchy Machine Check Register (core 2) */
#define _BGP_IC_MEM_HMCHKR3_OFFSET (0x07AC)        /*  Hierarchy Machine Check Register (core 3) */


#define _BGP_IC_MEM_HR_TI_MCHECK_OFFSET             (0x07B0)      /*  hierarchy register ti_m_check (RO) */
#define _BGP_IC_MEM_HR_UPC_TIMESTAMP_OFFSET         (0x07B4)      /*  hierarchy register upc_timestamp_event (RO) */
#define _BGP_IC_MEM_HR_CI_SYNC_STOP_OFFSET          (0x07B8)      /*  hierarchy register ci_sync_stop (RO) */


#define _BGP_IC_MEM_ERR_RW_OFFSET                   (0x07C0)      /*  IC Error Register (RW) */
#define _BGP_IC_MEM_ERR_RDCLR_OFFSET                (0x07C4)      /*  IC Error Register (RO) (Read Clear all bits) */
#define _BGP_IC_MEM_ERR_ADDR_OFFSET                 (0x07C8)      /*  IC Error Address Register (RO) */
#define _BGP_IC_MEM_ERR_DATA_OFFSET                 (0x07CC)      /*  IC Error Data Register (RO) */


#define _BGP_IC_MEM_HR_TI_MCHECK_WOF_OFFSET         (0x07D0)    /*  hierarchy register ti_m_check_WOF (RW) */
#define _BGP_IC_MEM_HR_UPC_TIMESTAMP_WOF_OFFSET     (0x07D4)    /*  hierarchy register upc_timestamp_event_WOF (RW) */
#define _BGP_IC_MEM_HR_CI_SYNC_STOP_WOF_OFFSET      (0x07D8)    /*  hierarchy register ci_sync_stop_WOF (RW) */



/* ************************************************************************* */
/*              definitions for each interrupt generating device             */
/* ************************************************************************* */

/* ************************************************************************* */
/* Core-to-Core Software interrupts: Group 0 bits 00:31                      */
/* ************************************************************************* */

#define _BGP_IC_C2C_HIER_POS      0
#define _BGP_IC_C2C_UNIT_NUM      0
#define _BGP_IC_C2C_UNIT_POS      0
#define _BGP_IC_C2C_UNIT_SIZE     32
#define _BGP_IC_C2C_UNIT_MASK     0xffffffff

/* ************************************************************************* */
/* Core-to-Core Software interrupts: Group 0 bits 00:07  (Core 0)            */
/* ************************************************************************* */

#define _BGP_IC_C2C_C0_HIER_POS   0
#define _BGP_IC_C2C_C0_UNIT_NUM   0
#define _BGP_IC_C2C_C0_UNIT_POS   0
#define _BGP_IC_C2C_C0_UNIT_SIZE  8
#define _BGP_IC_C2C_C0_UNIT_MASK  0xff000000


/* ************************************************************************* */
/* Core-to-Core Software interrupts: Group 0 bits 08:15  (Core 1)            */
/* ************************************************************************* */

#define _BGP_IC_C2C_C1_HIER_POS   0
#define _BGP_IC_C2C_C1_UNIT_NUM   0
#define _BGP_IC_C2C_C1_UNIT_POS   8
#define _BGP_IC_C2C_C1_UNIT_SIZE  8
#define _BGP_IC_C2C_C1_UNIT_MASK  0x00ff0000


/* ************************************************************************* */
/* Core-to-Core Software interrupts: Group 0 bits 16:23  (Core 2)            */
/* ************************************************************************* */

#define _BGP_IC_C2C_C2_HIER_POS   0
#define _BGP_IC_C2C_C2_UNIT_NUM   0
#define _BGP_IC_C2C_C2_UNIT_POS   16
#define _BGP_IC_C2C_C2_UNIT_SIZE  8
#define _BGP_IC_C2C_C2_UNIT_MASK  0x0000ff00



/* ************************************************************************* */
/* Core-to-Core Software interrupts: Group 0 bits 24:31  (Core 3)            */
/* ************************************************************************* */

#define _BGP_IC_C2C_C3_HIER_POS   0
#define _BGP_IC_C2C_C3_UNIT_NUM   0
#define _BGP_IC_C2C_C3_UNIT_POS   24
#define _BGP_IC_C2C_C3_UNIT_SIZE  8
#define _BGP_IC_C2C_C3_UNIT_MASK  0x000000ff





/* ************************************************************************* */
/* DMA Fatal Interrupt Request: Group 1 bits 00:31                           */
/* ************************************************************************* */

#define _BGP_IC_DMA_FT_HIER_POS   1
#define _BGP_IC_DMA_FT_UNIT_NUM   1
#define _BGP_IC_DMA_FT_UNIT_POS   0
#define _BGP_IC_DMA_FT_UNIT_SIZE  32
#define _BGP_IC_DMA_FT_UNIT_MASK  0xffffffff

/* ************************************************************************* */
/* DMA Non-Fatal Interrupt Request: Group 2 bits 00:31                       */
/* ************************************************************************* */

#define _BGP_IC_DMA_NFT_G2_HIER_POS   2
#define _BGP_IC_DMA_NFT_G2_UNIT_NUM   2
#define _BGP_IC_DMA_NFT_G2_UNIT_POS   0
#define _BGP_IC_DMA_NFT_G2_UNIT_SIZE  32
#define _BGP_IC_DMA_NFT_G2_UNIT_MASK  0xffffffff

/* ************************************************************************* */
/* DMA Non-Fatal Interrupt Request: Group 3 bits 00:31                       */
/* ************************************************************************* */

#define _BGP_IC_DMA_NFT_G3_HIER_POS   3
#define _BGP_IC_DMA_NFT_G3_UNIT_NUM   3
#define _BGP_IC_DMA_NFT_G3_UNIT_POS   0
#define _BGP_IC_DMA_NFT_G3_UNIT_SIZE  32
#define _BGP_IC_DMA_NFT_G3_UNIT_MASK  0xffffffff


/* ************************************************************************* */
/* DP0 PU0 Interrupt Request:  Group 4  bits 00:02                           */
/* ************************************************************************* */

#define _BGP_IC_DP0_PU0_HIER_POS      4
#define _BGP_IC_DP0_PU0_UNIT_NUM      4
#define _BGP_IC_DP0_PU0_UNIT_POS      0
#define _BGP_IC_DP0_PU0_UNIT_SIZE     3
#define _BGP_IC_DP0_PU0_UNIT_MASK     0xE0000000

/* ************************************************************************* */
/* DP0 PU1 Interrupt Request:  Group 4  bits 03:05                           */
/* ************************************************************************* */

#define _BGP_IC_DP0_PU1_HIER_POS      4
#define _BGP_IC_DP0_PU1_UNIT_NUM      4
#define _BGP_IC_DP0_PU1_UNIT_POS      3
#define _BGP_IC_DP0_PU1_UNIT_SIZE     3
#define _BGP_IC_DP0_PU1_UNIT_MASK     0x1C000000

/* ************************************************************************* */
/* DP1 PU0 Interrupt Request:  Group 4  bits 06:08                           */
/* ************************************************************************* */

#define _BGP_IC_DP1_PU0_HIER_POS      4
#define _BGP_IC_DP1_PU0_UNIT_NUM      4
#define _BGP_IC_DP1_PU0_UNIT_POS      6
#define _BGP_IC_DP1_PU0_UNIT_SIZE     3
#define _BGP_IC_DP1_PU0_UNIT_MASK     0x03800000

/* ************************************************************************* */
/* DP1 PU1 Interrupt Request:  Group 4  bits 09:11                           */
/* ************************************************************************* */

#define _BGP_IC_DP1_PU1_HIER_POS      4
#define _BGP_IC_DP1_PU1_UNIT_NUM      4
#define _BGP_IC_DP1_PU1_UNIT_POS      9
#define _BGP_IC_DP1_PU1_UNIT_SIZE     3
#define _BGP_IC_DP1_PU1_UNIT_MASK     0x00700000


/* ************************************************************************* */
/* Global Interrupt:           Group 4  bits 12:21                           */
/* ************************************************************************* */

#define _BGP_IC_GINT_HIER_POS         4
#define _BGP_IC_GINT_UNIT_NUM         4
#define _BGP_IC_GINT_UNIT_POS         12
#define _BGP_IC_GINT_UNIT_SIZE        10
#define _BGP_IC_GINT_UNIT_MASK        0x000FFC00


/* ************************************************************************* */
/* SRAM Interrupt Request:      Group 4  bits 22:24                          */
/* ************************************************************************* */

#define _BGP_IC_SRAM_HIER_POS         4
#define _BGP_IC_SRAM_UNIT_NUM         4
#define _BGP_IC_SRAM_UNIT_POS         22
#define _BGP_IC_SRAM_UNIT_SIZE        3
#define _BGP_IC_SRAM_UNIT_MASK        0x00000380


/* ************************************************************************* */
/* TI Global Attention Interrupt request:     Group 4 bit 25                 */
/* ************************************************************************* */

#define _BGP_IC_GLOB_ATT_HIER_POS     4
#define _BGP_IC_GLOB_ATT_UNIT_NUM     4
#define _BGP_IC_GLOB_ATT_UNIT_POS     25
#define _BGP_IC_GLOB_ATT_UNIT_SIZE    1
#define _BGP_IC_GLOB_ATT_UNIT_MASK    0x00000040


/* ************************************************************************* */
/* TI LB Scan Attention Interrupt request:    Group 4 bit 26                 */
/* ************************************************************************* */

#define _BGP_IC_LB_SCATTN_HIER_POS    4
#define _BGP_IC_LB_SCATTN_UNIT_NUM    4
#define _BGP_IC_LB_SCATTN_UNIT_POS    26
#define _BGP_IC_LB_SCATTN_UNIT_SIZE   1
#define _BGP_IC_LB_SCATTN_UNIT_MASK   0x00000020


/* ************************************************************************* */
/* TI AB Scan Attention Interrupt request:    Group 4 bit 27                 */
/* ************************************************************************* */

#define _BGP_IC_AB_SCATTN_HIER_POS    4
#define _BGP_IC_AB_SCATTN_UNIT_NUM    4
#define _BGP_IC_AB_SCATTN_UNIT_POS    27
#define _BGP_IC_AB_SCATTN_UNIT_SIZE   1
#define _BGP_IC_AB_SCATTN_UNIT_MASK   0x00000010


/* ************************************************************************* */
/* TI HB Scan Attention Interrupt request:    Group 4 bit 28                 */
/* ************************************************************************* */

#define _BGP_IC_HB_SCATTN_HIER_POS    4
#define _BGP_IC_HB_SCATTN_UNIT_NUM    4
#define _BGP_IC_HB_SCATTN_UNIT_POS    28
#define _BGP_IC_HB_SCATTN_UNIT_SIZE   1
#define _BGP_IC_HB_SCATTN_UNIT_MASK   0x00000008


/* ************************************************************************* */
/* TI DCR Read Timeout Interrupt request:    Group 4 bit 29                  */
/* ************************************************************************* */

#define _BGP_IC_DCR_RD_TO_HIER_POS    4
#define _BGP_IC_DCR_RD_TO_UNIT_NUM    4
#define _BGP_IC_DCR_RD_TO_UNIT_POS    29
#define _BGP_IC_DCR_RD_TO_UNIT_SIZE   1
#define _BGP_IC_DCR_RD_TO_UNIT_MASK   0x00000004


/* ************************************************************************* */
/* TI DCR Write Timeout Interrupt request:    Group 4 bit 30                 */
/* ************************************************************************* */

#define _BGP_IC_DCR_WR_TO_HIER_POS    4
#define _BGP_IC_DCR_WR_TO_UNIT_NUM    4
#define _BGP_IC_DCR_WR_TO_UNIT_POS    30
#define _BGP_IC_DCR_WR_TO_UNIT_SIZE   1
#define _BGP_IC_DCR_WR_TO_UNIT_MASK   0x00000002



/* ************************************************************************* */
/* Collective Non-Critical interrupt:       Group 5 bits 00:19               */
/* ************************************************************************* */

#define _BGP_IC_COLNCRIT_HIER_POS     5
#define _BGP_IC_COLNCRIT_UNIT_NUM     5
#define _BGP_IC_COLNCRIT_UNIT_POS     0
#define _BGP_IC_COLNCRIT_UNIT_SIZE    20
#define _BGP_IC_COLNCRIT_UNIT_MASK    0xFFFFF000

/* ************************************************************************* */
/* Collective Critical interrupt:           Group 5 bits 20:23               */
/* ************************************************************************* */

#define _BGP_IC_COLCRIT_HIER_POS      5
#define _BGP_IC_COLCRIT_UNIT_NUM      5
#define _BGP_IC_COLCRIT_UNIT_POS      20
#define _BGP_IC_COLCRIT_UNIT_SIZE     4
#define _BGP_IC_COLCRIT_UNIT_MASK     0x00000f00


/* ************************************************************************* */
/* SerDesr machine check:                   Group 6 bits 0:23                */
/* ************************************************************************* */

#define _BGP_IC_SERDES_MCK_HIER_POS   6
#define _BGP_IC_SERDES_MCK_UNIT_NUM   6
#define _BGP_IC_SERDES_MCK_UNIT_POS   0
#define _BGP_IC_SERDES_MCK_UNIT_SIZE  24
#define _BGP_IC_SERDES_MCK_UNIT_MASK  0xFFFFFF00


/* ************************************************************************* */
/* UPC interrupt request:                   Group 6 bit 24                   */
/* ************************************************************************* */

#define _BGP_IC_UPC_HIER_POS          6
#define _BGP_IC_UPC_UNIT_NUM          6
#define _BGP_IC_UPC_UNIT_POS          24
#define _BGP_IC_UPC_UNIT_SIZE         1
#define _BGP_IC_UPC_UNIT_MASK         0x00000080


/* ************************************************************************* */
/* UPC Error interrupt request:             Group 6 bit 25                   */
/* ************************************************************************* */

#define _BGP_IC_UPCERR_HIER_POS       6
#define _BGP_IC_UPCERR_UNIT_NUM       6
#define _BGP_IC_UPCERR_UNIT_POS       25
#define _BGP_IC_UPCERR_UNIT_SIZE      1
#define _BGP_IC_UPCERR_UNIT_MASK      0x00000040

/* ************************************************************************* */
/* DCR Bus interrupt request:               Group 6 bit 26                   */
/* ************************************************************************* */

#define _BGP_IC_DCRBUS_HIER_POS       6
#define _BGP_IC_DCRBUS_UNIT_NUM       6
#define _BGP_IC_DCRBUS_UNIT_POS       26
#define _BGP_IC_DCRBUS_UNIT_SIZE      1
#define _BGP_IC_DCRBUS_UNIT_MASK      0x00000020

/* ************************************************************************* */
/* BIC machine check:                      Group 6 bit 27                    */
/* ************************************************************************* */

#define _BGP_IC_BIC_MCHK_HIER_POS     6
#define _BGP_IC_BIC_MCHK_UNIT_NUM     6
#define _BGP_IC_BIC_MCHK_UNIT_POS     27
#define _BGP_IC_BIC_MCHK_UNIT_SIZE    1
#define _BGP_IC_BIC_MCHK_UNIT_MASK    0x00000010

/* ************************************************************************* */
/* BIC interrupt request:                   Group 6 bit 28                   */
/* ************************************************************************* */

#define _BGP_IC_BIC_IRQ_HIER_POS      6
#define _BGP_IC_BIC_IRQ_UNIT_NUM      6
#define _BGP_IC_BIC_IRQ_UNIT_POS      28
#define _BGP_IC_BIC_IRQ_UNIT_SIZE     1
#define _BGP_IC_BIC_IRQ_UNIT_MASK     0x00000008

/* ************************************************************************* */
/* DEVBUS interrupt request:                Group 6 bit 29                   */
/* ************************************************************************* */

#define _BGP_IC_DEVBUS_IRQ_HIER_POS   6
#define _BGP_IC_DEVBUS_IRQ_UNIT_NUM   6
#define _BGP_IC_DEVBUS_IRQ_UNIT_POS   29
#define _BGP_IC_DEVBUS_IRQ_UNIT_SIZE  1
#define _BGP_IC_DEVBUS_IRQ_UNIT_MASK  0x00000004

/* ************************************************************************* */
/* Clockstop Stopped interrupt request:     Group 6 bit 30                   */
/* ************************************************************************* */

#define _BGP_IC_CLK_STOP_HIER_POS     6
#define _BGP_IC_CLK_STOP_UNIT_NUM     6
#define _BGP_IC_CLK_STOP_UNIT_POS     30
#define _BGP_IC_CLK_STOP_UNIT_SIZE    1
#define _BGP_IC_CLK_STOP_UNIT_MASK    0x00000002

/* ************************************************************************* */
/* Environment Monitor interrupt request:   Group 6 bit 31                   */
/* ************************************************************************* */

#define _BGP_IC_ENV_MON_HIER_POS      6
#define _BGP_IC_ENV_MON_UNIT_NUM      6
#define _BGP_IC_ENV_MON_UNIT_POS      31
#define _BGP_IC_ENV_MON_UNIT_SIZE     1
#define _BGP_IC_ENV_MON_UNIT_MASK     0x00000001


/* ************************************************************************* */
/* L30 machine check:                       Group 7 bits 0:10                */
/* ************************************************************************* */

#define _BGP_IC_L30_MCHK_HIER_POS     7
#define _BGP_IC_L30_MCHK_UNIT_NUM     7
#define _BGP_IC_L30_MCHK_UNIT_POS     0
#define _BGP_IC_L30_MCHK_UNIT_SIZE    11
#define _BGP_IC_L30_MCHK_UNIT_MASK    0xFFE00000

/* ************************************************************************* */
/* L30 interrupt request:                   Group 7 bits 11                  */
/* ************************************************************************* */

#define _BGP_IC_L30_IRQ_HIER_POS     7
#define _BGP_IC_L30_IRQ_UNIT_NUM     7
#define _BGP_IC_L30_IRQ_UNIT_POS     11
#define _BGP_IC_L30_IRQ_UNIT_SIZE    1
#define _BGP_IC_L30_IRQ_UNIT_MASK    0x00100000

/* ************************************************************************* */
/* L31 machine check:                       Group 7 bits 12:22               */
/* ************************************************************************* */

#define _BGP_IC_L31_MCHK_HIER_POS     7
#define _BGP_IC_L31_MCHK_UNIT_NUM     7
#define _BGP_IC_L31_MCHK_UNIT_POS     12
#define _BGP_IC_L31_MCHK_UNIT_SIZE    11
#define _BGP_IC_L31_MCHK_UNIT_MASK    0x000FFE00

/* ************************************************************************* */
/* L31 interrupt request:                   Group 7 bits 23                  */
/* ************************************************************************* */

#define _BGP_IC_L31_IRQ_HIER_POS      7
#define _BGP_IC_L31_IRQ_UNIT_NUM      7
#define _BGP_IC_L31_IRQ_UNIT_POS      23
#define _BGP_IC_L31_IRQ_UNIT_SIZE     1
#define _BGP_IC_L31_IRQ_UNIT_MASK     0x00000100


/* ************************************************************************* */
/* DDR 0 Recoverable error:                 Group 7 bit 24                   */
/* ************************************************************************* */

#define _BGP_IC_DDR0_RERR_HIER_POS    7
#define _BGP_IC_DDR0_RERR_UNIT_NUM    7
#define _BGP_IC_DDR0_RERR_UNIT_POS    24
#define _BGP_IC_DDR0_RERR_UNIT_SIZE   1
#define _BGP_IC_DDR0_RERR_UNIT_MASK   0x00000080

/* ************************************************************************* */
/* DDR 0 Special Attention:                 Group 7 bit 25                   */
/* ************************************************************************* */

#define _BGP_IC_DDR0_SATT_HIER_POS    7
#define _BGP_IC_DDR0_SATT_UNIT_NUM    7
#define _BGP_IC_DDR0_SATT_UNIT_POS    25
#define _BGP_IC_DDR0_SATT_UNIT_SIZE   1
#define _BGP_IC_DDR0_SATT_UNIT_MASK   0x00000040

/* ************************************************************************* */
/* DDR 0 Machine Check:                     Group 7 bit 26                   */
/* ************************************************************************* */

#define _BGP_IC_DDR0_MCHK_HIER_POS    7
#define _BGP_IC_DDR0_MCHK_UNIT_NUM    7
#define _BGP_IC_DDR0_MCHK_UNIT_POS    26
#define _BGP_IC_DDR0_MCHK_UNIT_SIZE   1
#define _BGP_IC_DDR0_MCHK_UNIT_MASK   0x00000020


/* ************************************************************************* */
/* DDR 1 Recoverable error:                 Group 7 bit 27                   */
/* ************************************************************************* */

#define _BGP_IC_DDR1_RERR_HIER_POS    7
#define _BGP_IC_DDR1_RERR_UNIT_NUM    7
#define _BGP_IC_DDR1_RERR_UNIT_POS    27
#define _BGP_IC_DDR1_RERR_UNIT_SIZE   1
#define _BGP_IC_DDR1_RERR_UNIT_MASK   0x00000010

/* ************************************************************************* */
/* DDR 1 Special Attention:                 Group 7 bit 28                   */
/* ************************************************************************* */

#define _BGP_IC_DDR1_SATT_HIER_POS    7
#define _BGP_IC_DDR1_SATT_UNIT_NUM    7
#define _BGP_IC_DDR1_SATT_UNIT_POS    28
#define _BGP_IC_DDR1_SATT_UNIT_SIZE   1
#define _BGP_IC_DDR1_SATT_UNIT_MASK   0x00000008

/* ************************************************************************* */
/* DDR 1 Machine Check:                     Group 7 bit 29                   */
/* ************************************************************************* */

#define _BGP_IC_DDR1_MCHK_HIER_POS    7
#define _BGP_IC_DDR1_MCHK_UNIT_NUM    7
#define _BGP_IC_DDR1_MCHK_UNIT_POS    29
#define _BGP_IC_DDR1_MCHK_UNIT_SIZE   1
#define _BGP_IC_DDR1_MCHK_UNIT_MASK   0x00000004


/* ************************************************************************* */
/* Test Interface interrupt request:        Group 7  bit 30:31               */
/* ************************************************************************* */

#define _BGP_IC_TESTINT_HIER_POS      7
#define _BGP_IC_TESTINT_UNIT_NUM      7
#define _BGP_IC_TESTINT_UNIT_POS      30
#define _BGP_IC_TESTINT_UNIT_SIZE     2
#define _BGP_IC_TESTINT_UNIT_MASK     0x00000003


/* ************************************************************************* */
/* Ethernet TOMAL interrupt request:        Group 8 bits 0:1                 */
/* ************************************************************************* */

#define _BGP_IC_TOMAL_HIER_POS        8
#define _BGP_IC_TOMAL_UNIT_NUM        8
#define _BGP_IC_TOMAL_UNIT_POS        0
#define _BGP_IC_TOMAL_UNIT_SIZE       2
#define _BGP_IC_TOMAL_UNIT_MASK       0xC0000000



/* ************************************************************************* */
/* Ethernet XEMAC interrupt request:         Group 9 bits 0                  */
/* ************************************************************************* */

#define _BGP_IC_XEMAC_HIER_POS        9
#define _BGP_IC_XEMAC_UNIT_NUM        9
#define _BGP_IC_XEMAC_UNIT_POS        0
#define _BGP_IC_XEMAC_UNIT_SIZE       1
#define _BGP_IC_XEMAC_UNIT_MASK       0x80000000

/* ************************************************************************* */
/* Ethernet interrupt request:              Group 9 bits 1                   */
/* ************************************************************************* */

#define _BGP_IC_ETH_HIER_POS          9
#define _BGP_IC_ETH_UNIT_NUM          9
#define _BGP_IC_ETH_UNIT_POS          1
#define _BGP_IC_ETH_UNIT_SIZE         1
#define _BGP_IC_ETH_UNIT_MASK         0x40000000

/* ************************************************************************* */
/* Ethernet XENPAK interrupt request:       Group 9 bits 2                   */
/* ************************************************************************* */

#define _BGP_IC_XENPAK_HIER_POS       9
#define _BGP_IC_XENPAK_UNIT_NUM       9
#define _BGP_IC_XENPAK_UNIT_POS       2
#define _BGP_IC_XENPAK_UNIT_SIZE      1
#define _BGP_IC_XENPAK_UNIT_MASK      0x20000000




#endif
