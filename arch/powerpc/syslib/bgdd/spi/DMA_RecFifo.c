/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2006,2010
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
/*! \file DMA_RecFifo.c
 *
 * \brief Implementations for Functions Defined in bgp/arch/include/spi/DMA_RecFifo.h.
 *
 */
#include <linux/version.h>
#include <linux/module.h>
#include <asm/bitops.h>

#undef  DEBUG_PRINT
/* #define DEBUG_PRINT 1 */

#ifndef __LINUX_KERNEL__

#include <spi/DMA_RecFifo.h>
#include <stdio.h>
#include <bpcore/ppc450_inlines.h>
#include <bpcore/ic_memmap.h>
#include <common/bgp_bitnumbers.h>
#include <errno.h>

#else

#include <spi/linux_kernel_spi.h>
/* Interrupt encoding for Blue Gene/P hardware).
 * Given a BIC group and bit index within the group,
 * bic_hw_to_irq(group, gint) returns the Linux IRQ number.
 * ( really from asm/bluegene.h but we get mismatches if we include it)
 */

#endif /* ! __LINUX_KERNEL__ */

#include <linux/dma-mapping.h>

#define TRACE(x) printk x


#if defined(BGP_DD1_WORKAROUNDS)

/*!
 * \brief Number of times the poll functions have been called and returned
 *        no packets processed.
 *
 * Special Value:  -1 means that the Kernel_ClearFullReceptionFifo() syscall
 *                 has been invoked, but no packets have been processed
 *                 since.  This tells the poll function that even if it
 *                 does not process any packets, it should not increment
 *                 this counter and ultimately issue the syscall again, because
 *                 there is no need.
 */
int NumEmptyPollFunctionCalls = -1;

/*!
 * \brief Limit for NumEmptyPollFunctionCalls
 */
const int NUM_EMPTY_POLL_FUNCTION_CALL_LIMIT = 10;

#endif

#if defined(CONFIG_BGP_STATISTICS)
int reception_fifo_histogram[33] ;
unsigned int reception_hi_watermark ;
#endif
static inline int get_tlb_pageid(int tlbindex)
  {
    int rc ;
     /*  PPC44x_TLB_PAGEID is 0 */
    asm volatile( "tlbre  %[rc],%[index],0"
                    : [rc] "=r" (rc)
                    : [index] "r" (tlbindex)
                    ) ;
    return rc ;
 }

static inline int get_tlb_xlat(int tlbindex)
  {
    int rc ;
     /*  PPC44x_TLB_XLAT is 1 */
    asm volatile( "tlbre  %[rc],%[index],1"
                    : [rc] "=r" (rc)
                    : [index] "r" (tlbindex)
                    ) ;
    return rc ;
 }

static inline int get_tlb_attrib(int tlbindex)
  {
    int rc ;
     /*  PPC44x_TLB_ATTRIB is 2 */
    asm volatile( "tlbre  %[rc],%[index],2"
                    : [rc] "=r" (rc)
                    : [index] "r" (tlbindex)
                    ) ;
    return rc ;
 }

static inline int search_tlb(unsigned int vaddr)
  {
    int rc ;
     /*  PPC44x_TLB_ATTRIB is 2 */
    asm volatile( "tlbsx  %[rc],0,%[vaddr]"
                    : [rc] "=r" (rc)
                    : [vaddr] "r" (vaddr)
                    ) ;
    return rc ;
 }

static void show_tlbs(unsigned int mioaddr) __attribute__((unused)) ;
static void show_tlbs(unsigned int mioaddr)
{
  int i ;
  int tlb_index = search_tlb(mioaddr) ;
  for(i=0;i<64;i+=1)
    {
      int pageid=get_tlb_pageid(i) ;
      int xlat=get_tlb_xlat(i) ;
      int attrib=get_tlb_attrib(i) ;
      if( pageid & 0x00000200)
        {
          printk(KERN_INFO "tlb[%02d]=[%08x %08x %08x]\n",i,pageid,xlat,attrib) ;
        }
    }
  printk(KERN_INFO "mioaddr=0x%08x tlb_index=%d\n", mioaddr,tlb_index) ;
}

/* char temp_packet[256] __attribute__ ((aligned ( 16))) ; */

/*!
 * \brief DMA Reception Fifo Shared Memory Structure
 *
 * This structure must be shared among the processors in a compute node.  It
 * contains info that must be maintained and shared for the duration of a job.
 * This storage is static, maintained across function calls.
 * In sharedmemory mode, core 0 maintains this info.
 * In virtual node mode, each core maintains its own info.
 *
 */
typedef struct DMA_RecFifoSharedMemory_t
{
  DMA_RecFifoRecvFunction_t recvFunctions[256]; /*!< The registered "normal"
                                         reception fifo receive functions.
                                         Filled in by calls to
                                         DMA_RecFifoRegisterRecvFunction().   */

  void *recvFunctionsParms[256];    /*!< recvFunctionsParms[i] is the
                                         parameter to pass to
                                         recvFunctions[i].
                                         Filled in by calls to
                                         DMA_RecFifoRegisterRecvFunction().   */

  DMA_RecFifoRecvFunction_t headerRecvFunction; /*!< The registered "header"
                                         reception fifo receive function.
                                         Filled in by a call to
                                         DMA_RecFifoRegisterRecvFunction().   */

  void *headerRecvFunctionParm;     /*!< The parameter to pass to
                                         headerRecvFunction.
                                         Filled in by a call to
                                         DMA_RecFifoRegisterRecvFunction().   */

  DMA_RecFifoRecvFunction_t errorRecvFunction; /*!< The registered "error"
                                         reception fifo receive function.
                                         Defaulted to
                                         &DMA_RecFifoDefaultErrorRecvFunction.
                                         Filled in by a call to
                                         DMA_RecFifoRegisterRecvFunction().   */

  void *errorRecvFunctionParm;      /*!< The parameter to pass to
                                         errorRecvFunction.
                                         Filled in by a call to
                                         DMA_RecFifoRegisterRecvFunction().   */

  DMA_RecFifoGroup_t groups[DMA_NUM_REC_FIFO_GROUPS]; /*!< Reception fifo
                                         group structures, one for each group.
                                         groups[i] is the group shared by all
                                         users of reception fifo group i.     */

  unsigned int groupsInitialized[DMA_NUM_REC_FIFO_GROUPS]; /*!< Indicator of
                                         groups[i] having been initialized.
                                         0 = not initialized by a call to
                                             DMA_RecFifoGetFifoGroup() for
                                             group i.
                                         1 = initialized.                     */

} DMA_RecFifoSharedMemory_t;


/*!
 * \brief Storage for the Reception Fifo Shared Memory Structure
 *
 * This storage is static, maintained across function calls.
 * In sharedmemory mode, core 0 maintains reception fifo info.
 * In virtual node mode, each core maintains its own reception fifo info.
 */
static DMA_RecFifoSharedMemory_t DMA_RecFifoInfo;


/*!
 * \brief DMA Packet I/O Vector Structure
 *
 * This structure describes the payload of a memory fifo packet.
 * Because of fifo wrapping, the payload may consist of 0, 1, or 2 segments:
 * - 0 segments:   this is a packet in the header-only, debug fifo.
 * - 1 segment:    the packet does not wrap the fifo.
 * - 2 segments:   the packet does wrap the fifo.
 *
 */
typedef struct DMA_PacketIovec_t
{
  int   num_segments;    /*!< Number of segments in the payload               */
  void *payload_ptr[2] ; /*!< Pointer to the payloads in each segment (NULL
                              if not used).                                   */
  int   num_bytes[2];    /*!< Number of payload bytes in each segment (0 if
                              not used).                                      */
}
ALIGN_L1D_CACHE DMA_PacketIovec_t;


static void dumpmem(const void *address, unsigned int length, const char * label)
  {
    int x ;
    printk(KERN_INFO "(>)[%s:%d] Memory dump: %s\n",__func__, __LINE__,label) ;
    for (x=0;x<length;x+=32)
      {
        int *v = (int *)(address+x) ;
        printk(KERN_INFO "%p: %08x %08x %08x %08x %08x %08x %08x %08x\n",
            v,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]
            ) ;
      }
    printk(KERN_INFO "(<)[%s:%d] Memory dump\n",__func__, __LINE__) ;
  }



/*!
 * \brief Get DMA Reception Fifo Group
 *
 * This is a wrapper around a System Call. This function returns THE
 * one-and-only pointer to the fifo group structure, with the entries all
 * filled in from info in the DCRs.  If called multiple times with the same
 * group, it will always return the same pointer, and the system call will
 * not be invoked again.
 *
 * It must be called AFTER DMA_RecFifoSetMap().
 *
 * By convention, the same "target" is used for normal and header fifo
 * interrupts (could be changed).  In addition, by convention, interrupts for
 * fifos in group g come out of the DMA as non-fatal irq bit 28+g,
 * ie, only fifos in group g can cause the "type g" threshold interrupts.
 *
 * \param[in]  grp      The group number (0 through DMA_NUM_REC_FIFO_GROUPS).
 * \param[in]  target   The core that will receive the interrupt when a
 *                      fifo in this group reaches its threshold
 *                      (0 to DMA_NUM_REC_FIFO_GROUPS-1).
 *                      Ignored on subsequent call with the same group.
 * \param[in]  normal_handler  A pointer to the function to receive control in
 *                             the I/O thread to handle the interrupt when a
 *                             normal fifo in this group reaches its threshold.
 *                             This function must be coded to take 4 uint32_t
 *                             parameters:
 *                             - A pointer to storage specific to this
 *                               handler.  This is the normal_handler_parm
 *                               specified on this function call.
 *                             - 3 uint32_t parameters that are not used.
 *                             If normal_handler is NULL, threshold interrupts
 *                             are not delivered for normal fifos in this group.
 *                             Ignored on subsequent call with the same group.
 * \param[in]  normal_handler_parm   A pointer to storage that should be passed
 *                                   to the normal interrupt handling function
 *                                   (see normal_handler parameter).
 *                                   Ignored on subsequent call with the same
 *                                   group.
 * \param[in]  header_handler  ** This parameter is deprecated.  Specify NULL.**
 *                             A pointer to the function to receive control in
 *                             the I/O thread to handle the interrupt when a
 *                             header fifo in this group reaches its threshold.
 *                             This function must be coded to take 2 parameters:
 *                               void* A pointer to storage specific to this
 *                                     handler.  This is the header_handler_parm
 *                                     specified on this function call.
 *                               int   The global fifo ID of the fifo that hit
 *                                     its threshold (0 through
 *                                     NUM_DMA_REC_FIFOS-1).
 *                             If header_handler is NULL, threshold interrupts
 *                             are not delivered for header fifos in this group.
 *                             Ignored on subsequent call with the same group.
 * \param[in]  header_handler_parm   ** This parameter is deprecated.  Specify
 *                                      NULL. **
 *                                   A pointer to storage that should be passed
 *                                   to the header interrupt handling function
 *                                   (see header_handler parameter).
 *                                   Ignored on subsequent call with the same
 *                                   group.
 * \param[in]  interruptGroup  A InterruptGroup_t that identifies the
 *                             group of interrupts that the fifos in this group
 *                             will become part of.
 *                             Ignored on subsequent call with the same group.
 *
 * \return  RecFifoGroupStruct  Pointer to a DMA Reception Fifo Group structure
 *                              that reflects the fifos that are being used in
 *                              this group.  This same structure is shared by
 *                              all users of this reception fifo group.
 *                              NULL is returned if an error occurs.
 *
 * \note  The following comments from Phil about the internals of the syscall:
 *   - error checks
 *     - 0 <= group_id < 4
 *     - the start of the fifo group is a valid virtual address (tlb mapped)?
 *   - disable the rDMA
 *   - call _BGP_rDMA_Fifo_Get_Map to get the DCR mapping information
 *   - loop through the map to determine how many and which fifos in this group
 *     are used, including headers
 *   - filling in the addresses of used fifos
 *     - In particular, any pointer to any fifo in the group that is not used
 *       will have a null pointer
 *   - furthermore,
 *     - write starting values to all used fifos
 *     - make sure all interrupts are cleared
 *     - enable rDMA
 *
 */
DMA_RecFifoGroup_t *
DMA_RecFifoGetFifoGroup(
			int                               grp,
			int                               target,
			Kernel_CommThreadHandler          normal_handler,
			void                             *normal_handler_parm,
			Kernel_CommThreadHandler          header_handler,
			void                             *header_handler_parm,
			Kernel_InterruptGroup_t           interruptGroup
		       )
{
  int rc;

  TRACE((
		  KERN_INFO "(>) DMA_RecFifoGetFifoGroup\n"));

  SPI_assert( (0 <= grp   ) && (grp    < DMA_NUM_REC_FIFO_GROUPS ) );
  SPI_assert( (0 <= target) && (target < DMA_NUM_REC_FIFO_GROUPS ) );

  if ( DMA_RecFifoInfo.groupsInitialized[grp] == 0 ) /* Is                    */
                                             /* DMA_RecFifoGroups[grp] not    */
                                             /* filled-in yet?                */
    {
      /*
       * If an interrupt handler has been specified, invoke the system call
       * to configure the kernel to invoke the handler when the reception
       * fifo threshold crossed interrupt fires.
       */

      if (normal_handler)
      {
          {
	/*
	 * Calculate the IRQ to be one of
	 * - 28: rec fifo type 0 crossed threshold
	 * - 29: rec fifo type 0 crossed threshold
	 * - 30: rec fifo type 0 crossed threshold
	 * - 31: rec fifo type 0 crossed threshold
	 * based on the DMA group number.
	 */
	unsigned irqInGroup = 28 + grp;
/*  tjcw ???? not sure what gets the right interrupts ... */
/*  28+ gives something to do with memory tranfers. */
/*  we want 8+, which is related to FIFO fullness */
/*   unsigned irqInGroup = 8 + grp; */

	/*
	 * Calculate an interrupt ID, which is the BIC interrupt group (2)
	 * combined with the IRQ number.
	 */
/* 	int interruptID = Kernel_MkInterruptID(_BGP_IC_DMA_NFT_G2_HIER_POS, */
/* 					       irqInGroup); */
	int interruptID = bic_hw_to_irq(_BGP_IC_DMA_NFT_G2_HIER_POS,irqInGroup);

	/*
	 * Calculate the opcode indicating
	 * - the target core for interrupt
	 * - to call the specified function when the interrupt fires
	 * - to disable interrupts before calling the specified function
	 * - to enable interrupts after callling the specified function
	 */
	int opcode = ( COMMTHRD_OPCODE_CORE0 + target ) |
	               COMMTHRD_OPCODE_CALLFUNC |
	               COMMTHRD_OPCODE_DISABLEINTONENTRY |
	               COMMTHRD_OPCODE_ENABLEINTONPOOF  ;

	/*
	 * Configure this interrupt with the kernel.
	 */
	  TRACE((
			  KERN_INFO "(=) DMA_RecFifoGetFifoGroup interruptID=%d\n",interruptID));
	rc = Kernel_SetCommThreadConfig(interruptID,
					opcode,
					(uint32_t*)interruptGroup,
					normal_handler,
					(uint32_t)normal_handler_parm,
					(uint32_t)NULL,
					(uint32_t)NULL,
					(uint32_t)NULL);
	if (rc) return NULL;
          }

      /*
       * Proceed to get the reception fifo group
       */
      rc = Kernel_RecFifoGetFifoGroup( (uint32_t*)&(DMA_RecFifoInfo.groups[grp]),
				       grp,
				       target,
				       (uint32_t) NULL, /* Normal handler.       Not used */
                                       (uint32_t) NULL, /* Normal handler parm.  Not used */
                                       (uint32_t) NULL, /* Header handler.       Not used */
                                       (uint32_t) NULL, /* Header handler parm.  Not used */
                                       (uint32_t) NULL  /* InterruptGroup.       Not used */
                                     );
      if ( rc == 0 ) /* Success? */
	{
	  DMA_RecFifoInfo.groupsInitialized[grp] = 1; /* Remember success.    */
	}
      else
	{
	  return NULL; /* Failure */
	}
    }
    }
  TRACE((
		  KERN_INFO "(<) DMA_RecFifoGetFifoGroup\n"));

  return &(DMA_RecFifoInfo.groups[grp]);  /* Return the pointer.              */

}


