/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2010
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses>.
 *
 * Author: Chris Ward <tjcw@uk.ibm.com>
 *
 * Description: Quadword ops for copying data, in particular torus-packet-sized
 *              (240 byte) sequences. Not currently used, but provided for
 *              reference.
 *
 *
 ********************************************************************/
#ifndef __BGP_DMA_TCP_QUADS_H__
#define __BGP_DMA_TCP_QUADS_H__

/*  TODO: take away the use of FP regs, now that software FIFO frames are 'rare', so we can avoid FP-in-kernel */
/*  Drop 240 bytes of payload from regs into 'software FIFO' */
static inline void torus_frame_payload_store(
    void * payloadptr)
  {
    unsigned int index1 ;
    unsigned int index2 ;
    torus_frame_payload *payload=payloadptr ;

    TRACEN(k_t_detail, "torus_payload_store payload=%p",payload) ;
           asm  (
               "li      %[index1],16                    \n\t"  /* Indexing values */
               "stfpdx  1,0,%[payload]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
               "li      %[index2],32                    \n\t"  /* Indexing values */
               "stfpdx  2,%[index1],%[payload]       \n\t"  /* F2=Q2 load */
               "li      %[index1],48                    \n\t"  /* Indexing values */
               "stfpdx  3,%[index2],%[payload]       \n\t"  /* F3=Q3 load */
               "li      %[index2],64                    \n\t"  /* Indexing values */
               "stfpdx  4,%[index1],%[payload]       \n\t"  /* F4=Q4 load */
               "li      %[index1],80                    \n\t"  /* Indexing values */
               "stfpdx  5,%[index2],%[payload]       \n\t"  /* F5=Q5 load */
               "li      %[index2],96                    \n\t"  /* Indexing values */
               "stfpdx  6,%[index1],%[payload]       \n\t"  /* F6=Q6 load */
               "li      %[index1],112                   \n\t"  /* Indexing values */
               "stfpdx  7,%[index2],%[payload]       \n\t"  /* F7=Q7 load */
               "li      %[index2],128                    \n\t"  /* Indexing values */
               "stfpdx  8,%[index1],%[payload]       \n\t"  /* F8=Q8 load */
               "li      %[index1],144                    \n\t"  /* Indexing values */
               "stfpdx  9,%[index2],%[payload]       \n\t"  /* F9=Q9 load */
               "li      %[index2],160                    \n\t"  /* Indexing values */
               "stfpdx  10,%[index1],%[payload]       \n\t"  /* F0=Q10 load */
               "li      %[index1],176                   \n\t"  /* Indexing values */
               "stfpdx  11,%[index2],%[payload]       \n\t"  /* F1=Q11 load */
               "li      %[index2],192                    \n\t"  /* Indexing values */
               "stfpdx  12,%[index1],%[payload]       \n\t"  /* F2=Q12 load */
               "li      %[index1],208                   \n\t"  /* Indexing values */
               "stfpdx  13,%[index2],%[payload]       \n\t"  /* F3=Q13 load */
               "li      %[index2],224                    \n\t"  /* Indexing values */
               "stfpdx  14,%[index1],%[payload]       \n\t"  /* F4=Q14 load */
               "stfpdx  15,%[index2],%[payload]       \n\t"  /* F3=Q15load */
                     :          /* outputs */
                       "=m" (*payload),
                       [index1] "=&b" (index1),
                       [index2] "=&b" (index2)
                     :            /* Inputs */
                       [payload] "b" (payload)         /* inputs */
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14", "fr15"
                       );
  }

