export ARCH=arm64
export SUBARCH=arm64
export DTC_EXT=$PWD/../tools/dtc
export PATH=$PWD/../tools/proton-clang/bin/:$PATH
export LD_LIBRARY_PATH=$PWD/../tools/proton-clang/lib/:$LD_LIBRARY_PATH
export CROSS_COMPILE=aarch64-linux-gnu-
export CROSS_COMPILE_ARM32=arm-linux-gnueabi-