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
/*! \file DMA_InjFifo.c
 *
 * \brief Implementations for Functions Defined in bgp/arch/include/spi/DMA_InjFifo.h.
 *
 */

#undef  DEBUG_PRINT
/* #define DEBUG_PRINT 1 */

#ifndef __LINUX_KERNEL__

#include <common/bgp_personality_inlines.h>
#include <spi/bgp_SPI.h>
#include <stdio.h>
#include <errno.h>

#else

#include <spi/linux_kernel_spi.h>

#endif /* ! __LINUX_KERNEL__ */

/*!
 *
 * \brief Remote Get Fifo Full Handler Table
 *
 * An array of entries, one per injection fifo.  Each entry specifies the fifo
 * group structure and the handler function that will receive control to
 * handle a remote get fifo full condition for fifos in that fifo group.
 */
DMA_InjFifoRgetFifoFullHandlerEntry_t DMA_RgetFifoFullHandlerTable[DMA_NUM_INJ_FIFOS];


/*!
 * \brief Remote Get Fifo Full Init Has Been Done Indicator
 *
 *  0 means the initialization has not been done.
 *  1 means the initialization has been done.
 */
int DMA_InjFifoRgetFifoFullInitHasBeenDone = 0;


/*!
 * \brief Pointer to Barrier function Used By Remote Get Fifo Full Interrupt Handler
 */
static void (*DMA_RgetFifoFullHandlerBarrierFcn)(void *);
/*!
 * \brief Generic arg for Barrier function
 */
static void *DMA_RgetFifoFullHandlerBarrierArg;


/*!
 * \brief Remote Get Fifo Full Interrupt Handler
 *
 * This function receives control when a remote get fifo becomes full
 * It attempts to recover from the condition and restart the DMA.
 * It receives control in all cores (a broadcast interrupt).
 *
 * Upon entry, the DMA is assumed to have been stopped, both the iDMA
 * and the rDMA.  This has been done by the kernel's interrupt
 * handler that invoked this function.
 */
void DMA_InjFifoRgetFifoFullInterruptHandler(uint32_t arg1,
                                             uint32_t arg2,
                                             uint32_t arg3,
                                             uint32_t arg4)
{
  uint32_t global_fnum, freeSpaceInBytes;
  uint32_t core_num = Kernel_PhysicalProcessorID();

  /* If Init has not been done yet, ignore the interrupt.
   */
  if ( DMA_InjFifoRgetFifoFullInitHasBeenDone == 0 )
  {
    pthread_poof_np();  /*  Return from this interrupt. */
  }

  /*
   * Barrier across all cores.  This is needed to ensure that
   * 1. The DMA has been stopped (only the last core to see this interrupt
   *    stops the DMA).
   * 2. We don't exit from this handler until the core that needs to handle
   *    the rget fifo full condition has cleared the condition causing the
   *    interrupt, or else it will fire right away again.
   *
   * This barrier, while allocated by the main core of each process on the
   * compute node, has been modified during DMA SPI Setup to expect the
   * appropriate number of cores to participate.
   */

  DMA_RgetFifoFullHandlerBarrierFcn( DMA_RgetFifoFullHandlerBarrierArg );

  /*
   * For each injection fifo...
   *   For each entry of the RgetFifoFullHandlerTable that is managed
   *   by our core and has a registered rget fifo full handler,
   *   1. Determine whether this rget fifo is full (or nearly so)
   *   2. If full, call the registered handler to handle the condition.
   */
  for ( global_fnum=0; global_fnum<DMA_NUM_INJ_FIFOS; global_fnum++)
  {
    if ( ( DMA_RgetFifoFullHandlerTable[global_fnum].core_num == core_num ) &&
	 ( DMA_RgetFifoFullHandlerTable[global_fnum].handler ) )
    {
      /* The rget fifo is considered full (or nearly so) if there is
       * only enough freespace in the fifo to hold one descriptor or less.
       */
      freeSpaceInBytes =
	DMA_InjFifoGetFreeSpaceById (
			      DMA_RgetFifoFullHandlerTable[global_fnum].fg_ptr,
			      global_fnum & 0x1f,  /*  relative fifo number */
			      1,
			      1) << 4;
      if ( freeSpaceInBytes <= (DMA_MIN_INJECT_SIZE_IN_QUADS*16) +
	                        DMA_FIFO_DESCRIPTOR_SIZE_IN_BYTES )
      {
	/*
	 * Call the handler function to free up space in the fifo,
	 * if possible.
	 */

	(*(DMA_RgetFifoFullHandlerTable[global_fnum].handler))(
			DMA_RgetFifoFullHandlerTable[global_fnum].fg_ptr,
			global_fnum & 0x1F,
			DMA_RgetFifoFullHandlerTable[global_fnum].handler_parm);
      }
    }
  }

  /*
   * Barrier.  Wait here until all cores reach this point in the interrupt
   * handler.
   */

  DMA_RgetFifoFullHandlerBarrierFcn( DMA_RgetFifoFullHandlerBarrierArg );

  /*
   * Exit from the interrupt.
   */
  pthread_poof_np();
}

/*!
 * \brief Remote Get Fifo Full Initialization
 *
 * Initialize data structures and interrupt handlers to handle a remote get
 * fifo full condition.
 */
void DMA_InjFifoRgetFifoFullInit( Kernel_InterruptGroup_t  rget_interruptGroup,
                                  void                   (*rget_barrier)(void *) ,
                                  void                    *rget_barrier_arg
                                )
{
   int i;

   /*
    * Clear the handler table.
    */
   for ( i=0; i<DMA_NUM_INJ_FIFOS; i++ )
   {
     DMA_RgetFifoFullHandlerTable[i].fg_ptr       = NULL;
     DMA_RgetFifoFullHandlerTable[i].handler      = NULL;
     DMA_RgetFifoFullHandlerTable[i].handler_parm = NULL;
     DMA_RgetFifoFullHandlerTable[i].core_num     = 0;
   }

   /*
    * Clear the lockbox counter associated with this interrupt.
    * The lockbox keeps track of which cores have entered and exited
    * the kernel's interrupt handler.
    */
   LockBox_FetchAndClear( rget_interruptGroup );

   DMA_RgetFifoFullHandlerBarrierFcn = rget_barrier;
   DMA_RgetFifoFullHandlerBarrierArg = rget_barrier_arg;

   /*
    * Register the interrupt handler to handle the remote get
    * fifo full condition.
    */
   Kernel_SetCommThreadConfig(Kernel_MkInterruptID(_BGP_IC_DMA_NFT_G3_HIER_POS, 24),
                              COMMTHRD_OPCODE_BCAST             |
                              COMMTHRD_OPCODE_CALLFUNC,
                              rget_interruptGroup,
			      DMA_InjFifoRgetFifoFullInterruptHandler,
			      0, 0, 0, 0);
}

