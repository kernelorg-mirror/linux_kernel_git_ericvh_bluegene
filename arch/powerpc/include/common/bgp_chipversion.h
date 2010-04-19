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
 * \file common/bgp_chipversion.h
 */

#ifndef	_BGP_CHIPVERSION_H_  /*  Prevent multiple inclusion */
#define	_BGP_CHIPVERSION_H_



#include <common/namespace.h>

__BEGIN_DECLS

#define BGP_CHIPVERSION_DD2

#if defined BGP_CHIPVERSION_DD1
/*   Settings for DD1 */
#define BGP_DD1_WORKAROUNDS 1

#elif defined BGP_CHIPVERSION_DD2
/*   Settings for DD2 */

#else
/*   */
#error "Invalid chip version setting"

#endif


__END_DECLS



#endif  /*  Add nothing below this line. */
