#!/bin/bash

#
# Copyright 2024, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#

# Usage
# ./get_padded_memsize.sh <TINYSYS/SLPI/QSH> <obj_name>

# Quit if any command produces an error.
set -e

# Parse variables
PLATFORM=$1
PLATFORM_NOT_CORRECT_STR="You must specify the platform being analyzed. Should be TINYSYS, QSH, or SLPI"
: ${PLATFORM:?$PLATFORM_NOT_CORRECT_STR}
if [ "$PLATFORM" != "TINYSYS" ] && [ "$PLATFORM" != "SLPI" ] && [ "$PLATFORM" != "QSH" ]; then
  echo $PLATFORM_NOT_CORRECT_STR
  exit 1
fi

OBJ=$2
: ${OBJ:?"You must specify the .so to size."}

# Setup required paths and obtain segments
if [ "$PLATFORM" == "TINYSYS" ]; then
  : ${RISCV_TOOLCHAIN_PATH}:?"Set RISCV_TOOLCHAIN_PATH, e.g. prebuilts/clang/md32rv/linux-x86"
  READELF_PATH="$RISCV_TOOLCHAIN_PATH/bin/llvm-readelf"
elif [ "$PLATFORM" == "SLPI" ] || [ "$PLATFORM" == "QSH" ]; then
  : ${HEXAGON_TOOLS_PREFIX:?"Set HEXAGON_TOOLS_PREFIX, e.g. export HEXAGON_TOOLS_PREFIX=\$HOME/Qualcomm/HEXAGON_Tools/8.1.04"}
  READELF_PATH="$HEXAGON_TOOLS_PREFIX/Tools/bin/hexagon-readelf"
else
  READELF_PATH="readelf"
fi

SEGMENTS="$($READELF_PATH -l $OBJ | grep LOAD)"

# Save current IFS to restore later.
CURR_IFS=$IFS

printf "\n$OBJ\n"
TOTAL=0
IFS=$'\n'
for LINE in $SEGMENTS; do
  # Headers: Type Offset VirtAddr PhysAddr FileSiz MemSiz Flg Align
  IFS=" " HEADERS=(${LINE})
  LEN=${#HEADERS[@]}

  MEMSIZE=$(( HEADERS[5] ))
  # Flg can have a space in it, 'R E', for example.
  ALIGN=$(( HEADERS[LEN - 1] ))
  # Rounded up to the next integral multiple of Align.
  QUOTIENT=$(( (MEMSIZE + ALIGN - 1) / ALIGN ))
  PADDED=$(( ALIGN * QUOTIENT ))
  PADDING=$(( PADDED - MEMSIZE ))

  printf '  MemSize:0x%x Align:0x%x Padded:0x%x Padding:%d\n' $MEMSIZE $ALIGN $PADDED $PADDING
  TOTAL=$(( TOTAL + PADDED ))
done

IFS=$CURR_IFS
printf 'Total Padded MemSize: 0x%x (%d)\n' $TOTAL $TOTAL

