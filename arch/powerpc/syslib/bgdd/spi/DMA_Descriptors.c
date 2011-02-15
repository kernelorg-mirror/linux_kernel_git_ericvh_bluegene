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
/*!
 * \file DMA_Descriptors.c
 *
 * \brief Implementations for Functions defined in bgp/arch/include/spi/DMA_Descriptors.h
 */
#include <linux/version.h>
#include <linux/module.h>

#ifndef __LINUX_KERNEL__

#include <bpcore/bgp_types.h>

/*!
 * \brief For kernel_interface.h so that rts_get_personality gets defined
 */
#define SPI_DEPRECATED 1
#include <spi/kernel_interface.h>

#include <spi/DMA_Descriptors.h>
#include <spi/DMA_Counter.h>
#include <spi/DMA_InjFifo.h>
#include <spi/DMA_RecFifo.h>

#include <spi/DMA_Assert.h>

#ifdef __CNK__
#include <cnk/PersUtils.h>
#endif

#else

#include <spi/linux_kernel_spi.h>

#endif /* ! __LINUX_KERNEL__ */


/*!
 * \brief Static Info from Personality
 *
 * The following structure defines information from the personality.
 * They are intended to be static so, once the info is retrieved from
 * the personality, it does not need to be retrieved again (it is a
 * system call to retrieve personality info).
 *
 * It is assumed that this is initialized to zero when the program is
 * loaded.
 *
 */
static DMA_PersonalityInfo_t personality_info;


/*!
 * \brief Get Personality Information
 *
 * Gets personality information into the "personality_info" static structure.
 *
 * \post The personality information is retrieved into the structure
 *
 */
void DMA_GetPersonalityInfo(void)
{
  _BGP_Personality_t *pers_ptr;

#ifndef __CNK__

  _BGP_Personality_t pers;

  rts_get_personality( &pers,
		       sizeof(pers) );

  pers_ptr = &pers;

#else

  pers_ptr = _bgp_GetPersonality();

#endif

  personality_info.nodeXCoordinate     = pers_ptr->Network_Config.Xcoord;
  personality_info.nodeYCoordinate     = pers_ptr->Network_Config.Ycoord;
  personality_info.nodeZCoordinate     = pers_ptr->Network_Config.Zcoord;
  personality_info.xNodes              = pers_ptr->Network_Config.Xnodes;
  personality_info.yNodes              = pers_ptr->Network_Config.Ynodes;
  personality_info.zNodes              = pers_ptr->Network_Config.Znodes;

  _bgp_msync(); /* Ensure the info has been stored before setting the flag */
  personality_info.personalityRetrieved = 1;
  _bgp_msync();
}


/*!
 * \brief Create a DMA Descriptor For a Torus Direct Put Message
 *
 * A torus direct put message is one that is sent to another node and its data
 * is directly put into memory by the DMA on the destination node...it does
 * not go into a reception fifo.
 *
 * A torus direct-put DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 0
 *   - local_memcopy   = 0
 *   - idma_counterId  = Injection counter ID associated with the data being
 *                       sent.  This counter contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = Message length.  Set to caller's msg_len.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = DMA_CSUM_SKIP.
 *   - Sk              = DMA_CSUM_BIT.
 *   - Hint            = Set to caller's "hints".
 *   - Pid0, Pid1      = Set based on caller's "recv_ctr_grp_id" (see note).
 *   - Chunks          = Set to largest size consistent with msg_len.
 *   - Dm              = 1 (Indicates a direct-put packet).
 *   - Dynamic         = Set based on caller's "vc".
 *   - VC              = Set to caller's "vc".
 *   - X,Y,Z           = Set to caller's "x", "y", "z".
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = Destination message offset (from the reception
 *                       counter's base address).  Set to caller's recv_offset.
 *   - rDMA_Counter    = Reception counter ID.  This counter is located on the
 *                       destination node and contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       recv_ctr_grp_id and recv_ctr_id.
 *   - Payload_Bytes   = Number of valid bytes in the payload.  Set by iDMA.
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 0.
 *   - iDMA_Fifo_ID    = 0 (not used).
 *   - Func_Id         = 0 (not used).
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      x                The destination's x coordinate (8 bits).
 * \param[in]      y                The destination's y coordinate (8 bits).
 * \param[in]      z                The destination's z coordinate (8 bits).
 * \param[in]      hints            Hint bits for torus routing (6 bits).
 *                                  Each bit corresponds to x+, x-, y+, y-,
 *                                  z+, z-.  If a bit is set, it indicates that
 *                                  the packet wants to travel along the
 *                                  corresponding direction.  If all bits are
 *                                  zero, the hardware calculates the hint bits.
 *                                  Both of x+ and x- cannot be set at the same
 *                                  time...same with y and z.
 * \param[in]      vc               The virtual channel that the packet must go
 *                                  into if it fails to win the bypass
 *                                  arbitration in the receiving node.
 *                                  - 0 = Virtual channel dynamic 0
 *                                  - 1 = Virtual channel dynamic 1
 *                                  - 2 = Virtual channel deterministic bubble
 *                                  - 3 = Virtual channel deterministic priority
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      recv_ctr_grp_id  Reception counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      recv_ctr_id      Reception counter ID (within the recv counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      recv_offset      Offset of the payload from the pa_base
 *                                  associated with the specified reception
 *                                  counter.
 * \param[in]      msg_len          Total message length (in bytes).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, all payload bytes are included in the torus injection
 *       checksum.  In the first byte of the torus hardware packet header,
 *       this corresponds to setting CSum_Skip = 0x8 (16 bytes) and Sk=0.
 *       The defaults can be changed by changing DMA_CSUM_SKIP and
 *       DMA_CSUM_BIT in this include file.
 *
 * \note By default, the packet size is set to the largest value consistent
 *       with the message size.  For example,
 *       - if msg_len >= 209, there will be 8 32-byte chunks in each packet,
 *         with the possible exception of the last packet, which could contain
 *         fewer chunks (209... of payload + 16 header).
 *       - if 177 <= msg_len < 208, there will be 7 chunks in the packet, etc.
 *
 * \note By default, for direct-put DMA messages, the pid0 and pid1 bits in the
 *       torus hardware packet header are determined by the recv_ctr_grp_id:
 *       - if recv_ctr_grp_id = 0 => (pid0,pid1) = (0,0)
 *       - if recv_ctr_grp_id = 1 => (pid0,pid1) = (0,1)
 *       - if recv_ctr_grp_id = 2 => (pid0,pid1) = (1,0)
 *       - if recv_ctr_grp_id = 3 => (pid0,pid1) = (1,1)
 *       Pid0 determines into which physical torus fifo group on the destination
 *       node the packet is put, prior to the dma receiving it.  Other than that,
 *       the only use for the pid bits is for debug, ie, if headers are being
 *       saved.
*/
int  DMA_TorusDirectPutDescriptor(
				  DMA_InjDescriptor_t *desc,
				  unsigned int         x,
				  unsigned int         y,
				  unsigned int         z,
				  unsigned int         hints,
				  unsigned int         vc,
				  unsigned int         inj_ctr_grp_id,
				  unsigned int         inj_ctr_id,
				  unsigned int         send_offset,
				  unsigned int         recv_ctr_grp_id,
				  unsigned int         recv_ctr_id,
				  unsigned int         recv_offset,
				  unsigned int         msg_len
				 )
{
  int c;

  SPI_assert( desc != NULL );
  SPI_assert( (hints & 0x0000003F) == hints );
  SPI_assert( vc <= 3 );
  SPI_assert( inj_ctr_grp_id  < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id      < DMA_NUM_COUNTERS_PER_GROUP );
  SPI_assert( recv_ctr_grp_id < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( recv_ctr_id     < DMA_NUM_COUNTERS_PER_GROUP );

#ifndef NDEBUG

  if ( personality_info.personalityRetrieved == 0 )
    {
      DMA_GetPersonalityInfo();
    }

  SPI_assert( x < personality_info.xNodes );
  SPI_assert( y < personality_info.yNodes );
  SPI_assert( z < personality_info.zNodes );

#endif

  DMA_ZeroOutDescriptor(desc);

  desc->idma_counterId =
    inj_ctr_id + inj_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP); /* 8 bits       */

  desc->base_offset    =  send_offset;
  desc->msg_length     =  msg_len;

  /* Torus Headers */

  desc->hwHdr.CSum_Skip = DMA_CSUM_SKIP;    /* Checksum all but header        */
  desc->hwHdr.Sk        = DMA_CSUM_BIT;     /* Checksum entire packet         */
  desc->hwHdr.Hint      = hints;            /* Hint Bits from caller          */

  DMA_SetDescriptorPids( desc,
			 recv_ctr_grp_id ); /* Pids based on recv group id    */

  c = DMA_PacketChunks(msg_len); /* Calculate number of 32B chunks in first   */
                                 /* packet.                                   */
  SPI_assert( c!=0 );
  desc->hwHdr.Chunks = c - 1;    /* Packet header has 0 for 1 chunk, ... ,    */
                                 /* 7 for 8 chunks).                          */

  desc->hwHdr.Dm        = 1;                /* 1=DMA Mode, 0=Fifo Mode        */

  DMA_SetVc( desc,
	     vc );                          /* Virtual channel & Dynamic.     */

  desc->hwHdr.X         = x;                /* Destination coordinates        */
  desc->hwHdr.Y         = y;
  desc->hwHdr.Z         = z;

  desc->hwHdr.Put_Offset   = recv_offset;
  desc->hwHdr.rDMA_Counter =
    recv_ctr_id + recv_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP);

  /* Note: The desc->hwHrd3.Payload_Bytes field is set by the iDMA            */

