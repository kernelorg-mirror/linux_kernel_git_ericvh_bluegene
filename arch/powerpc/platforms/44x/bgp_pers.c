/*
 *
 * Blue Gene personality /proc interface with the control system
 *
 * Copyright 2003,2005 International Business Machines
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * User apps can mmap /proc/personality to directly access the binary
 * personality in SRAM (see bglpersonality.h), or they can read
 * /proc/personality.sh which expands to shell commands (so it can be sourced)
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mm.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <asm/bluegene.h>
#include <asm/bgp_personality.h>


static struct proc_dir_entry *personality_proc_entry = NULL;
static struct proc_dir_entry *personality_sh_proc_entry = NULL;


static BGP_Personality_t bgpers;

/* Binary personality interface.  Doesn't need to be fast. */
static int bgpersonality_read(char *page, char **start, off_t offset,
			      int count, int *eof, void *data)
{
	bluegene_getPersonality(&bgpers, count);
	memcpy(page, &bgpers, count);
	*eof = 1;

	return count;
}


static void* bgpers_sh_seq_start(struct seq_file* f,
				loff_t* pos)
{
	return *pos <= 32 ? (void*) pos : (void*) NULL;
}


static void* bgpers_sh_seq_next(struct seq_file* f,
				void* v,
				loff_t* pos)
{
	return  ++(*pos) <= 32 ? (void*) pos : (void*) NULL;
}


static void bgpers_sh_seq_stop(struct seq_file* f,
			       void* v)
{
	return;
}


