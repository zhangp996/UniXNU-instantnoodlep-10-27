#!/bin/sh
# Install this board's platform package and device config into a Mu-Silicium checkout.
set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${MU_ROOT:?set MU_ROOT to the Mu-Silicium checkout}"

rm -rf "$MU_ROOT/Platforms/OnePlus/instantnoodlepPkg"
mkdir -p "$MU_ROOT/Platforms/OnePlus" "$MU_ROOT/Resources/Configs"
cp -R "$here/Platforms/OnePlus/instantnoodlepPkg" "$MU_ROOT/Platforms/OnePlus/"
cp "$here/Resources/Configs/instantnoodlep.toml" "$MU_ROOT/Resources/Configs/"

echo "installed instantnoodlepPkg into $MU_ROOT"
echo "build it with: ./unixnu build firmware -D ../UniXNU-instantnoodlep"
