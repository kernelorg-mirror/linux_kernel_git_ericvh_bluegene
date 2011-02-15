#!/usr/bin/env python
#***************************************************************************
# ZEPTOOS:zepto-info
#      This file is part of ZeptoOS: The Small Linux for Big Computers.
#      See www.mcs.anl.gov/zeptoos for more information.
# ZEPTOOS:zepto-info
#
# ZEPTOOS:zepto-fillin
#      $Id:  $
#      ZeptoOS_Version: 2.0
#      ZeptoOS_Heredity: FOSS_ORIG
#      ZeptoOS_License: GPL
# ZEPTOOS:zepto-fillin
#
# ZEPTOOS:zepto-gpl
#       Copyright: Argonne National Laboratory, Department of Energy,
#                  and UChicago Argonne, LLC.  2004, 2005, 2006, 2007, 2008
#       ZeptoOS License: GPL
#  
#       This software is free.  See the file ZeptoOS/misc/license.GPL
#       for complete details on your rights to copy, modify, and use this
#       software.
# ZEPTOOS:zepto-gpl
#***************************************************************************


import sys, os, re

debug = 0

def main():
    if len(sys.argv) < 2 :
        print ""
        print "Usage: zkparam.py KernelImage [param ...]"
        print ""
        print "This tool overwrites additional kernel command line parameters to "
        print "BG/P 2.6.29 based kernel."
        print ""
        print "Avaiable Kernel Parameters: zepto_debug, bigmemsizeMB and zepto_console_output"
        print "See http://wiki.mcs.anl.gov/zeptoos/index.php/Kernel"
        print ""
        sys.exit(1)

    bootargs="console=bgcons root=/dev/ram0 lpj=8500000 profile=2 log_buf_len=8388608 rdinit=/sbin/init "

    print "[zkparam2]"
    print ""
    print "dt bootagrs:", bootargs, "(arch/powerpc/boot/dts/bgp.dts)"
    print ""

    kimage = sys.argv[1]

    printonly = 0
    kparam = ""

    if len(sys.argv) < 3 :
        printonly = 1
    else:
        printonly = 0
        if len(sys.argv[2]) > 0 :
            for s in sys.argv[2:]:
                kparam = kparam + " " + s
            kparam = kparam + '\0'
        else:
            kparam = '\0'

    kparam = bootargs + kparam

    # Find the virtual address and file offset of the data section
    # Here is an output from readlef
    # [ 3] __builtin_cmdline PROGBITS        0080ac7c 02ac7c 000200 00  WA  0   0  4
  
    cmd = "readelf -S " + kimage
    try:
        fp = os.popen(cmd)
    except:
        print 'failed to popen(' , cmd , ')'
        sys.exit(1)
  
    re_section = re.compile( ".*\__builtin_cmdline\s+PROGBITS\s+(\S+)\s(\S+)" )
    data_file_offset = 0
    data_v_addr = 0
    for line in fp.readlines():
        m = re_section.match(line) 
        if m :
            data_v_addr  = int( m.group(1) , 16)  # conver a sting as a hex decimal value
            data_file_offset = int( m.group(2), 16 )
            break

    if data_file_offset > 0 :
        if debug > 0:
            print "data_v_addr=%08x" % data_v_addr
            print "data_file_offset=%08x" % data_file_offset
    else:
        print "Error: could not find ELF section!"
        sys.exit(1)

    fp.close();

    builtin_cmdline_file_offset =  data_file_offset
    if debug > 0 :
        print "builtin_cmdline_file_offset=%08x" % builtin_cmdline_file_offset

    if printonly < 1 : 
        #
        # overwrite zcl_cmd_line
        #
        fd = os.open( kimage, os.O_RDWR )
        if fd < 0 :
            print "Error: failed to open ", kimage
            sys.exit(1)
        os.lseek( fd, builtin_cmdline_file_offset, 0)
        buf = os.write( fd, kparam )
        os.close(fd)
    else:
        #
        # overwrite builtin_cmdline
        #
        fd = os.open( kimage, os.O_RDONLY )
        if fd < 0 :
            print "Error: failed to open ", kimage
            sys.exit(1)
        os.lseek( fd, builtin_cmdline_file_offset, 0)
        buf = os.read( fd, 160 )
        os.close(fd)
        nullpos=0
        for i in range(0,160):
            if buf[i] == '\0' :
                nullpos = i
                break
        str = buf[:nullpos]

        if len(str)==0 :
            print "No additional kernel command line found!"
        else:
            print "Current kernel command line:"
            print str
	print "\n"

if __name__ == '__main__':
    main()
    sys.exit(0)

