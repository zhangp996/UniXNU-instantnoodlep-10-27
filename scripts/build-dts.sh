#!/bin/sh
# Compile the board DTS against the selected SoC include directory.
# Usage: build-dts.sh <soc-dts-dir> <board.dts> [output.dtb]
set -e
SOC_DTS=${1:?usage: build-dts.sh <soc-dts-dir> <board.dts> [output.dtb]}
BOARD_DTS=${2:?usage: build-dts.sh <soc-dts-dir> <board.dts> [output.dtb]}
OUT=${3:-/tmp/board.dtb}
dtc -I dts -O dtb -i "$SOC_DTS" -o "$OUT" "$BOARD_DTS"
echo "built $OUT"
