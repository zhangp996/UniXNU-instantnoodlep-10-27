#!/system/bin/sh
# UniXNU device inventory collector for OnePlus 8 Pro (instantnoodlep / IN2020).
# Read-only: every command only reads files, sysfs, props or dumpsys.
# Usage: sh collect-in2020.sh [--root]
ROOT=""
if [ "$1" = "--root" ]; then ROOT="su"; fi

sec() { echo; echo "=== SECTION: $1 ==="; }
run() {
  echo "--- $1 ---"
  if [ -n "$ROOT" ]; then su -c "$2" 2>&1; else sh -c "$2" 2>&1; fi
  echo
}

sec identity
run id "id"
run uname "uname -a"
run android_release "getprop ro.build.version.release"
run sdk "getprop ro.build.version.sdk"
run security_patch "getprop ro.build.version.security_patch"
run build_fingerprint "getprop ro.build.fingerprint"
run build_display_id "getprop ro.build.display.id"
run build_desc "getprop ro.build.description"
run build_date "getprop ro.build.date"
run product_device "getprop ro.product.device"
run product_name "getprop ro.product.name"
run product_model "getprop ro.product.model"
run board_platform "getprop ro.board.platform"
run soc_manufacturer "getprop ro.soc.manufacturer"
run soc_model "getprop ro.soc.model"
run hardware "getprop ro.hardware"
run boot_hardware "getprop ro.boot.hardware"
run boot_prj "getprop ro.boot.prjname"
run boot_prj_version "getprop ro.boot.prj_version"
run boot_slot "getprop ro.boot.slot_suffix"
run boot_verified "getprop ro.boot.verifiedbootstate"
run boot_flashing "getprop ro.boot.flashing"
run oplus_rom "getprop ro.build.version.oplusrom"
run oplus_ota "getprop ro.build.version.ota"
run region "getprop ro.boot.region"

sec cmdline
run proc_cmdline "cat /proc/cmdline"

sec props_full
run getprop "getprop"

sec cpu
run cpu_present "cat /sys/devices/system/cpu/present"
run cpu_possible "cat /sys/devices/system/cpu/possible"
run cpu_online "cat /sys/devices/system/cpu/online"
run cpu_isolated "cat /sys/devices/system/cpu/isolated"
run cpuinfo "cat /proc/cpuinfo"
run cpufreq_max "for p in /sys/devices/system/cpu/cpufreq/policy*; do echo $p; cat $p/related_cpus 2>/dev/null; cat $p/cpuinfo_max_freq 2>/dev/null; cat $p/cpuinfo_min_freq 2>/dev/null; done"
run cpu_topology "for c in /sys/devices/system/cpu/cpu*/topology; do echo $c; cat $c/core_id 2>/dev/null; cat $c/cluster_id 2>/dev/null; cat $c/core_siblings_list 2>/dev/null; done"
run cpu_parts "grep -E 'CPU implementer|CPU part|CPU architecture' /proc/cpuinfo | sort -u"

sec memory
run meminfo "cat /proc/meminfo"
run iomem "cat /proc/iomem"
run memblock "dmesg 2>/dev/null | grep -iE 'memory node|memblock|Memory: ' | head -60"

sec storage_partitions
run partitions "cat /proc/partitions"
run byname "ls -l /dev/block/by-name"
run block_devices "ls -l /dev/block"
run mounts "cat /proc/mounts"
run df "df -h"
run sys_block "for d in /sys/block/sd*; do echo $d; cat $d/size 2>/dev/null; cat $d/device/model 2>/dev/null; cat $d/device/vendor 2>/dev/null; cat $d/queue/logical_block_size 2>/dev/null; cat $d/queue/physical_block_size 2>/dev/null; done"
run part_geometry "for p in /sys/block/sd*/sd*; do s=$(cat $p/start 2>/dev/null); n=$(cat $p/size 2>/dev/null); if [ -n \"$s\" ]; then echo \"$p start=$s size=$n\"; fi; done"