/*!
 * \brief Register a Reception Fifo Receive Function
 *
 * Register a specified receive function to handle packets having a specific
 * "registration ID".  It returns a registration ID (0-255) that is to be used
 * in the packet header Func_Id field, such that packets that arrive in a
 * reception fifo will result in the corresponding receive function being called
 * when that fifo is processed by a polling or interrupt handler function.
 *
 * \param[in]  recv_func          Pointer to the receive function.
 * \param[in]  recv_func_parm     Arbitrary pointer to be passed to the
 *                                recv_func when it is called.
 * \param[in]  is_error_function  1 means this is the receiver function
 *                                to be called if a packet contains an invalid
 *                                (unregistered) registration ID.  The return
 *                                value from this function is zero, indicating
 *                                success, not indicating a registration ID.
 *                                A default function is provided if one is not
 *                                registered.  If there is already a non-default
 *                                error receive function registered, -EBUSY is
 *                                returned.
 *                                0 means this is not the error receiver
 *                                function.
 * \param[in]  is_header_fifo     Indicates whether the fifo is normal or
 *                                header:
 *                                - 0 is normal.  The return code is the
 *                                  registration ID.
 *                                - 1 is header.  The return code is 0,
 *                                  indicating success, because packets in
 *                                  header fifos are direct-put packets, and
 *                                  hence have no registration ID.
 *                                If there is already a header receive function
 *                                registered, -EBUSY is returned.
 *
 * If both is_error_function and is_header_fifo are 1, -EINVAL is returned.
 *
 * \retval   0            This is a registration ID if is_error_function=0 and
 *                        is_header_fifo=0.  Otherwise, it indicates success.
 *           1-255        This is a registration ID.  Successful.
 *           negative     Failure.  This is a negative errno value.
 *
 * \see DMA_RecFifoDeRegisterRecvFunction
 */
static int DMA_RecFifoRegisterRecvFunction_next_free_ID = 0;
int DMA_RecFifoRegisterRecvFunction(
			        DMA_RecFifoRecvFunction_t  recv_func,
				void                      *recv_func_parm,
				int                        is_error_function,
				int                        is_header_fifo
				)
{
  int next_free_ID = DMA_RecFifoRegisterRecvFunction_next_free_ID;
  int i;

  /* Perform error checks */
  if ( ( recv_func == NULL ) ||
       ( ( is_error_function != 0 ) &&
	 ( is_error_function != 1 ) ) ||
       ( ( is_header_fifo    != 0 ) &&
	 ( is_header_fifo    != 1 ) ) ||
       ( ( is_header_fifo == 1 ) && ( is_error_function == 1 ) ) )
  {
    return -EINVAL;
  }

  /*
   * Handle a "header" receive function.
   */
  if ( is_header_fifo == 1 )
    {
      if ( DMA_RecFifoInfo.headerRecvFunction != NULL ) /* Already registered?*/
	{
	  return -EBUSY;
	}
      DMA_RecFifoInfo.headerRecvFunction     = recv_func;
      DMA_RecFifoInfo.headerRecvFunctionParm = recv_func_parm;
      return 0; /* Indicate success */
    }

  /*
   * Handle a "error" receive function.
   */
  if ( is_error_function == 1 )
    {
      if ( DMA_RecFifoInfo.errorRecvFunction !=
	     &DMA_RecFifoDefaultErrorRecvFunction ) /* Already registered? */
	{
	  return -EBUSY;
	}
      DMA_RecFifoInfo.errorRecvFunction     = recv_func;
      DMA_RecFifoInfo.errorRecvFunctionParm = recv_func_parm;
      return 0; /* Indicate success */
    }

  /*
   * Handle a "normal" receive function.
   */

  for (i=next_free_ID; i < 256; i++) /* Search for an empty slot */
    {
      if ( DMA_RecFifoInfo.recvFunctions[i] == NULL ) /* Found a slot? */
	{
	  DMA_RecFifoInfo.recvFunctions[i]      = recv_func;
	  DMA_RecFifoInfo.recvFunctionsParms[i] = recv_func_parm;
	  next_free_ID = i;
	  return i; /* Return the registration ID */
	}
    }
  DMA_RecFifoRegisterRecvFunction_next_free_ID = next_free_ID;

  return -EBUSY; /* No open slots */

}


/*!
 * \brief De-Register a Reception Fifo Receive Function
 *
 * De-register a previously registered receive function.
 *
 * \param[in]  registrationId     Registration Id returned from
 *                                DMA_RecFifoRegisterRecvFunction (0..255).
 *                                A negative value means that no
 *                                registration id is specified.
 * \param[in]  is_error_function  1 means the error receive function is
 *                                to be de-registered.
 *                                0 otherwise.
 * \param[in]  is_header_fifo     1 means the header fifo receive function is
 *                                to be de-registered.
 *                                0 otherwise.
 *
 * \retval   0            Success
 *           negative     Error value
 *
 * \see DMA_RecFifoRegisterRecvFunction
 */
int DMA_RecFifoDeRegisterRecvFunction(
				      int registrationId,
				      int is_error_function,
				      int is_header_fifo
				     )
{
  /* Perform error checks */
  if ( ( registrationId > 255 ) ||
       ( ( is_error_function != 0 ) &&
	 ( is_error_function != 1 ) ) ||
       ( ( is_header_fifo    != 0 ) &&
	 ( is_header_fifo    != 1 ) ) )
  {
    return -EINVAL;
  }

  /*
   * Handle a "header" receive function.
   */
  if ( is_header_fifo == 1 )
  {
    DMA_RecFifoInfo.headerRecvFunction     = NULL;
    DMA_RecFifoInfo.headerRecvFunctionParm = NULL;
  }

  /*
   * Handle a "error" receive function.
   */
  if ( is_error_function == 1 )
  {
    DMA_RecFifoInfo.errorRecvFunction     = NULL;
    DMA_RecFifoInfo.errorRecvFunctionParm = NULL;
  }

  /*
   * Handle a "normal" receive function.
   */

  if ( registrationId >= 0 )
  {
    DMA_RecFifoInfo.recvFunctions[registrationId]      = NULL;
    DMA_RecFifoInfo.recvFunctionsParms[registrationId] = NULL;
    DMA_RecFifoRegisterRecvFunction_next_free_ID = 0; /* Start at beginning next time */
  }

  return 0;

}


/*!
 * \brief DMA Reception Fifo Default Error Receive Function
 *
 * This is the default function that will handle packets having an
 * unregistered registration ID.
 *
 * \param[in]  f_ptr           Pointer to the reception fifo.
 * \param[in]  packet_ptr      Pointer to the packet header (== va_head).
 *                             This is quad-aligned for optimized copying.
 * \param[in]  recv_func_parm  Pointer to storage specific to this receive
 *                             function.  This pointer was specified when the
 *                             receive function was registered with the kernel,
 *                             and is passed to the receive function
 *                             unchanged.
 * \param[in]  payload_ptr     Pointer to the beginning of the payload.
 *                             This is quad-aligned for optimized copying.
 * \param[in]  payload_bytes   Number of bytes in the payload
 *
 * \retval  -1  An unregistered packet was just processed.  This is considered
 *              an error.
 */
int  DMA_RecFifoDefaultErrorRecvFunction(
					 DMA_RecFifo_t      *f_ptr,
					 DMA_PacketHeader_t *packet_ptr,
					 void               *recv_func_parm,
					 char               *payload_ptr,
					 int                 payload_bytes
					)
{
  int i;

  printf ( "\nUnregistered Packet Received in Reception Fifo %d\n",
	   f_ptr->global_fifo_id);

  printf ( "Packet Header:\n");
  printf ( "%08x%08x%08x%08x\n",*((int*)&packet_ptr[0]),
	                        *((int*)&packet_ptr[4]),
	                        *((int*)&packet_ptr[8]),
	                        *((int*)&packet_ptr[12]));
  printf ( "Packet Payload:\n");

  for (i=0; i<payload_bytes; i+=16);
    {
      printf ( "%08x%08x%08x%08x\n",*((int*)&payload_ptr[i]),
	                            *((int*)&payload_ptr[i+4]),
                                    *((int*)&payload_ptr[i+8]),
	                            *((int*)&payload_ptr[i+12]));
    }

  SPI_assert(0);

  return -1;
}


/*!
 * \brief DMA Reception Fifo Get Addresses
 *
 * Analyze the packet at the head of the reception fifo and return a
 * DMA_PacketIovec_t describing the payload of the packet.  In particular,
 * determine if the packet is contiguous in the fifo, or whether it wraps
 * around to the start of the fifo.
 *
 * \param[in]      f_ptr   Pointer to the reception fifo structure.
 * \param[in,out]  io_vec  Pointer to the packet I/O vector structure to
 *                         be filled in.
 *
 * \return  The io_vec structure has been filled-in.
 *
 * \pre  The caller has determined that the fifo has a packet in it (it
 *       is not empty).
 *
 * \note
 * - For non-header packets, only non-DMA packets (memory fifo packets)
 *   are in the fifo and need to be handled.
 */
void DMA_RecFifoGetAddresses(
			     DMA_RecFifo_t     *f_ptr,
			     DMA_PacketIovec_t *io_vec
			    )
{
  DMA_PacketHeader_t *packet_ptr;
  unsigned int        payload_bytes;
  unsigned int        payload_bytes_to_end_of_fifo = 0;

  SPI_assert( f_ptr  != NULL );
  SPI_assert( io_vec != NULL );

      if ( f_ptr->global_fifo_id < DMA_NUM_NORMAL_REC_FIFOS )  /* Is this a   */
	                                                       /* normal fifo?*/
	{ /* Yes.  Process a normal packet */
	  packet_ptr = (DMA_PacketHeader_t*)f_ptr->dma_fifo.va_head; /* Point */
    	                                                   /*  to the packet. */

	  payload_bytes = ( (packet_ptr->Chunks + 1) << 5 ) -
	    sizeof(DMA_PacketHeader_t);           /* Calculate payload bytes. */

	  io_vec->payload_ptr[0] =
	    (char*)packet_ptr +
	      sizeof(DMA_PacketHeader_t);         /* Set first payload ptr    */

	  /* Determine if the payload is contiguous in the fifo, and set up   */
	  /* the iovec accordingly.                                           */
	  if ( ( payload_bytes <= 16 ) || /* A 32-byte packet will always be  */
               	                          /* contiguous...this is an          */
                                          /* optimization to avoid the next   */
                                          /* set of calculations.             */
	       ( payload_bytes <=         /* Calculate how much space to the  */
		 ( payload_bytes_to_end_of_fifo = /* end of the fifo.         */
		   ( (unsigned)f_ptr->dma_fifo.va_end - /* Check if entire    */
		     (unsigned)io_vec->payload_ptr[0] ) ) ) ) /* payload fits.*/
	    {
	      /* Set up io_vec for contiguous payload                         */
	      io_vec->num_segments   = 1;  /* Indicate contiguous payload.    */
	      io_vec->num_bytes[0]   = payload_bytes;
	      io_vec->payload_ptr[1] = NULL;
	      io_vec->num_bytes[1]   = 0;
	      return;
	    }
	  else
	    { /* Set up io_vec for non-contiguous payload.                    */

	      io_vec->num_segments   = 2; /* Indicate split payload.          */
	      io_vec->num_bytes[0]   = payload_bytes_to_end_of_fifo;
	      io_vec->payload_ptr[1] = f_ptr->dma_fifo.va_start;
	      io_vec->num_bytes[1]   = payload_bytes -
		                         payload_bytes_to_end_of_fifo;
	      return;
	    }
	} /* End: Non-header packet */

      else /* Header packet. */

	{ /* Header packet */
	  io_vec->num_segments   = 0;    /* Indicate header fifo.             */
	  io_vec->payload_ptr[0] = NULL; /* Everything else is NULL or zero.  */
	  io_vec->payload_ptr[1] = NULL;
	  io_vec->num_bytes[0]   = 0;
	  io_vec->num_bytes[1]   = 0;
	  return;
	}

} /* End: DMA_RecFifoGetAddresses() */


/*!
 * \brief Get Index of Next Reception Fifo in Group
 *
 * A reception fifo group contains up to DMA_NUM_REC_FIFOS_PER_GROUP.
 * It contains an array of fifos.  Up to fg_ptr->num_normal_fifos normal
 * fifos are in the first array slots.  Up to 1 header fifo is in the
 * last array slot.
 *
 * This function returns the array index of the next normal fifo in the group
 * that is being used, based upon the desired fifo_index and the not-empty
 * status.
 *
 * If *not_empty_status is -1, the status is fetched from the DMA SRAM (first
 * time condition).
 *
 * If the DMA SRAM not-empty status for this group is all zero (all fifos are
 * empty), the status is checked num_empty_passes times with a slight delay
 * in between to give the DMA time to make progress before returning a -1,
 * indicating that there is nothing more to process.
 *
 * \param[in]  fg_ptr              Pointer to the fifo group
 * \param[in]  desired_fifo_index  Index of the fifo that is desired to be
 *                                 processed.
 * \param[in,out]  fifo_bit        Pointer to the bit in the not_empty_status
 *                                 that corresponds to the desired_fifo_index
 *                                 (on input) and the returned next_fifo_index
 *                                 (on output).
 * \param[in]  num_empty_passes    When the not-empty status indicates that all
 *                                 fifos in the group are emtpy, this is the
 *                                 number of times the not-empty status is
 *                                 re-fetched and re-checked before officially
 *                                 declaring that they are indeed empty
 *                                 (0 means no extra passes are made).
 * \param[in]  not_empty_poll_delay  The number of pclks to delay between polls
 *                                   of the not-empty status when the fifos are
 *                                   empty.
 * \param[in,out]  not_empty_status  Pointer to the location to shadow the
 *                                   not empty status.
 *
 * \retval  next_fifo_index  Index of the next fifo in the group to be
 *                           processed.
 * \retval  -1               Indicates that the normal fifos in the group are
 *                           all empty.
 *
 * \post The va_tail of the fifo that is returned has been refreshed from
 *       the DMA hardware.
 *
 */
