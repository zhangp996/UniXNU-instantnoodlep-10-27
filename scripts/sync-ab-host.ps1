# Flash every A/B pair in slot B with the slot A dump pulled from the device.
# Run from the directory holding the *.img dumps, with the device in bootloader mode.
$ErrorActionPreference = 'Stop'
$fb = 'C:\Users\132\AppData\Local\Temp\pt\platform-tools\fastboot.exe'
$pairs = @('abl', 'aop', 'bluetooth', 'boot', 'cmnlib', 'cmnlib64', 'devcfg', 'dsp', 'dtbo', 'featenabler', 'hyp', 'imagefv', 'keymaster', 'logo', 'mdtp', 'mdtpsecapp', 'modem', 'multiimgoem', 'multiimgqti', 'opproduct', 'qupfw', 'recovery', 'storsec', 'tz', 'uefisecapp', 'vbmeta', 'vbmeta_system', 'xbl', 'xbl_config')
if (-not ((& $fb devices 2>&1 | Out-String) -match 'fastboot')) { throw 'device is not in fastboot mode' }
foreach ($n in $pairs) {
  $img = Join-Path (Get-Location) "$n.img"
  if (-not (Test-Path $img)) { Write-Host "SKIP $n (no dump)"; continue }
  Write-Host "== $n -> ${n}_b"
  & $fb flash "${n}_b" $img 2>&1 | Out-String | Write-Host
  if ($LASTEXITCODE -ne 0) { throw "flash ${n}_b failed" }
}
Write-Host 'done'
