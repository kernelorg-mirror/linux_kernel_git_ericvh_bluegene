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
 * \file bpcore/bgp_types.h
 */

#ifndef _BGP_TYPES_H_   /*  Prevent multiple inclusion. */
#define _BGP_TYPES_H_

#include <common/namespace.h>

__BEGIN_DECLS


#if !defined(__ASSEMBLY__) && !defined(__BGP_HIDE_STANDARD_TYPES__)

#include <common/alignment.h>

#ifdef _AIX
#include <inttypes.h>
#elif ! defined(__LINUX_KERNEL__)
#include <stdint.h>
#include <sys/types.h>
#else
#include <linux/types.h>
#endif


typedef  int8_t  _bgp_i8_t;
typedef uint8_t  _bgp_u8_t;
typedef  int16_t _bgp_i16_t;
typedef uint16_t _bgp_u16_t;
typedef  int32_t _bgp_i32_t;
typedef uint32_t _bgp_u32_t;
typedef  int64_t _bgp_i64_t;
typedef uint64_t _bgp_u64_t;

typedef union T_BGP_QuadWord
               {
               uint8_t   ub[ 16];
               uint16_t  us[  8];
               uint32_t  ul[  4];
               uint64_t ull[ 2];
               float      f[   4];
               double     d[   2];
               }
               ALIGN_QUADWORD _bgp_QuadWord_t;

typedef _bgp_QuadWord_t _QuadWord_t;

#endif  /*  !__ASSEMBLY__ && !__BGP_HIDE_STANDARD_TYPES__ */

__END_DECLS

#endif  /*  Add nothing below this line. */