sec storage_ufs
run ufshc_paths "ls -l /sys/devices/platform/soc/*.ufshc /sys/devices/platform/soc/*.ufsphy 2>/dev/null; ls /sys/class/scsi_host 2>/dev/null; ls /sys/bus/ufs 2>/dev/null"
run ufshc_state "for h in /sys/devices/platform/soc/*.ufshc; do echo $h; cat $h/link_state 2>/dev/null; cat $h/ufs_version 2>/dev/null; cat $h/uevent 2>/dev/null; done"
run ufs_lun "for d in /sys/block/sd*; do echo $d; cat $d/device/rev 2>/dev/null; cat $d/device/type 2>/dev/null; cat $d/device/vendor 2>/dev/null; done"
run scsi_hosts "for h in /sys/class/scsi_host/host*; do echo $h; cat $h/proc_name 2>/dev/null; done"

sec display
run wm_size "wm size"
run wm_density "wm density"
run surfaceflinger "dumpsys SurfaceFlinger 2>/dev/null | grep -iE 'Display |resolution|refresh|mode' | head -40"
run backlight "ls -l /sys/class/backlight/ 2>/dev/null; for b in /sys/class/backlight/*; do echo $b; cat $b/max_brightness 2>/dev/null; cat $b/brightness 2>/dev/null; cat $b/type 2>/dev/null; done"
run drm "ls /sys/class/drm 2>/dev/null"
run display_dmesg "dmesg 2>/dev/null | grep -iE 'dsi|panel|mdss|dpu|sde_|backlight' | head -60"

sec input
run input_devices "cat /proc/bus/input/devices"
run input_sysfs "ls /sys/class/input 2>/dev/null"
run getevent_pl "getevent -pl 2>&1 | head -160"
run dumpsys_input "dumpsys input 2>/dev/null | grep -iE 'Device [0-9]|Name:|Driver:|Touch' | head -60"

sec touch_hw
run i2c_devices "ls -l /sys/bus/i2c/devices 2>/dev/null"
run spi_devices "ls -l /sys/bus/spi/devices 2>/dev/null"
run qup "ls -d /sys/devices/platform/soc/*.geniqup* /sys/devices/platform/soc/*.i2c* /sys/devices/platform/soc/*.spi* 2>/dev/null"
run touch_dmesg "dmesg 2>/dev/null | grep -iE 'touch|ft5|ft7|goodix|novatek|synaptics|focal' | head -40"

sec network
run net_interfaces "ls /sys/class/net"
run wlan_props "getprop | grep -iE 'wlan|wifi|wcn' | head -40"
run wlan_dmesg "dmesg 2>/dev/null | grep -iE 'wlan|wifi|wcn|cnss|qca' | head -40"

sec power
run power_supply "for p in /sys/class/power_supply/*; do echo $p; cat $p/type 2>/dev/null; cat $p/uevent 2>/dev/null; done"
run thermal "for z in /sys/class/thermal/thermal_zone*; do echo -n \"$z \"; cat $z/type 2>/dev/null; done | head -30"
run pmic_dmesg "dmesg 2>/dev/null | grep -iE 'pmic|spmi|pm8150|pm8250|pmi' | head -30"

sec firmware
run kernel_version "cat /proc/version"
run modules "cat /proc/modules 2>/dev/null | head -40"
run dmesg_tail "dmesg 2>/dev/null | tail -80"
run dmesg_access "dmesg 2>&1 | head -2"
run firmware_dir "ls /vendor/firmware_mnt/image 2>/dev/null | head -40; ls /vendor/firmware 2>/dev/null | head -20"

sec devicetree
run dt_model "cat /proc/device-tree/model 2>/dev/null; echo; cat /proc/device-tree/compatible 2>/dev/null; echo"
run dt_root "ls /proc/device-tree"
run dt_list "find /proc/device-tree -maxdepth 2 2>/dev/null | head -200"
run su_check "which su 2>/dev/null; su -c id 2>&1 | head -2"

echo
echo "=== END OF COLLECTION ==="