/*  Produce a personality in a form parsable by a shell. */
static int bgpers_sh_seq_show(struct seq_file* f,
				void* v)
{
	loff_t offset = *((loff_t*) v);
	BGP_UCI_ComputeCard_t* uci;

	bluegene_getPersonality(&bgpers, sizeof(bgpers));
	uci = (BGP_UCI_ComputeCard_t*) &bgpers.Kernel_Config.UniversalComponentIdentifier;

	switch((unsigned long) offset) {
		case 0:
			seq_printf(f, "BG_UCI=%08x\n",
				   bgpers.Kernel_Config.UniversalComponentIdentifier);
			break;
		case 1:
			seq_printf(f, "BG_LOCATION=R%1x%1x-M%c-N%02d-J%02d\n",
				   uci->RackRow, uci->RackColumn, (uci->Midplane ? '1' : '0'),
				   uci->NodeCard, uci->ComputeCard);
			break;
		case 2:
			seq_printf(f, "BG_MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
				   bgpers.Ethernet_Config.EmacID[0],
				   bgpers.Ethernet_Config.EmacID[1],
				   bgpers.Ethernet_Config.EmacID[2],
				   bgpers.Ethernet_Config.EmacID[3],
				   bgpers.Ethernet_Config.EmacID[4],
				   bgpers.Ethernet_Config.EmacID[5]);
			break;
		case 3:
			seq_printf(f, "BG_IP=%d.%d.%d.%d\n",
				   bgpers.Ethernet_Config.IPAddress.octet[12],
				   bgpers.Ethernet_Config.IPAddress.octet[13],
				   bgpers.Ethernet_Config.IPAddress.octet[14],
				   bgpers.Ethernet_Config.IPAddress.octet[15]);
			break;
		case 4:
			seq_printf(f, "BG_NETMASK=%d.%d.%d.%d\n",
				   bgpers.Ethernet_Config.IPNetmask.octet[12],
				   bgpers.Ethernet_Config.IPNetmask.octet[13],
				   bgpers.Ethernet_Config.IPNetmask.octet[14],
				   bgpers.Ethernet_Config.IPNetmask.octet[15]);
			break;
		case 5:
			seq_printf(f, "BG_BROADCAST=%d.%d.%d.%d\n",
				   bgpers.Ethernet_Config.IPBroadcast.octet[12],
				   bgpers.Ethernet_Config.IPBroadcast.octet[13],
				   bgpers.Ethernet_Config.IPBroadcast.octet[14],
				   bgpers.Ethernet_Config.IPBroadcast.octet[15]);
			break;
		case 6:
			seq_printf(f, "BG_GATEWAY=%d.%d.%d.%d\n",
				   bgpers.Ethernet_Config.IPGateway.octet[12],
			   	   bgpers.Ethernet_Config.IPGateway.octet[13],
			    	   bgpers.Ethernet_Config.IPGateway.octet[14],
			  	   bgpers.Ethernet_Config.IPGateway.octet[15]);
			break;
		case 7:
        		seq_printf(f, "BG_MTU=%d\n", bgpers.Ethernet_Config.MTU);
			break;
		case 8:
			seq_printf(f, "BG_FS=%d.%d.%d.%d\n",
				   bgpers.Ethernet_Config.NFSServer.octet[12],
				   bgpers.Ethernet_Config.NFSServer.octet[13],
				   bgpers.Ethernet_Config.NFSServer.octet[14],
				   bgpers.Ethernet_Config.NFSServer.octet[15]);
			break;
		case 9:
			seq_printf(f, "BG_EXPORTDIR=\"%s\"\n", bgpers.Ethernet_Config.NFSExportDir);
			break;
		case 10:
			seq_printf(f, "BG_SIMULATION=%d\n",
			(bgpers.Kernel_Config.NodeConfig & BGP_PERS_ENABLE_Simulation ? 1 : 0));
			break;
		case 11:
			seq_printf(f, "BG_PSETNUM=%d\n", bgpers.Network_Config.PSetNum);
			break;
		case 12:
			seq_printf(f, "BG_NUMPSETS=%d\n", bgpers.Network_Config.IOnodes);
			break;
		case 13:
			seq_printf(f, "BG_NODESINPSET=%d\n", bgpers.Network_Config.PSetSize);
			break;
		case 14:
			seq_printf(f, "BG_XSIZE=%d\n", bgpers.Network_Config.Xnodes);
			break;
		case 15:
			seq_printf(f, "BG_YSIZE=%d\n", bgpers.Network_Config.Ynodes);
			break;
		case 16:
			seq_printf(f, "BG_ZSIZE=%d\n", bgpers.Network_Config.Znodes);
			break;
		case 17:
			seq_printf(f, "BG_VERBOSE=%d\n", (bgpers.Kernel_Config.TraceConfig & BGP_TRACE_VERBOSE) ? 1 : 0);
			break;
		case 18:
			switch (bgpers.Network_Config.PSetSize) {
				case 16:
					seq_printf(f, "BG_PSETSIZE=\"4 2 2\"\n");
					break;
				case 32:
					seq_printf(f, "BG_PSETSIZE=\"4 4 2\"\n");
					break;
				case 64:
					seq_printf(f, "BG_PSETSIZE=\"4 4 4\"\n");
					break;
				case 128:
					seq_printf(f, "BG_PSETSIZE=\"4 4 8\"\n");
					break;
				case 256:
					seq_printf(f, "BG_PSETSIZE=\"8 4 8\"\n");
					break;
				case 512:
					seq_printf(f, "BG_PSETSIZE=\"8 8 8\"\n");
					break;
				default:
					seq_printf(f, "BG_PSETSIZE=\"? ? ?\"\n");
			}
			break;
		case 19:
/* 			if (bgpers.Network_Config.RankInPSet) */
/* 				// Not an IO node so display pset origin. */
				seq_printf(f, "BG_PSETORG=\"%d %d %d\"\n",
					   bgpers.Network_Config.Xcoord,
					   bgpers.Network_Config.Ycoord,
					   bgpers.Network_Config.Zcoord);
			break;
		case 20:
			seq_printf(f, "BG_CLOCKHZ=%d\n", bgpers.Kernel_Config.FreqMHz);
			break;
		case 21:
			seq_printf(f, "BG_GLINTS=%d\n",
				   (bgpers.Kernel_Config.NodeConfig & BGP_PERS_ENABLE_GlobalInts) ? 1 : 0);
			break;
		case 22:
			seq_printf(f, "BG_ISTORUS=\"%s%s%s\"\n",
				   (bgpers.Kernel_Config.NodeConfig & BGP_PERS_ENABLE_TorusMeshX) ? "X" : "",
                        	   (bgpers.Kernel_Config.NodeConfig & BGP_PERS_ENABLE_TorusMeshY) ? "Y" : "",
                        	   (bgpers.Kernel_Config.NodeConfig & BGP_PERS_ENABLE_TorusMeshZ) ? "Z" : "");
			 break;
		case 23: {
			char blockID[BGP_PERSONALITY_LEN_NFSDIR+1];

			strncpy(blockID, bgpers.Ethernet_Config.NFSMountDir, sizeof(blockID));
			blockID[sizeof(blockID)-1] = '\0';
			seq_printf(f, "BG_BLOCKID=\"%s\"\n", blockID);
			break;
		}
		case 24:
			seq_printf(f, "BG_SN=%d.%d.%d.%d\n",
				   bgpers.Ethernet_Config.serviceNode.octet[12],
				   bgpers.Ethernet_Config.serviceNode.octet[13],
				   bgpers.Ethernet_Config.serviceNode.octet[14],
				   bgpers.Ethernet_Config.serviceNode.octet[15]);
			break;
		case 25:
			seq_printf(f, "BG_IS_IO_NODE=%d\n", (bgpers.Network_Config.RankInPSet ? 0 : 1));
			break;
		case 26:
			seq_printf(f, "BG_RANK_IN_PSET=%d\nBG_RANK=%d\n",
					bgpers.Network_Config.RankInPSet,
					bgpers.Network_Config.Rank);
			break;
		case 27:
			seq_printf(f, "BG_IP_OVER_COL=%d\n", (bgpers.Block_Config & BGP_PERS_BLKCFG_IPOverCollective) ? 1 : 0);
			break;
		case 28:
			seq_printf(f, "BG_IP_OVER_TOR=%d\n", (bgpers.Block_Config & BGP_PERS_BLKCFG_IPOverTorus) ? 1 : 0);
			break;
		case 29:
			seq_printf(f, "BG_IP_OVER_COL_VC=%d\n", (bgpers.Block_Config & BGP_PERS_BLKCFG_IPOverCollectiveVC) ? 1 : 0);
			break;
		case 30:
			if ((bgpers.Block_Config & BGP_PERS_BLKCFG_CIOModeSel(3)) == BGP_PERS_BLKCFG_CIOModeSel(BGP_PERS_BLKCFG_CIOMode_MuxOnly))
				seq_printf(f, "BG_CIO_MODE=MUX_ONLY\n");
			else if ((bgpers.Block_Config & BGP_PERS_BLKCFG_CIOModeSel(3)) == BGP_PERS_BLKCFG_CIOModeSel(BGP_PERS_BLKCFG_CIOMode_None))
				seq_printf(f, "BG_CIO_MODE=NONE\n");
			else if ((bgpers.Block_Config & BGP_PERS_BLKCFG_CIOModeSel(3)) == BGP_PERS_BLKCFG_CIOModeSel(BGP_PERS_BLKCFG_CIOMode_Full))
				seq_printf(f, "BG_CIO_MODE=FULL\n");
			else
				seq_printf(f, "BG_CIO_MODE=UNKNOWN\n");
			break;
		case 31:
			if ((bgpers.Block_Config & BGP_PERS_BLKCFG_bgsysFSSel(3)) == BGP_PERS_BLKCFG_bgsysFSSel(BGP_PERS_BLKCFG_bgsys_NFSv3))
				seq_printf(f, "BG_BGSYS_FS_TYPE=NFSv3\n");
			else if ((bgpers.Block_Config & BGP_PERS_BLKCFG_bgsysFSSel(3)) == BGP_PERS_BLKCFG_bgsysFSSel(BGP_PERS_BLKCFG_bgsys_NFSv4))
                                seq_printf(f, "BG_BGSYS_FS_TYPE=NFSv4\n");
			else
				seq_printf(f, "BG_BGSYS_FS_TYPE=UNKNOWN\n");
			break;
		case 32:
			seq_printf(f, "BG_HTC_MODE=%d\n",
                                   (bgpers.Kernel_Config.NodeConfig & BGP_PERS_ENABLE_HighThroughput) ? 1 : 0);
			break;
		default:
			seq_printf(f, "Illegal offset %d\n", (unsigned int) offset);
	}

	return 0;
}

