/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2010
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
 * Author: Chris Ward <tjcw@uk.ibm.com>
 *
 * Description: Blue Gene low-level driver for collective and torus
 *
 *
 ********************************************************************/
#ifndef __BGP_NET_TRACEFLAGS_H__
#define __BGP_NET_TRACEFLAGS_H__

enum {
  k_t_general       = 0x01 ,
  k_t_lowvol        = 0x02 ,
  k_t_irqflow       = 0x04 ,
  k_t_irqflow_rcv   = 0x08 ,
  k_t_protocol      = 0x10 ,
  k_t_detail        = 0x20 ,
  k_t_fifocontents  = 0x40 ,
  k_t_toruspkt      = 0x80 ,
  k_t_bgcolpkt      = 0x80 ,
  k_t_init          = 0x100 ,
  k_t_request       = 0x200 ,
  k_t_error         = 0x400 ,
  k_t_sync          = 0x800 ,
  k_t_api           = 0x1000 ,
  k_t_diagnosis     = 0x2000 ,
  k_t_congestion    = 0x4000 ,
  k_t_startxmit     = 0x8000 ,
  k_t_napi          = 0x10000 ,
  k_t_scattergather = 0x20000 ,
  k_t_flowcontrol   = 0x40000 ,
  k_t_entryexit     = 0x80000 ,
  k_t_dmacopy       = 0x100000 ,
  k_t_fpucopy       = 0x200000 ,
  k_t_sgdiag        = 0x400000 ,
  k_t_sgdiag_detail = 0x800000 ,
  k_t_inject_detail = 0x1000000 ,
};

#endif