__INLINE__ int DMA_RecFifoGetNextFifo(
				      DMA_RecFifoGroup_t *fg_ptr,
				      int                 desired_fifo_index,
				      unsigned int       *fifo_bit,
				      int                 num_empty_passes,
				      int                 not_empty_poll_delay,
				      unsigned int       *not_empty_status
				     )
{
  unsigned int status     = *not_empty_status; /* Make a local copy */
  unsigned int status_bit = *fifo_bit;
  int          fifo_index = desired_fifo_index;

  /*
   * If *not_empty_status is 0, either the status has not been fetched yet
   * (first-time condition), or all fifos were emptied.  Go fetch the
   * not-empty status again.
   */
  if ( status ==  0 )
    {
      status = DMA_RecFifoGetNotEmpty( fg_ptr,
				       0 );   /* Get Normal fifo   */
                                              /* not-empty status. */
      *not_empty_status = status; /* Return the status to the caller */

#ifdef DEBUG_PRINT
      printf("New notEmptyStatus1=0x%08x\n",*not_empty_status);
#endif
    }

  /*
   * If the DMA SRAM not-empty status for this group is all zero (all fifos are
   * empty), the status is checked num_empty_passes times with a slight delay
   * in between to give the DMA time to make progress before returning a -1,
   * indicating that there is nothing more to process.
   */
  while ( ( status == 0 ) &&
	  ( num_empty_passes-- > 0 ) )
    {
      /* Delay, allowing the DMA to update its status */
      unsigned int pclks = not_empty_poll_delay;
      while( pclks-- )
	{
	  asm volatile("nop;");
	}

      /* Re-fetch the not-empty status */
      status = DMA_RecFifoGetNotEmpty( fg_ptr,
				       0 );   /* Get Normal fifo   */
                                              /* not-empty status. */
      *not_empty_status = status; /* Return the status to the caller */

#ifdef DEBUG_PRINT
      printf("New notEmptyStatus2=0x%08x\n",*not_empty_status);
#endif
    }

  if ( status == 0 ) return (-1);  /* Can't find any not empty     */

  /*
   * We have some fifos that are not empty.
   * Determine the fifo_index to be returned.
   * Loop until we hit a non-empty fifo.
   */
#ifdef DEBUG_PRINT
  printf("Checking status1 = 0x%08x for fifo_index %d, bit 0x%08x\n", status, fifo_index, status_bit);
#endif

  while ( ( status & status_bit ) == 0 )
    {
      fifo_index++;                     /* Try next fifo.                     */
      if ( fifo_index >= fg_ptr->num_normal_fifos ) /* Wrap?                  */
	fifo_index = 0;                 /* Start over with zero.              */

      status_bit = _BN(fg_ptr->fifos[fifo_index].global_fifo_id); /* Map to   */
     		                        /* proper not-empty bit.              */

#ifdef DEBUG_PRINT
      printf("Checking status2 = 0x%08x for fifo_index %d, bit 0x%08x\n", status, fifo_index, status_bit);
#endif
    }

  /* Refresh the tail because the DMA may have moved it */
  DMA_RecFifoGetTailById( fg_ptr,
			  fifo_index );

  *fifo_bit = status_bit;               /* Return the fifo index and its bit  */

#ifdef DEBUG_PRINT
  printf("Returning fifo_index=%d, status bit 0x%08x\n",fifo_index,status_bit);
#endif

  return (fifo_index);

} /* End: DMA_RecFifoGetNextFifo() */


/*!
 * \brief Poll Normal Reception Fifos
 *
 * Poll the "normal" reception fifos in the specified fifo group, removing one
 * packet after another from the fifos, dispatching the appropriate receive
 * function for each packet, until one of the following occurs:
 * 1.  Total_packets packets are received
 * 2.  All the fifos are empty
 * 3.  A receive function returns a non-zero value
 * 4.  The last packet removed from a fifo has an invalid registration id.  The
 *     error receive function will have been called, but polling ends.
 *     The invalid packet is counted as a processed packet, and the return
 *     code from the error receive function is returned.
 *
 * Polling occurs in a round-robin fashion through the array of normal fifos in
 * the group, beginning with array index starting_index. If a fifo has a packet,
 * the appropriate receive function is called.  Upon return, the packet is
 * removed from the fifo (the fifo head is moved past the packet).
 *
 * After processing packets_per_fifo packets in a fifo (or emptying that fifo),
 * the next fifo in the group is processed.  When the last index in the fifo
 * array is processed, processing continues with the first fifo in the array.
 * Multiple loops through the array of fifos in the group may occur.
 *
 * The receive functions must be registered through the
 * DMA_RecFifoRegisterRecvFunction interface.  The receive function is
 * called with a pointer to the packet header, pointer to the payload, and
 * length of the payload.  The packet header is always be 16 bytes of
 * contiguous storage, in the fifo.  Because the fifo is a circular buffer,
 * the payload of a packet may wrap from the end of the fifo to the beginning.
 * For large fifos, this happens infrequently.  To make it easier for
 * user/messaging code, the poll function will always return a starting payload
 * address and number of bytes so that the receive function can treat the packet
 * as contiguous storage in memory.  If the packet does not wrap, the starting
 * payload address will be a pointer to the appropriate address in the fifo.
 * If the packet does wrap, the poll function will copy bytes from the fifo to
 * a contiguous buffer (on the stack) and call the receive function with a
 * payload pointer pointing to this temporary buffer.  In either case, when the
 * receive function returns, user code cannot assume that the payload buffer is
 * permanent, i.e., after return, it may be overwritten by either the DMA or
 * the poll function.  To keep a copy of the packet, the receive function would
 * have to copy it to some other location.  The packet header and payload are
 * 16-byte aligned for optimized copying.
 *
 * \param[in]  total_packets     The maximum number of packets that will be
 *                               processed.
 * \param[in]  packets_per_fifo  The maximum number of packets that will be
 *                               processed in a given fifo before switching
 *                               to the next fifo.
 * \param[in]  starting_index    The fifos in the fifo group are maintained
 *                               in an array.  This is the array index of the
 *                               first fifo to be processed (0 through
 *                               DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP-1).
 * \param[in]  num_empty_passes  The number of passes over the normal fifos
 *                               while they are empty that this function
 *                               should tolerate before giving up and
 *                               returning.  This is an optimization
 *                               to catch late arriving packets.
 *                               (0 means no extra passes are made).
 * \param[in]  not_empty_poll_delay  The number of pclks to delay between polls
 *                                   of the not-empty status when the fifos are
 *                                   empty.
 * \param[in]  fg_ptr            Pointer to the fifo group.
 * \param[out] next_fifo_index   Pointer to an int where the recommended
 *                               starting_index for the next call is returned.
 *
 * \retval  num_packets_received  The number of packets received and processed.
 *                                next_fifo_index is set.
 * \retval  negative_value        The return code from the receive function that
 *                                caused polling to end.  next_fifo_index is
 *                                set.
 *
 * \pre  The caller is responsible for disabling interrupts before invoking this
 *       function.
 *
 * \note next_fifo_index is set to the index of the fifo that had the last
 *       packet received if all packets_per_fifo packets were not received from
 *       that fifo.  However, if all packets_per_fifo packets were received
 *       from that fifo, the index of the next fifo will be returned.
 *
 */
int DMA_RecFifoPollNormalFifos(int                 total_packets,
			       int                 packets_per_fifo,
			       int                 starting_index,
			       int                 num_empty_passes,
			       int                 not_empty_poll_delay,
			       DMA_RecFifoGroup_t *fg_ptr,
			       int                *next_fifo_index
			      )
{
  int fifo_index;                          /* Index of fifo being processed   */
  unsigned int fifo_bit_number;            /* The bit number of the fifo      */
                                           /* being processed.  Group0: 0-7,  */
                                           /* Group1: 8-15, Group2: 16-23,    */
                                           /* Group3: 24-31.  Corresponds to  */
                                           /* the DMA not-empty status bits.  */
  int num_fifos_in_group;                  /* Number of fifos in this group.  */
  int num_packets_in_fifo;                 /* Count of packets processed in a */
					   /* fifo.                           */
  unsigned int not_empty_status=0;         /* Snapshot of the not empty status*/
                                           /* for this group.  0 indicates    */
                                           /* that no snapshot has occurred   */
                                           /* yet.                            */
  int rc = 0;                              /* Return code from recv_func.     */
  int num_processed = 0;                   /* Number of packets processed     */
  DMA_PacketIovec_t io_vec;                /* Payload I/O vector              */
  DMA_RecFifoRecvFunction_t recv_func_ptr; /* Pointer to receive function     */
  void                     *recv_func_parm;/* Receive function parameter      */
  int                       recv_func_id;  /* Function ID from the packet     */
                                           /* header.                         */
  void                  *recv_func_payload;/* Pointer to recv func payload    */
  void                  *recv_func_packet; /* Pointer to recv func packet     */
  DMA_RecFifo_t *fifo_ptr;                 /* Pointer to fifo being processed */
  char temp_packet[256] ALIGN_QUADWORD;    /* Temporary packet copy.          */
                                           /* Align for efficient copying.    */
  char *load_ptr, *store_ptr;              /* Used for copying bytes          */
  int num_quads;                           /* Number of quads to copy         */
  DMA_PacketHeader_t *packet_ptr;          /* Pointer to packet header        */

  SPI_assert( total_packets     > 0 );
  SPI_assert( packets_per_fifo  > 0 );
  SPI_assert( packets_per_fifo <= total_packets );
  SPI_assert( num_empty_passes  >= 0 );
  SPI_assert( fg_ptr           != NULL );
  SPI_assert( next_fifo_index  != NULL );
  SPI_assert( ( starting_index >= 0 ) &&
	   ( starting_index < fg_ptr->num_normal_fifos ) );

  num_fifos_in_group = fg_ptr->num_normal_fifos;
  *next_fifo_index = starting_index; /* Tell caller to start with the same    */
                                     /* fifo next time.                       */
  fifo_index       = starting_index; /* Start with the fifo the caller says to*/

#ifdef DEBUG_PRINT
  int i;
  for (i=0; i<fg_ptr->num_normal_fifos; i++)
    printf("FifoIndex=%d <--> GlobalID=%d\n",i,fg_ptr->fifos[i].global_fifo_id);
#endif

  /*
   * Circularly loop through the not-empty fifos in the fifo group.
   * Keep going until one of the termination conditions documented in the
   * prolog occurs.
   *
   */
  for (;;)
    {
      /*
       * Find the next fifo to process.
       */
      fifo_ptr        = &fg_ptr->fifos[fifo_index]; /* This is the fifo itself*/
      fifo_bit_number = _BN(fifo_ptr->global_fifo_id);/* The fifo's status bit*/

      fifo_index = DMA_RecFifoGetNextFifo(fg_ptr,
					  fifo_index,
					  &fifo_bit_number,
					  num_empty_passes,
					  not_empty_poll_delay,
					  &not_empty_status);
      if (fifo_index < 0) { /* No more packets to process? */

#if defined(BGP_DD1_WORKAROUNDS)
	/*
	 * If there are no more non-empty fifos, count the number of consecutive
	 * times the poll function came up dry (num_processed == 0), and if it
	 * exceeds a threshold, issue a system call to clear the rDMA's "full
	 * reception fifo" condition so it begins to receive packets again.
	 *
	 * When a non-empty fifo is returned, its shadow va_tail pointer has been
	 * updated to reflect the amount of packet data in the fifo.
	 */
	if (num_processed > 0) { /* Did we process at least 1 packet? */
	  NumEmptyPollFunctionCalls = 0; /* The DMA must be active.  It has    */
	                                 /* likely not encountered a fifo full */
 	                                 /* condition and stopped.  Reset the  */
    	                                 /* fifo counter so we will start      */
                                         /* tracking empty calls to poll.      */
	}
	else {
	  if ( (NumEmptyPollFunctionCalls >= 0) && /* We are tracking empty calls? */
	       (++NumEmptyPollFunctionCalls >= NUM_EMPTY_POLL_FUNCTION_CALL_LIMIT) ) {
	     /*  printf("Hit Empty Poll Limit...invoking syscall to clear full condition\n"); */
	    rc = Kernel_ClearFullReceptionFifo(); /* Activate rDMA in case the */
                                             /* reception fifos filled and the */
                                             /* DMA has stopped.               */
	     /*  printf("Returned from ClearFull syscall with rc=%d\n",rc); */
	    NumEmptyPollFunctionCalls = -1; /* The DMA is active.  Reset the    */
                                            /* fill-fifo counter.               */
	  }
	}
#endif
	 /* 	printf("Poll: returned %d processed\n",num_processed); */
	return (num_processed);
      }

      *next_fifo_index = fifo_index; /* Tell caller to start with this fifo   */
                                     /* next time.                            */
      fifo_ptr = &(fg_ptr->fifos[fifo_index]);
      num_packets_in_fifo = 0;

      /*
       * MSYNC before we look at the data in the fifo to ensure that snoops
       * issued by the DMA have completed.  This ensures the L1 cache
       * invalidations have completed so we don't look at stale data.
       */
      _bgp_msync();

      /*
       * Within a fifo: The area between the va_head and va_tail shadow pointers
       * contains packets to be processed.  Loop, processing those packets until
       * we have processed packets_per_fifo of them, or all of them, or other
       * issues come up.
       *
       */
#if defined(CONFIG_BGP_STATISTICS)
      {
      unsigned int used_space = (fifo_ptr->dma_fifo.va_tail >= fifo_ptr->dma_fifo.va_head)
                 ? ( ((unsigned)(fifo_ptr->dma_fifo.va_tail) - (unsigned)(fifo_ptr->dma_fifo.va_head)) >> 4 )
                 : (fifo_ptr->dma_fifo.fifo_size + ( ((unsigned)(fifo_ptr->dma_fifo.va_tail) - (unsigned)(fifo_ptr->dma_fifo.va_head)) >> 4 ) )
                 ;
                 reception_fifo_histogram[fls(used_space)] += 1 ;
      }
#endif
      while ( ( num_packets_in_fifo < packets_per_fifo ) &&
	      ( fifo_ptr->dma_fifo.va_head != fifo_ptr->dma_fifo.va_tail ) )
	{
	  DMA_RecFifoGetAddresses( fifo_ptr,
				   &io_vec ); /* Get the payload pointer(s)   */
      	                                      /* for the packet at the head   */
                                              /* of the fifo.                 */

	  packet_ptr = (DMA_PacketHeader_t*)
	                 fifo_ptr->dma_fifo.va_head; /* Point to packet header*/

#ifdef DEBUG_PRINT
	  printf("ReceivedPacketHead = 0x%08x\n",(unsigned)packet_ptr);
	  printf("ReceivedPacketIovec= 0x%08x %d, 0x%08x %d\n",
		 (unsigned)io_vec.payload_ptr[0], io_vec.num_bytes[0],
		 (unsigned)io_vec.payload_ptr[1], io_vec.num_bytes[1]);
#endif
	  /*
	   * Determine the receive function to call.  Index into
	   * recvFunctions array is in the packet header.
	   */
	  recv_func_id  = packet_ptr->Func_Id;
	  recv_func_ptr = DMA_RecFifoInfo.recvFunctions[recv_func_id];
	  if ( recv_func_ptr != NULL )
	    {
	      recv_func_parm =
		DMA_RecFifoInfo.recvFunctionsParms[recv_func_id];
	    }
	  else
	    {
	      recv_func_ptr  = DMA_RecFifoInfo.errorRecvFunction;
	      recv_func_parm = DMA_RecFifoInfo.errorRecvFunctionParm;
	    }
	  /*
	   * Use a temporary copy of the packet, when the payload
	   * wraps.
	   */
	  if ( io_vec.num_segments > 1 )
	    {
#ifdef DEBUG_PRINT
	      printf("Payload Wraps: Packet Header: 0x%08x, Iovecs: 0x%08x %d, 0x%08x %d\n",
		     (unsigned)packet_ptr,
		     (unsigned)io_vec.payload_ptr[0], io_vec.num_bytes[0],
		     (unsigned)io_vec.payload_ptr[1], io_vec.num_bytes[1]);
#endif

	      /* Copy packet header and first payload segment */
	      load_ptr  = (char*)packet_ptr;
	      store_ptr = temp_packet;
	      num_quads = (sizeof(DMA_PacketHeader_t) + io_vec.num_bytes[0]) >> 4;
	      while ( num_quads > 0 )
		{
#ifdef DEBUG_PRINT
		  printf("load_ptr =0x%08x, load_value =0x%08x%08x%08x%08x\n",
			 (unsigned)load_ptr, *(unsigned*)load_ptr, *(unsigned*)(load_ptr+4),
			 *(unsigned*)(load_ptr+8), *(unsigned*)(load_ptr+12));
#endif
		  _bgp_QuadLoad ( load_ptr,     0 );

		  _bgp_QuadStore( store_ptr,    0 );
#ifdef DEBUG_PRINT
		  printf("store_ptr=0x%08x, store_value=0x%08x%08x%08x%08x\n",
			 (unsigned)store_ptr, *(unsigned*)store_ptr, *(unsigned*)(store_ptr+4),
			 *(unsigned*)(store_ptr+8), *(unsigned*)(store_ptr+12));
#endif

		  load_ptr  += 16;
		  store_ptr += 16;
		  num_quads--;
		}
	      /* Copy second payload segment */
	      load_ptr  = (char*)io_vec.payload_ptr[1];
	      num_quads = io_vec.num_bytes[1] >> 4;
	      while ( num_quads > 0 )
		{
#ifdef DEBUG_PRINT
		  printf("load_ptr =0x%08x, load_value =0x%08x%08x%08x%08x\n",
			 (unsigned)load_ptr, *(unsigned*)load_ptr, *(unsigned*)(load_ptr+4),
			 *(unsigned*)(load_ptr+8), *(unsigned*)(load_ptr+12));
#endif
		  _bgp_QuadLoad ( load_ptr,     0 );

		  _bgp_QuadStore( store_ptr,    0 );
#ifdef DEBUG_PRINT
		  printf("store_ptr=0x%08x, store_value=0x%08x%08x%08x%08x\n",
			 (unsigned)store_ptr, *(unsigned*)store_ptr, *(unsigned*)(store_ptr+4),
			 *(unsigned*)(store_ptr+8), *(unsigned*)(store_ptr+12));
#endif
		  load_ptr  += 16;
		  store_ptr += 16;
		  num_quads--;
		}
	      recv_func_payload = temp_packet + sizeof(DMA_PacketHeader_t);
	      recv_func_packet  = temp_packet;

	    } /* End: Set up temporary copy of split packet */

	  else /* Set up for contiguous packet */
	    {
	      recv_func_payload = (char*)packet_ptr +
		sizeof(DMA_PacketHeader_t);
	      recv_func_packet  = packet_ptr;
	    }

	  /* Call the receive function */
	  if( recv_func_ptr )
	    {
	  rc = (*recv_func_ptr)(fifo_ptr,
				recv_func_packet,
				recv_func_parm,
				recv_func_payload,
				io_vec.num_bytes[0]+io_vec.num_bytes[1]);
	    }
	  else
	    {
	      printk(KERN_ERR "DMA_RecFifoPollNormalFifos recv_func_ptr was NULL recv_func_id=%02x fifo_ptr=%p recv_func_packet=%p recv_func_parm=%p recv_func_payload=%p length=%d\n",
	          recv_func_id,fifo_ptr,recv_func_packet,recv_func_parm,recv_func_payload,io_vec.num_bytes[0]+io_vec.num_bytes[1]) ;
	    }

	  /* Increment the head by the size of the packet */
	  DMA_RecFifoIncrementHead(fifo_ptr,
				   (io_vec.num_bytes[0]+
				    io_vec.num_bytes[1] +
				    sizeof(DMA_PacketHeader_t))>> 4);

	  num_processed++;

	  if ( rc != 0 ) /* Did receive function fail? */
	    {
#if defined(BGP_DD1_WORKAROUNDS)
  	      NumEmptyPollFunctionCalls = 0; /* The DMA must be active.  It has    */
	                                     /* likely not encountered a fifo full */
 	                                     /* condition and stopped.  Reset the  */
    	                                     /* fifo counter so we will start      */
                                             /* tracking empty calls to poll.      */
#endif
	      /* Clear the threshold crossed condition, in case we have gone below
	       * the threshold.
	       */
	      DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
						   fifo_bit_number,
						   0 );
	      return (rc); /* Yes...return that return code */
	    }

	  if ( num_processed >= total_packets ) /* Got what they wanted? */
	    {
#if defined(BGP_DD1_WORKAROUNDS)
  	      NumEmptyPollFunctionCalls = 0; /* The DMA must be active.  It has    */
	                                     /* likely not encountered a fifo full */
 	                                     /* condition and stopped.  Reset the  */
    	                                     /* fifo counter so we will start      */
                                             /* tracking empty calls to poll.      */
#endif
	      /* Clear the threshold crossed condition, in case we have gone below
	       * the threshold.
	       */
	      DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
						   fifo_bit_number,
						   0 );
	      return (num_processed); /* Yes...all done */
	    }

	  num_packets_in_fifo++;

	} /* End: Process up to packets_per_fifo packets in this fifo */

      /*
       * We exited the loop processing the fifo_index fifo.
       * - If we exited because we reached the packets_per_fifo limit, we want
       *   to turn off this fifo's not-empty status in our shadow copy of the
       *   status so we process all of the other fifos before re-fetching the
       *   true status, giving this fifo another chance.
       * - If we exited because the fifo was empty according to our snapshot
       *   of the fifo's tail (head == tail snapshot), we want to turn off this
       *   fifo's not-empty status in our shadow copy of the status so we
       *   process all of the other fifos before re-fetching the true status and
       *   tail for this fifo, giving this fifo another chance.
       * Either way, we turn off the status bit.
       *
       */
      not_empty_status &= ~(fifo_bit_number);

      /* Clear the threshold crossed condition, in case we have gone below
       * the threshold.
       */
      DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
					   fifo_bit_number,
					   0 );

