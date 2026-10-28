# Boot image recipe for this board

Measured on the device, not inferred:

| Fact | Value | Where it comes from |
| --- | --- | --- |
| ABL requires an ARM64 Image header | yes | a kernel without `ARMd` at offset 0x38 gives `Failed to load/authenticate boot image: Load Error` |
| Boot image header version | 2 | the stock `boot_a` image |
| Kernel compression | none | the stock kernel starts with a branch and has `ARMd` at 0x38; ABL does not decompress for this board |
| dtb field must carry `qcom,msm-id` | yes | with a dtb built from this repository's DTS, ABL refuses the image |
| dtb used for the test | stock `boot_a` dtb | 11028247 bytes, `qcom,msm-id = 0x164 0x20001` |
| Panel timing | 1080x2376 | `qcom,mdss-dsi-panel-width = 0x438`, `-height = 0x948` in the live device tree |

The image that boots the firmware on the device is:

```sh
# shim with the ARM64 header, then the firmware volume, uncompressed
clang --target=aarch64-none-elf -c -DFD_BASE=0x9E400000 -DFD_SIZE=0x400000 shim.S -o shim.o
llvm-objcopy -O binary shim.o shim.bin
cat shim.bin SILICIUM_UEFI.fd > kernel.bin

python3 Resources/Scripts/mkbootimg.py \
  --kernel kernel.bin --ramdisk payload.img --dtb stock.dtb \
  --header_version 2 --base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
  --cmdline "console=ttyMSM0,115200n8 androidboot.hardware=qcom" -o boot.img

fastboot boot boot.img
```

`unixnu bootimg` produces a header version 0 image with a gzip kernel and this
repository's own device tree, which this bootloader rejects; until the tool grows
the options above, the image is assembled by hand.

## Observed on the device

With that image the firmware runs: the screen shows EDK2 console output. The text is
drawn as a narrow vertical band, so the framebuffer geometry the firmware assumes
does not match the buffer ABL scans out. Both 1440x3168 and 1080x2376 have been tried
with the same result, which points at the stride or at the panel pipeline rather than
at the width and height alone.