#ifdef DEBUG_MSG
  Dump_InjDescriptor(desc);
#endif

  return 0;
}


/*!
 * \brief Create a DMA Descriptor For a Local Direct Put Message
 *
 * A local direct put message is one that is targeted within the same node, and
 * its data is directly put into memory by the DMA...it does not go into a
 * reception fifo.  This is essentially a memcpy via DMA.
 *
 * A local direct-put DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 0
 *   - local_memcopy   = 1
 *   - idma_counterId  = Injection counter ID associated with the data being
 *                       sent.  This counter contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = Message length.  Set to caller's msg_len.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = 0 (not used).
 *   - Sk              = 0 (not used).
 *   - Hint            = 0 (not used).
 *   - Pid0, Pid1      = Set based on caller's "recv_ctr_grp_id".
 *   - Chunks          = Set to largest size consistent with msg_len.
 *   - Dm              = 1 (Indicates a direct-put packet).
 *   - Dynamic         = 0 (not used).
 *   - VC              = 0 (not used).
 *   - X,Y,Z           = 0 (not used).
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = Destination message offset (from the reception
 *                       counter's base address).  Set to caller's recv_offset.
 *   - rDMA_Counter    = Reception counter ID.  This counter is located on the
 *                       destination node and contains the base address of the
 *                       message and the message length..  Set based on caller's
 *                       recv_ctr_grp_id and recv_ctr_id.
 *   - Payload_Bytes   = Number of valid bytes in the payload.  Set by iDMA.
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 0.
 *   - iDMA_Fifo_ID    = 0 (not used).
 *   - Func_Id         = 0 (not used).
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      recv_ctr_grp_id  Reception counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      recv_ctr_id      Reception counter ID (within the recv counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      recv_offset      Offset of the payload from the pa_base
 *                                  associated with the specified reception
 *                                  counter.
 * \param[in]      msg_len          Total message length (in bytes).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, the packet size is set to the largest value consistent
 *       with the message size.  For example,
 *       - if msg_len >= 209, there will be 8 32-byte chunks in each packet,
 *         with the possible exception of the last packet, which could contain
 *         fewer chunks (209... of payload + 16 header).
 *       - if 177 <= msg_len < 208, there will be 7 chunks in the packet, etc.
 *
 * \note By default, for direct-put DMA messages, the pid0 and pid1 bits in the
 *       torus hardware packet header are determined by the recv_ctr_grp_id:
 *       - if recv_ctr_grp_id = 0 => (pid0,pid1) = (0,0)
 *       - if recv_ctr_grp_id = 1 => (pid0,pid1) = (0,1)
 *       - if recv_ctr_grp_id = 2 => (pid0,pid1) = (1,0)
 *       - if recv_ctr_grp_id = 3 => (pid0,pid1) = (1,1)
 *       The only use for the pid bits is for debug, ie, if headers are
 *       being saved.
 */
int  DMA_LocalDirectPutDescriptor(
				  DMA_InjDescriptor_t *desc,
				  unsigned int         inj_ctr_grp_id,
				  unsigned int         inj_ctr_id,
				  unsigned int         send_offset,
				  unsigned int         recv_ctr_grp_id,
				  unsigned int         recv_ctr_id,
				  unsigned int         recv_offset,
				  unsigned int         msg_len
				 )
{
  int c;

  SPI_assert( desc != NULL );
  SPI_assert( inj_ctr_grp_id  < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id      < DMA_NUM_COUNTERS_PER_GROUP );
  SPI_assert( recv_ctr_grp_id < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( recv_ctr_id     < DMA_NUM_COUNTERS_PER_GROUP );

  DMA_ZeroOutDescriptor(desc);

  desc->local_memcopy  = 1; /* 1 bit */

  desc->idma_counterId =
    inj_ctr_id + inj_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP); /* 8 bits */

  desc->base_offset    =  send_offset;
  desc->msg_length     =  msg_len;

   /*  Torus Headers */

  DMA_SetDescriptorPids( desc,
			 recv_ctr_grp_id );

  c = DMA_PacketChunks(msg_len); /* Calculate number of 32B chunks in first   */
                                 /* packet.                                   */
  SPI_assert( c!=0 );
  desc->hwHdr.Chunks = c - 1;    /* Packet header has 0 for 1 chunk, ... ,    */
                                 /* 7 for 8 chunks).                          */

  desc->hwHdr.Dm        = 1;                /* 1=DMA Mode, 0=Fifo Mode        */

  desc->hwHdr.Put_Offset   = recv_offset;
  desc->hwHdr.rDMA_Counter =
    recv_ctr_id + recv_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP);

  /* Note: The desc->hwHrd3.Payload_Bytes field is set by the iDMA            */