#ifdef DEBUG_PRINT
      printf("PollNormal: Turning off status bit 0x%08x, status=0x%08x\n",fifo_bit_number,not_empty_status);
#endif

      /* Bump to next fifo */
      fifo_index = (fifo_index+1) % num_fifos_in_group;

      /*
       * If we have processed the max number of packets from the previous fifo,
       * the recommended next fifo to process is the one after that.
       *
       */
      if ( num_packets_in_fifo == packets_per_fifo )
	{
	  *next_fifo_index  = fifo_index;
	}

    } /* End: Keep looping through the fifos. */

} /* End: DMA_RecFifoPollNormalFifos() */





static int dumpmem_count ;

static inline void quadcpy(void *dest, const void *src)
{
	unsigned int *desti=(unsigned int *) dest ;
	const unsigned int *srci=(const unsigned int *) src ;
	unsigned int w0 = srci[0] ;
	unsigned int w1 = srci[1] ;
	unsigned int w2 = srci[2] ;
	unsigned int w3 = srci[3] ;
	desti[0] = w0 ;
	desti[1] = w1 ;
	desti[2] = w2 ;
	desti[3] = w3 ;
}
/*!
 * \brief Poll Normal Reception Fifo Given a Fifo Group and Fifo ID
 *
 * Poll the specified "normal" reception fifo in the specified fifo group,
 * removing one packet after another from the fifo, dispatching the appropriate
 * receive function for each packet, until one of the following occurs:
 * 1.  num_packets packets are received
 * 2.  The specified fifo is empty
 * 3.  A receive function returns a non-zero value
 * 4.  The last packet removed from the fifo has an invalid registration id. The
 *     error receive function will have been called, but polling ends.
 *     The invalid packet is counted as a processed packet, and the return
 *     code from the error receive function is returned.
 *
 * If the specified fifo has a packet, the appropriate receive function is
 * called.  Upon return, the packet is removed from the fifo (the fifo head is
 * moved past the packet).
 *
 * After processing num_packets packets in the fifo (or emptying that fifo),
 * the function returns the number of packets processed *
 * The receive functions must be registered through the
 * DMA_RecFifoRegisterRecvFunction interface.  The receive function is
 * called with a pointer to the packet header, pointer to the payload, and
 * length of the payload.  The packet header is always be 16 bytes of
 * contiguous storage, in the fifo.  Because the fifo is a circular buffer,
 * the payload of a packet may wrap from the end of the fifo to the beginning.
 * For large fifos, this happens infrequently.  To make it easier for
 * user/messaging code, the poll function will always return a starting payload
 * address and number of bytes so that the receive function can treat the packet
 * as contiguous storage in memory.  If the packet does not wrap, the starting
 * payload address will be a pointer to the appropriate address in the fifo.
 * If the packet does wrap, the poll function will copy bytes from the fifo to
 * a contiguous buffer (on the stack) and call the receive function with a
 * payload pointer pointing to this temporary buffer.  In either case, when the
 * receive function returns, user code cannot assume that the payload buffer is
 * permanent, i.e., after return, it may be overwritten by either the DMA or
 * the poll function.  To keep a copy of the packet, the receive function would
 * have to copy it to some other location.  The packet header and payload are
 * 16-byte aligned for optimized copying.
 *
 * \param[in]  num_packets       The maximum number of packets that will be
 *                               processed.
 * \param[in]  fifo_id           The ID of the fifo to be polled.
 *                               (0 through
 *                               DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP-1).
 * \param[in]  num_empty_passes    When the not-empty status indicates that all
 *                                 fifos in the group are emtpy, this is the
 *                                 number of times the not-empty status is
 *                                 re-fetched and re-checked before officially
 *                                 declaring that they are indeed empty.
 *                                 (0 means no extra passes are made).
 * \param[in]  not_empty_poll_delay  The number of pclks to delay between polls
 *                                   of the not-empty status when the fifos are
 *                                   empty.
 * \param[in]  fg_ptr            Pointer to the fifo group.
 *
 * \param[in]  empty_callback    Function to call when spinning because the FIFO looks empty.
 *
 * \retval  num_packets_received  The number of packets received and processed.
 * \retval  negative_value        The return code from the receive function that
 *                                caused polling to end.
 *
 * \pre  The caller is responsible for disabling interrupts before invoking this
 *       function.
 *
 */