/*  Load 240 bytes of payload from memory into regs */
static inline void torus_frame_payload_load(
    void * payloadptr)
  {
    unsigned int index1 ;
    unsigned int index2 ;
    torus_frame_payload *payload=payloadptr ;

    TRACEN(k_t_detail, "torus_payload_load payload=%p",payload) ;
           asm  (
               "li      %[index1],16                    \n\t"  /* Indexing values */
               "lfpdx  1,0,%[payload]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
               "li      %[index2],32                    \n\t"  /* Indexing values */
               "lfpdx  2,%[index1],%[payload]       \n\t"  /* F2=Q2 load */
               "li      %[index1],48                    \n\t"  /* Indexing values */
               "lfpdx  3,%[index2],%[payload]       \n\t"  /* F3=Q3 load */
               "li      %[index2],64                    \n\t"  /* Indexing values */
               "lfpdx  4,%[index1],%[payload]       \n\t"  /* F4=Q4 load */
               "li      %[index1],80                    \n\t"  /* Indexing values */
               "lfpdx  5,%[index2],%[payload]       \n\t"  /* F5=Q5 load */
               "li      %[index2],96                    \n\t"  /* Indexing values */
               "lfpdx  6,%[index1],%[payload]       \n\t"  /* F6=Q6 load */
               "li      %[index1],112                   \n\t"  /* Indexing values */
               "lfpdx  7,%[index2],%[payload]       \n\t"  /* F7=Q7 load */
               "li      %[index2],128                    \n\t"  /* Indexing values */
               "lfpdx  8,%[index1],%[payload]       \n\t"  /* F8=Q8 load */
               "li      %[index1],144                    \n\t"  /* Indexing values */
               "lfpdx  9,%[index2],%[payload]       \n\t"  /* F9=Q9 load */
               "li      %[index2],160                    \n\t"  /* Indexing values */
               "lfpdx  10,%[index1],%[payload]       \n\t"  /* F0=Q10 load */
               "li      %[index1],176                   \n\t"  /* Indexing values */
               "lfpdx  11,%[index2],%[payload]       \n\t"  /* F1=Q11 load */
               "li      %[index2],192                    \n\t"  /* Indexing values */
               "lfpdx  12,%[index1],%[payload]       \n\t"  /* F2=Q12 load */
               "li      %[index1],208                   \n\t"  /* Indexing values */
               "lfpdx  13,%[index2],%[payload]       \n\t"  /* F3=Q13 load */
               "li      %[index2],224                    \n\t"  /* Indexing values */
               "lfpdx  14,%[index1],%[payload]       \n\t"  /* F4=Q14 load */
               "lfpdx  15,%[index2],%[payload]       \n\t"  /* F3=Q15 load */
                     :          /* outputs */
                       "=m" (*payload),
                       [index1] "=&b" (index1),
                       [index2] "=&b" (index2)
                     :            /* Inputs */
                       [payload] "b" (payload)         /* inputs */
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14", "fr15"
                       );
  }