#ifdef DEBUG_MSG
  Dump_InjDescriptor(desc);
#endif

  return 0;
}


/*!
 * \brief Create a DMA Descriptor For a Local L3 Prefetch Only Message
 *
 * A local prefetch is one in which the DMA simply prefetches the send buffer
 * into L3.
 *
 * A local prefetch DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 1
 *   - local_memcopy   = 1
 *   - idma_counterId  = Injection counter ID associated with the message being
 *                       prefetched.  This counter contains the base address of
 *                       the message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = Message length.  Set to caller's msg_len.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = 0 (not used).
 *   - Sk              = 0 (not used).
 *   - Hint            = 0 (not used).
 *   - Pid0, Pid1      = 0 (not used).
 *   - Chunks          = Set to largest size consistent with msg_len.
 *   - Dm              = 1 (Indicates a DMA packet).
 *   - Dynamic         = 0 (not used).
 *   - VC              = 0 (not used).
 *   - X,Y,Z           = 0 (not used).
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = 0 (not used).
 *   - rDMA_Counter    = 0 (not used).
 *   - Payload_Bytes   = 0 (not used).
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 0.
 *   - iDMA_Fifo_ID    = 0 (not used).
 *   - Func_Id         = 0 (not used).
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      msg_len          Total message length (in bytes).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, the packet size is set to the largest value consistent
 *       with the message size.  For example,
 *       - if msg_len >= 209, there will be 8 32-byte chunks in each packet,
 *         with the possible exception of the last packet, which could contain
 *         fewer chunks (209... of payload + 16 header).
 *       - if 177 <= msg_len < 208, there will be 7 chunks in the packet, etc.
 *
 */
int  DMA_LocalPrefetchOnlyDescriptor(
				     DMA_InjDescriptor_t *desc,
				     unsigned int         inj_ctr_grp_id,
				     unsigned int         inj_ctr_id,
				     unsigned int         send_offset,
				     unsigned int         msg_len
				    )
{
  int c;

  SPI_assert( desc != NULL );
  SPI_assert( inj_ctr_grp_id  < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id      < DMA_NUM_COUNTERS_PER_GROUP );

  DMA_ZeroOutDescriptor(desc);

  desc->local_memcopy  = 1; /* 1 bit */
  desc->prefetch_only  = 1; /* 1 bit */

  desc->idma_counterId =
    inj_ctr_id + inj_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP); /* 8 bits */

  desc->base_offset    =  send_offset;
  desc->msg_length     =  msg_len;

   /*  Torus Headers */
  c = DMA_PacketChunks(msg_len); /* Calculate number of 32B chunks in first   */
                                 /* packet.                                   */
  SPI_assert( c!=0 );
  desc->hwHdr.Chunks = c - 1;    /* Packet header has 0 for 1 chunk, ... ,    */
                                 /* 7 for 8 chunks).                          */

  desc->hwHdr.Dm        = 1;                /* 1=DMA Mode, 0=Fifo Mode        */

#ifdef DEBUG_MSG
  Dump_InjDescriptor(desc);
#endif

  return 0;
}


/*!
 * \brief Create a DMA Descriptor For a Torus Remote-Get Message
 *
 * A torus remote-get message is one that is sent to another node and its data
 * is directly put by the DMA into an injection fifo on the destination
 * node...it does not go into a reception fifo.  Therefore, the payload of this
 * message is one (or more) descriptors for another message that is to be sent
 * back to the originating node.
 *
 * By default, we assume that the payload of this remote get packet is a single
 * descriptor.  Thus, Chunks = (2)-1 (64 byte packet) and msg_length = 32.
 * For remote gets whose payload is greater than 1 descriptor, the caller can
 * change the packet Chunks and msg_length after this function builds the
 * default descriptor.
 *
 * It is also assumed that the payload is NOT checksummed, since it is not
 * always reproducible.  Things like idma_counterId and base_offset may be
 * different on another run, making checksumming inconsistent.
 *
 * A torus remote-get DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 0
 *   - local_memcopy   = 0
 *   - idma_counterId  = Injection counter ID associated with the data being
 *                       sent.  This counter contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = 32.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = 0 (not used because Sk is 1).
 *   - Sk              = 1 (do not checksum this packet).
 *   - Hint            = Set to caller's "hints".
 *   - Pid0, Pid1      = Set based on caller's "recv_inj_fifo_id" (see note).
 *   - Chunks          = Set to (2)-1 = 1.
 *   - Dm              = 1 (Indicates a DMA packet).
 *   - Dynamic         = Set based on caller's "vc".
 *   - VC              = Set to caller's "vc".
 *   - X,Y,Z           = Set to caller's "x", "y", "z".
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = 0 (not used).
 *   - rDMA_Counter    = 0 (not used).
 *   - Payload_Bytes   = Number of valid bytes in the payload.  Set by iDMA.
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 1.
 *   - iDMA_Fifo_ID    = Injection fifo ID where the payload will be injected.
 *                       Set based on caller's recv_inj_ctr_grp_id and
 *                       recv_inj_ctr_id.
 *   - Func_Id         = 0 (not used).
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      x                The destination's x coordinate (8 bits).
 * \param[in]      y                The destination's y coordinate (8 bits).
 * \param[in]      z                The destination's z coordinate (8 bits).
 * \param[in]      hints            Hint bits for torus routing (6 bits).
 *                                  Each bit corresponds to x+, x-, y+, y-,
 *                                  z+, z-.  If a bit is set, it indicates that
 *                                  the packet wants to travel along the
 *                                  corresponding direction.  If all bits are
 *                                  zero, the hardware calculates the hint bits.
 *                                  Both of x+ and x- cannot be set at the same
 *                                  time...same with y and z.
 * \param[in]      vc               The virtual channel that the packet must go
 *                                  into if it fails to win the bypass
 *                                  arbitration in the receiving node.
 *                                  - 0 = Virtual channel dynamic 0
 *                                  - 1 = Virtual channel dynamic 1
 *                                  - 2 = Virtual channel deterministic bubble
 *                                  - 3 = Virtual channel deterministic priority
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      recv_inj_fifo_grp_id  Injection fifo group ID where payload
 *                                       will be injected on destination node
 *                                       (0 to DMA_NUM_INJ_FIFO_GROUPS-1).
 * \param[in]      recv_inj_fifo_id      Injection fifo ID (within the
 *                                       recv_inj_fifo_grp_id group)
 *                                       (0 to DMA_NUM_INJ_FIFOS_PER_GROUP-1).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, for remote-get DMA messages, the pid0 and pid1 bits in the
 *       torus hardware packet header are determined by the recv_inj_fifo_grp_id:
 *       - if recv_inj_fifo_grp_id = 0 => (pid0,pid1) = (0,0)
 *       - if recv_inj_fifo_grp_id = 1 => (pid0,pid1) = (0,1)
 *       - if recv_inj_fifo_grp_id = 2 => (pid0,pid1) = (1,0)
 *       - if recv_inj_fifo_grp_id = 3 => (pid0,pid1) = (1,1)
 *       Pid0 determines into which physical torus fifo group on the destination
 *       node the packet is put, prior to the dma receiving it.  Other than that,
 *       the only use for the pid bits is for debug, ie, if headers are being
 *       saved.
 */
