# -*- coding: utf-8 -*-
"""Drive every published SAPI voice through the real engine and check it speaks.

Uses sapi_probe, which creates the shipping COM object and captures what it writes, so
this exercises the same path a screen reader takes -- including, for the 64-bit engine,
the bridge to the 32-bit worker. Nothing has to be registered and no elevation is needed.

    python tools/verify_sapi.py --probe sapi_probe32.exe --dll output/BestspeechSAPI.dll

Exit status is non-zero if any voice fails.
"""
import argparse
import os
import re
import subprocess
import sys

# Mirrors engines[] in src/engines.hpp: (id, label, voice count).
ENGINES = [
    ("classic", "English (Classic 1994)", 14),
    ("eng", "English", 14),
    ("dut", "Dutch", 14),
    ("fre", "French", 14),
    ("ger", "German", 14),
    ("gre", "Greek", 1),
    ("heb", "Hebrew", 14),
    ("ita", "Italian", 14),
    ("jpn", "Japanese", 1),
    ("pol", "Polish", 1),
    ("por", "Portuguese", 14),
    ("rus", "Russian", 14),
    ("spa", "Spanish", 14),
]

VOICE_NAMES = ["Fred", "Sara", "Hary", "Wendy", "Dexter", "Alien", "Kit", "Bruno",
               "Ghost", "Peeper", "Dracula", "Granny", "Martha", "Tim"]

# Latin text on purpose: the Greek and Russian frontends drop the Latin alphabet
# entirely, so this is silence unless the transliteration is doing its job.
PROBE_TEXT = "Hello world, open Firefox settings. Test 123."

# The Dutch frontend ignores ~e excitation, so the two voices that differ only in
# excitation genuinely cannot be told apart there. Listed so the duplicate check still
# catches anything that is not a known engine limitation.
KNOWN_ALIKE = {("dut", "Bruno", "Ghost")}

LINE = re.compile(r"Speak -> 0x(?P<hr>[0-9A-Fa-f]{8}) \| (?P<rate>\d+) hz \| "
                  r"(?P<secs>[\d.]+)s peak (?P<peak>\d+) rms (?P<rms>\d+)")


def run(probe, dll, engine, voice, wav, extra=None):
    cmd = [probe, dll, engine, str(voice), wav, PROBE_TEXT] + (extra or [])
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    m = LINE.search(out.stdout or "")
    if not m:
        return None
    d = m.groupdict()
    return {"hr": int(d["hr"], 16), "rate": int(d["rate"]), "secs": float(d["secs"]),
            "peak": int(d["peak"]), "rms": int(d["rms"])}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", required=True, help="sapi_probe32.exe or sapi_probe64.exe")
    ap.add_argument("--dll", required=True, help="BestspeechSAPI.dll to exercise")
    ap.add_argument("--wav", default="", help="keep one wav per voice in this directory")
    args = ap.parse_args()

    wavdir = args.wav or os.path.join(here, "..", "build_x86", "verify_wav")
    os.makedirs(wavdir, exist_ok=True)

    failures = []
    total = 0
    print("%-9s %-22s %-6s %s" % ("engine", "voice", "rate", "result"))
    print("-" * 66)

    for engine_id, label, count in ENGINES:
        seen = {}
        for v in range(count):
            total += 1
            name = label if count == 1 else VOICE_NAMES[v]
            wav = os.path.join(wavdir, "%s_%d.wav" % (engine_id, v))
            r = run(args.probe, args.dll, engine_id, v, wav)

            if r is None:
                failures.append("%s/%s: probe produced no result" % (engine_id, name))
                print("%-9s %-22s %-6s NO RESULT" % (engine_id, name, "-"))
                continue
            if r["hr"] != 0:
                failures.append("%s/%s: Speak returned 0x%08X" % (engine_id, name, r["hr"]))
            if r["peak"] < 500:
                failures.append("%s/%s: silence (peak %d)" % (engine_id, name, r["peak"]))
            if r["secs"] < 0.5:
                failures.append("%s/%s: almost no audio (%.2fs)" % (engine_id, name, r["secs"]))

            try:
                digest = open(wav, "rb").read()
            except OSError:
                digest = b""
            if digest and digest in seen and (engine_id, seen[digest], name) not in KNOWN_ALIKE:
                failures.append("%s: %s is identical to %s" % (engine_id, name, seen[digest]))
            seen[digest] = name

            print("%-9s %-22s %-6d %.2fs peak %-6d rms %d" % (
                engine_id, name, r["rate"], r["secs"], r["peak"], r["rms"]))

    # Parameters have to actually do something, on both an engine that takes inline
    # commands and one that ignores them.
    print("-" * 66)
    for engine_id in ("classic", "gre"):
        for flag, values in (("--rate", (-10, 0, 10)), ("--pitch", (-10, 0, 10)),
                             ("--volume", (100, 40))):
            results = []
            for val in values:
                wav = os.path.join(wavdir, "p_%s_%s_%s.wav" % (engine_id, flag[2:], val))
                r = run(args.probe, args.dll, engine_id, 0, wav, [flag, str(val)])
                results.append((val, r, open(wav, "rb").read() if os.path.exists(wav) else b""))

            distinct = len({d for _, _, d in results if d})
            # Pitch is genuinely unavailable on the engines that ignore inline commands;
            # everything else must change the audio.
            expected = 1 if (engine_id == "gre" and flag == "--pitch") else len(values)
            ok = distinct == expected
            if not ok:
                failures.append("%s %s: %d distinct outputs, expected %d"
                                % (engine_id, flag, distinct, expected))
            note = " (not supported by this engine, as expected)" if expected == 1 else ""
            print("%-9s %-22s %s -> %d distinct%s" % (engine_id, flag, values, distinct, note))

    print("-" * 66)
    print("%d voices exercised" % total)
    if failures:
        print("FAILED (%d)" % len(failures))
        for f in failures:
            print("  - %s" % f)
        return 1
    print("every voice speaks, and all supported parameters take effect")
    return 0


if __name__ == "__main__":
    sys.exit(main())
