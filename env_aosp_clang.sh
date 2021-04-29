export ARCH=arm64
export SUBARCH=arm64
export DTC_EXT=$PWD/../tools/dtc
export PATH=$PWD/../tools/aosp-clang/bin/:$PATH
export LD_LIBRARY_PATH=$PWD/../tools/aosp-clang/lib64/:$LD_LIBRARY_PATH
export CROSS_COMPILE=$PWD/../tools/proton-clang/bin/aarch64-linux-gnu-
export CROSS_COMPILE_ARM32=$PWD/../tools/proton-clang/bin/arm-linux-gnueabi-