int DMA_RecFifoPollNormalFifoById( int                 num_packets,
				   int                 fifo_id,
				   int                 num_empty_passes,
				   int                 not_empty_poll_delay,
				   DMA_RecFifoGroup_t *fg_ptr,
				   void 		(*empty_callback)(void)
				 )
{
  int num_packets_in_fifo;                 /* Count of packets processed in a */
					   /* fifo.                           */
  unsigned int status;                     /* Snapshot of the not empty status*/
                                           /* for this group.                 */
  int rc = 0;                              /* Return code from recv_func.     */
  int num_processed = 0;                   /* Number of packets processed     */
  DMA_PacketIovec_t io_vec;                /* Payload I/O vector              */
  DMA_RecFifoRecvFunction_t recv_func_ptr; /* Pointer to receive function     */
  void                     *recv_func_parm;/* Receive function parameter      */
  int                       recv_func_id;  /* Function ID from the packet     */
                                           /* header.                         */
  void                  *recv_func_payload;/* Pointer to recv func payload    */
  void                  *recv_func_packet; /* Pointer to recv func packet     */
  DMA_RecFifo_t *fifo_ptr;                 /* Pointer to fifo being processed */
  char temp_packet[256] ALIGN_QUADWORD;    /* Temporary packet copy.          */
                                           /* Align for efficient copying.    */
  char *load_ptr, *store_ptr;              /* Used for copying bytes          */
  int num_quads;                           /* Number of quads to copy         */
  DMA_PacketHeader_t *packet_ptr;          /* Pointer to packet header        */
  int passes;                              /* Counter of not-empty passes     */

  SPI_assert( num_packets       > 0 );
  SPI_assert( num_empty_passes  >= 0 );
  SPI_assert( fg_ptr           != NULL );
  SPI_assert( ( fifo_id >= 0 ) &&
	   ( fifo_id <  DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP ) );

  fifo_ptr = &(fg_ptr->fifos[fifo_id]);

  /*
   * Loop until the specified fifo is declared empty, or
   * until one of the termination conditions documented in the prolog occurs.
   *
   */
  for (;;)
    {
      /*
       * If the DMA SRAM not-empty status for this fifo is zero (the fifo is
       * empty), the status is checked num_empty_passes times with a slight
       * delay in between to give the DMA time to make progress before declaring
       * that the fifo is truely empty.
       */
      passes = num_empty_passes;
      status = DMA_RecFifoGetNotEmptyById( fg_ptr,
					   fifo_id ); /* Get Normal fifo   */
                                                      /* not-empty status. */
      while ( ( status == 0 ) &&
	      ( num_empty_passes-- > 0 ) )
	{
	  /* Delay, allowing the DMA to update its status */
	  unsigned int pclks = not_empty_poll_delay;
	  (*empty_callback)() ;
	  while( pclks-- )
	    {
	      asm volatile("nop;");
	    }

	  /* Re-fetch the not-empty status */
	  status = DMA_RecFifoGetNotEmptyById(
					    fg_ptr,
					    fifo_id ); /* Get Normal fifo  */
	                                               /* not-empty status.*/
	}

      if ( status == 0 ) {       /* Fifo is empty?                             */

#if defined(BGP_DD1_WORKAROUNDS)
	if (num_processed > 0) { /* Did we process at least 1 packet?          */
	  NumEmptyPollFunctionCalls = 0; /* The DMA must be active.  It has    */
	                                 /* likely not encountered a fifo full */
 	                                 /* condition and stopped.  Reset the  */
    	                                 /* fifo counter so we will start      */
                                         /* tracking empty calls to poll.      */
	  /* Clear the threshold crossed condition, in case we have gone below
	   * the threshold.
	   */
	  DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
					       _BN(fifo_ptr->global_fifo_id),
					       0 );
	}
	else {
	  if ( (NumEmptyPollFunctionCalls >= 0) && /* We are tracking empty calls? */
	       (++NumEmptyPollFunctionCalls >= NUM_EMPTY_POLL_FUNCTION_CALL_LIMIT) ) {
	     /*  printf("Hit Empty Poll Limit...invoking syscall to clear full condition\n"); */
	    rc = Kernel_ClearFullReceptionFifo(); /* Activate rDMA in case the */
                                             /* reception fifos filled and the */
                                             /* DMA has stopped.               */
	     /*  printf("Returned from ClearFull syscall with rc=%d\n",rc); */
	    NumEmptyPollFunctionCalls = -1; /* The DMA is active.  Reset the    */
                                            /* fill-fifo counter.               */
	  }
	}
#endif

	return (num_processed);
      }

      /* The fifo has something in it.
       * Update its shadow va_tail pointer to reflect the amount of packet
       * data in the fifo.
       */
      DMA_RecFifoGetTailById( fg_ptr,
			      fifo_id );

      num_packets_in_fifo = 0;

      /*
       * MSYNC before we look at the data in the fifo to ensure that snoops
       * issued by the DMA have completed.  This ensures the L1 cache
       * invalidations have completed so we don't look at stale data.
       */
      _bgp_msync();

      /*
       * Within a fifo: The area between the va_head and va_tail shadow pointers
       * contains packets to be processed.  Loop, processing those packets until
       * we have processed packets_per_fifo of them, or all of them, or other
       * issues come up.
       *
       */
#if defined(CONFIG_BGP_STATISTICS)
      {
	      unsigned int tail = (unsigned int) fifo_ptr->dma_fifo.va_tail ;
	      unsigned int head = (unsigned int) fifo_ptr->dma_fifo.va_head ;
	      unsigned int end  = (unsigned int) fifo_ptr->dma_fifo.va_end ;
	      unsigned int start = (unsigned int) fifo_ptr->dma_fifo.va_start ;
	      unsigned int used_space = ( tail >= head ) ? (tail-head) : ((tail-start)+(end-head)) ;
	      reception_fifo_histogram[fls(used_space >> 4)] += 1 ;
	      if( used_space > reception_hi_watermark ) reception_hi_watermark = used_space ;

/*       unsigned int used_space = (fifo_ptr->dma_fifo.va_tail >= fifo_ptr->dma_fifo.va_head) */
/*                  ? ( ((unsigned)(fifo_ptr->dma_fifo.va_tail) - (unsigned)(fifo_ptr->dma_fifo.va_head)) >> 4 ) */
/*                  : (fifo_ptr->dma_fifo.fifo_size + ( ((unsigned)(fifo_ptr->dma_fifo.va_tail) - (unsigned)(fifo_ptr->dma_fifo.va_head)) >> 4 ) ) */
/*                  ; */
/*                  reception_fifo_histogram[fls(used_space)] += 1 ; */
      }
#endif
      while ( ( num_packets_in_fifo < num_packets ) &&
	      ( fifo_ptr->dma_fifo.va_head != fifo_ptr->dma_fifo.va_tail ) )
	{
	  DMA_RecFifoGetAddresses( fifo_ptr,
				   &io_vec ); /* Get the payload pointer(s)   */
      	                                      /* for the packet at the head   */
                                              /* of the fifo.                 */

	  packet_ptr = (DMA_PacketHeader_t*)
	                 fifo_ptr->dma_fifo.va_head; /* Point to packet header*/
	  /*
	   * Determine the receive function to call.  Index into
	   * recvFunctions array is in the packet header.
	   */
	  recv_func_id  = packet_ptr->Func_Id;
	  recv_func_ptr = DMA_RecFifoInfo.recvFunctions[recv_func_id];
	  if ( recv_func_ptr != NULL )
	    {
	      recv_func_parm =
		DMA_RecFifoInfo.recvFunctionsParms[recv_func_id];
	    }
	  else
	    {
	      recv_func_ptr  = DMA_RecFifoInfo.errorRecvFunction;
	      recv_func_parm = DMA_RecFifoInfo.errorRecvFunctionParm;
	    }
	  /*
	   * Use a temporary copy of the packet, when the payload
	   * wraps.
	   */
	  if ( io_vec.num_segments > 1 )
	    {
	      /* Copy packet header and first payload segment */
	      load_ptr  = (char*)packet_ptr;
	      store_ptr = temp_packet;
	      num_quads = (sizeof(DMA_PacketHeader_t) + io_vec.num_bytes[0]) >> 4;
	      while ( num_quads > 0 )
		{
			 /*  Don't bother doing this via doublehummer; it only happens 'occasionally' and means the caller has to enable for floating-point */
			quadcpy(store_ptr,load_ptr) ;
/* 		  _bgp_QuadLoad ( load_ptr,     0 ); */
/* 		  _bgp_QuadStore( store_ptr,    0 ); */
		  load_ptr  += 16;
		  store_ptr += 16;
		  num_quads--;
		}
	      /* Copy second payload segment */
	      load_ptr  = (char*)io_vec.payload_ptr[1];
	      num_quads = io_vec.num_bytes[1] >> 4;
	      while ( num_quads > 0 )
		{
			quadcpy(store_ptr,load_ptr) ;
/* 		  _bgp_QuadLoad ( load_ptr,     0 ); */
/* 		  _bgp_QuadStore( store_ptr,    0 ); */
		  load_ptr  += 16;
		  store_ptr += 16;
		  num_quads --;
		}
	      recv_func_payload = temp_packet + sizeof(DMA_PacketHeader_t);
	      recv_func_packet  = temp_packet;

	    } /* End: Set up temporary copy of split packet */

	  else /* Set up for contiguous packet */
	    {
	      recv_func_payload = (char*)packet_ptr +
		sizeof(DMA_PacketHeader_t);
	      recv_func_packet  = packet_ptr;
	    }

	  /* Call the receive function */
          if( recv_func_ptr )
            {
/*               dumpmem(recv_func_packet-32, 128, "Software FIFO around call") ; */
              rc = (*recv_func_ptr)(fifo_ptr,
                                    recv_func_packet,
                                    recv_func_parm,
                                    recv_func_payload,
                                    io_vec.num_bytes[0]+io_vec.num_bytes[1]);
            }
          else
            {
              printk(KERN_ERR "DMA_RecFifoPollNormalFifoById recv_func_ptr was NULL recv_func_id=%02x fifo_ptr=%p recv_func_packet=%p recv_func_parm=%p recv_func_payload=%p length=%d\n",
                  recv_func_id,fifo_ptr,recv_func_packet,recv_func_parm,recv_func_payload,io_vec.num_bytes[0]+io_vec.num_bytes[1]) ;
              if( dumpmem_count < 10 )
                {
                  dumpmem(recv_func_packet-256, 512, "Software FIFO around misread") ;
                  dumpmem_count += 1 ;
                }
/*               show_tlbs((unsigned int) recv_func_packet) ; */
/*               (void)dma_map_single(NULL,recv_func_packet-32, 128,DMA_FROM_DEVICE) ; */
/*               dumpmem(recv_func_packet-32, 128, "Software FIFO around misread after cache discard") ; */
            }

	  /* Increment the head by the size of the packet */
	  DMA_RecFifoIncrementHead(fifo_ptr,
				   (io_vec.num_bytes[0]+
				    io_vec.num_bytes[1] +
				    sizeof(DMA_PacketHeader_t))>> 4);

	  num_processed++;

	  if ( rc != 0 ) /* Did receive function fail? */
	    {
	      /* Clear the threshold crossed condition, in case we have gone below
	       * the threshold.
	       */
	      DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
						   _BN(fifo_ptr->global_fifo_id),
						   0 );
	      return (rc); /* Yes...return that return code */
	    }

	  if ( num_processed >= num_packets ) /* Got what they wanted? */
	    {
	      /* Clear the threshold crossed condition, in case we have gone below
	       * the threshold.
	       */
	      DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
						   _BN(fifo_ptr->global_fifo_id),
						   0 );
	      return (num_processed); /* Yes...all done */
	    }

	  num_packets_in_fifo++;

	} /* End: Process up to packets_per_fifo packets in this fifo */

    } /* End: Keep looping through the fifo. */

} /* End: DMA_RecFifoPollNormalFifoById() */




/*!
 *
 * \brief Prime Receive Function Cache for Polling Function
 *
 * The reception fifo receive function maintains a simple cache of information
 * about the last receive function called.  This function is called to return
 * that information for a given function ID.
 *
 * \param [in]   recv_func_id    The function ID whose receive function info
 *                               is to be returned.
 * \param [out]  recv_func_ptr   Pointer to the receive function's address,
 *                               returned by this function.
 * \param [out]  recv_func_parm  Pointer to the receive function's parameter.
 *
 * \return The information (function pointer and function parameter) for the
 *         specified receive function is returned as described.
 */
inline
void DMA_RecFifoPollPrimeRecvFuncCache( int                         recv_func_id,
					DMA_RecFifoRecvFunction_t  *recv_func_ptr,
					void                      **recv_func_parm )
{
  DMA_RecFifoRecvFunction_t  local_recv_func_ptr;
  void                      *local_recv_func_parm;

  local_recv_func_ptr = DMA_RecFifoInfo.recvFunctions[recv_func_id];
  if ( local_recv_func_ptr != NULL ) {
    local_recv_func_parm =
      DMA_RecFifoInfo.recvFunctionsParms[recv_func_id];
  }
  else {
    local_recv_func_ptr  = DMA_RecFifoInfo.errorRecvFunction;
    local_recv_func_parm = DMA_RecFifoInfo.errorRecvFunctionParm;
  }
  *recv_func_ptr = local_recv_func_ptr;
  *recv_func_parm= local_recv_func_parm;

} /* End: DMA_RecFifoPrimeRecvFuncCache() */




/*!
 *
 * \brief Process a Wrap of a Reception Fifo While Polling
 *
 * This function is meant to be called by a polling function that has processed
 * packets in a reception fifo such that there are just a few left to be
 * processed before it hits the end of the fifo and wraps.  This function
 * processes those packets at the end of the fifo until the wrap occurs,
 * and then returns, leaving the rest of the packets in the fifo to be
 * processed by the calling function.
 *
 * \param[in]      rec_fifo_ptr             Pointer to reception fifo
 * \param[in,out]  va_head                  Pointer to the fifo's virtual address
 *                                          head.  Updated by this function.
 * \param[in,out]  va_tail                  Pointer to the fifo's virtual address
 *                                          tail.  Updated by this function.
 * \param[in,out]  num_processed            Pointer to the number of packets
 *                                          processed by the calling poll
 *                                          function.  Updated by this function.
 * \param[in,out]  num_processed_in_fifo    Pointer to the number of packets
 *                                          in this particular fifo processed
 *                                          by the calling poll function.
 *                                          Updated by this function.
 * \param[in]      max_num_packets          The max number of packets that can be
 *                                          processed before the poll function
 *                                          must return.
 * \param[in]      max_num_packets_in_fifo  The max number of packets that can be
 *                                          processed in this fifo.
 *
 * \retval  0                     Processing complete successfully.  Output
 *                                parameters have been updated as described.
 * \retval  negative_value        The return code from the receive function that
 *                                caused polling to end.
 */

int DMA_RecFifoPollProcessWrap ( DMA_RecFifo_t  *rec_fifo_ptr,
				 void          **va_head,
				 void           *va_tail,
				 int            *num_processed,
				 int            *num_processed_in_fifo,
				 int             max_num_packets,
				 int             max_num_packets_in_fifo) {
  int                 rc = 0;
  DMA_PacketIovec_t   io_vec;              /* Payload I/O vector              */
  DMA_PacketHeader_t *packet_ptr;          /* Pointer to packet header        */
  DMA_RecFifoRecvFunction_t recv_func_ptr; /* Pointer to receive function     */
  void                     *recv_func_parm;/* Receive function parameter      */
  int                       recv_func_id;  /* Function ID from the packet     */
                                           /* header.                         */
  void                  *recv_func_payload;/* Pointer to recv func payload    */
  void                  *recv_func_packet; /* Pointer to recv func packet     */
  char temp_packet[256] ALIGN_QUADWORD;    /* Temporary packet copy.          */
                                           /* Align for efficient copying.    */
  char *load_ptr, *store_ptr;              /* Used for copying bytes          */
  int num_quads;                           /* Number of quads to copy         */

  while ( rc == 0 ) { /* Loop while things are good until we exit after       */
                      /* processing the wrap.                                 */

    DMA_RecFifoGetAddresses( rec_fifo_ptr,
			     &io_vec ); /* Get the payload pointer(s)         */
    	                                /* for the packet at the head         */
                                        /* of the fifo.                       */

    packet_ptr = (DMA_PacketHeader_t*)
                   rec_fifo_ptr->dma_fifo.va_head; /* Point to packet header  */

    /*
     * Determine the receive function to call.  Index into
     * recvFunctions array is in the packet header.
     */
    recv_func_id  = packet_ptr->Func_Id;
    recv_func_ptr = DMA_RecFifoInfo.recvFunctions[recv_func_id];
    if ( recv_func_ptr != NULL )
      {
	recv_func_parm =
	  DMA_RecFifoInfo.recvFunctionsParms[recv_func_id];
      }
    else
      {
	recv_func_ptr  = DMA_RecFifoInfo.errorRecvFunction;
	recv_func_parm = DMA_RecFifoInfo.errorRecvFunctionParm;
      }
    /*
     * Use a temporary copy of the packet, when the payload
     * wraps.
     */
    if ( io_vec.num_segments > 1 )
      {
	/* Copy packet header and first payload segment */
	load_ptr  = (char*)packet_ptr;
	store_ptr = temp_packet;
	num_quads = (sizeof(DMA_PacketHeader_t) + io_vec.num_bytes[0]) >> 4;
	while ( num_quads > 0 )
	  {
	    _bgp_QuadLoad ( load_ptr,     0 );
	    _bgp_QuadStore( store_ptr,    0 );
	    load_ptr  += 16;
	    store_ptr += 16;
	    num_quads --;
	  }
	/* Copy second payload segment */
	load_ptr  = (char*)io_vec.payload_ptr[1];
	num_quads = io_vec.num_bytes[1] >> 4;
	while ( num_quads > 0 )
	  {
	    _bgp_QuadLoad ( load_ptr,     0 );
	    _bgp_QuadStore( store_ptr,    0 );
	    load_ptr  += 16;
	    store_ptr += 16;
	    num_quads --;
	  }
	recv_func_payload = temp_packet + sizeof(DMA_PacketHeader_t);
	recv_func_packet  = temp_packet;

      } /* End: Set up temporary copy of split packet */

    else /* Set up for contiguous packet */
      {
	recv_func_payload = (char*)packet_ptr +
	                       sizeof(DMA_PacketHeader_t);
	recv_func_packet  = packet_ptr;
      }

    /* Call the receive function */
    if( recv_func_ptr)
      {
    rc = (*recv_func_ptr)(rec_fifo_ptr,
			  recv_func_packet,
			  recv_func_parm,
			  recv_func_payload,
			  io_vec.num_bytes[0]+io_vec.num_bytes[1]);
      }
    else
      {
        printk(KERN_ERR "DMA_RecFifoPollProcessWrap recv_func_ptr was NULL recv_func_id=%02x rec_fifo_ptr=%p recv_func_packet=%p recv_func_parm=%p recv_func_payload=%p length=%d\n",
            recv_func_id,rec_fifo_ptr,recv_func_packet,recv_func_parm,recv_func_payload,io_vec.num_bytes[0]+io_vec.num_bytes[1]) ;

      }

    /* Increment the head by the size of the packet */
    DMA_RecFifoIncrementHead(rec_fifo_ptr,
			     (io_vec.num_bytes[0]+
			      io_vec.num_bytes[1] +
			      sizeof(DMA_PacketHeader_t))>> 4);
    *va_head = rec_fifo_ptr->dma_fifo.va_head; /* Refresh caller's head */

    (*num_processed)++;
    (*num_processed_in_fifo)++;

#ifdef DEBUG_PRINT
    printf("PollWrap: num_processed=%d, va_head=0x%08x, Part1Len=%d, Part2Len=%d, Part1Ptr=0x%08x, Part2Ptr=0x%08x\n",*num_processed,(unsigned)*va_head,io_vec.num_bytes[0],io_vec.num_bytes[1],(unsigned)io_vec.payload_ptr[0],(unsigned)io_vec.payload_ptr[1]);
#endif

    if ( ( (unsigned)*va_head < (unsigned)packet_ptr ) || /* Did we wrap? */
	 ( *num_processed >= max_num_packets ) || /* Got enough packets? */
	 ( *num_processed_in_fifo > max_num_packets_in_fifo ) ) /* Got enough */
                                                  /* packets for this fifo?   */
      {
	break;
      }

  } /* End: Keep looping through the fifo. */

  return(rc);

} /* End: DMA_RecFifoPollProcessWrap() */


