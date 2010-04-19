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
 *
 * Description: Statistic collection for Blue Gene low-level driver for sockets over torus
 *
 *
 ********************************************************************/
#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/fs.h>
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
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/highmem.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>

#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/bootmem.h>

#include <linux/alignment_histograms.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/time.h>
#include <linux/vmalloc.h>

#include <linux/dma-mapping.h>

#include <net/inet_connection_sock.h>
#include <net/inet_sock.h>
#include <net/inet_hashtables.h>
#include <net/tcp.h>


static int  bgp_statistics_init    (void);
static void bgp_statistics_cleanup (void);

module_init(bgp_statistics_init);
module_exit(bgp_statistics_cleanup);


MODULE_DESCRIPTION("BG/P statistics driver");
MODULE_LICENSE("GPL");

#ifndef CTL_UNNUMBERED
#define CTL_UNNUMBERED -2
#endif

/*  Parameters, statistics, and debugging */
#if defined(CONFIG_DEBUG_ALIGNMENT_HISTOGRAM)
struct alignment_histogram al_histogram ;
#endif

static struct ctl_path bgp_statistics_ctl_path[] = {
		{ .procname = "bgp", .ctl_name = 0, },
	{ .procname = "statistics", .ctl_name = 0, },
/* 	{ .procname = "torus", .ctl_name = 0, }, */
	{ },
};

#define CTL_PARAM_EXT(Name,Var)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .data           = &(Var),              \
          .maxlen         = sizeof(int),         \
          .mode           = 0644,                \
          .proc_handler   = &proc_dointvec       \
  }

#define CTL_PARAM_EXT_LL(Name,Var)                      \
  {                                              \
          .ctl_name       = CTL_UNNUMBERED,      \
          .procname       = Name ,               \
          .data           = &(Var),              \
          .maxlen         = 2*sizeof(int),       \
          .mode           = 0644,                \
          .proc_handler   = &proc_dointvec       \
  }


