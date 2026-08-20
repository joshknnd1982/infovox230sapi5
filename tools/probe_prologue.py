#!/usr/bin/env python3
"""Does the engine parse control tags that sit directly against each other?

The prologue this project prepends to every utterance is three tags in a row,
"\\Spd=..\\\\Pit=..\\\\Vol=..\\", which puts two backslashes together at each
join -- and in tagged text a doubled backslash is the escape for a literal
backslash. Earlier probes only ever tested one tag at a time, so this measures
whether adjacent tags survive, and if not, what separator does.
"""

import os
import sys
import array
import math

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADDON = os.path.join(ROOT, "bin", "infovox230")
sys.path.insert(0, os.path.join(ADDON, "host"))

import infovox_host as H  # noqa: E402

BS = chr(92)


def rms(pcm):
    a = array.array("h")
    a.frombytes(pcm[: len(pcm) & ~1])
    if not len(a):
        return 0.0
    return math.sqrt(sum(v * v for v in a) / len(a))


def main():
    from comtypes import CoInitialize

    CoInitialize()
    pump = H.make_pump()
    scratch = os.environ.get("IVX_SCRATCH", os.path.join(HERE, "_probe"))
    os.makedirs(scratch, exist_ok=True)

    eng = H.Engine(os.path.join(ADDON, "engine"),
                   modes_json=os.path.join(ADDON, "modes.json"),
                   hive_path=os.path.join(scratch, "probe.hive"))
    eng.load()
    modes = eng.list_modes()
    target = next((m for m in modes
                   if (m.szModeName or "").strip() == "American English Male"), modes[0])
    eng.select(str(target.gModeID))

    SENT = "The quick brown fox jumps over the lazy dog."

    def clean(text):
        eng.attrs.SpeedSet(150)
        eng.attrs.PitchSet(101)
        eng.attrs.VolumeSet(0xFFFFFFFF)
        eng.buf.reset()
        pcm, wfx, _ = eng.speak(text, pump, timeout=60)
        bps = wfx.nAvgBytesPerSec if wfx else 32000
        return len(pcm) / float(bps), rms(pcm)

    def tag(name, value):
        return BS + "%s=%s" % (name, value) + BS

    vol = lambda pct: (int(pct * 0xFFFF / 100) | (int(pct * 0xFFFF / 100) << 16))

    cases = [
        ("baseline",                    SENT),
        ("Spd only",                    tag("Spd", 300) + SENT),
        ("Vol only (25%)",              tag("Vol", vol(25)) + SENT),
        ("Vol only (0%)",               tag("Vol", 0) + SENT),
        ("Spd+Pit adjacent",            tag("Spd", 300) + tag("Pit", 200) + SENT),
        ("Spd+Pit+Vol adjacent",        tag("Spd", 300) + tag("Pit", 200) + tag("Vol", vol(25)) + SENT),
        ("Spd Pit Vol, space between",  tag("Spd", 300) + " " + tag("Pit", 200) + " " + tag("Vol", vol(25)) + " " + SENT),
        ("Vol then Spd adjacent",       tag("Vol", vol(25)) + tag("Spd", 300) + SENT),
        ("Vol alone then text, Spd last", tag("Vol", vol(25)) + SENT + tag("Spd", 300)),
    ]

    print("%-32s %8s %10s" % ("case", "seconds", "rms"))
    base_s = base_r = None
    for label, text in cases:
        try:
            secs, r = clean(text)
        except Exception as e:
            print("%-32s ERROR %s" % (label, e))
            continue
        if base_s is None:
            base_s, base_r = secs, r
        print("%-32s %8.2f %10.1f   (%+.0f%% dur, %+.0f%% rms)"
              % (label, secs, r, (secs / base_s - 1) * 100,
                 (r / base_r - 1) * 100 if base_r else 0))

    print()
    print("Expected if adjacent tags parse: 'Spd+Pit+Vol adjacent' should be")
    print("about as short as 'Spd only' and about as quiet as 'Vol only (25%)'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
