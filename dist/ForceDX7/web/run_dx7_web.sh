#!/bin/sh
############################################################
# Copy this file to $mmPath/AddOns to launch automatically
# at boot (manage.sh ENABLE does that for you).
############################################################
#
# Runs the Force DX7 web control panel (server.py). Independent of the
# engine's own addon/manage.sh/run_dx7_host.sh - the engine only ever
# starts on demand via the nodeServer Modules page, but the *panel* can be
# up and reachable at all times, same as this project's other web panels.
#
# PID-file based kill, not `killall python3` or a `pgrep -f` name match:
# this device runs other python3 processes (nodeServer's tooling, other
# addons' own web panels), and a name-based kill would take those down too.

mmPath=$(cat /dev/shm/.mmPath)
. $mmPath/MockbaMod/env.sh

APPDIR="$mmPath/AddOns/ForceDX7/web"
PIDFILE="$APPDIR/.dx7_web.pid"

if [ "$1" = "kill" ]; then
    if [ -f "$PIDFILE" ]; then
        kill "$(cat "$PIDFILE")" 2>/dev/null
        rm -f "$PIDFILE"
    fi
else
    cd "$APPDIR" || exit 1
    python3 server.py --port 8307 --ctrl-sock /tmp/dx7_ctrl.sock >/tmp/dx7_web.log 2>&1 &
    echo $! > "$PIDFILE"
fi