void bgpersonality_cleanup_module(void)
{
	if (personality_proc_entry) {
		remove_proc_entry(personality_proc_entry->name, NULL);
	}

	if (personality_sh_proc_entry) {
		remove_proc_entry(personality_sh_proc_entry->name, NULL);
	}
}



static struct seq_operations bgpers_sh_seq_ops = {
	.start = bgpers_sh_seq_start,
	.next = bgpers_sh_seq_next,
	.stop = bgpers_sh_seq_stop,
	.show = bgpers_sh_seq_show
};



static int bgpers_sh_proc_open(struct inode* inode,
			       struct file* f)
{
	return seq_open(f, &bgpers_sh_seq_ops);
}


static struct file_operations bgpers_sh_fops = {
	.owner = THIS_MODULE,
	.open = bgpers_sh_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};


int bgpersonality_init_module(void)
{
	personality_proc_entry = create_proc_read_entry("personality", 0644, NULL,
							 bgpersonality_read, (void *) 0);
	if (!personality_proc_entry)
		goto out;

	personality_sh_proc_entry = create_proc_entry("personality.sh", 0, NULL);
	if (!personality_sh_proc_entry)
		goto out;
	else
		personality_sh_proc_entry->proc_fops = &bgpers_sh_fops;

	return 0;

out:
	bgpersonality_cleanup_module();

	return -ENOMEM;
}


module_init(bgpersonality_init_module);
module_exit(bgpersonality_cleanup_module);