int  DMA_TorusRemoteGetDescriptor(
				  DMA_InjDescriptor_t *desc,
				  unsigned int         x,
				  unsigned int         y,
				  unsigned int         z,
				  unsigned int         hints,
				  unsigned int         vc,
				  unsigned int         inj_ctr_grp_id,
				  unsigned int         inj_ctr_id,
				  unsigned int         send_offset,
				  unsigned int         recv_inj_fifo_grp_id,
				  unsigned int         recv_inj_fifo_id
				 )
{

  SPI_assert( desc != NULL );
  SPI_assert( (hints & 0x0000003F) == hints );
  SPI_assert( vc <= 3 );
  SPI_assert( inj_ctr_grp_id       < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id           < DMA_NUM_COUNTERS_PER_GROUP );
  SPI_assert( recv_inj_fifo_grp_id < DMA_NUM_INJ_FIFO_GROUPS );
  SPI_assert( recv_inj_fifo_id     < DMA_NUM_INJ_FIFOS_PER_GROUP );

#ifndef NDEBUG

  if ( personality_info.personalityRetrieved == 0 )
    {
      DMA_GetPersonalityInfo();
    }

  SPI_assert( x < personality_info.xNodes );
  SPI_assert( y < personality_info.yNodes );
  SPI_assert( z < personality_info.zNodes );

#endif

  DMA_ZeroOutDescriptor(desc);

  desc->idma_counterId =
    inj_ctr_id + inj_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP); /* 8 bits */

  desc->base_offset    =  send_offset;
  desc->msg_length     =  32;

   /*  Torus Headers */

  desc->hwHdr.Sk      =  1;  /* Don't checksum this packet */

  desc->hwHdr.Hint    = hints;              /* Hint Bits from caller          */

  DMA_SetDescriptorPids( desc,
			 recv_inj_fifo_grp_id ); /* Pids based on recv fifo   */
                                                 /* group id                  */

  desc->hwHdr.Chunks    = 1;   /* Size in Chunks of 32B 1 => 64 bytes         */
  desc->hwHdr.Dm        = 1;   /* 1=DMA Mode, 0=Fifo Mode                     */

  DMA_SetVc(desc,vc);          /* Set virtual channel and dynamic             */

  desc->hwHdr.X         = x;   /* Destination coordinates                     */
  desc->hwHdr.Y         = y;
  desc->hwHdr.Z         = z;

  desc->hwHdr.Flags          = 0x1;  /* Flags[7]=Remote-Get                   */
  desc->hwHdr.iDMA_Fifo_ID   =       /* Destination inj fifo ID               */
    recv_inj_fifo_id + ( recv_inj_fifo_grp_id * DMA_NUM_INJ_FIFOS_PER_GROUP );

#ifdef DEBUG_MSG
  Dump_InjDescriptor(desc);
#endif

  return 0;
}


/*!
 * \brief Create a DMA Descriptor For a Local Remote-Get Message
 *
 * A local remote-get message is one whose data is directly put by the DMA into
 * an injection fifo on the local node...it does not go into a reception fifo.
 * Therefore, the payload of this message is one (or more) descriptors for
 * another message that is to be injected on the local node.
 *
 * By default, we assume that the payload of this remote get packet is a single
 * descriptor.  Thus, Chunks = (2)-1 (64 byte packet) and msg_length = 32.
 * For remote gets whose payload is greater than 1 descriptor, the caller can
 * change the packet Chunks and msg_length after this function builds the
 * default descriptor.
 *
 * A local remote-get DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 0
 *   - local_memcopy   = 1
 *   - idma_counterId  = Injection counter ID associated with the data being
 *                       sent.  This counter contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = 32.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = 0 (not used).
 *   - Sk              = 0 (not used).
 *   - Hint            = 0 (Set to caller's "hints".
 *   - Pid0, Pid1      = Set based on caller's "recv_inj_fifo_id" (see note).
 *   - Chunks          = Set to (2)-1 = 1.
 *   - Dm              = 1 (Indicates a DMA packet).
 *   - Dynamic         = 0 (not used).
 *   - VC              = 0 (not used).
 *   - X,Y,Z           = 0 (not used).
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = 0 (not used).
 *   - rDMA_Counter    = 0 (not used).
 *   - Payload_Bytes   = Number of valid bytes in the payload.  Set by iDMA.
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 1.
 *   - iDMA_Fifo_ID    = Injection fifo ID where the payload will be injected.
 *                       Set based on caller's inj_ctr_grp_id and inj_ctr_id.
 *   - Func_Id         = 0 (not used).
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      recv_inj_fifo_grp_id  Injection fifo group ID where payload
 *                                       will be injected on local node
 *                                       (0 to DMA_NUM_INJ_FIFO_GROUPS-1).
 * \param[in]      recv_inj_fifo_id      Injection fifo ID (within the
 *                                       recv_inj_fifo_grp_id group)
 *                                       (0 to DMA_NUM_INJ_FIFOS_PER_GROUP-1).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, for remote-get DMA messages, the pid0 and pid1 bits in the
 *       hardware packet header are determined by the recv_inj_fifo_grp_id:
 *       - if recv_inj_fifo_grp_id = 0 => (pid0,pid1) = (0,0)
 *       - if recv_inj_fifo_grp_id = 1 => (pid0,pid1) = (0,1)
 *       - if recv_inj_fifo_grp_id = 2 => (pid0,pid1) = (1,0)
 *       - if recv_inj_fifo_grp_id = 3 => (pid0,pid1) = (1,1)
 *
 */
int  DMA_LocalRemoteGetDescriptor(
				  DMA_InjDescriptor_t *desc,
				  unsigned int         inj_ctr_grp_id,
				  unsigned int         inj_ctr_id,
				  unsigned int         send_offset,
				  unsigned int         recv_inj_fifo_grp_id,
				  unsigned int         recv_inj_fifo_id
				 )
{

  SPI_assert( desc != NULL );
  SPI_assert( inj_ctr_grp_id       < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id           < DMA_NUM_COUNTERS_PER_GROUP );
  SPI_assert( recv_inj_fifo_grp_id < DMA_NUM_INJ_FIFO_GROUPS );
  SPI_assert( recv_inj_fifo_id     < DMA_NUM_INJ_FIFOS_PER_GROUP );

  DMA_ZeroOutDescriptor(desc);

  desc->local_memcopy  =  1; /* 1 bit */

  desc->idma_counterId =
    inj_ctr_id + inj_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP); /* 8 bits */
  desc->base_offset    =  send_offset;
  desc->msg_length     =  32;

   /*  Torus Headers */
  DMA_SetDescriptorPids( desc,
			 recv_inj_fifo_grp_id ); /* Pids based on recv fifo   */
                                                 /* group id                  */

  desc->hwHdr.Chunks    = 1;   /* Size in Chunks of 32B 1 => 64 bytes         */
  desc->hwHdr.Dm        = 1;   /* 1=DMA Mode, 0=Fifo Mode                     */

  desc->hwHdr.Flags          = 0x1;  /* Flags[7]=Remote-Get                   */
  desc->hwHdr.iDMA_Fifo_ID   =       /* Destination inj fifo ID               */
    recv_inj_fifo_id + ( recv_inj_fifo_grp_id * DMA_NUM_INJ_FIFOS_PER_GROUP );

  return 0;
}


