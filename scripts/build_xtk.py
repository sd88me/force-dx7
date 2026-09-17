#!/usr/bin/env python3
"""Build addon/Force DX7 Control.xtk -- a Force track template that
pre-assigns Q-Link knobs (Force's physical knob bank) to dx7_host's CC map
(src/dx7_host.cpp's PARAMS[] table), so loading it onto a MIDI track gets
you named, correctly-ranged knobs instead of hand MIDI-learning each one.
The web panel (web/dx7_ui.html) covers every chain_param; this template
covers the 16 that fit a physical Q-Link bank (one bank is 16 knobs --
Dexed's 20 chain_params need 4 left for the web panel only: lfo_delay,
lfo_pms, lfo_sync, transpose).

Format background (reverse-engineered, not documented by Akai/InMusic - see
docs/capture-xtk.md and force-acid/force-maze/force-jv880's own
build_xtk.py, which this is adapted from): a .xtk is

    <5-line ASCII header>\n<gzip-compressed JSON>

    ACVS
    3.3.0.0
    SerialisableTrackData
    json
    Linux

scripts/xtk-seed.json is the JSON body of a real captured template
(Harpie4T's own control track, pulled from a live MockbaMod Force) - generic
Force/mixer/pad-bank boilerplate, reused byte-for-byte here (same file every
other port in this project reuses) except for `data.program.customQLinks`
(rebuilt below) and self-referential name fields.

CAVEAT, same as this project's other ports: NOT YET CONFIRMED to load
correctly in the Force's UI (nobody has clicked through and looked at it on
a real screen). Load it once and check: knob names show up, ranges look
right. See docs/capture-xtk.md.

Usage:
    python3 scripts/build_xtk.py [--track-name "DX7 CTRL"] [--out "addon/Force DX7 Control.xtk"]
"""
import argparse
import gzip
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SEED_PATH = HERE / "xtk-seed.json"
HEADER = "ACVS\n3.3.0.0\nSerialisableTrackData\njson\nLinux\n"

# (key, label, cc) - must match src/dx7_host.cpp's PARAMS[] table exactly
# (key names, CC numbers) or the knob will move but nothing will happen on
# the device. All of Dexed's chain_params are plain linear int ranges (no
# momentary triggers) same as force-jv880's, so there's no "kind"/momentary
# column here either.
KNOBS = [
    ("preset",            "PRESET",   20),
    ("output_level",      "OUTPUT",   21),
    ("octave_transpose",  "OCTAVE",   22),
    ("algorithm",         "ALGO",     23),
    ("feedback",          "FEEDBACK", 24),
    ("osc_sync",          "SYNC",     25),
    ("lfo_speed",         "LFO SPD",  26),
    ("lfo_pmd",           "LFO PMD",  27),
    ("lfo_amd",           "LFO AMD",  28),
    ("lfo_wave",          "LFO WAVE", 29),
    ("op1_level",         "OP1 LVL",  30),
    ("op2_level",         "OP2 LVL",  31),
    ("op3_level",         "OP3 LVL",  32),
    ("op4_level",         "OP4 LVL",  33),
    ("op5_level",         "OP5 LVL",  34),
    ("op6_level",         "OP6 LVL",  35),
]
assert len(KNOBS) <= 16, "one Q-Link bank is only 16 knobs"

FULL_RANGE = {"min": 0.0, "max": 1.0, "stride": 0.0, "deadspot": 0.0, "skew": 1.0}
INPUT_RANGE = {"min": 0.0, "max": 1.0, "stride": 0.0, "deadspot": 2.0, "skew": 1.0}


def make_qlink(label, cc, track_name):
    return {
        "name": label,
        "controlType": 0,
        "targetData": [{
            "version": 1,
            "parameter": cc,
            "track": track_name,
            "insertParamIndex": {"initialized": False},
            "instrumentIndex": 257,
            "paramType": 1,
            "controlInputRange": dict(INPUT_RANGE),
            "parameterRange": dict(FULL_RANGE),
            "behaviour": 0,
        }],
        "momentary": 0,
        "controlValue": 0.0,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--track-name", default="DX7 CTRL",
                     help='must match the MIDI track name exactly once loaded on the Force, and '
                          'src/dx7_host.cpp\'s --control-channel must match that track\'s output '
                          'MIDI channel (default: "DX7 CTRL")')
    ap.add_argument("--out", default=str(HERE.parent / "addon" / "Force DX7 Control.xtk"))
    ap.add_argument("--template-name", default="Force DX7 Control")
    args = ap.parse_args()

    if not SEED_PATH.exists():
        sys.exit(f"seed file missing: {SEED_PATH}")

    doc = json.loads(SEED_PATH.read_text())

    program = doc["data"]["program"]
    program["customQLinks"] = [
        make_qlink(label, cc, args.track_name)
        for (_key, label, cc) in KNOBS
    ]

    def rename(obj):
        if isinstance(obj, dict):
            for k, v in obj.items():
                if k == "name" and isinstance(v, str) and v.startswith("Harpie 4T Control"):
                    obj[k] = v.replace("Harpie 4T Control", args.template_name)
                else:
                    rename(v)
        elif isinstance(obj, list):
            for item in obj:
                rename(item)

    rename(doc)

    body = HEADER + json.dumps(doc, indent=4)
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with gzip.GzipFile(out_path, "wb", mtime=0) as f:
        f.write(body.encode("utf-8"))

    print(f"wrote {out_path} ({out_path.stat().st_size} bytes)")
    print(f"Q-Link bank: {len(program['customQLinks'])} knobs, track name '{args.track_name}'")
    print("Load it on the Force onto a MIDI track literally named "
          f"'{args.track_name}' -- the CC targets are bound by track NAME, not by track index.")
    print("dx7_host must be started with --control-channel matching that track's output channel "
          "(default 1) for any of this to actually reach dx7_plugin.cpp.")


if __name__ == "__main__":
    main()
