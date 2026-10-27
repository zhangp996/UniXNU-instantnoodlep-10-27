#!/system/bin/sh
# Focused capture of the data SELinux denied before the device was set permissive.
sec() { echo; echo "=== SECTION: $1 ==="; }
run() { echo "--- $1 ---"; sh -c "$2" 2>&1; echo; }

sec input
run input_devices "cat /proc/bus/input/devices"
run getevent "getevent -pl 2>&1 | head -160"
run input_names "for e in /sys/class/input/event*; do echo $e $(cat $e/device/name 2>/dev/null); done"

sec devicetree_soc
run soc_list "ls /proc/device-tree/soc"
run i2c_nodes "ls -d /proc/device-tree/soc/*i2c* 2>/dev/null"
run spi_nodes "ls -d /proc/device-tree/soc/*spi* 2>/dev/null"
run qup_nodes "ls -d /proc/device-tree/soc/*geniqup* /proc/device-tree/soc/*qup* 2>/dev/null"
run ufs_nodes "ls /proc/device-tree/soc/ufshc@1d84000; echo ---; od -An -tx1 /proc/device-tree/soc/ufshc@1d84000/reg"
run touch_nodes "for d in /proc/device-tree/soc/*i2c*/*/ /proc/device-tree/soc/*spi*/*/; do ls $d 2>/dev/null | grep -qiE 'touch|synaptics|focal|novatek' && echo $d; done; ls /proc/device-tree/soc/*/ | grep -iE 'synaptics|touch' | head"

sec display
run backlight "for b in /sys/class/backlight/*; do echo $b; cat $b/max_brightness $b/brightness $b/type 2>/dev/null; done"
run wm "wm size; wm density"
run dsi "ls /proc/device-tree/soc | grep -i dsi"

sec power
run power_supply "for p in /sys/class/power_supply/*; do echo $p; cat $p/type $p/capacity $p/voltage_now 2>/dev/null; done"
run thermal "for z in /sys/class/thermal/thermal_zone*; do echo $(cat $z/type) $(cat $z/temp); done | head -20"

sec misc
run interrupts_top "head -25 /proc/interrupts"
run cmdline "cat /proc/cmdline"
echo
echo "=== END ==="
