#!/usr/bin/env bash
# Build the SD-card release zip: dist-zip/ForceDX7-<version>.zip, unpacking to
#   AddOns/ForceDX7/        addon/ + the prebuilt engine (build/dx7_host) + web/
# Unzip it onto the SD card root. Uses the committed build/dx7_host - run
# scripts/build.sh (and commit the result) first if the sources changed.
# .github/workflows/release.yml runs this for every published release.
set -euo pipefail
cd "${PKG_ROOT:-$(dirname "$0")/..}"
VER="${1:-$(git describe --tags --always)}"
OUT="$PWD/dist-zip"
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
A="$STAGE/AddOns/ForceDX7"
mkdir -p "$STAGE/AddOns" "$OUT"
cp -r addon "$A"
cp build/dx7_host "$A/dx7_host"
cp -r web "$A/web"
find "$STAGE" \( -name __pycache__ -prune -o -name '*.pyc' -o -name .gitkeep \) -exec rm -rf {} +
chmod 0755 "$A"/*.sh "$A"/web/*.sh "$A/dx7_host"
rm -f "$OUT/ForceDX7-$VER.zip"
python3 -c "import shutil,sys; shutil.make_archive(sys.argv[1], 'zip', sys.argv[2], 'AddOns')" "$OUT/ForceDX7-$VER" "$STAGE"
python3 -m zipfile -l "$OUT/ForceDX7-$VER.zip"
