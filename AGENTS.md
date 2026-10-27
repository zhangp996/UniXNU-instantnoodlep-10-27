# Device agent guide

This repository owns the board DTS, peripheral wiring, component selections,
firmware inputs, partition layout and packaging for the OnePlus 8 Pro
(instantnoodlep, SM8250).

## Read first

| Task | Required reading |
| --- | --- |
| Every port | [Porting](https://unixnu.com/porting/), [device repository](https://unixnu.com/porting/device-repository), [component catalog](https://unixnu.com/reference/components) |
| Hardware | [Device tree](https://unixnu.com/porting/device-tree), the selected SoC repository's README, `soc.toml` and DTSI |
| Storage | [UFS porting](https://unixnu.com/porting/ufs) |
| Images | [Prelink](https://unixnu.com/concepts/prelink), [payload](https://unixnu.com/concepts/payload), [UEFI](https://unixnu.com/porting/uefi-firmware) |
| Verification | [Building](https://unixnu.com/building), [bring-up](https://unixnu.com/porting/bring-up), [debugging](https://unixnu.com/guide/debugging) |

## Work

Replace every `REQUIRED_` value with a measurement from this board, then set
`example = false`. Hardware addresses, partition geometry and interrupts must
come from the device's own device tree, GPT and register reads, not from another
board's record.

Shared kona resources belong in `UniXNU-sm8250-common`. Reusable drivers belong
in their component repositories. Select a component only when its hardware scope
and required inputs match this board; otherwise record the gap in `porting.toml`.

```sh
./unixnu defconfig instantnoodlep_defconfig -D ../UniXNU-instantnoodlep
./unixnu status -D ../UniXNU-instantnoodlep
./unixnu tree -D ../UniXNU-instantnoodlep
python3 scripts/porting-record.py check --device ../UniXNU-instantnoodlep
```

Collect board facts with `scripts/collect-in2020.sh` and record what each value
was measured from. Report firmware, kernel, storage, display and input results
separately.

## Porting record

Read `porting.toml` and the [record guide](https://unixnu.com/porting/records)
before implementation. Update the affected area's findings or evidence with
every code change, and keep the completion level honest: `planned` until source,
host or device evidence exists.
