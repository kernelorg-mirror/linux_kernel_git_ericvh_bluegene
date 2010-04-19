/*
 * Andrew Tauferner
 *
 * Copyright 2006, 2007 International Business Machines
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __BLUEGENE_RAS_H__
#define __BLUEGENE_RAS_H__


typedef enum {
        bg_comp_none =                0x00,
        bg_comp_kernel =              0x01,
        bg_comp_application =         0x02,
        bg_comp_card =                0x03,
        bg_comp_mc =                  0x04,
        bg_comp_mcserver =            0x05,
        bg_comp_mmcs =                0x06,
        bg_comp_diags =               0x07,

        bg_comp_max                   // always last
} bg_ras_comp;


typedef enum {
        bg_subcomp_none =             0x00,
        bg_subcomp_ppc450 =           0x01,
        bg_subcomp_fpu =              0x02,
        bg_subcomp_snoop =            0x03,
        bg_subcomp_dp0 =              0x04,
        bg_subcomp_dp1 =              0x05,
        bg_subcomp_l2 =               0x06,
        bg_subcomp_l3 =               0x07,
        bg_subcomp_ddr =              0x08,
        bg_subcomp_sram =             0x09,
        bg_subcomp_dma =              0x0a,
        bg_subcomp_testint =          0x0b,
        bg_subcomp_testint_dcr =      0x0c,
        bg_subcomp_lockbox =          0x0d,
        bg_subcomp_plb =              0x0e,
        bg_subcomp_collective =       0x0f,
        bg_subcomp_torus =            0x10,
        bg_subcomp_globint =          0x11,
        bg_subcomp_serdes =           0x12,
        bg_subcomp_upc =              0x13,
        bg_subcomp_dcr =              0x14,
        bg_subcomp_bic =              0x15,
        bg_subcomp_devbus =           0x16,
        bg_subcomp_netbus =           0x17,
        bg_subcomp_envmon =           0x18,
        bg_subcomp_tomal =            0x19,
        bg_subcomp_xemac =            0x1a,
        bg_subcomp_phy =              0x1b,
        bg_subcomp_bootloader =       0x1c,
        bg_subcomp_cnk =              0x1d,
        bg_subcomp_ciod =             0x1e,
        bg_subcomp_svc_host =         0x1f,
        bg_subcomp_diagnostic =       0x20,
        bg_subcomp_application =      0x21,
        bg_subcomp_linux =            0x22,
	bg_subcomp_cns = 	      0x23,
	bg_subcomp_e10000 = 	      0x24,

        bg_subcomp_max                // always last
} bg_ras_subcomp;


typedef enum {
	bg_code_none =				0x00,
	bg_code_halted = 			0x01,
	bg_code_script_error =			0x02,
	bg_code_boot_complete = 		0x03,
	bg_code_panic =				0x04,
	bg_code_oops = 				0x05,
	bg_code_tty_alloc_failure = 		0x06,
	bg_code_tty_reg_failure	=		0x07,
	bg_code_mbox_thread_create_failure = 	0x08,
	bg_code_sysrq_thread_create_failure =	0x09,
	bg_code_oom =				0x0a,
	bg_ras_max			// always last
} bg_ras_code;


/*
 * bg_ras -- RAS data structure
 */
#define BG_RAS_DATA_MAX  216
typedef struct {
        unsigned short comp;
        unsigned short subcomp;
        unsigned short code;
	unsigned short length;
	unsigned char  data[BG_RAS_DATA_MAX];
} bg_ras;


#define BG_RAS_FILE "/proc/ras"
#define BG_RAS_ASCII_FILE "/proc/ras_ascii"


#endif   // __BLUEGENE_RAS_H__
