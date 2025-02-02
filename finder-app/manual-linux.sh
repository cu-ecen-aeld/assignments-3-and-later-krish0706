#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo 
# Modified by: Krish Shah

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
YYLLOC_FIX_COMMIT_HASH=e33a814e772cdc36436c8c188d8c42d019fda639
YYLLOC_FIX_FILE="scripts/dtc/dtc-lexer.l"
ARM_CROSS_COMPILER_ROOT="/home/kshah/Documents/AESD/arm-cross-compiler/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu"

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}

    # Fetch fix for YYLLOC multiple definition error
    git -C ./linux-stable fetch origin ${YYLLOC_FIX_COMMIT_HASH} --depth=1

    # Checkout the file from FETCH_HEAD
    git -C ./linux-stable checkout FETCH_HEAD -- ${YYLLOC_FIX_FILE} 
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    echo "Running steps to build kernel"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    # make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git@github.com:mirror/busybox.git 
    cd busybox
    git checkout ${BUSYBOX_VERSION}

    echo "Configuring busybox"
    make distclean
    make defconfig
else
    cd busybox
fi

echo "Make and install busybox"
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install


echo "Adding Library dependencies"
cd "$OUTDIR"
${CROSS_COMPILE}readelf -a ./rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ./rootfs/bin/busybox | grep "Shared library"

cp ${ARM_CROSS_COMPILER_ROOT}/aarch64-none-linux-gnu/libc/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib
cp ${ARM_CROSS_COMPILER_ROOT}/aarch64-none-linux-gnu/libc/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64/
cp ${ARM_CROSS_COMPILER_ROOT}/aarch64-none-linux-gnu/libc/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64/
cp ${ARM_CROSS_COMPILER_ROOT}/aarch64-none-linux-gnu/libc/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64/

echo "Make device nodes"
cd "$OUTDIR"
sudo mknod -m 666 ./rootfs/dev/null c 1 3
sudo mknod -m 600 ./rootfs/dev/console c 5 1

echo "Clean and build the writer utility"
cd ${FINDER_APP_DIR}
make clean
make all CROSS_COMPILE=${CROSS_COMPILE}

echo "Copying the finder related scripts and executables to the /home directory on the target rootfs"
cd ${FINDER_APP_DIR}
cp -r autorun-qemu.sh finder-test.sh writer finder.sh ${OUTDIR}/rootfs/home/
mkdir -p ${OUTDIR}/rootfs/home/conf && cp -rL conf/*.txt ${OUTDIR}/rootfs/home/conf

echo "Chown the root directory"
cd "${OUTDIR}/rootfs/"
sudo chown -R root:root *

echo "Create initramfs.cpio.gz"
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd "${OUTDIR}"
gzip -f initramfs.cpio