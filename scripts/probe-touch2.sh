#!/system/bin/sh
N=/proc/device-tree/soc/i2c@a94000/synaptics_20828@4B
for f in chip-name tp_type project_id touchpanel,panel-coords touchpanel,display-coords touchpanel,tx-rx-num touchpanel,max-num-support touchpanel,int-mode irq-gpio reset-gpio; do
  printf '%-28s ' "$f"
  od -An -tx1 "$N/$f" 2>/dev/null | tr -d ' \n'
  echo
done
