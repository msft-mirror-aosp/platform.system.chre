#!/bin/bash
# Script used to build Tinysys.

# make sure $ANDROID_BUILD_TOP is set
if [[ -z "$ANDROID_BUILD_TOP" ]]; then
    echo "ANDROID_BUILD_TOP must be defined" 1>&2
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

usage() {
    echo "Usage: $0 [options] [target]" 1>&2;
    echo "options:" 1>&2;
    echo "    -c    clean build that runs 'make clean' before building" 1>&2;
    echo "Supported targets:" 1>&2;
    echo "    aosp_riscv55e03_tinysys (default)" 1>&2;
    echo "    aosp_riscv55e300_tinysys" 1>&2;
}

# do incremental build by default.
clean_build="false"
while getopts "c" opt; do
  case ${opt} in
    c) clean_build="true" ;;
    *) usage; exit 0 ;;
  esac
done

pushd $ANDROID_BUILD_TOP/system/chre > /dev/null

shift $(($OPTIND - 1))
target=${1:-aosp_riscv55e03_tinysys}

if [[ "$clean_build" == "true" ]];then
  make clean
fi

CHRE_VARIANT_MK_INCLUDES=variant/tinysys/variant.mk \
 IS_ARCHIVE_ONLY_BUILD=true \
 make $target

popd > /dev/null
