#!/system/bin/sh
# Dump every A/B pair from slot A into /data/local/tmp/ab-sync for the host.
set -e
OUT=/data/local/tmp/ab-sync
mkdir -p $OUT
for n in abl aop bluetooth boot cmnlib cmnlib64 devcfg dsp dtbo featenabler hyp imagefv keymaster logo mdtp mdtpsecapp modem multiimgoem multiimgqti opproduct qupfw recovery storsec tz uefisecapp vbmeta vbmeta_system xbl xbl_config; do
  src=/dev/block/by-name/${n}_a
  dst=$OUT/${n}.img
  if [ ! -e "$src" ]; then echo "MISSING $n"; continue; fi
  dd if="$src" of="$dst" bs=1048576 2>/dev/null
  echo "$n $(stat -c %s "$dst")"
done
echo DONE
