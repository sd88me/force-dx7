#!/usr/bin/env bash
# Build force-dx7's engine (dx7_host) for armhf, native-under-QEMU via
# Docker (see Dockerfile for why bookworm, not the stretch base this
# project's earlier ports use). Set CROSS_PREFIX to skip Docker if you
# already have a real armhf toolchain (e.g. building on a Pi).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="force-dx7-builder"

if [ -z "${CROSS_PREFIX:-}" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Force DX7 Build (via Docker/QEMU armhf) ==="
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build --platform linux/arm/v7 -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm --platform linux/arm/v7 \
        -v "$REPO_ROOT:/build" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh
    echo "=== Done: dist/ForceDX7/dx7_host ==="
    exit 0
fi

cd "$REPO_ROOT"
mkdir -p build dist/ForceDX7 dist/ForceDX7/web dist/ForceDX7/banks

echo "Compiling RtMidi..."
g++ -O2 -c -fPIC -std=c++14 -D__LINUX_ALSA__ src/rtmidi/RtMidi.cpp -o build/RtMidi.o -Isrc/rtmidi

echo "Compiling dx7_host (DSP core + host shim)..."
g++ -O3 -fPIC -std=c++14 \
    -mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard \
    src/dx7_host.cpp src/dsp/dx7_plugin.cpp \
    src/dsp/msfa/dx7note.cc src/dsp/msfa/env.cc src/dsp/msfa/exp2.cc \
    src/dsp/msfa/fm_core.cc src/dsp/msfa/fm_op_kernel.cc src/dsp/msfa/freqlut.cc \
    src/dsp/msfa/lfo.cc src/dsp/msfa/pitchenv.cc src/dsp/msfa/sin.cc src/dsp/msfa/porta.cpp \
    build/RtMidi.o \
    -o build/dx7_host \
    -Isrc -Isrc/dsp -Isrc/rtmidi \
    -lasound -lpthread -lrt

echo "Packaging..."
cp build/dx7_host dist/ForceDX7/dx7_host
cp src/module.json dist/ForceDX7/module.json
cp src/help.json dist/ForceDX7/help.json
cp addon/manage.sh addon/run_dx7_host.sh addon/NSMODULE.json dist/ForceDX7/
cp web/dx7_ui.html web/server.py web/manage.sh web/run_dx7_web.sh dist/ForceDX7/web/

echo ""
echo "=== Build Complete ==="
echo "Output: dist/ForceDX7/"
echo ""
echo "Works out of the box (built-in Init patch). For real patches, drop"
echo "DX7-compatible .syx bank files (32 voices each) into:"
echo "  dist/ForceDX7/banks/"
