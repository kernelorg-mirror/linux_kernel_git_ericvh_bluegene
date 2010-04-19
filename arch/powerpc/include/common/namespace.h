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
 * \file common/namespace.h
 */

#ifndef	_NAMESPACE_H_  /*  Prevent multiple inclusion */
#define	_NAMESPACE_H_




#if !defined(__ASSEMBLY__) && defined(__cplusplus)
#define __BEGIN_DECLS extern "C" {
#define __C_LINKAGE extern "C"
#else
#define __BEGIN_DECLS
#define __C_LINKAGE
#endif


#if !defined(__ASSEMBLY__) && defined(__cplusplus)
#define __END_DECLS }
#else
#define __END_DECLS
#endif




#endif  /*  Add nothing below this line */