static inline int torus_frame_payload_memcpy_base(
		torus_frame_payload * target,
		torus_frame_payload * source
    )
  {
    unsigned int index1 ;
    unsigned int index2 ;

    TRACEN(k_t_detail, "torus_payload_memcpy target=%p source=%p",target,source) ;
           asm  (
               "li      %[index1],16                    \n\t"  /* Indexing values */
               "lfpdx  1,0,%[source]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
               "li      %[index2],32                    \n\t"  /* Indexing values */
               "lfpdx  2,%[index1],%[source]       \n\t"  /* F2=Q2 load */
               "li      %[index1],48                    \n\t"  /* Indexing values */
               "lfpdx  3,%[index2],%[source]       \n\t"  /* F3=Q3 load */
               "li      %[index2],64                    \n\t"  /* Indexing values */
               "lfpdx  4,%[index1],%[source]       \n\t"  /* F4=Q4 load */
               "li      %[index1],80                    \n\t"  /* Indexing values */
               "lfpdx  5,%[index2],%[source]       \n\t"  /* F5=Q5 load */
               "li      %[index2],96                    \n\t"  /* Indexing values */
               "lfpdx  6,%[index1],%[source]       \n\t"  /* F6=Q6 load */
               "li      %[index1],112                   \n\t"  /* Indexing values */
               "lfpdx  7,%[index2],%[source]       \n\t"  /* F7=Q7 load */
               "li      %[index2],128                    \n\t"  /* Indexing values */
               "lfpdx  8,%[index1],%[source]       \n\t"  /* F8=Q8 load */
               "li      %[index1],144                    \n\t"  /* Indexing values */
               "lfpdx  9,%[index2],%[source]       \n\t"  /* F9=Q9 load */
               "li      %[index2],160                    \n\t"  /* Indexing values */
               "lfpdx  10,%[index1],%[source]       \n\t"  /* F0=Q10 load */
               "li      %[index1],176                   \n\t"  /* Indexing values */
               "lfpdx  11,%[index2],%[source]       \n\t"  /* F1=Q11 load */
               "li      %[index2],192                    \n\t"  /* Indexing values */
               "lfpdx  12,%[index1],%[source]       \n\t"  /* F2=Q12 load */
               "li      %[index1],208                   \n\t"  /* Indexing values */
               "lfpdx  13,%[index2],%[source]       \n\t"  /* F3=Q13 load */
               "li      %[index2],224                    \n\t"  /* Indexing values */
               "lfpdx  14,%[index1],%[source]       \n\t"  /* F4=Q14 load */
               "lfpdx  15,%[index2],%[source]       \n\t"  /* F3=Q15 load */
    	               "li      %[index1],16                    \n\t"  /* Indexing values */
    	               "stfpdx  1,0,%[target]       \n\t"  /* F1=Q1 load from (%[remaining_quads]) */
    	               "li      %[index2],32                    \n\t"  /* Indexing values */
    	               "stfpdx  2,%[index1],%[target]       \n\t"  /* F2=Q2 load */
    	               "li      %[index1],48                    \n\t"  /* Indexing values */
    	               "stfpdx  3,%[index2],%[target]       \n\t"  /* F3=Q3 load */
    	               "li      %[index2],64                    \n\t"  /* Indexing values */
    	               "stfpdx  4,%[index1],%[target]       \n\t"  /* F4=Q4 load */
    	               "li      %[index1],80                    \n\t"  /* Indexing values */
    	               "stfpdx  5,%[index2],%[target]       \n\t"  /* F5=Q5 load */
    	               "li      %[index2],96                    \n\t"  /* Indexing values */
    	               "stfpdx  6,%[index1],%[target]       \n\t"  /* F6=Q6 load */
    	               "li      %[index1],112                   \n\t"  /* Indexing values */
    	               "stfpdx  7,%[index2],%[target]       \n\t"  /* F7=Q7 load */
    	               "li      %[index2],128                    \n\t"  /* Indexing values */
    	               "stfpdx  8,%[index1],%[target]       \n\t"  /* F8=Q8 load */
    	               "li      %[index1],144                    \n\t"  /* Indexing values */
    	               "stfpdx  9,%[index2],%[target]       \n\t"  /* F9=Q9 load */
    	               "li      %[index2],160                    \n\t"  /* Indexing values */
    	               "stfpdx  10,%[index1],%[target]       \n\t"  /* F0=Q10 load */
    	               "li      %[index1],176                   \n\t"  /* Indexing values */
    	               "stfpdx  11,%[index2],%[target]       \n\t"  /* F1=Q11 load */
    	               "li      %[index2],192                    \n\t"  /* Indexing values */
    	               "stfpdx  12,%[index1],%[target]       \n\t"  /* F2=Q12 load */
    	               "li      %[index1],208                   \n\t"  /* Indexing values */
    	               "stfpdx  13,%[index2],%[target]       \n\t"  /* F3=Q13 load */
    	               "li      %[index2],224                    \n\t"  /* Indexing values */
    	               "stfpdx  14,%[index1],%[target]       \n\t"  /* F4=Q14 load */
    	               "stfpdx  15,%[index2],%[target]       \n\t"  /* F3=Q15load */
                     :          /* outputs */
                       "=m" (*target),
                       [index1] "=&b" (index1),
                       [index2] "=&b" (index2)
                     :            /* Inputs */
                       [source] "b" (source),         /* inputs */
                       [target] "b" (target)         /* inputs */
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14", "fr15"
                       );
           return 0 ;
  }
#define loadreg(Reg,Name,Offset) \
	"li %[index]," #Offset " \n\t" \
	"lfpdx " #Reg ",%[index],%[" #Name "] \n\t"

#define savereg(Reg,Name,Offset) \
	"li %[index]," #Offset " \n\t" \
	"stfpdx " #Reg ",%[index],%[" #Name "] \n\t"


static inline int torus_frame_payload_memcpy(
		torus_frame_payload * target,
		torus_frame_payload * source
    )
  {
    unsigned int index ;

    TRACEN(k_t_detail, "torus_payload_memcpy target=%p source=%p",target,source) ;
           asm  (
			loadreg(0,source,0x00)
			loadreg(1,source,0x10)
			loadreg(2,source,0x20)
			loadreg(3,source,0x30)
			loadreg(4,source,0x40)
			loadreg(5,source,0x50)
			loadreg(6,source,0x60)
			loadreg(7,source,0x70)
			loadreg(8,source,0x80)
			loadreg(9,source,0x90)
			loadreg(10,source,0xa0)
			loadreg(11,source,0xb0)
			loadreg(12,source,0xc0)
			loadreg(13,source,0xd0)
			loadreg(14,source,0xe0)
			savereg(0,target,0x00)
			savereg(1,target,0x10)
			savereg(2,target,0x20)
			savereg(3,target,0x30)
			savereg(4,target,0x40)
			savereg(5,target,0x50)
			savereg(6,target,0x60)
			savereg(7,target,0x70)
			savereg(8,target,0x80)
			savereg(9,target,0x90)
			savereg(10,target,0xa0)
			savereg(11,target,0xb0)
			savereg(12,target,0xc0)
			loadreg(0,source,0xf0)             /*  Speculate that we will need this soon */
			savereg(13,target,0xd0)
			loadreg(1,source,0x110)            /*  Speculate that we will need this soon */
			savereg(14,target,0xe0)
			loadreg(2,source,0x130)            /*  Speculate that we will need this soon */

                     :          /* outputs */
                       "=m" (*target),
                       [index] "=&b" (index)
                     :            /* Inputs */
                       [source] "b" (source),         /* inputs */
                       [target] "b" (target)         /* inputs */
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14"
                       );
           return 0 ;
  }

