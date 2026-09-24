#!/usr/bin/env bash
# Deploy + enable Force DX7 on a live MockbaMod Force in one command.
# Usage: scripts/deploy.sh user@force-ip
#
# Standard MockbaMod deploy.sh pattern - see
# ~/.claude/skills/mockbamod-module-creator/references/deploy-debug.md
# ("Standard deploy.sh pattern") for the template this was generated from.
set -euo pipefail
cd "$(dirname "$0")/.."

APP_DIR="ForceDX7"   # device-side AddOns/<APP_DIR> folder name
HAS_WEB=1             # 1 if the web panel (web/manage.sh) needs its own ENABLE

HOST="${1:?usage: scripts/deploy.sh user@force-ip}"

# addon/ alone has no engine binary or web/ - package.sh assembles the full
# device folder (the same one the release zip ships, plus local files).
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
scripts/package.sh --stage "$STAGE"

mmPath="$(ssh "$HOST" 'cat /dev/shm/.mmPath')"
echo "== remote mmPath: $mmPath =="

# scp -r into an existing destination NESTS rather than merges - remove first.
ssh "$HOST" "rm -rf '$mmPath/AddOns/$APP_DIR'"
scp -r "$STAGE/AddOns/$APP_DIR" "$HOST:$mmPath/AddOns/$APP_DIR"

ssh "$HOST" "'$mmPath/AddOns/$APP_DIR/manage.sh' ENABLE"
if [ "$HAS_WEB" = 1 ]; then
  ssh "$HOST" "'$mmPath/AddOns/$APP_DIR/web/manage.sh' ENABLE"
fi

cat <<EOF

== $APP_DIR deployed and enabled. ==
Still needed once, if not already installed: the separate ForceAudioJack
addon (the shared audio-injection tap DX7 depends on) - see
../README.md's Requirements section.

Start dx7_host itself from the nodeServer Modules page (/moduler) - it is
never auto-launched at boot by design. Web panel: http://${HOST#*@}:8307

Drop DX7-compatible .syx bank files (32 voices each) into
  $mmPath/AddOns/$APP_DIR/banks/
Rescanned automatically, no restart needed.
EOF
