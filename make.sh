#!/bin/sh
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
#
#
# $ ID: $

_SRCDIR=`dirname $0`
SRCDIR=`cd $_SRCDIR ; pwd`

cd $SRCDIR

#
# default value
#
# compute node is default for 2.6.29 kernel
#
CONFIG=./arch/powerpc/configs/44x/bgpzepto_defconfig

CROSS_COMPILE=/bgsys/drivers/ppcfloor/gnu-linux/bin/powerpc-bgp-linux-
BUILD_DIR_PREFIX=build-

usage() {
cat - <<EOF

Usage: $0 [options]

options:
 --help             Show the help message
 --builddirpre=STR  build directory prefix
 --cross=STR        cross compiler prefix
 --config=STR       kernel config file
 --ramdisk=STR      path of ramdisk(basically for CN)
 --spi              path of spi headers( required to compile zepto bigmem)
EOF
}

_getval() {  echo "$1" | sed -e 's/^[^=]*=//' ; }


while test $# -gt 0 ; do
   case $1 in
    --help | -h )
      usage; exit 0 ;;
    --config=* )
      CONFIG=`_getval "$1"`
      shift
      ;;
    --cross=* )
      CROSS_COMPILE=`_getval "$1"`
      shift
      ;;
    --builddirpre=* )
      BUILD_DIR_PREFIX=`_getval "$1"`
      shift
      ;;
    --ramdisk=* )
      ZEPTO_CN_RAMDISK=`_getval "$1"`
      shift
      ;;
    --spi=* )
      ZEPTO_SPI=`_getval "$1"`
      shift
      ;;
   *) 
     break
     ;;
   esac
done


if [ ! -z "$ZEPTO_CN_RAMDISK" ] ; then
    if [ ! -f "$ZEPTO_CN_RAMDISK" ] ; then
	echo "$ZEPTO_CN_RAMDISK not found"
	usage 
	exit 1
    fi
fi



# basic sanity check

if [ ! -f $CONFIG ] ; then
    echo "$CONFIG does not exist"
    exit 1
fi


if [ ! -x ${CROSS_COMPILE}gcc ] ; then
    echo "${CROSS_COMPILE} is not valid"
fi 

CONFIGNAME=`basename $CONFIG`
_BUILDDIR=$SRCDIR/../${BUILD_DIR_PREFIX}$CONFIGNAME
[ -d $_BUILDDIR ] || mkdir $_BUILDDIR
if [ ! -d $_BUILDDIR ] ; then
    echo "Faild to mkdir $_BUILDDIR"
    exit 1
fi 
BUILDDIR=`cd $_BUILDDIR ; pwd`


MAKE="make O=$BUILDDIR ARCH=powerpc CROSS_COMPILE=$CROSS_COMPILE"
NCPUS=`if [ -x /usr/bin/getconf ] ; then getconf _NPROCESSORS_ONLN ; else echo 1; fi`



echo ""
echo "KERNEL_BUILDIR=$BUILDDIR"
echo "KERNEL_CONFIG=$CONFIG"
echo "CROSS_COMPILE=$CROSS_COMPILE"
echo "MAKE=$MAKE"
echo "NCPUS=$NCPUS"
echo "ZEPTO_CN_RAMDISK=$ZEPTO_CN_RAMDISK"
echo "ZEPTO_SPI=$ZEPTO_SPI"
echo ""
echo ""


if [ ! -z $ZEPTO_SPI ] ; then
    SPI_DIR=`(cd $ZEPTO_SPI;pwd)`
    # check see if dir exists
    for i in spi common cnk bpcore zepto ; do
	if [ ! -d $SPI_DIR/$i ] ; then
	    echo $SPI_DIR/$i not found
	    exit 1
	fi
    done

    ZSPI=arch/powerpc/include/zspi
    [ -d $ZSPI ] || mkdir -p $ZSPI

    for i in spi common cnk bpcore zepto ; do
	if [ ! -L $ZSPI/$i ] ; then 
	    echo Creating a link to $SPI_DIR/$i with $ZSPI/$i
	    ( cd $ZSPI ; ln -s $SPI_DIR/$i )
	fi
    done
fi


if [ ! -z $ZEPTO_CN_RAMDISK ] ; then
    mkdir -p $BUILDDIR/arch/powerpc/boot/images
    cp $ZEPTO_CN_RAMDISK  $BUILDDIR/arch/powerpc/boot/ramdisk.image.gz
fi

if [ ! -f $BUILDDIR/.config -o $BUILDDIR/.config -ot $CONFIG ] ; then
#   make mrproper ARCH=powerpc
    ${MAKE} mrproper
    cp $CONFIG $BUILDDIR/.config
    ${MAKE} oldconfig
fi

if [ ! -z $ZEPTO_CN_RAMDISK ] ; then 
    if [ z"$@" = z"" ] ; then
       ${MAKE} -j${NCPUS} zImage.initrd $@
       if [ $? -ne 0 ] ; then 
	  exit 1
       fi
    fi
else
    ${MAKE} -j${NCPUS} $@
    if [ $? -ne 0 ] ; then 
        exit 1
    fi
fi


echo ""
if [ ! -z $ZEPTO_CN_RAMDISK ] ; then
    echo "Kernel image: $BUILDDIR/arch/powerpc/boot/dtbImage.initrd.bgp"
else
    echo "Kernel image: $BUILDDIR/arch/powerpc/boot/dtbImage.bgp"
fi
echo ""

exit 0