static inline int torus_frame_payload_memcpy_try1(
		torus_frame_payload * target,
		torus_frame_payload * source
    )
  {
    unsigned int index ;

    TRACEN(k_t_detail, "torus_payload_memcpy target=%p source=%p",target,source) ;
           asm  (
				loadreg(0,source,0x00)
				loadreg(2,source,0x20)
				loadreg(4,source,0x40)
				loadreg(1,source,0x10)
				savereg(0,target,0x00)
				loadreg(6,source,0x60)
				savereg(2,target,0x20)
				loadreg(3,source,0x30)
				savereg(4,target,0x40)
				loadreg(8,source,0x80)
				savereg(1,target,0x10)
				loadreg(5,source,0x50)
				savereg(6,target,0x60)
				loadreg(10,source,0xa0)
				savereg(3,target,0x30)
				loadreg(7,source,0x70)
				savereg(8,target,0x80)
				loadreg(12,source,0xc0)
				savereg(5,target,0x50)
				loadreg(9,source,0x90)
				savereg(10,target,0xa0)
				loadreg(14,source,0xe0)
				savereg(7,target,0x70)
				loadreg(11,source,0xb0)
				savereg(12,target,0xc0)
				loadreg(13,source,0xd0)
				savereg(9,target,0x90)
				savereg(14,target,0xe0)
				savereg(11,target,0xb0)
				savereg(13,target,0xd0)

                     :          /* outputs */
                       "=m" (*target),
                       [index] "=&b" (index)
                     :            /* Inputs */
                       [source] "b" (source),         /* inputs */
                       [target] "b" (target)         /* inputs */
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14"
                       );
           return 0 ;
  }

static inline int torus_frame_payload_memcpy_try2(
		torus_frame_payload * target,
		torus_frame_payload * source
    )
  {
    unsigned int index ;

    TRACEN(k_t_detail, "torus_payload_memcpy target=%p source=%p",target,source) ;
           asm  (
				loadreg(0,source,0x00)
				loadreg(1,source,0x10)
				loadreg(2,source,0x20)
				loadreg(4,source,0x40)
				savereg(0,target,0x00)
				loadreg(6,source,0x60)
				savereg(2,target,0x20)
				loadreg(3,source,0x30)
				savereg(4,target,0x40)
				loadreg(8,source,0x80)
				savereg(1,target,0x10)
				loadreg(5,source,0x50)
				savereg(6,target,0x60)
				loadreg(10,source,0xa0)
				savereg(3,target,0x30)
				loadreg(7,source,0x70)
				savereg(8,target,0x80)
				loadreg(12,source,0xc0)
				savereg(5,target,0x50)
				loadreg(9,source,0x90)
				savereg(10,target,0xa0)
				loadreg(14,source,0xe0)
				savereg(7,target,0x70)
				loadreg(11,source,0xb0)
				savereg(12,target,0xc0)
				loadreg(13,source,0xd0)
				savereg(9,target,0x90)
				savereg(14,target,0xe0)
				savereg(11,target,0xb0)
				savereg(13,target,0xd0)

                     :          /* outputs */
                       "=m" (*target),
                       [index] "=&b" (index)
                     :            /* Inputs */
                       [source] "b" (source),         /* inputs */
                       [target] "b" (target)         /* inputs */
                     : "fr0", "fr1", "fr2", /* Clobbers */
                       "fr3", "fr4", "fr5",
                       "fr6", "fr7", "fr8",
                       "fr9", "fr10", "fr11",
                       "fr12","fr13", "fr14"
                       );
           return 0 ;
  }
#endif
