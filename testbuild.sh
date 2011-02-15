#!/bin/sh

SPIDIR=../zepto-arch-runtime
ZEPTODIR=../BGP

RAMDISK=$ZEPTODIR/ramdisk/CN/bgp-cn-ramdisk.cpio.gz

if [ ! -d $ZEPTODIR ] ; then 
    echo "Please checkout the ZeptoOS svn repo, configure and make to create a ramdisk"
    echo ""
    echo "For example:"
    echo '$ cd ../'
    echo '$ svn svn co https://svn.mcs.anl.gov/repos/ZeptoOS/trunk/BGP'
    echo '$ cd BGP'
    echo '$ ./configure ; make'
    echo '$ cd ../linux-2.6.29.1-BGP # back to this dir'
    echo ""
    exit 1
fi

if [ ! -d $SPIDIR/ ] ; then 
    echo "Please checkout the Zepto arch runtime git repo, configure and make to create a ramdisk"
    echo ""
    echo "For example:"
    echo '$ cd ../'
    echo '$ git clone http://git.anl-external.org/bg-linux.repos/zepto-arch-runtime.git'
    echo '$ cd ../linux-2.6.29.1-BGP # back to this dir'
    echo ""
    exit 1
fi    


if [ -f ../build-bgpzepto_defconfig/include/linux/compile.h ] ; then
    rm ../build-bgpzepto_defconfig/include/linux/compile.h  # to always update timestamp in version
fi

sh make.sh --ramdisk=$RAMDISK --spi=$SPIDIR/arch/include  
if [ $? -ne 0 ] ; then
    exit 1
fi


./zkparam2.py ../build-bgpzepto_defconfig/arch/powerpc/boot/dtbImage.initrd.bgp  zepto_debug=3 zepto_console_output=1 bigmemsize=1024M

# to make sure kernel param
./zkparam2.py ../build-bgpzepto_defconfig/arch/powerpc/boot/dtbImage.initrd.bgp 

echo ""
echo "======================================================================"
echo ""
echo "Zepto kernel image(w/ ramdisk): ../build-bgpzepto_defconfig/arch/powerpc/boot/dtbImage.initrd.bgp"
if [ -d /bgsys/argonne-utils/profiles/ ] ; then 
    echo ""
    echo "Please configure your kernel profile manually"
    echo "For example:"
    echo "$ cp ../build-bgpzepto_defconfig/arch/powerpc/boot/dtbImage.initrd.bgp /bgsys/argonne-utils/profiles/$USER/CNK"
fi
echo ""
echo "======================================================================"


echo ""
echo "done"
echo ""


