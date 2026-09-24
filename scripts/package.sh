#!/usr/bin/env bash
# Assemble AddOns/ForceDX7/ - addon/ + the engine (build/dx7_host) + web/ -
# the one folder the device needs.
#   scripts/package.sh [version]    -> dist-zip/ForceDX7-<version>.zip, to unzip
#                                      onto the SD card root. Leaves out
#                                      your .syx banks in addon/banks/.
#   scripts/package.sh --stage DIR  -> DIR/AddOns/ForceDX7, local files included;
#                                      scripts/deploy.sh copies this to the device.
# Uses the committed build/dx7_host - run scripts/build.sh (and commit the
# result) first if the sources changed.
# .github/workflows/release.yml runs this for every published release.
set -euo pipefail
cd "${PKG_ROOT:-$(dirname "$0")/..}"
if [ "${1:-}" = --stage ]; then
  STAGE="${2:?usage: scripts/package.sh --stage DIR}"; VER=
else
  VER="${1:-$(git describe --tags --always)}"
  STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
fi
A="$STAGE/AddOns/ForceDX7"
rm -rf "$A"; mkdir -p "$STAGE/AddOns"
cp -r addon "$A"
cp build/dx7_host "$A/dx7_host"
cp -r web "$A/web"
find "$A" \( -name __pycache__ -prune -o -name '*.pyc' -o -name .gitkeep \) -exec rm -rf {} +
chmod 0755 "$A"/*.sh "$A"/web/*.sh "$A/dx7_host"
[ -n "$VER" ] || exit 0

[ -d "$A/banks" ] && find "$A/banks" -mindepth 1 -delete
OUT="$PWD/dist-zip"; mkdir -p "$OUT"
rm -f "$OUT/ForceDX7-$VER.zip"
python3 -c "import shutil,sys; shutil.make_archive(sys.argv[1], 'zip', sys.argv[2], 'AddOns')" "$OUT/ForceDX7-$VER" "$STAGE"
python3 -m zipfile -l "$OUT/ForceDX7-$VER.zip"
