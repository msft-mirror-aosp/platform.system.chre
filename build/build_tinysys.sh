#!/bin/bash
# Script used to build Tinysys.

# make sure $ANDROID_BUILD_TOP is set
if [[ -z "$ANDROID_BUILD_TOP" ]]; then
    echo "Must setup Android build environment first" 1>&2
    echo "Run the following commands from the repo root:" 1>&2
    echo " source build/envsetup.sh" 1>&2
    echo " lunch vext_k6985v1_64-userdebug" 1>&2
    exit 1
fi

# make sure $RISCV_TOOLCHAIN_PATH & $RISCV_TINYSYS_PREFIX are set
if [[ -z "$RISCV_TOOLCHAIN_PATH" ]] || [[ -z "$RISCV_TINYSYS_PREFIX" ]]; then
    echo "Must provide RISCV_TOOLCHAIN_PATH & RISCV_TINYSYS_PREFIX" 1>&2
    echo "Example:" 1>&2
    echo " RISCV_TOOLCHAIN_PATH=\$ANDROID_BUILD_TOP/prebuilts/clang/md32rv/linux-x86 \\" 1>&2
    echo " RISCV_TINYSYS_PREFIX=\$ANDROID_BUILD_TOP/vendor/mediatek/proprietary/tinysys \\" 1>&2
    echo " build/tools/build_tinysys.sh" 1>&2
    exit 1
fi

_LIB_PATH="$ANDROID_BUILD_TOP/system/chre/out/aosp_riscv_tinysys/libchre.a"
_VENDOR_PATH="$ANDROID_BUILD_TOP/vendor/mediatek/proprietary/tinysys/scp/middleware/chre/libchre.a"

pushd $ANDROID_BUILD_TOP/system/chre > /dev/null

CHRE_VARIANT_MK_INCLUDES=variant/tinysys/variant.mk \
 IS_ARCHIVE_ONLY_BUILD=true \
 make aosp_riscv_tinysys

if test -f "$_LIB_PATH"; then
    if [[ "$_LIB_PATH" -nt "$_VENDOR_PATH" ]]; then
        echo "cp $_LIB_PATH $_VENDOR_PATH"
        cp -f $_LIB_PATH $_VENDOR_PATH
    fi
fi

popd > /dev/null

unset _LIB_PATH
unset _VENDOR_PATH
