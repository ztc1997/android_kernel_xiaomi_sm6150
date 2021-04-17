ANYKERNEL3_DIR=$PWD/AnyKernel3
FINAL_KERNEL_ZIP=kernel-phoenix-r-ztc1997-$(git rev-parse --short=7 HEAD).zip
IMAGE_GZ=$PWD/out/arch/arm64/boot/Image.gz
DTB=$PWD/out/arch/arm64/boot/dts/qcom/sdmmagpie.dtb
DTBO_IMG=$PWD/out/arch/arm64/boot/dtbo.img

make -j$(nproc --all) O=out \
                      ARCH="arm64" \
                      CC="clang" \
                      CROSS_COMPILE=$CROSS_COMPILE \
                      CROSS_COMPILE_ARM32=$CROSS_COMPILE_ARM32 \
                      AR="llvm-ar" \
                      NM="llvm-nm" \
                      OBJCOPY="llvm-objcopy" \
                      OBJDUMP="llvm-objdump" \
                      STRIP="llvm-strip" \
                      2>&1 | tee kernel.log

echo "**** Verify target files ****"
if [ ! -f "$IMAGE_GZ" ]; then
    echo "!!! Image.gz not found"
    exit 1
fi
if [ ! -f "$DTB" ]; then
    echo "!!! dtb not found"
    exit 1
fi
if [ ! -f "$DTBO_IMG" ]; then
    echo "!!! dtbo.img not found"
    exit 1
fi

echo "**** Moving target files ****"
mv $IMAGE_GZ $ANYKERNEL3_DIR/Image.gz
mv $DTB $ANYKERNEL3_DIR/dtb
mv $DTBO_IMG $ANYKERNEL3_DIR/dtbo.img

echo "**** Time to zip up! ****"
cd $ANYKERNEL3_DIR/
zip -r9 $FINAL_KERNEL_ZIP *

echo "**** Removing leftovers ****"
cd ..
rm $ANYKERNEL3_DIR/Image.gz
rm $ANYKERNEL3_DIR/dtb
rm $ANYKERNEL3_DIR/dtbo.img

mv -f $ANYKERNEL3_DIR/$FINAL_KERNEL_ZIP out/

echo "Check out/$FINAL_KERNEL_ZIP"