/*!
 * \brief Create a DMA Descriptor For a Torus Memory Fifo Message
 *
 * A torus memory fifo message is one that is sent to another node and its data
 * is put into a reception memory fifo by the DMA on the destination node.
 *
 * A torus memory fifo DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 0
 *   - local_memcopy   = 0
 *   - idma_counterId  = Injection counter ID associated with the data being
 *                       sent.  This counter contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = Message length.  Set to caller's msg_len.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = DMA_CSUM_SKIP.
 *   - Sk              = DMA_CSUM_BIT.
 *   - Hint            = Set to caller's "hints".
 *   - Pid0, Pid1      = Set based on caller's "recv_ctr_grp_id" (see note).
 *   - Chunks          = Set to largest size consistent with msg_len.
 *   - Dm              = 0 (Indicates a memory fifo packet).
 *   - Dynamic         = Set based on caller's "vc".
 *   - VC              = Set to caller's "vc".
 *   - X,Y,Z           = Set to caller's "x", "y", "z".
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = 0 (initialized to 0, and unchanged in the first packet.
 *                          Increased by 240 in each subsequent packet, reflecting
 *                          the number of bytes transferred in all previous
 *                          packets).
 *   - rDMA_Counter    = 0 (not used).
 *   - Payload_Bytes   = 0 (not used).
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 0.
 *   - iDMA_Fifo_ID    = 0 (not used).
 *   - SW_Arg          = User-defined 24 bits.  Set to caller's sw_arg.
 *   - Func_Id         = The registration ID of a function to receive control
 *                       on the destination node to process the packet.
 *                       Set to caller's function_id.
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      x                The destination's x coordinate (8 bits).
 * \param[in]      y                The destination's y coordinate (8 bits).
 * \param[in]      z                The destination's z coordinate (8 bits).
 * \param[in]      recv_fifo_grp_id Reception fifo group ID
 *                                  (0 to DMA_NUM_REC_FIFO_GROUPS-1).
 * \param[in]      hints            Hint bits for torus routing (6 bits).
 *                                  Each bit corresponds to x+, x-, y+, y-,
 *                                  z+, z-.  If a bit is set, it indicates that
 *                                  the packet wants to travel along the
 *                                  corresponding direction.  If all bits are
 *                                  zero, the hardware calculates the hint bits.
 *                                  Both of x+ and x- cannot be set at the same
 *                                  time...same with y and z.
 * \param[in]      vc               The virtual channel that the packet must go
 *                                  into if it fails to win the bypass
 *                                  arbitration in the receiving node.
 *                                  - 0 = Virtual channel dynamic 0
 *                                  - 1 = Virtual channel dynamic 1
 *                                  - 2 = Virtual channel deterministic bubble
 *                                  - 3 = Virtual channel deterministic priority
 * \param[in]      sw_arg           User-defined 24 bits to be placed into the
 *                                  packets (bits 8-31).
 * \param[in]      function_id      Function id (8 bit registration ID) of the
 *                                  function to receive control on the
 *                                  destination node to process packets for this
 *                                  message.
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      msg_len          Total message length (in bytes).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, all payload bytes are included in the torus injection
 *       checksum.  In the first byte of the torus hardware packet header,
 *       this corresponds to setting CSum_Skip = 0x8 (16 bytes) and Sk=0.
 *       The defaults can be changed by changing DMA_CSUM_SKIP and
 *       DMA_CSUM_BIT in this include file.
 *
 * \note By default, the packet size is set to the largest value consistent
 *       with the message size.  For example,
 *       - if msg_len >= 209, there will be 8 32-byte chunks in each packet,
 *         with the possible exception of the last packet, which could contain
 *         fewer chunks (209... of payload + 16 header).
 *       - if 177 <= msg_len < 208, there will be 7 chunks in the packet, etc.
 *
 * \note By default, for DMA messages, the pid0 and pid1 bits in the
 *       torus hardware packet header are determined by the recv_fifo_grp_id:
 *       - if recv_fifo_grp_id = 0 => (pid0,pid1) = (0,0)
 *       - if recv_fifo_grp_id = 1 => (pid0,pid1) = (0,1)
 *       - if recv_fifo_grp_id = 2 => (pid0,pid1) = (1,0)
 *       - if recv_fifo_grp_id = 3 => (pid0,pid1) = (1,1)
 *       Pid0 determines into which physical torus fifo group on the destination
 *       node the packet is put, prior to the dma receiving it.  Other than that,
 *       the only use for the pid bits is for debug, ie, if headers are being
 *       saved.
*/
int  DMA_TorusMemFifoDescriptor(
				DMA_InjDescriptor_t *desc,
				unsigned int         x,
				unsigned int         y,
				unsigned int         z,
				unsigned int         recv_fifo_grp_id,
				unsigned int         hints,
				unsigned int         vc,
				unsigned int         sw_arg,
				unsigned int         function_id,
				unsigned int         inj_ctr_grp_id,
				unsigned int         inj_ctr_id,
				unsigned int         send_offset,
				unsigned int         msg_len
			       )
{
  int c;

  SPI_assert( desc != NULL );
  SPI_assert( (hints & 0x0000003F) == hints );
  SPI_assert( vc <= 3 );
  SPI_assert( inj_ctr_grp_id   < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id       < DMA_NUM_COUNTERS_PER_GROUP );
  SPI_assert( recv_fifo_grp_id < DMA_NUM_REC_FIFO_GROUPS );

#ifndef NDEBUG

  if ( personality_info.personalityRetrieved == 0 )
    {
      DMA_GetPersonalityInfo();
    }

  SPI_assert( x < personality_info.xNodes );
  SPI_assert( y < personality_info.yNodes );
  SPI_assert( z < personality_info.zNodes );

#endif

  DMA_ZeroOutDescriptor(desc);

  desc->idma_counterId =
    inj_ctr_id + inj_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP); /* 8 bits       */

  desc->base_offset    =  send_offset;
  desc->msg_length     =  msg_len;

   /*  Torus Headers */

  desc->hwHdr.CSum_Skip = DMA_CSUM_SKIP;    /* Checksum all but header        */
  desc->hwHdr.Sk        = DMA_CSUM_BIT;     /* Checksum entire packet         */
  desc->hwHdr.Hint      = hints;            /* Hint Bits from caller          */

  DMA_SetDescriptorPids( desc,
			 recv_fifo_grp_id ); /* Pids based on recv group id   */

  c = DMA_PacketChunks(msg_len); /* Calculate number of 32B chunks in first   */
                                 /* packet.                                   */
  SPI_assert( c!=0 );
  desc->hwHdr.Chunks = c - 1;    /* Packet header has 0 for 1 chunk, ... ,    */
                                 /* 7 for 8 chunks).                          */

  DMA_SetVc( desc,
	     vc );                          /* Virtual channel & Dynamic.     */

  desc->hwHdr.X         = x;                /* Destination coordinates        */
  desc->hwHdr.Y         = y;
  desc->hwHdr.Z         = z;

  desc->hwHdr.SW_Arg    = sw_arg;           /* User-defined                   */
  desc->hwHdr.Func_Id   = function_id;      /* Registration id                */

