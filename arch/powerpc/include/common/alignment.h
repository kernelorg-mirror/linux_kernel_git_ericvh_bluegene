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
 * \file common/alignment.h
 */

#ifndef	_ALIGNMENT_H_  /*  Prevent multiple inclusion */
#define	_ALIGNMENT_H_



#include <common/namespace.h>

__BEGIN_DECLS

#if defined(__ASSEMBLY__)

#define ALIGN_L1_DIRTYBIT  3
#define ALIGN_QUADWORD     4
#define ALIGN_L1_CACHE     5
#define ALIGN_L1I_CACHE    5
#define ALIGN_L1D_CACHE    5
#define ALIGN_L3_CACHE     7

#elif defined(__GNUC__) || defined(__xlC__)

#define ALIGN_L1_DIRTYBIT __attribute__ ((aligned (  8)))
#define ALIGN_QUADWORD    __attribute__ ((aligned ( 16)))
#define ALIGN_L1_CACHE    __attribute__ ((aligned ( 32)))
#define ALIGN_L1I_CACHE   __attribute__ ((aligned ( 32)))
#define ALIGN_L1D_CACHE   __attribute__ ((aligned ( 32)))
#define ALIGN_L3_CACHE    __attribute__ ((aligned (128)))

#else

#warning "Need alignment directives for your compiler!"

#define ALIGN_QUADWORD
#define ALIGN_L1_CACHE
#define ALIGN_L1I_CACHE
#define ALIGN_L1D_CACHE
#define ALIGN_L3_CACHE

#endif  /*  __ASSEMBLY__ */

__END_DECLS



#endif  /*  Add nothing below this line */
