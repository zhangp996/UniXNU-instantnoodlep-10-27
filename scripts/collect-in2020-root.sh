#!/system/bin/sh
# Root collection for the UniXNU hardware inventory (docs/porting/steps.md step 1).
# Read-only. Output goes to stdout; the host captures it.
sec() { echo; echo "=== SECTION: $1 ==="; }
run() { echo "--- $1 ---"; sh -c "$2" 2>&1; echo; }

echo "# collected $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(getprop ro.product.model)"

sec identity
run getprop "getprop | sort"
run uname "uname -a"
run cmdline "cat /proc/cmdline"

sec cpu
run cpuinfo "cat /proc/cpuinfo"
run cpupresent "cat /sys/devices/system/cpu/present; cat /sys/devices/system/cpu/online"
run topology "for c in /sys/devices/system/cpu/cpu*/topology; do echo $c; cat $c/core_id $c/cluster_id $c/core_siblings_list 2>/dev/null; done"
run cpufreq "for p in /sys/devices/system/cpu/cpufreq/policy*; do echo $p; cat $p/related_cpus $p/cpuinfo_min_freq $p/cpuinfo_max_freq 2>/dev/null; done"

sec memory
run meminfo "cat /proc/meminfo"
run iomem "cat /proc/iomem"
run dtmem "for n in /proc/device-tree/memory*; do echo $n; od -An -tx4 $n/reg; done"
run reservedlist "ls /proc/device-tree/reserved-memory"
run reservedregs "for n in /proc/device-tree/reserved-memory/*; do echo $n; od -An -tx1 $n/reg 2>/dev/null; done"

sec interrupts
run interrupts "cat /proc/interrupts"

sec storage
run partitions "cat /proc/partitions"
run byname "ls -l /dev/block/by-name"
run blockinfo "for d in /sys/block/sd*; do echo $d model=$(cat $d/device/model 2>/dev/null) size=$(cat $d/size) lbs=$(cat $d/queue/logical_block_size) pbs=$(cat $d/queue/physical_block_size); done"
run partgeometry "for p in /sys/block/sd*/sd*; do echo $p start=$(cat $p/start 2>/dev/null) size=$(cat $p/size 2>/dev/null); done"
run ufshc "ls /sys/devices/platform/soc/1d84000.ufshc 2>/dev/null; cat /sys/devices/platform/soc/1d84000.ufshc/ufs_version 2>/dev/null; cat /sys/devices/platform/soc/1d84000.ufshc/link_state 2>/dev/null"
run scsihost "for h in /sys/class/scsi_host/host*; do echo $h $(cat $h/proc_name 2>/dev/null); done"
run dtufs "ls /proc/device-tree/soc/ufshc@1d84000; od -An -tx1 /proc/device-tree/soc/ufshc@1d84000/reg"

sec display
run backlight "for b in /sys/class/backlight/*; do echo $b; cat $b/max_brightness $b/brightness $b/type 2>/dev/null; done"
run dtpanel "ls /proc/device-tree/soc/ | grep -iE dsi"
run dmesgdisplay "dmesg | grep -iE dsi | head -60"

sec input
run inputdevices "cat /proc/bus/input/devices"
run inputnames "for e in /sys/class/input/event*; do echo $e $(cat $e/device/name 2>/dev/null); done"
run getevent "getevent -pl 2>&1 | head -120"

sec buses
run i2c "ls -l /sys/bus/i2c/devices"
run spi "ls -l /sys/bus/spi/devices"
run qup "ls -d /sys/devices/platform/soc/*geniqup* /sys/devices/platform/soc/*i2c* /sys/devices/platform/soc/*spi* 2>/dev/null"

sec network
run netif "ls /sys/class/net"
run pci "for d in /sys/bus/pci/devices/*; do echo $d $(cat $d/vendor 2>/dev/null) $(cat $d/device 2>/dev/null) $(cat $d/class 2>/dev/null); done | head -30"
run dmesgwifi "dmesg | grep -iE cnss | head -40"

sec power
run powersupply "for p in /sys/class/power_supply/*; do echo $p; cat $p/type $p/uevent 2>/dev/null; done"
run thermal "for z in /sys/class/thermal/thermal_zone*; do echo $(cat $z/type) $(cat $z/temp); done | head -20"

sec gpu
run kgsl "ls /sys/class/kgsl 2>/dev/null; dmesg | grep -iE kgsl | head -30"

sec firmware
run modules "cat /proc/modules 2>/dev/null | head -40"
run dtroot "ls /proc/device-tree"
run fwfiles "ls /vendor/firmware_mnt/image 2>/dev/null | head -50"
echo
echo "=== END ==="
