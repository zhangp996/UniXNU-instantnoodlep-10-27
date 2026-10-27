#!/system/bin/sh
# Dump the touch controller node from the live device tree.
N=/proc/device-tree/soc/i2c@a94000/synaptics_20828@4B
echo "node: $N"
ls "$N"
echo "--- properties (hex) ---"
for f in compatible reg interrupt-parent interrupts vdd-supply; do
  printf '%-18s ' "$f"
  od -An -tx1 "$N/$f" 2>/dev/null | tr -d ' \n'
  echo
done
echo "--- parent i2c@a94000 ---"
for f in reg interrupts clock-frequency; do
  printf '%-18s ' "$f"
  od -An -tx1 "/proc/device-tree/soc/i2c@a94000/$f" 2>/dev/null | tr -d ' \n'
  echo
done