/*!
 * \brief Simple Poll Normal Reception Fifos
 *
 * Poll the "normal" reception fifos in the specified fifo group, removing one
 * packet after another from the fifos, dispatching the appropriate receive
 * function for each packet, until one of the following occurs:
 * 1.  All packets in all of the fifos have been received.
 * 2.  A receive function returns a non-zero value.
 * 3.  The last packet removed from a fifo has an invalid registration id.  The
 *     error receive function will have been called, but polling ends.
 *     The invalid packet is counted as a processed packet, and the return
 *     code from the error receive function is returned.
 * 4.  There have been fruitfulPollLimit polls attempted (summed across all
 *     fifos).
 *
 * Polling occurs in a round-robin fashion through the array of normal fifos in
 * the group.  If a fifo has a packet, the appropriate receive function is
 * called.  Upon return, the packet is removed from the fifo (the fifo head is
 * moved past the packet).
 *
 * After processing all of the packets in a fifo (or emptying that fifo),
 * the next fifo in the group is processed.  When the last index in the fifo
 * array is processed, processing continues with the first fifo in the array.
 * Multiple loops through the array of fifos in the group may occur until all
 * fifos are empty or fruitfulPollLimit polls have been completed.
 *
 * It is risky to set the fruitfulPollLimit to zero, allowing this function to
 * poll indefinitely as long as there are packets to be processed.  This may
 * starve the node in a scenario where other nodes send "polling" packets to
 * our node, and our node never gets a chance to do anything else except
 * process those polling packets.
 *
 * The receive functions must be registered through the
 * DMA_RecFifoRegisterRecvFunction interface.  The receive function is
 * called with a pointer to the packet header, pointer to the payload, and
 * length of the payload.  The packet header is always be 16 bytes of
 * contiguous storage, in the fifo.  Because the fifo is a circular buffer,
 * the payload of a packet may wrap from the end of the fifo to the beginning.
 * For large fifos, this happens infrequently.  To make it easier for
 * user/messaging code, the poll function will always return a starting payload
 * address and number of bytes so that the receive function can treat the packet
 * as contiguous storage in memory.  If the packet does not wrap, the starting
 * payload address will be a pointer to the appropriate address in the fifo.
 * If the packet does wrap, the poll function will copy bytes from the fifo to
 * a contiguous buffer (on the stack) and call the receive function with a
 * payload pointer pointing to this temporary buffer.  In either case, when the
 * receive function returns, user code cannot assume that the payload buffer is
 * permanent, i.e., after return, it may be overwritten by either the DMA or
 * the poll function.  To keep a copy of the packet, the receive function would
 * have to copy it to some other location.  The packet header and payload are
 * 16-byte aligned for optimized copying.
 *
 * \param[in]  fg_ptr             Pointer to the fifo group.
 * \param[in]  fruitfulPollLimit  The limit on the number of fruitful polls that
 *                                will be attempted (summed across all fifos).
 *                                If the limit is reached, this function
 *                                returns.  A value of zero means there is no
 *                                limit imposed.  A fruitful poll is one where
 *                                at least one packet has arrived in the fifo
 *                                since the last poll.
 *
 * \retval  num_packets_received  The number of packets received and processed.

 * \retval  negative_value        The return code from the receive function that
 *                                caused polling to end.
 *
 * \pre  The caller is responsible for disabling interrupts before invoking this
 *       function.
 *
 */
int DMA_RecFifoSimplePollNormalFifos( DMA_RecFifoGroup_t *fg_ptr,
				      int                 fruitfulPollLimit)
{
  int rc = 0;                              /* Return code from recv_func.     */
  int num_processed = 0;                   /* Number of packets processed     */
  int num_processed_in_fifo = 0;           /* Not used, but needed for calling*/
                                           /* wrap function.                  */
  int fruitfulPollCount;                   /* Number of fruitful polls.       */

  /*
   *The following is actually a cache of the last receive function called.
   * We cache it so we don't need to keep looking up the receive function
   * info on each packet.
   */
  DMA_RecFifoRecvFunction_t recv_func_ptr=NULL;  /* Pointer to receive function*/
  void                     *recv_func_parm=NULL;;/* Receive function parameter */
  int                       recv_func_id=-1;  /* Function ID from the packet  */
                                              /* header. Init to -1 means     */
                                              /* recv_func_ptr and            */
                                              /* recv_func_parm do not cache  */
                                              /* the previous packet values.  */

  DMA_PacketHeader_t *packet_ptr;          /* Pointer to packet header        */
  unsigned int        packet_bytes;        /* Number of bytes in the packet.  */
  unsigned int        wrap;                /* 1: A wrap of the fifo is going  */
                                           /*    to occur.                    */
                                           /* 0: No wrap is going to occur.   */

  /*
   * Processing of packets occurs in the fifo in three phases:
   * Normal Phase 1   :  Packets before the wrap.
   * Handle Wrap Phase:  Packets during the wrap.
   * Normal Phase 2   :  Packets after the wrap.
   */
  void *va_logical_tail;                   /* The point beyond which normal   */
                                           /* processing of packets ends.     */
  void *va_starting_head;                  /* Pointer to the first packet in  */
                                           /* a contiguous group extracted    */
                                           /* from the fifo.                  */
  void *va_nextHead;                       /* Pointer to the next packet to   */
                                           /* be processed.                   */
  void *va_tail;                           /* Snapshot of the fifo's tail.    */
  unsigned int num_packets_processed_since_moving_fifo_head; /*
				      	      Tells us when we should move the
                                              hardware head.                  */

  /*
   * Control variables for looping through the fifos
   */
  int fifo_index=0;                        /* Index of fifo being processed.  */
                                           /* Start with first fifo.          */
  unsigned int fifo_bit_number;            /* The bit number of the fifo      */
                                           /* being processed.  Group0: 0-7,  */
                                           /* Group1: 8-15, Group2: 16-23,    */
                                           /* Group3: 24-31.  Corresponds to  */
                                           /* the DMA not-empty status bits.  */
  int num_fifos_in_group;                  /* Number of fifos in this group.  */
  int num_packets_in_fifo;                 /* Count of packets processed in a */
					   /* fifo.                           */
  unsigned int not_empty_status=0;         /* Snapshot of the not empty status*/
                                           /* for this group.  0 indicates    */
                                           /* that no snapshot has occurred   */
                                           /* yet.                            */
  DMA_RecFifo_t *rec_fifo_ptr;             /* Pointer to reception fifo being */
                                           /* processed.                      */


  SPI_assert( fg_ptr != NULL );

  num_fifos_in_group = fg_ptr->num_normal_fifos;

  /*
   * Start the fruitful poll count at the max.
   * For unlimited, set to a very high value.
   */
  fruitfulPollCount = (fruitfulPollLimit == 0) ? 0x7FFFFFFF : fruitfulPollLimit;

  /*
   * Circularly loop through the not-empty fifos in the fifo group.
   * Keep going until one of the termination conditions documented in the
   * prolog occurs.
   *
   */
  for (;;) {
    /*
     * Find the next fifo to process.
     */
    rec_fifo_ptr    = &fg_ptr->fifos[fifo_index]; /* This is the fifo itself*/
    fifo_bit_number = _BN(rec_fifo_ptr->global_fifo_id);/* fifo's status bit*/

    fifo_index = DMA_RecFifoGetNextFifo(fg_ptr,
					fifo_index,
					&fifo_bit_number,
					0, /*  num_empty_passes */
					0, /*  not_empty_poll_delay */
					&not_empty_status);
    if (fifo_index < 0) { /* No more packets to process? */
#if defined(BGP_DD1_WORKAROUNDS)
      /*
       *
       * If there are no more non-empty fifos, count the number of consecutive
       * times the poll function came up dry (num_processed == 0), and if it
       * exceeds a threshold, issue a system call to clear the rDMA's "full
       * reception fifo" condition so it begins to receive packets again.
       *
       * When a non-empty fifo is returned, its shadow va_tail pointer has been
       * updated to reflect the amount of packet data in the fifo.
       */
      if (num_processed > 0) { /* Did we process at least 1 packet? */
	NumEmptyPollFunctionCalls = 0; /* The DMA must be active.  It has    */
	                               /* likely not encountered a fifo full */
 	                               /* condition and stopped.  Reset the  */
    	                               /* fifo counter so we will start      */
                                       /* tracking empty calls to poll.      */
      }
      else {
	if ( (NumEmptyPollFunctionCalls >= 0) && /* We are tracking empty calls? */
	     (++NumEmptyPollFunctionCalls >= NUM_EMPTY_POLL_FUNCTION_CALL_LIMIT) ) {
	   /*  printf("Hit Empty Poll Limit...invoking syscall to clear full condition\n"); */
	  rc = Kernel_ClearFullReceptionFifo(); /* Activate rDMA in case the */
                                                /* reception fifos filled and the */
                                                /* DMA has stopped.               */
	   /*  printf("Returned from ClearFull syscall with rc=%d\n",rc); */
	  NumEmptyPollFunctionCalls = -1; /* The DMA is active.  Reset the    */
                                          /* fill-fifo counter.               */
	}
      }
#endif
       /* 	printf("Poll: returned %d processed\n",num_processed); */
      return (num_processed);
    }

    num_packets_in_fifo = 0;

    /*
     * Establish pointers to the reception fifo and the DMA fifo.
     * Snapshot the hardware head and tail pointers...they may change while we
     * are running.  We will snapshot the tail again after processing everything
     * up to this snapshot, until the fifo is empty (head == tail).
     */
    rec_fifo_ptr = &(fg_ptr->fifos[fifo_index]);
    DMA_Fifo_t    *fifo_ptr     = &(rec_fifo_ptr->dma_fifo);
    void          *va_head      = fifo_ptr->va_head;
    va_tail      = DMA_FifoGetTailNoFreeSpaceUpdate( fifo_ptr ); /* Snapshot HW */
                                                                 /* tail.       */
    num_packets_processed_since_moving_fifo_head =
      rec_fifo_ptr->num_packets_processed_since_moving_fifo_head; /* Fetch      */
                                                              /* for later use. */

#if defined(CONFIG_BGP_STATISTICS)
      {
      unsigned int used_space = (fifo_ptr->va_tail >= fifo_ptr->va_head)
                 ? ( ((unsigned)(fifo_ptr->va_tail) - (unsigned)(fifo_ptr->va_head)) >> 4 )
                 : (fifo_ptr->fifo_size + ( ((unsigned)(fifo_ptr->va_tail) - (unsigned)(fifo_ptr->va_head)) >> 4 ) )
                 ;
                 reception_fifo_histogram[fls(used_space)] += 1 ;
      }
#endif
    /*
     * Loop processing packets until the fifo is empty or until the fruitful poll
     * limit is reached.
     * At the top of the loop, we have a new snapshot of the tail, so something
     * may have appeared in the fifo.
     */
    while ( ( rc == 0 ) &&
	    ( va_tail != va_head ) &&
	    ( fruitfulPollCount > 0) ) { /* Is there something in this fifo?  */
                                         /* Yes...                            */
      fruitfulPollCount--; /* Count the polls */

      /*
       * MSYNC before we look at the data in the fifo to ensure that snoops
       * issued by the DMA have completed.  This ensures the L1 cache
       * invalidations have completed so we don't look at stale data.
       */
      _bgp_msync();

      /*
       * Touch the first packet right away so it is is loaded into the memory
       * cache before we try to use it.
       */
      _bgp_dcache_touch_line( va_head );

      /*
       * Prepare to split up the processing between "normal" and "handleWrap".
       * Establish a "logicalTail" which is the point beyond which "normal"
       * processing changes to "handleWrap" processing.
       */
      if ( va_head < va_tail ) { /* No wrap will occur? */
	wrap            = 0;
	va_logical_tail = va_tail; /* Logical tail is the physical tail */
      }
      else { /* Wrap will occur.  Logical tail is 256 bytes before the end
	      * of the fifo.  We need to stop normal phase 1 there because
	      * that is the first point at which the next packet could wrap.
	      */
	wrap             = 1;
	va_logical_tail  = (void*)( ((unsigned)fifo_ptr->va_end) - 256 );
      }

      /* Loop processing packets until we hit our tail snapshot */
      while ( ( rc == 0 ) &&
	      ( va_head != va_tail ) ) {
	/*
	 * Process packets that do not wrap.  This is everything up to the
	 * logical tail.  This gets executed both before and after wrapping.
	 * This is normal phase 1 and normal phase 2.
	 */
	va_starting_head = va_head;

	while ( ( rc == 0 ) &&
		( va_head  < va_logical_tail ) ) {

	  packet_ptr    = (DMA_PacketHeader_t*)va_head;
	  packet_bytes  = (packet_ptr->Chunks + 1) << 5;

	  /*
	   * Touch the NEXT packet to ensure it will be in L1 cache when we
	   * are ready for it on the next iteration.  Even though the packet will
	   * likely be touched in its entirety by the receive function, and that
	   * will likely cause the processor to perform prefetching of the next
	   * packet, bringing in the next packet now has been shown to improve
	   * bandwidth from 1.41 bytes/cycle to 1.44 bytes/cycle, so we put
	   * this dcbt here.
	   */
	  va_nextHead = (void*) ( (unsigned)va_head + packet_bytes );

	  if ( va_nextHead < va_logical_tail )
	    _bgp_dcache_touch_line( va_nextHead );

	  /*
	   * Determine the receive function to call.
	   * The packet header Func_Id contains the ID of the function to call.
	   * We cache the previous packet's values because it is likely this
	   * packet will be the same.  If not, call out of line function to
	   * re-prime the cache.
	   */
	  if ( packet_ptr->Func_Id != recv_func_id ) {
	    recv_func_id = packet_ptr->Func_Id;
	    DMA_RecFifoPollPrimeRecvFuncCache( recv_func_id,
					       &recv_func_ptr,
					       &recv_func_parm );
	  }

	  /* Call the receive function, and no matter what happens, increment
	   * the number of packets processed and move our head snapshot to the
	   * next packet.
	   */
	    if( recv_func_ptr)
	      {
	          rc = (*recv_func_ptr)( rec_fifo_ptr,
	                                 packet_ptr,
	                                 recv_func_parm,
	                                 (char*)((unsigned)packet_ptr + sizeof(DMA_PacketHeader_t)),
	                                 packet_bytes - sizeof(DMA_PacketHeader_t) );
	      }
	    else
	      {
	        printk(KERN_ERR "DMA_RecFifoSimplePollNormalFifos recv_func_ptr was NULL recv_func_id=%02x rec_fifo_ptr=%p packet_ptr=%p recv_func_parm=%p recv_func_payload=%p length=%d\n",
	            recv_func_id,rec_fifo_ptr,packet_ptr,recv_func_parm,(char*)((unsigned)packet_ptr + sizeof(DMA_PacketHeader_t)),packet_bytes - sizeof(DMA_PacketHeader_t)) ;

	      }
	  num_packets_processed_since_moving_fifo_head++;
	  num_packets_in_fifo++;

#ifdef DEBUG_PRINT
	printf("SimplePollById: num_processed=%d, va_head=0x%08x, va_tail=0x%08x, va_logical_tail=0x%08x, va_end=0x%08x, willWrap=%d\n",num_processed,(unsigned)va_head,(unsigned)va_tail,(unsigned)va_logical_tail,(unsigned)fifo_ptr->va_end,wrap);
#endif

  	  va_head = va_nextHead;

	} /* End: Process packets that do not wrap */

	/*
	 * We are done processing all packets prior to the wrap.
	 * If the shadow va_head is not in sync with the hardware head, or if
	 * we are going to wrap, sync up the hardware head and recalculate the
	 * free space.  The movement of the head causes the fifo's free space
	 * to be recalculated.
	 *
	 * The wrap function requires that the shadow and hardware heads be in
	 * sync.  If we are not wrapping, we condition the syncing of the heads
	 * on whether we have exceeded our limit on the number of packets we
	 * processed in a fifo since the last time we moved the
	 * hardware head.  If we have only processed a few packets, we just
	 * leave the hardware head where it is and don't incur the expense of
	 * moving the hardware head.  If we have processed at least our limit
	 * of packets, then it is good to move the hardware head.
	 */
	if ( ( num_packets_processed_since_moving_fifo_head >
	       DMA_MAX_NUM_PACKETS_BEFORE_MOVING_HEAD ) ||
	     ( wrap ) ) {

	  DMA_FifoSetHead( fifo_ptr, va_head );

	  num_packets_processed_since_moving_fifo_head = 0;
	}

	/*
	 * If we are anticipating a wrap, go handle the wrap.
	 */
	if ( ( rc == 0 ) && wrap ) {
	  /*
	   * Handle the wrapping of the fifo.  This requires extra checking
	   * and moving of the head, and thus is in its own function.
	   * It is a generic function, used by other poll functions.  Some of
	   * these other poll functions have the ability to quit processing
	   * packets when a specified limit is reached overall, or per fifo.
	   * That is what the last two parameters specify.  For this poll
	   * function, we don't have any limit...we process packets until the
	   * fifo is empty, so we pass in large unreachable limits.
	   */
	  rc = DMA_RecFifoPollProcessWrap (
			   rec_fifo_ptr,
			   &va_head,
			   va_tail,
			   &num_processed,
			   &num_processed_in_fifo,
			   0x7FFFFFFF, /* Infinite packet limit, overall */
			   0x7FFFFFFF);/* Infinite packet limit per fifo */

	  va_logical_tail = va_tail;     /* Set to actual tail now.        */
	  wrap = 0;     /* Next time around, don't do wrap processing.     */
	}

      } /* End: Process packets until we hit our snapshotted tail */

#if defined(BGP_DD1_WORKAROUNDS)
      NumEmptyPollFunctionCalls = 0; /* The DMA must be active.  It has    */
                                     /* likely not encountered a fifo full */
	                             /* condition and stopped.  Reset the  */
	                             /* fifo counter so we will start      */
	                             /* tracking empty calls to poll.      */
#endif

      va_tail = DMA_FifoGetTailNoFreeSpaceUpdate( fifo_ptr ); /* Snapshot HW */
                                                              /* tail again. */

    } /* End: Loop while there is something in the fifo */

    /*
     * The fifo is now empty.  If we have processed at least one packet,
     * return the number, or if the receive function returned an error,
     * return that return code.
     */
    if ( num_packets_in_fifo > 0 ) {
      /* Store in the fifo structure the number of packets processed since
       * last moving the hardware head, and the current head */
      rec_fifo_ptr->num_packets_processed_since_moving_fifo_head =
	num_packets_processed_since_moving_fifo_head;
      fifo_ptr->va_head = va_head;
      num_processed += num_packets_in_fifo;
      /* Clear the threshold crossed condition, in case we have gone below
       * the threshold.
       */
      DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
					   _BN(rec_fifo_ptr->global_fifo_id),
					   0 );

      /* If the receive function returned an error, exit with that error now */
      if ( rc )	return (rc);
    }
    /*
     * We exited the loop processing the fifo_index fifo.
     * - If we exited because the fifo was empty according to our snapshot
     *   of the fifo's tail (head == tail snapshot), we want to turn off this
     *   fifo's not-empty status in our shadow copy of the status so we
     *   process all of the other fifos before re-fetching the true status and
     *   tail for this fifo, giving this fifo another chance.
     */
    not_empty_status &= ~(fifo_bit_number);

#ifdef DEBUG_PRINT
    printf("PollNormal: Turning off status bit 0x%08x, status=0x%08x\n",fifo_bit_number,not_empty_status);
#endif

    /* Bump to next fifo */
    fifo_index = (fifo_index+1) % num_fifos_in_group;

  } /* End: for loop processing reception fifos */

} /* End: DMA_RecFifoSimplePollNormalFifos() */