#ifdef DEBUG_MSG
  Dump_InjDescriptor(desc);
#endif

  return 0;
}


/*!
 * \brief Create a DMA Descriptor For a Local Memory Fifo Message
 *
 * A local memory fifo message is one whose data is put into a reception
 * memory fifo on the same node by the DMA.
 *
 * A local memory fifo DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 0
 *   - local_memcopy   = 1
 *   - idma_counterId  = Injection counter ID associated with the data being
 *                       sent.  This counter contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = Message length.  Set to caller's msg_len.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = 0 (not used).
 *   - Sk              = 0 (not used).
 *   - Hint            = 0 (not used).
 *   - Pid0, Pid1      = Set based on caller's "recv_fifo_grp_id" (see note).
 *   - Chunks          = Set to largest size consistent with msg_len.
 *   - Dm              = 0 (Indicates a memory fifo packet).
 *   - Dynamic         = 0 (not used).
 *   - VC              = 0 (not used).
 *   - X,Y,Z           = 0 (not used).
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = 0 (not used).
 *   - rDMA_Counter    = 0 (not used).
 *   - Payload_Bytes   = 0 (not used).
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 0.
 *   - iDMA_Fifo_ID    = 0 (not used).
 *   - SW_Arg          = User-defined 24 bits.  Set to caller's sw_arg.
 *   - Func_Id         = The registration ID of a function to receive control
 *                       on this local node to process the packet.
 *                       Set to caller's function_id.
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      recv_fifo_grp_id Reception fifo group ID
 *                                  (0 to DMA_NUM_REC_FIFO_GROUPS-1).
 * \param[in]      sw_arg           User-defined 24 bits to be placed into the
 *                                  packets (bits 8-31).
 * \param[in]      function_id      Function id (8 bit registration ID) of the
 *                                  function to receive control on this
 *                                  local node to process packets for this
 *                                  message.
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      msg_len          Total message length (in bytes).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, the packet size is set to the largest value consistent
 *       with the message size.  For example,
 *       - if msg_len >= 209, there will be 8 32-byte chunks in each packet,
 *         with the possible exception of the last packet, which could contain
 *         fewer chunks (209... of payload + 16 header).
 *       - if 177 <= msg_len < 208, there will be 7 chunks in the packet, etc.
 *
 * \note By default, for direct-put DMA messages, the pid0 and pid1 bits in the
 *       torus hardware packet header are determined by the recv_fifo_grp_id:
 *       - if recv_fifo_grp_id = 0 => (pid0,pid1) = (0,0)
 *       - if recv_fifo_grp_id = 1 => (pid0,pid1) = (0,1)
 *       - if recv_fifo_grp_id = 2 => (pid0,pid1) = (1,0)
 *       - if recv_fifo_grp_id = 3 => (pid0,pid1) = (1,1)
*/
int  DMA_LocalMemFifoDescriptor(
				DMA_InjDescriptor_t *desc,
				unsigned int         recv_fifo_grp_id,
				unsigned int         sw_arg,
				unsigned int         function_id,
				unsigned int         inj_ctr_grp_id,
				unsigned int         inj_ctr_id,
				unsigned int         send_offset,
				unsigned int         msg_len
			       )
{
  int c;

  SPI_assert( desc != NULL );
  SPI_assert( inj_ctr_grp_id   < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id       < DMA_NUM_COUNTERS_PER_GROUP );
  SPI_assert( recv_fifo_grp_id < DMA_NUM_REC_FIFO_GROUPS );

  DMA_ZeroOutDescriptor(desc);

  desc->local_memcopy  =  1; /* 1 bit */

  desc->idma_counterId =
    inj_ctr_id + inj_ctr_grp_id*(DMA_NUM_COUNTERS_PER_GROUP); /* 8 bits       */

  desc->base_offset    =  send_offset;
  desc->msg_length     =  msg_len;

   /*  Torus Headers */
  DMA_SetDescriptorPids( desc,
			 recv_fifo_grp_id ); /* Pids based on recv group id   */

  c = DMA_PacketChunks(msg_len); /* Calculate number of 32B chunks in first   */
                                 /* packet.                                   */
  SPI_assert( c!=0 );
  desc->hwHdr.Chunks = c - 1;    /* Packet header has 0 for 1 chunk, ... ,    */
                                 /* 7 for 8 chunks).                          */

  desc->hwHdr.SW_Arg    = sw_arg;           /* User-defined                   */
  desc->hwHdr.Func_Id   = function_id;      /* Registration id                */

#ifdef DEBUG_MSG
  Dump_InjDescriptor(desc);
#endif

  return 0;
}