struct ctl_table bgp_statistics_table[] = {
#if defined(CONFIG_DEBUG_ALIGNMENT_HISTOGRAM)
        CTL_PARAM_EXT("ah_min",al_histogram.min_size_of_interest) ,
        CTL_PARAM_EXT("sah0",AL_HISTOGRAM(src_alignment_histogram_crc,0)) ,
        CTL_PARAM_EXT("sah1",AL_HISTOGRAM(src_alignment_histogram_crc,1)) ,
        CTL_PARAM_EXT("sah2",AL_HISTOGRAM(src_alignment_histogram_crc,2)) ,
        CTL_PARAM_EXT("sah3",AL_HISTOGRAM(src_alignment_histogram_crc,3)) ,
        CTL_PARAM_EXT("sah4",AL_HISTOGRAM(src_alignment_histogram_crc,4)) ,
        CTL_PARAM_EXT("sah5",AL_HISTOGRAM(src_alignment_histogram_crc,5)) ,
        CTL_PARAM_EXT("sah6",AL_HISTOGRAM(src_alignment_histogram_crc,6)) ,
        CTL_PARAM_EXT("sah7",AL_HISTOGRAM(src_alignment_histogram_crc,7)) ,
        CTL_PARAM_EXT("sah8",AL_HISTOGRAM(src_alignment_histogram_crc,8)) ,
        CTL_PARAM_EXT("sah9",AL_HISTOGRAM(src_alignment_histogram_crc,9)) ,
        CTL_PARAM_EXT("saha",AL_HISTOGRAM(src_alignment_histogram_crc,10)) ,
        CTL_PARAM_EXT("sahb",AL_HISTOGRAM(src_alignment_histogram_crc,11)) ,
        CTL_PARAM_EXT("sahc",AL_HISTOGRAM(src_alignment_histogram_crc,12)) ,
        CTL_PARAM_EXT("sahd",AL_HISTOGRAM(src_alignment_histogram_crc,13)) ,
        CTL_PARAM_EXT("sahe",AL_HISTOGRAM(src_alignment_histogram_crc,14)) ,
        CTL_PARAM_EXT("sahf",AL_HISTOGRAM(src_alignment_histogram_crc,15)) ,
        CTL_PARAM_EXT("dah0",AL_HISTOGRAM(dst_alignment_histogram_crc,0)) ,
        CTL_PARAM_EXT("dah1",AL_HISTOGRAM(dst_alignment_histogram_crc,1)) ,
        CTL_PARAM_EXT("dah2",AL_HISTOGRAM(dst_alignment_histogram_crc,2)) ,
        CTL_PARAM_EXT("dah3",AL_HISTOGRAM(dst_alignment_histogram_crc,3)) ,
        CTL_PARAM_EXT("dah4",AL_HISTOGRAM(dst_alignment_histogram_crc,4)) ,
        CTL_PARAM_EXT("dah5",AL_HISTOGRAM(dst_alignment_histogram_crc,5)) ,
        CTL_PARAM_EXT("dah6",AL_HISTOGRAM(dst_alignment_histogram_crc,6)) ,
        CTL_PARAM_EXT("dah7",AL_HISTOGRAM(dst_alignment_histogram_crc,7)) ,
        CTL_PARAM_EXT("dah8",AL_HISTOGRAM(dst_alignment_histogram_crc,8)) ,
        CTL_PARAM_EXT("dah9",AL_HISTOGRAM(dst_alignment_histogram_crc,9)) ,
        CTL_PARAM_EXT("daha",AL_HISTOGRAM(dst_alignment_histogram_crc,10)) ,
        CTL_PARAM_EXT("dahb",AL_HISTOGRAM(dst_alignment_histogram_crc,11)) ,
        CTL_PARAM_EXT("dahc",AL_HISTOGRAM(dst_alignment_histogram_crc,12)) ,
        CTL_PARAM_EXT("dahd",AL_HISTOGRAM(dst_alignment_histogram_crc,13)) ,
        CTL_PARAM_EXT("dahe",AL_HISTOGRAM(dst_alignment_histogram_crc,14)) ,
        CTL_PARAM_EXT("dahf",AL_HISTOGRAM(dst_alignment_histogram_crc,15)) ,
        CTL_PARAM_EXT("rah0",AL_HISTOGRAM(rel_alignment_histogram_crc,0)) ,
        CTL_PARAM_EXT("rah1",AL_HISTOGRAM(rel_alignment_histogram_crc,1)) ,
        CTL_PARAM_EXT("rah2",AL_HISTOGRAM(rel_alignment_histogram_crc,2)) ,
        CTL_PARAM_EXT("rah3",AL_HISTOGRAM(rel_alignment_histogram_crc,3)) ,
        CTL_PARAM_EXT("rah4",AL_HISTOGRAM(rel_alignment_histogram_crc,4)) ,
        CTL_PARAM_EXT("rah5",AL_HISTOGRAM(rel_alignment_histogram_crc,5)) ,
        CTL_PARAM_EXT("rah6",AL_HISTOGRAM(rel_alignment_histogram_crc,6)) ,
        CTL_PARAM_EXT("rah7",AL_HISTOGRAM(rel_alignment_histogram_crc,7)) ,
        CTL_PARAM_EXT("rah8",AL_HISTOGRAM(rel_alignment_histogram_crc,8)) ,
        CTL_PARAM_EXT("rah9",AL_HISTOGRAM(rel_alignment_histogram_crc,9)) ,
        CTL_PARAM_EXT("raha",AL_HISTOGRAM(rel_alignment_histogram_crc,10)) ,
        CTL_PARAM_EXT("rahb",AL_HISTOGRAM(rel_alignment_histogram_crc,11)) ,
        CTL_PARAM_EXT("rahc",AL_HISTOGRAM(rel_alignment_histogram_crc,12)) ,
        CTL_PARAM_EXT("rahd",AL_HISTOGRAM(rel_alignment_histogram_crc,13)) ,
        CTL_PARAM_EXT("rahe",AL_HISTOGRAM(rel_alignment_histogram_crc,14)) ,
        CTL_PARAM_EXT("rahf",AL_HISTOGRAM(rel_alignment_histogram_crc,15)) ,
        CTL_PARAM_EXT("scah0",AL_HISTOGRAM(src_alignment_histogram_copy,0)) ,
        CTL_PARAM_EXT("scah1",AL_HISTOGRAM(src_alignment_histogram_copy,1)) ,
        CTL_PARAM_EXT("scah2",AL_HISTOGRAM(src_alignment_histogram_copy,2)) ,
        CTL_PARAM_EXT("scah3",AL_HISTOGRAM(src_alignment_histogram_copy,3)) ,
        CTL_PARAM_EXT("scah4",AL_HISTOGRAM(src_alignment_histogram_copy,4)) ,
        CTL_PARAM_EXT("scah5",AL_HISTOGRAM(src_alignment_histogram_copy,5)) ,
        CTL_PARAM_EXT("scah6",AL_HISTOGRAM(src_alignment_histogram_copy,6)) ,
        CTL_PARAM_EXT("scah7",AL_HISTOGRAM(src_alignment_histogram_copy,7)) ,
        CTL_PARAM_EXT("scah8",AL_HISTOGRAM(src_alignment_histogram_copy,8)) ,
        CTL_PARAM_EXT("scah9",AL_HISTOGRAM(src_alignment_histogram_copy,9)) ,
        CTL_PARAM_EXT("scaha",AL_HISTOGRAM(src_alignment_histogram_copy,10)) ,
        CTL_PARAM_EXT("scahb",AL_HISTOGRAM(src_alignment_histogram_copy,11)) ,
        CTL_PARAM_EXT("scahc",AL_HISTOGRAM(src_alignment_histogram_copy,12)) ,
        CTL_PARAM_EXT("scahd",AL_HISTOGRAM(src_alignment_histogram_copy,13)) ,
        CTL_PARAM_EXT("scahe",AL_HISTOGRAM(src_alignment_histogram_copy,14)) ,
        CTL_PARAM_EXT("scahf",AL_HISTOGRAM(src_alignment_histogram_copy,15)) ,
        CTL_PARAM_EXT("dcah0",AL_HISTOGRAM(dst_alignment_histogram_copy,0)) ,
        CTL_PARAM_EXT("dcah1",AL_HISTOGRAM(dst_alignment_histogram_copy,1)) ,
        CTL_PARAM_EXT("dcah2",AL_HISTOGRAM(dst_alignment_histogram_copy,2)) ,
        CTL_PARAM_EXT("dcah3",AL_HISTOGRAM(dst_alignment_histogram_copy,3)) ,
        CTL_PARAM_EXT("dcah4",AL_HISTOGRAM(dst_alignment_histogram_copy,4)) ,
        CTL_PARAM_EXT("dcah5",AL_HISTOGRAM(dst_alignment_histogram_copy,5)) ,
        CTL_PARAM_EXT("dcah6",AL_HISTOGRAM(dst_alignment_histogram_copy,6)) ,
        CTL_PARAM_EXT("dcah7",AL_HISTOGRAM(dst_alignment_histogram_copy,7)) ,
        CTL_PARAM_EXT("dcah8",AL_HISTOGRAM(dst_alignment_histogram_copy,8)) ,
        CTL_PARAM_EXT("dcah9",AL_HISTOGRAM(dst_alignment_histogram_copy,9)) ,
        CTL_PARAM_EXT("dcaha",AL_HISTOGRAM(dst_alignment_histogram_copy,10)) ,
        CTL_PARAM_EXT("dcahb",AL_HISTOGRAM(dst_alignment_histogram_copy,11)) ,
        CTL_PARAM_EXT("dcahc",AL_HISTOGRAM(dst_alignment_histogram_copy,12)) ,
        CTL_PARAM_EXT("dcahd",AL_HISTOGRAM(dst_alignment_histogram_copy,13)) ,
        CTL_PARAM_EXT("dcahe",AL_HISTOGRAM(dst_alignment_histogram_copy,14)) ,
        CTL_PARAM_EXT("dcahf",AL_HISTOGRAM(dst_alignment_histogram_copy,15)) ,
        CTL_PARAM_EXT("rcah0",AL_HISTOGRAM(rel_alignment_histogram_copy,0)) ,
        CTL_PARAM_EXT("rcah1",AL_HISTOGRAM(rel_alignment_histogram_copy,1)) ,
        CTL_PARAM_EXT("rcah2",AL_HISTOGRAM(rel_alignment_histogram_copy,2)) ,
        CTL_PARAM_EXT("rcah3",AL_HISTOGRAM(rel_alignment_histogram_copy,3)) ,
        CTL_PARAM_EXT("rcah4",AL_HISTOGRAM(rel_alignment_histogram_copy,4)) ,
        CTL_PARAM_EXT("rcah5",AL_HISTOGRAM(rel_alignment_histogram_copy,5)) ,
        CTL_PARAM_EXT("rcah6",AL_HISTOGRAM(rel_alignment_histogram_copy,6)) ,
        CTL_PARAM_EXT("rcah7",AL_HISTOGRAM(rel_alignment_histogram_copy,7)) ,
        CTL_PARAM_EXT("rcah8",AL_HISTOGRAM(rel_alignment_histogram_copy,8)) ,
        CTL_PARAM_EXT("rcah9",AL_HISTOGRAM(rel_alignment_histogram_copy,9)) ,
        CTL_PARAM_EXT("rcaha",AL_HISTOGRAM(rel_alignment_histogram_copy,10)) ,
        CTL_PARAM_EXT("rcahb",AL_HISTOGRAM(rel_alignment_histogram_copy,11)) ,
        CTL_PARAM_EXT("rcahc",AL_HISTOGRAM(rel_alignment_histogram_copy,12)) ,
        CTL_PARAM_EXT("rcahd",AL_HISTOGRAM(rel_alignment_histogram_copy,13)) ,
        CTL_PARAM_EXT("rcahe",AL_HISTOGRAM(rel_alignment_histogram_copy,14)) ,
        CTL_PARAM_EXT("rcahf",AL_HISTOGRAM(rel_alignment_histogram_copy,15)) ,
        CTL_PARAM_EXT("tagh0",AL_HISTOGRAM(tagged,0)) ,
        CTL_PARAM_EXT("tagh1",AL_HISTOGRAM(tagged,1)) ,
        CTL_PARAM_EXT("tagh2",AL_HISTOGRAM(tagged,2)) ,
        CTL_PARAM_EXT("tagh3",AL_HISTOGRAM(tagged,3)) ,
        CTL_PARAM_EXT("tagh4",AL_HISTOGRAM(tagged,4)) ,
        CTL_PARAM_EXT("tagh5",AL_HISTOGRAM(tagged,5)) ,
        CTL_PARAM_EXT("tagh6",AL_HISTOGRAM(tagged,6)) ,
        CTL_PARAM_EXT("tagh7",AL_HISTOGRAM(tagged,7)) ,
        CTL_PARAM_EXT("tagh8",AL_HISTOGRAM(tagged,8)) ,
        CTL_PARAM_EXT("tagh9",AL_HISTOGRAM(tagged,9)) ,
        CTL_PARAM_EXT("tagha",AL_HISTOGRAM(tagged,10)) ,
        CTL_PARAM_EXT("taghb",AL_HISTOGRAM(tagged,11)) ,
        CTL_PARAM_EXT("taghc",AL_HISTOGRAM(tagged,12)) ,
        CTL_PARAM_EXT("taghd",AL_HISTOGRAM(tagged,13)) ,
        CTL_PARAM_EXT("taghe",AL_HISTOGRAM(tagged,14)) ,
        CTL_PARAM_EXT("taghf",AL_HISTOGRAM(tagged,15)) ,
        CTL_PARAM_EXT_LL("qcopy",al_histogram.qcopybytes) ,
        CTL_PARAM_EXT_LL("copy",al_histogram.copybytes) ,
        CTL_PARAM_EXT_LL("copyshort",al_histogram.copybytesshort) ,
        CTL_PARAM_EXT_LL("copymisalign",al_histogram.copybytesmisalign) ,
        CTL_PARAM_EXT_LL("copybroke",al_histogram.copybytesbroke) ,
        CTL_PARAM_EXT_LL("crcb",al_histogram.crcbytes) ,
        CTL_PARAM_EXT_LL("csumpartial",al_histogram.csumpartialbytes) ,
#endif
        { 0 },
};



static void register_statistics_sysctl(void)
{
	register_sysctl_paths(bgp_statistics_ctl_path,bgp_statistics_table) ;
}
static int bgp_statistics_init(void)
  {
    register_statistics_sysctl() ;
    return 0 ;
  }

static void bgp_statistics_cleanup (void)
{

}