/*!
 * \brief Simple Poll Normal Reception Fifo Given a Fifo Group and Fifo ID
 *
 * Poll the specified "normal" reception fifo in the specified fifo group,
 * removing one packet after another from the fifo, dispatching the appropriate
 * receive function for each packet, until one of the following occurs:
 * 1.  All packets in the fifo have been received.
 * 2.  The specified fifo is empty.
 * 3.  A receive function returns a non-zero value.
 * 4.  The last packet removed from the fifo has an invalid registration id. The
 *     error receive function will have been called, but polling ends.
 *     The invalid packet is counted as a processed packet, and the return
 *     code from the error receive function is returned.
 * 5.  There have been fruitfulPollLimit polls attempted.
 *
 * If the specified fifo has a packet, the appropriate receive function is
 * called.  Upon return, the packet is removed from the fifo (the fifo head is
 * moved past the packet).
 *
 * After processing all of the packets in the fifo (emptying that fifo),
 * or the fruitfulPollLimit has been reached, the function returns the number
 * of packets processed.
 *
 * It is risky to set the fruitfulPollLimit to zero, allowing this function to
 * poll indefinitely as long as there are packets to be processed.  This may
 * starve the node in a scenario where other nodes send "polling" packets to
 * our node, and our node never gets a chance to do anything else except
 * process those polling packets.
 *
 * The receive functions must be registered through the
 * DMA_RecFifoRegisterRecvFunction interface.  The receive function is
 * called with a pointer to the packet header, pointer to the payload, and
 * length of the payload.  The packet header is always be 16 bytes of
 * contiguous storage, in the fifo.  Because the fifo is a circular buffer,
 * the payload of a packet may wrap from the end of the fifo to the beginning.
 * For large fifos, this happens infrequently.  To make it easier for
 * user/messaging code, the poll function will always pass a starting payload
 * address and number of bytes so that the receive function can treat the packet
 * as contiguous storage in memory.  If the packet does not wrap, the starting
 * payload address will be a pointer to the appropriate address in the fifo.
 * If the packet does wrap, the poll function will copy bytes from the fifo to
 * a contiguous buffer (on the stack) and call the receive function with a
 * payload pointer pointing to this temporary buffer.  In either case, when the
 * receive function returns, user code cannot assume that the payload buffer is
 * permanent, i.e., after return, it may be overwritten by either the DMA or
 * the poll function.  To keep a copy of the packet, the receive function has
 * to copy it to some other location.  The packet header and payload are
 * 16-byte aligned for optimized copying.
 *
 * \param[in]  fifo_id           The ID of the fifo to be polled.
 *                               (0 through
 *                               DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP-1).
 * \param[in]  fg_ptr            Pointer to the fifo group.
 * \param[in]  fruitfulPollLimit  The limit on the number of fruitful polls that
 *                                will be attempted.
 *                                If the limit is reached, this function
 *                                returns.  A value of zero means there is no
 *                                limit imposed.  A fruitful poll is one where
 *                                at least one packet has arrived in the fifo
 *                                since the last poll.
 *
 * \retval  num_packets_received  The number of packets received and processed.
 * \retval  negative_value        The return code from the receive function that
 *                                caused polling to end.
 *
 * \pre  The caller is responsible for disabling interrupts before invoking this
 *       function.
 *
 */
int DMA_RecFifoSimplePollNormalFifoById( int                 fifo_id,
					 DMA_RecFifoGroup_t *fg_ptr,
					 int                 fruitfulPollLimit
				       )
{
  int rc = 0;                              /* Return code from recv_func.     */
  int num_processed = 0;                   /* Number of packets processed     */
  int num_processed_in_fifo = 0;           /* Not used, but needed for calling*/
                                           /* wrap function.                  */
  int fruitfulPollCount;                   /* Number of fruitful polls.       */

  /*
   *The following is actually a cache of the last receive function called.
   * We cache it so we don't need to keep looking up the receive function
   * info on each packet.
   */
  DMA_RecFifoRecvFunction_t recv_func_ptr=NULL; /* Pointer to receive function*/
  void                     *recv_func_parm=NULL;/* Receive function parameter */
  int                       recv_func_id=-1;  /* Function ID from the packet  */
                                              /* header. Init to -1 means     */
                                              /* recv_func_ptr and            */
                                              /* recv_func_parm do not cache  */
                                              /* the previous packet values.  */

  DMA_PacketHeader_t *packet_ptr;          /* Pointer to packet header        */
  unsigned int        packet_bytes;        /* Number of bytes in the packet.  */
  unsigned int        wrap;                /* 1: A wrap of the fifo is going  */
                                           /*    to occur.                    */
                                           /* 0: No wrap is going to occur.   */

  /*
   * Processing of packets occurs in the fifo in three phases:
   * Normal Phase 1   :  Packets before the wrap.
   * Handle Wrap Phase:  Packets during the wrap.
   * Normal Phase 2   :  Packets after the wrap.
   */
  void *va_logical_tail;                   /* The point beyond which normal   */
                                           /* processing of packets ends.     */
  void *va_starting_head;                  /* Pointer to the first packet in  */
                                           /* a contiguous group extracted    */
                                           /* from the fifo.                  */
  void *va_nextHead;                       /* Pointer to the next packet to   */
                                           /* be processed.                   */
  void *va_tail;                           /* Snapshot of the fifo's tail.    */
  unsigned int num_packets_processed_since_moving_fifo_head; /*
				      	      Tells us when we should move the
                                              hardware head.                  */

  SPI_assert( fg_ptr           != NULL );
  SPI_assert( ( fifo_id >= 0 ) &&
	      ( fifo_id <  DMA_NUM_NORMAL_REC_FIFOS_PER_GROUP ) );
  /*
   * Start the fruitful poll count at the max.
   * For unlimited, set to a very high value.
   */
  fruitfulPollCount = (fruitfulPollLimit == 0) ? 0x7FFFFFFF : fruitfulPollLimit;

  /*
   * Establish pointers to the reception fifo and the DMA fifo.
   * Snapshot the hardware head and tail pointers...they may change while we
   * are running.  We will snapshot the tail again after processing everything
   * up to this snapshot, until the fifo is empty (head == tail).
   */
  DMA_RecFifo_t *rec_fifo_ptr = &(fg_ptr->fifos[fifo_id]);
  DMA_Fifo_t    *fifo_ptr     = &(rec_fifo_ptr->dma_fifo);
  void          *va_head      = fifo_ptr->va_head;
  va_tail      = DMA_FifoGetTailNoFreeSpaceUpdate( fifo_ptr ); /* Snapshot HW */
                                                               /* tail.       */
  num_packets_processed_since_moving_fifo_head =
      rec_fifo_ptr->num_packets_processed_since_moving_fifo_head; /* Fetch    */
                                                            /* for later use. */

#if defined(CONFIG_BGP_STATISTICS)
      {
      unsigned int used_space = (fifo_ptr->va_tail >= fifo_ptr->va_head)
                 ? ( ((unsigned)(fifo_ptr->va_tail) - (unsigned)(fifo_ptr->va_head)) >> 4 )
                 : (fifo_ptr->fifo_size + ( ((unsigned)(fifo_ptr->va_tail) - (unsigned)(fifo_ptr->va_head)) >> 4 ) )
                 ;
                 reception_fifo_histogram[fls(used_space)] += 1 ;
      }
#endif
  /*
   * Loop processing packets until the fifo is empty or the fruitfulPollLimit
   * has been reached.
   * At the top of the loop, we have a new snapshot of the tail, so something
   * may have appeared in the fifo.
   */
  while ( ( rc == 0 ) &&
	  ( va_tail != va_head ) &&
	  ( fruitfulPollCount > 0 ) ) { /* Is there something in this fifo?   */
                                        /* Yes...                             */
    fruitfulPollCount--; /* Count the polls */

    /*
     * MSYNC before we look at the data in the fifo to ensure that snoops
     * issued by the DMA have completed.  This ensures the L1 cache
     * invalidations have completed so we don't look at stale data.
     */
    _bgp_msync();

    /*
     * Touch the first packet right away so it is is loaded into the memory
     * cache before we try to use it.
     */
    _bgp_dcache_touch_line( va_head );

    /*
     * Prepare to split up the processing between "normal" and "handleWrap".
     * Establish a "logicalTail" which is the point beyond which "normal"
     * processing changes to "handleWrap" processing.
     */
    if ( va_head < va_tail ) { /* No wrap will occur? */
      wrap            = 0;
      va_logical_tail = va_tail; /* Logical tail is the physical tail */
    }
    else { /* Wrap will occur.  Logical tail is 256 bytes before the end
	    * of the fifo.  We need to stop normal phase 1 there because
	    * that is the first point at which the next packet could wrap.
	    */
      wrap             = 1;
      va_logical_tail  = (void*)( ((unsigned)fifo_ptr->va_end) - 256 );
    }

    /* Loop processing packets until we hit our tail snapshot */
    while ( ( rc == 0 ) &&
	    ( va_head != va_tail ) ) {
      /*
       * Process packets that do not wrap.  This is everything up to the
       * logical tail.  This gets executed both before and after wrapping.
       * This is normal phase 1 and normal phase 2.
       */
      va_starting_head = va_head;

      while ( ( rc == 0 ) &&
	      ( va_head  < va_logical_tail ) ) {

	packet_ptr    = (DMA_PacketHeader_t*)va_head;
	packet_bytes  = (packet_ptr->Chunks + 1) << 5;

	/*
	 * Touch the NEXT packet to ensure it will be in L1 cache when we
	 * are ready for it on the next iteration.  Even though the packet will
	 * likely be touched in its entirety by the receive function, and that
	 * will likely cause the processor to perform prefetching of the next
	 * packet, bringing in the next packet now has been shown to improve
	 * bandwidth from 1.41 bytes/cycle to 1.44 bytes/cycle, so we put
	 * this dcbt here.
	 */
	va_nextHead = (void*) ( (unsigned)va_head + packet_bytes );

	if ( va_nextHead < va_logical_tail )
	  _bgp_dcache_touch_line( va_nextHead );

	/*
	 * Determine the receive function to call.
	 * The packet header Func_Id contains the ID of the function to call.
	 * We cache the previous packet's values because it is likely this
	 * packet will be the same.  If not, call out of line function to
	 * re-prime the cache.
	 */
	if ( packet_ptr->Func_Id != recv_func_id ) {
	  recv_func_id = packet_ptr->Func_Id;
	  DMA_RecFifoPollPrimeRecvFuncCache( recv_func_id,
					     &recv_func_ptr,
					     &recv_func_parm );
	}

	/* Call the receive function, and no matter what happens, increment
	 * the number of packets processed and move our head snapshot to the
	 * next packet.
	 */
	SPI_assert ( recv_func_ptr != NULL );

        if( recv_func_ptr)
          {
            rc = (*recv_func_ptr)( rec_fifo_ptr,
                                   packet_ptr,
                                   recv_func_parm,
                                   (char*)((unsigned)packet_ptr + sizeof(DMA_PacketHeader_t)),
                                   packet_bytes - sizeof(DMA_PacketHeader_t) );
          }
        else
          {
            printk(KERN_ERR "DMA_RecFifoSimplePollNormalFifoById recv_func_ptr was NULL recv_func_id=%02x rec_fifo_ptr=%p packet_ptr=%p recv_func_parm=%p recv_func_payload=%p length=%d\n",
                recv_func_id,rec_fifo_ptr,packet_ptr,recv_func_parm,(char*)((unsigned)packet_ptr + sizeof(DMA_PacketHeader_t)),packet_bytes - sizeof(DMA_PacketHeader_t)) ;

          }
	num_processed++;
	num_packets_processed_since_moving_fifo_head++;

#ifdef DEBUG_PRINT
	printf("SimplePollById: num_processed=%d, va_head=0x%08x, va_tail=0x%08x, va_logical_tail=0x%08x, va_end=0x%08x, willWrap=%d\n",num_processed,(unsigned)va_head,(unsigned)va_tail,(unsigned)va_logical_tail,(unsigned)fifo_ptr->va_end,wrap);
#endif

	va_head = va_nextHead;

      } /* End: Process packets that do not wrap */

      /*
       * We are done processing all packets prior to the wrap.
       * If the shadow va_head is not in sync with the hardware head, or if
       * we are going to wrap, sync up the hardware head and recalculate the
       * free space.  The movement of the head causes the fifo's free space
       * to be recalculated.
       *
       * The wrap function requires that the shadow and hardware heads be in
       * sync.  If we are not wrapping, we condition the syncing of the heads
       * on whether we have exceeded our limit on the number of packets we
       * processed in a fifo since the last time we moved the
       * hardware head.  If we have only processed a few packets, we just
       * leave the hardware head where it is and don't incur the expense of
       * moving the hardware head.  If we have processed at least our limit
       * of packets, then it is good to move the hardware head.
       */
      if ( ( num_packets_processed_since_moving_fifo_head >
	     DMA_MAX_NUM_PACKETS_BEFORE_MOVING_HEAD ) ||
	   ( wrap ) ) {

	DMA_FifoSetHead( fifo_ptr, va_head );

	num_packets_processed_since_moving_fifo_head = 0;
      }

      /*
       * If we are anticipating a wrap, go handle the wrap.
       */
      if ( ( rc == 0 ) && wrap ) {
	/*
	 * Handle the wrapping of the fifo.  This requires extra checking
	 * and moving of the head, and thus is in its own function.
	 * It is a generic function, used by other poll functions.  Some of
	 * these other poll functions have the ability to quit processing
	 * packets when a specified limit is reached overall, or per fifo.
	 * That is what the last two parameters specify.  For this poll
	 * function, we don't have any limit...we process packets until the
	 * fifo is empty, so we pass in large unreachable limits.
	 */
	rc = DMA_RecFifoPollProcessWrap (
			   rec_fifo_ptr,
			   &va_head,
			   va_tail,
			   &num_processed,
			   &num_processed_in_fifo,
			   0x7FFFFFFF, /* Infinite packet limit, overall */
			   0x7FFFFFFF);/* Infinite packet limit per fifo */

	va_logical_tail = va_tail;     /* Set to actual tail now.        */
	wrap = 0;     /* Next time around, don't do wrap processing.     */
      }

    } /* End: Process packets until we hit our snapshotted tail */

#if defined(BGP_DD1_WORKAROUNDS)
    NumEmptyPollFunctionCalls = 0; /* The DMA must be active.  It has    */
 	                           /* likely not encountered a fifo full */
	                           /* condition and stopped.  Reset the  */
	                           /* fifo counter so we will start      */
	                           /* tracking empty calls to poll.      */
#endif

    va_tail = DMA_FifoGetTailNoFreeSpaceUpdate( fifo_ptr ); /* Snapshot HW */
                                                            /* tail again. */

  } /* End: Loop while there is something in the fifo */

  /*
   * The fifo is now empty.  If we have processed at least one packet,
   * return the number, or if the receive function returned an error,
   * return that return code.
   * Also, clear the reception fifo threshold crossed interrupt condition.
   */
  if ( num_processed > 0 ) {
    /* Store in the fifo structure the number of packets processed since
     * last moving the hardware head, and the current head */
    rec_fifo_ptr->num_packets_processed_since_moving_fifo_head =
      num_packets_processed_since_moving_fifo_head;
    fifo_ptr->va_head = va_head;
    DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
					 _BN(rec_fifo_ptr->global_fifo_id),
					 0 );

    if ( rc == 0 ) return (num_processed);
    else return (rc);
  }

  /*
   * We didn't process any packets.  This could be because the rDMA has
   * shut-down (a DD1 hardware behavior) because the reception fifo became full.
   * We count the number of times we consecutively come up empty, and reactivate
   * the rDMA via a system call.
   */
  else {

#if defined(BGP_DD1_WORKAROUNDS)
    if ( (NumEmptyPollFunctionCalls >= 0) && /* We are tracking empty calls? */
	 (++NumEmptyPollFunctionCalls >= NUM_EMPTY_POLL_FUNCTION_CALL_LIMIT) ) {
       /*  printf("Hit Empty Poll Limit...invoking syscall to clear full condition\n"); */
      rc = Kernel_ClearFullReceptionFifo(); /* Activate rDMA in case the */
                                            /* reception fifos filled and the */
	                                    /* DMA has stopped.               */
       /*  printf("Returned from ClearFull syscall with rc=%d\n",rc); */
      NumEmptyPollFunctionCalls = -1; /* The DMA is active.  Reset the    */
	                              /* fill-fifo counter.               */
    }
#endif

    return (0); /* Return no packets processed */
  }

} /* End: DMA_RecFifoSimplePollNormalFifoById() */