/*!
 * \brief Create a DMA Descriptor For a Torus Direct Put Broadcast Message
 *
 * A torus direct put broadcast message is one that is sent to all of the nodes
 * in a specified direction along a specified line, its data
 * is directly put into memory on the nodes along that line by the DMA on those
 * nodes...it does not go into a reception fifo.  Only one hint bit can be
 * specified, dictating the direction (plus or minus) and line (x, y, or z).
 *
 * By default, the packet is included in the checksum.  Retransmitted packets
 * should not be included in the checksum.
 *
 * By default, the deterministic bubble normal virtual channel is used.
 *
 * A torus direct-put broadcast DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 0
 *   - local_memcopy   = 0
 *   - idma_counterId  = Injection counter ID associated with the data being
 *                       sent.  This counter contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = Message length.  Set to caller's msg_len.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = DMA_CSUM_SKIP.
 *   - Sk              = DMA_CSUM_BIT.
 *   - Hint            = Set to caller's "hints".
 *   - Pid0, Pid1      = Set based on caller's "recv_ctr_grp_id" (see note).
 *   - Chunks          = Set to largest size consistent with msg_len.
 *   - Dm              = 1 (Indicates a direct-put packet).
 *   - Dynamic         = 0 (Deterministic).
 *   - VC              = Virtual Channel: Deterministic Bubble Normal.
 *   - X,Y,Z           = Set according to the hints:
 *                       Two of the directions are set to this node's
 *                       coordinates (no movement in those directions).
 *                       One direction is set to the dest specified
 *                       by the caller.
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = Destination message offset (from the reception
 *                       counter's base address).  Set to caller's recv_offset.
 *   - rDMA_Counter    = Reception counter ID.  This counter is located on the
 *                       destination node and contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       recv_ctr_grp_id and recv_ctr_id.
 *   - Payload_Bytes   = Number of valid bytes in the payload.  Set by iDMA.
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 0.
 *   - iDMA_Fifo_ID    = 0 (not used).
 *   - Func_Id         = 0 (not used).
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      dest             The final torus destination coordinate
 *                                  along the line specified by the hints.
 *                                  Should not exceed the number of nodes in
 *                                  the direction of travel.
 * \param[in]      hints            Hint bits for torus routing (6 bits).
 *                                  Each bit corresponds to x+, x-, y+, y-,
 *                                  z+, z-.  If a bit is set, it indicates that
 *                                  the packet wants to travel along the
 *                                  corresponding direction.  If all bits are
 *                                  zero, the hardware calculates the hint bits.
 *                                  Only one bit may be specified.
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      recv_ctr_grp_id  Reception counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      recv_ctr_id      Reception counter ID (within the recv counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      recv_offset      Offset of the payload from the pa_base
 *                                  associated with the specified reception
 *                                  counter.
 * \param[in]      msg_len          Total message length (in bytes).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, all payload bytes are included in the torus injection
 *       checksum.  In the first byte of the torus hardware packet header,
 *       this corresponds to setting CSum_Skip = 0x8 (16 bytes) and Sk=0.
 *       The defaults can be changed by changing DMA_CSUM_SKIP and
 *       DMA_CSUM_BIT in this include file.
 *
 * \note By default, the packet size is set to the largest value consistent
 *       with the message size.  For example,
 *       - if msg_len >= 209, there will be 8 32-byte chunks in each packet,
 *         with the possible exception of the last packet, which could contain
 *         fewer chunks (209... of payload + 16 header).
 *       - if 177 <= msg_len < 208, there will be 7 chunks in the packet, etc.
 *
 * \note By default, for direct-put DMA messages, the pid0 and pid1 bits in the
 *       torus hardware packet header are determined by the recv_ctr_grp_id:
 *       - if recv_ctr_grp_id = 0 => (pid0,pid1) = (0,0)
 *       - if recv_ctr_grp_id = 1 => (pid0,pid1) = (0,1)
 *       - if recv_ctr_grp_id = 2 => (pid0,pid1) = (1,0)
 *       - if recv_ctr_grp_id = 3 => (pid0,pid1) = (1,1)
 *       Pid0 determines into which physical torus fifo group on the destination
 *       node the packet is put, prior to the dma receiving it.  Other than that,
 *       the only use for the pid bits is for debug, ie, if headers are being
 *       saved.
*/
int  DMA_TorusDirectPutBcastDescriptor(
				       DMA_InjDescriptor_t *desc,
				       unsigned int         dest,
				       unsigned int         hints,
				       unsigned int         inj_ctr_grp_id,
				       unsigned int         inj_ctr_id,
				       unsigned int         send_offset,
				       unsigned int         recv_ctr_grp_id,
				       unsigned int         recv_ctr_id,
				       unsigned int         recv_offset,
				       unsigned int         msg_len
				      )
{

  int dest_x,dest_y,dest_z;

  SPI_assert( desc != NULL );
  SPI_assert( inj_ctr_grp_id  < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id      < DMA_NUM_COUNTERS_PER_GROUP );
  SPI_assert( recv_ctr_grp_id < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( recv_ctr_id     < DMA_NUM_COUNTERS_PER_GROUP );

  /*
   * Previous code to retrieve our node's x,y,z coords:
   *   BGLPartitionGetCoords( &dest_x, &dest_y, &dest_z );
   *
   * If the node's x,y,z coordinates have not yet been retrieved from the
   * personality, go get the personality and set the DMA_NodeXCoordinate,
   * DMA_NodeYCoordinate, and DMA_NodeZCoordinate static variables from
   * the personality info.  Then, use this to init the dest_x,y,z variables.
   */
  if ( personality_info.personalityRetrieved == 0 )
    {
      DMA_GetPersonalityInfo();
    }

  dest_x = personality_info.nodeXCoordinate;
  dest_y = personality_info.nodeYCoordinate;
  dest_z = personality_info.nodeZCoordinate;

  /*
   * Examine the hint bits specified by the caller:
   * - Ensure only one of them is specified
   * - Ensure dest is valid for the direction of the broadcast
   * - Override x, y, or z with dest for the specified direction
   */

  switch(hints) {

  case  DMA_PACKET_HINT_XP:
  case  DMA_PACKET_HINT_XM:
    dest_x = dest;
    SPI_assert( dest <= personality_info.xNodes );
    break;

  case  DMA_PACKET_HINT_YP:
  case  DMA_PACKET_HINT_YM:
    dest_y = dest;
    SPI_assert( dest <= personality_info.yNodes );
    break;

  case  DMA_PACKET_HINT_ZP:
  case  DMA_PACKET_HINT_ZM:
    dest_z = dest;
    SPI_assert( dest <= personality_info.zNodes );
    break;

  default:
    SPI_assert(0);

  }

  /* Build the descriptor */
  DMA_TorusDirectPutDescriptor(desc,
			       dest_x,
			       dest_y,
			       dest_z,
			       hints,
			       DMA_PACKET_VC_BN,
			       inj_ctr_grp_id,
			       inj_ctr_id,
			       send_offset,
			       recv_ctr_grp_id,
			       recv_ctr_id,
			       recv_offset,
			       msg_len);

   /*  set the deposit bit */
  desc->hwHdr.Dp =1;


  return 0;
}




/*!
 * \brief Create a DMA Descriptor For a Torus Memory Fifo Broadcast Message
 *
 * A torus memory fifo broadcast message is one that is sent to all of the nodes
 * in a specified direction along a specified line, its data is
 * put into a reception memory fifo by the DMA on the destination nodes along
 * that line.  Only one hint bit can be specified, dictating the direction
 * (plus or minus) and line (x, y, or z).
 *
 * By default, the packet is included in the checksum.  Retransmitted packets
 * should not be included in the checksum.
 *
 * By default, the deterministic bubble normal virtual channel is used.
 *
 * A torus memory fifo broadcast DMA descriptor contains the following:
 *
 * - 16 bytes of control information:
 *   - prefetch_only   = 0
 *   - local_memcopy   = 0
 *   - idma_counterId  = Injection counter ID associated with the data being
 *                       sent.  This counter contains the base address of the
 *                       message and the message length.  Set based on caller's
 *                       inj_ctr_grp_id and inj_ctr_id.
 *   - base_offset     = Message offset (from the injection counter's base
 *                       address).  Set to caller's send_offset.
 *   - msg_length      = Message length.  Set to caller's msg_len.
 *
 * - 8 byte torus hardware header
 *   - CSum_Skip       = DMA_CSUM_SKIP.
 *   - Sk              = DMA_CSUM_BIT.
 *   - Hint            = Set to caller's "hints".
 *   - Pid0, Pid1      = Set based on caller's "recv_fifo_grp_id" (see note).
 *   - Chunks          = Set to largest size consistent with msg_len.
 *   - Dm              = 0 (Indicates a memory fifo packet).
 *   - Dynamic         = 0 (Deterministic).
 *   - VC              = Virtual Channel: Deterministic Bubble Normal.
 *   - X,Y,Z           = Set according to the hints:
 *                       Two of the directions are set to this node's
 *                       coordinates (no movement in those directions).
 *                       One direction is set to the dest specified
 *                       by the caller.
 *
 * - 8 byte software header (initial values used by iDMA).
 *   - Put_Offset      = 0 (not used).
 *   - rDMA_Counter    = 0 (not used).
 *   - Payload_Bytes   = 0 (not used).
 *   - Flags           = Pacing     = 0.
 *                       Remote-Get = 0.
 *   - iDMA_Fifo_ID    = 0 (not used).
 *   - SW_Arg          = User-defined 24 bits.  Set to caller's sw_arg.
 *   - Func_Id         = The registration ID of a function to receive control
 *                       on the destination node to process the packet.
 *                       Set to caller's function_id.
 *
 * This function creates the above descriptor.
 *
 * \param[in,out]  desc             Pointer to the storage where the descriptor
 *                                  will be created.
 * \param[in]      dest             The final torus destination coordinate
 *                                  along the line specified by the hints.
 *                                  Should not exceed the number of nodes in
 *                                  the direction of travel.
 * \param[in]      recv_fifo_grp_id Reception fifo group ID
 *                                  (0 to DMA_NUM_REC_FIFO_GROUPS-1).
 * \param[in]      hints            Hint bits for torus routing (6 bits).
 *                                  Each bit corresponds to x+, x-, y+, y-,
 *                                  z+, z-.  If a bit is set, it indicates that
 *                                  the packet wants to travel along the
 *                                  corresponding direction.  If all bits are
 *                                  zero, the hardware calculates the hint bits.
 *                                  Only one bit may be specified.
 * \param[in]      sw_arg           User-defined 24 bits to be placed into the
 *                                  packets (bits 8-31).
 * \param[in]      function_id      Function id (8 bit registration ID) of the
 *                                  function to receive control on the
 *                                  destination node to process packets for this
 *                                  message.
 * \param[in]      inj_ctr_grp_id   Injection counter group ID
 *                                  (0 to DMA_NUM_COUNTER_GROUPS-1).
 * \param[in]      inj_ctr_id       Injection counter ID (within the inj counter
 *                                  group) (0 to DMA_NUM_COUNTERS_PER_GROUP-1).
 * \param[in]      send_offset      Offset of the send payload from the pa_base
 *                                  associated with the specified injection
 *                                  counter.
 * \param[in]      msg_len          Total message length (in bytes).
 *
 * \retval  0         Success
 * \retval  non-zero  Failure
 *
 * \note By default, all payload bytes are included in the torus injection
 *       checksum.  In the first byte of the torus hardware packet header,
 *       this corresponds to setting CSum_Skip = 0x8 (16 bytes) and Sk=0.
 *       The defaults can be changed by changing DMA_CSUM_SKIP and
 *       DMA_CSUM_BIT in this include file.
 *
 * \note By default, the packet size is set to the largest value consistent
 *       with the message size.  For example,
 *       - if msg_len >= 209, there will be 8 32-byte chunks in each packet,
 *         with the possible exception of the last packet, which could contain
 *         fewer chunks (209... of payload + 16 header).
 *       - if 177 <= msg_len < 208, there will be 7 chunks in the packet, etc.
 *
 * \note By default, for direct-put DMA messages, the pid0 and pid1 bits in the
 *       torus hardware packet header are determined by the recv_fifo_grp_id:
 *       - if recv_fifo_grp_id = 0 => (pid0,pid1) = (0,0)
 *       - if recv_fifo_grp_id = 1 => (pid0,pid1) = (0,1)
 *       - if recv_fifo_grp_id = 2 => (pid0,pid1) = (1,0)
 *       - if recv_fifo_grp_id = 3 => (pid0,pid1) = (1,1)
 *       Pid0 determines into which physical torus fifo group on the destination
 *       node the packet is put, prior to the dma receiving it.  Other than that,
 *       the only use for the pid bits is for debug, ie, if headers are being
 *       saved.
*/
int  DMA_TorusMemFifoBcastDescriptor(
				     DMA_InjDescriptor_t *desc,
				     unsigned int         dest,
				     unsigned int         recv_fifo_grp_id,
				     unsigned int         hints,
				     unsigned int         sw_arg,
				     unsigned int         function_id,
				     unsigned int         inj_ctr_grp_id,
				     unsigned int         inj_ctr_id,
				     unsigned int         send_offset,
				     unsigned int         msg_len
				    )
{
  int dest_x,dest_y,dest_z;

  SPI_assert( desc != NULL );
  SPI_assert( inj_ctr_grp_id   < DMA_NUM_COUNTER_GROUPS );
  SPI_assert( inj_ctr_id       < DMA_NUM_COUNTERS_PER_GROUP );
  SPI_assert( recv_fifo_grp_id < DMA_NUM_COUNTER_GROUPS );

  /*
   * Previous code to retrieve our node's x,y,z coords:
   *   BGLPartitionGetCoords( &dest_x, &dest_y, &dest_z );
   *
   * If the node's x,y,z coordinates have not yet been retrieved from the
   * personality, go get the personality and set the DMA_NodeXCoordinate,
   * DMA_NodeYCoordinate, and DMA_NodeZCoordinate static variables from
   * the personality info.  Then, use this to init the dest_x,y,z variables.
   */
  if ( personality_info.personalityRetrieved == 0 )
    {
      DMA_GetPersonalityInfo();
    }

  dest_x = personality_info.nodeXCoordinate;
  dest_y = personality_info.nodeYCoordinate;
  dest_z = personality_info.nodeZCoordinate;

  /*
   * Examine the hint bits specified by the caller:
   * - Ensure only one of them is specified
   * - Ensure dest is valid for the direction of the broadcast
   * - Override x, y, or z with dest for the specified direction
   */

  switch(hints) {

  case  DMA_PACKET_HINT_XP:
  case  DMA_PACKET_HINT_XM:
    dest_x = dest;
    SPI_assert( dest <= personality_info.xNodes );
    break;

  case  DMA_PACKET_HINT_YP:
  case  DMA_PACKET_HINT_YM:
    dest_y = dest;
    SPI_assert( dest <= personality_info.yNodes );
    break;

  case  DMA_PACKET_HINT_ZP:
  case  DMA_PACKET_HINT_ZM:
    dest_z = dest;
    SPI_assert( dest <= personality_info.zNodes );
    break;

  default:
    SPI_assert(0);

  }

  /* Build the descriptor */
  DMA_TorusMemFifoDescriptor(
    desc,
    dest_x,
    dest_y,
    dest_z,
    recv_fifo_grp_id,
    hints,
    DMA_PACKET_VC_BN,
    sw_arg,
    function_id,
    inj_ctr_grp_id,
    inj_ctr_id,
    send_offset,
    msg_len);

   /*  set the deposit bit */
  desc->hwHdr.Dp =1;


  return 0;
}
EXPORT_SYMBOL(DMA_GetPersonalityInfo) ;
EXPORT_SYMBOL(DMA_TorusDirectPutDescriptor) ;
EXPORT_SYMBOL(DMA_LocalDirectPutDescriptor) ;
EXPORT_SYMBOL(DMA_LocalPrefetchOnlyDescriptor) ;
EXPORT_SYMBOL(DMA_TorusRemoteGetDescriptor) ;
EXPORT_SYMBOL(DMA_LocalRemoteGetDescriptor) ;
EXPORT_SYMBOL(DMA_TorusMemFifoDescriptor) ;
EXPORT_SYMBOL(DMA_LocalMemFifoDescriptor) ;
EXPORT_SYMBOL(DMA_TorusDirectPutBcastDescriptor) ;
EXPORT_SYMBOL(DMA_TorusMemFifoBcastDescriptor) ;