/*!
 * \brief Poll Header Reception Fifo Given a Fifo Group
 *
 * Poll the "header" reception fifo in the specified fifo group,
 * removing one packet after another from the fifo, dispatching the appropriate
 * receive function for each packet, until one of the following occurs:
 * 1.  Total_packets packets are received
 * 2.  The specified fifo is empty
 * 3.  A receive function returns a non-zero value
 *
 * If the header fifo has a packet, the appropriate receive function is
 * called.  Upon return, the packet is removed from the fifo (the fifo head is
 * moved past the packet).
 *
 * After processing num_packets packets in the fifo (or emptying that fifo),
 * the function returns the number of packets processed.
 *
 * The receive function must be registered through the
 * DMA_RecFifoRegisterRecvFunction interface.  The receive function is
 * called with a pointer to the packet header. The packet header is always
 * 16 bytes of contiguous storage, in the fifo.  When the
 * receive function returns, user code cannot assume that the buffer is
 * permanent, i.e., after return, it may be overwritten by either the DMA or
 * the poll function.  To keep a copy of the packet, the receive function would
 * have to copy it to some other location.  The packet header is 16-byte aligned
 * for optimized copying.
 *
 * \param[in]  num_packets       The maximum number of packets that will be
 *                               processed.
 * \param[in]  num_empty_passes    When the not-empty status indicates that all
 *                                 fifos in the group are emtpy, this is the
 *                                 number of times the not-empty status is
 *                                 re-fetched and re-checked before officially
 *                                 declaring that they are indeed empty.
 *                                 (0 means no extra passes are made).
 * \param[in]  not_empty_poll_delay  The number of pclks to delay between polls
 *                                   of the not-empty status when the fifos are
 *                                   empty.
 * \param[in]  fg_ptr            Pointer to the fifo group.
 *
 * \retval  num_packets_received  The number of packets received and processed.
 * \retval  negative_value        The return code from the receive function that
 *                                caused polling to end.
 *
 * \pre  The caller is responsible for disabling interrupts before invoking this
 *       function.
 *
 */
int DMA_RecFifoPollHeaderFifo( int                 num_packets,
			       int                 num_empty_passes,
			       int                 not_empty_poll_delay,
			       DMA_RecFifoGroup_t *fg_ptr
			     )
{
  int fifo_index;                          /* Index of fifo being processed   */
  int num_packets_in_fifo;                 /* Count of packets processed in a */
					   /* fifo.                           */
  unsigned int status;                     /* Snapshot of the not empty status*/
                                           /* for this group.                 */
  int rc = 0;                              /* Return code from recv_func.     */
  int num_processed = 0;                   /* Number of packets processed     */
  DMA_PacketIovec_t io_vec;                /* Payload I/O vector              */
  DMA_RecFifoRecvFunction_t recv_func_ptr; /* Pointer to receive function     */
  void                     *recv_func_parm;/* Receive function parameter      */
  DMA_RecFifo_t *fifo_ptr;                 /* Pointer to fifo being processed */
  DMA_PacketHeader_t *packet_ptr;          /* Pointer to packet header        */
  int passes;                              /* Counter of not-empty passes     */

  SPI_assert( num_packets       > 0 );
  SPI_assert( num_empty_passes  >= 0 );
  SPI_assert( fg_ptr           != NULL );


  fifo_index = DMA_HEADER_REC_FIFO_ID;     /* We are working with the header  */
                                           /* fifo.                           */
  fifo_ptr = &(fg_ptr->fifos[fifo_index]);

  /*
   * Loop until the header fifo is declared empty, or
   * until one of the termination conditions documented in the prolog occurs.
   *
   */
  for (;;)
    {
      /*
       * If the DMA SRAM not-empty status for this fifo is zero (the fifo is
       * empty), the status is checked num_empty_passes times with a slight
       * delay in between to give the DMA time to make progress before declaring
       * that the fifo is truely empty.
       */
      passes = num_empty_passes;
      status = DMA_RecFifoGetNotEmptyById( fg_ptr,
					   fifo_index ); /* Get Header fifo   */
                                                         /* not-empty status. */
      while ( ( status == 0 ) &&
	      ( num_empty_passes-- > 0 ) )
	{
	  /* Delay, allowing the DMA to update its status */
	  unsigned int pclks = not_empty_poll_delay;
	  while( pclks-- )
	    {
	      asm volatile("nop;");
	    }

	  /* Re-fetch the not-empty status */
	  status = DMA_RecFifoGetNotEmptyById(
					    fg_ptr,
				  	    fifo_index ); /* Get Header fifo  */
	                                                  /* not-empty status.*/
	}

      if ( status == 0 ) {       /* Fifo is empty?                             */

#if defined(BGP_DD1_WORKAROUNDS)
	if (num_processed > 0) { /* Did we process at least 1 packet?          */
	  NumEmptyPollFunctionCalls = 0; /* The DMA must be active.  It has    */
	                                 /* likely not encountered a fifo full */
 	                                 /* condition and stopped.  Reset the  */
    	                                 /* fifo counter so we will start      */
                                         /* tracking empty calls to poll.      */
	}
	else {
	  if ( (NumEmptyPollFunctionCalls >= 0) && /* We are tracking empty calls? */
	       (++NumEmptyPollFunctionCalls >= NUM_EMPTY_POLL_FUNCTION_CALL_LIMIT) ) {
	     /*  printf("Hit Empty Poll Limit...invoking syscall to clear full condition\n"); */
	    rc = Kernel_ClearFullReceptionFifo(); /* Activate rDMA in case the */
                                             /* reception fifos filled and the */
                                             /* DMA has stopped.               */
	     /*  printf("Returned from ClearFull syscall with rc=%d\n",rc); */
	    NumEmptyPollFunctionCalls = -1; /* The DMA is active.  Reset the    */
                                            /* fill-fifo counter.               */
	  }
	}
#endif

	return (num_processed);
      }

      /* The fifo has something in it.
       * Update its shadow va_tail pointer to reflect the amount of packet
       * data in the fifo.
       */
      DMA_RecFifoGetTailById( fg_ptr,
			      fifo_index );

      num_packets_in_fifo = 0;

      /*
       * MSYNC before we look at the data in the fifo to ensure that snoops
       * issued by the DMA have completed.  This ensures the L1 cache
       * invalidations have completed so we don't look at stale data.
       */
      _bgp_msync();

      /*
       * Within a fifo: The area between the va_head and va_tail shadow pointers
       * contains packets to be processed.  Loop, processing those packets until
       * we have processed packets_per_fifo of them, or all of them, or other
       * issues come up.
       *
       */
      while ( ( num_packets_in_fifo < num_packets ) &&
	      ( fifo_ptr->dma_fifo.va_head != fifo_ptr->dma_fifo.va_tail ) )
	{
	  DMA_RecFifoGetAddresses( fifo_ptr,
				   &io_vec ); /* Get the payload pointer(s)   */
      	                                      /* for the packet at the head   */
                                              /* of the fifo.                 */

	  packet_ptr = (DMA_PacketHeader_t*)
	                 fifo_ptr->dma_fifo.va_head; /* Point to packet header*/

	  /* Determine the receive function to call */
	  recv_func_ptr = DMA_RecFifoInfo.headerRecvFunction;
	  if ( recv_func_ptr != NULL )
	    {
	      recv_func_parm = DMA_RecFifoInfo.headerRecvFunctionParm;
	    }
	  else
	    {
	      recv_func_ptr  = DMA_RecFifoInfo.errorRecvFunction;
	      recv_func_parm = DMA_RecFifoInfo.errorRecvFunctionParm;
	    }

	  /* Call the receive function */
	        if( recv_func_ptr)
	          {
	            rc = (*recv_func_ptr)(fifo_ptr,
	                                  packet_ptr,
	                                  recv_func_parm,
	                                  NULL, /* No payload */
	                                  0);   /* No payload bytes */
	          }
	        else
	          {
	            printk(KERN_ERR "DMA_RecFifoPollHeaderFifo recv_func_ptr was NULL rfifo_ptr=%p packet_ptr=%p recv_func_parm=%p recv_func_payload=%p length=%d\n",
	                fifo_ptr,packet_ptr,recv_func_parm,NULL,0) ;

	          }

	  DMA_RecFifoIncrementHead(fifo_ptr,
				   1);/* Increment head by 16 bytes   */

	  num_processed++;

	  if ( rc != 0 ) /* Did receive function fail? */
	    {
	      /* Clear the threshold crossed condition, in case we have gone below
	       * the threshold.
	       */
	      DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
						   _BN(fifo_ptr->global_fifo_id),
						   0 );
	      return (rc); /* Yes...return that return code */
	    }

	  if ( num_processed >= num_packets ) /* Got what they wanted? */
	    {
	      /* Clear the threshold crossed condition, in case we have gone below
	       * the threshold.
	       */
	      DMA_RecFifoSetClearThresholdCrossed( fg_ptr,
						   _BN(fifo_ptr->global_fifo_id),
						   0 );
	      return (num_processed); /* Yes...all done */
	    }

	  num_packets_in_fifo++;

	} /* End: Process up to packets_per_fifo packets in this fifo */

    } /* End: Keep looping through the fifo. */

} /* End: DMA_RecFifoPollHeaderFifo() */

EXPORT_SYMBOL(DMA_RecFifoRegisterRecvFunction) ;
EXPORT_SYMBOL(DMA_RecFifoGetFifoGroup) ;
EXPORT_SYMBOL(DMA_RecFifoPollNormalFifoById) ;
#if defined(CONFIG_BGP_STATISTICS)
EXPORT_SYMBOL(reception_fifo_histogram) ;
EXPORT_SYMBOL(reception_hi_watermark) ;
#endif

