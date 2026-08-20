#!/usr/bin/env python3
"""What does the engine actually accept as a voice definition?

User-defined voices need ids of their own, and invented ones were rejected. This
narrows down why. Each run starts from a FRESH hive -- an earlier version of this
probe reused one, which quietly accumulated every mode from every previous run
and made the results meaningless -- and prints exactly what the enumerator
returns.

    IVX_VARIANT=append|trim|renamed|fewer python tools/probe_modeguid.py
"""

import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADDON = os.path.join(ROOT, "bin", "infovox230")
sys.path.insert(0, os.path.join(ADDON, "host"))

import infovox_host as H  # noqa: E402

CANDIDATES = [
    ("ZZ speaker index 5",      "{c9c5eda0-7c89-11d0-0300-050000000000}"),
    ("ZZ speaker index 255",    "{c9c5eda0-7c89-11d0-0300-ff0000000000}"),
    ("ZZ spare bytes set",      "{c9c5eda0-7c89-11d0-0300-0000deadbeef}"),
    ("ZZ language index 0x20",  "{c9c5eda0-7c89-11d0-0320-000000000000}"),
    ("ZZ wholly arbitrary",     "{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"),
]


def main():
    from comtypes import CoInitialize

    CoInitialize()
    scratch = os.environ.get("IVX_SCRATCH", tempfile.gettempdir())
    os.makedirs(scratch, exist_ok=True)
    variant = os.environ.get("IVX_VARIANT", "append")

    with open(os.path.join(ADDON, "modes.json"), "r") as f:
        modes = json.load(f)
    template = next(m for m in modes if m["_name"] == "American English Male")

    if variant == "fewer":
        # Just ten stock modes, nothing added: does the engine return ten?
        modes = modes[:10]
        candidates = []
    elif variant == "trim":
        # Keep the total at 60 by dropping stock modes.
        modes = modes[: 60 - len(CANDIDATES)]
        candidates = CANDIDATES
    elif variant == "renamed":
        # One stock mode, renamed but otherwise untouched. If the engine goes by
        # its own table rather than by what the config says, this vanishes.
        modes = list(modes)
        for m in modes:
            if m["_name"] == "American English Zombie":
                m["_name"] = "ZZ renamed zombie"
                m["SpeakerName"] = "ZZ renamed zombie"
        candidates = []
    else:
        candidates = CANDIDATES

    for label, guid in candidates:
        entry = dict(template)
        entry["_name"] = label
        entry["ModeGUID"] = guid
        entry["SpeakerName"] = label
        modes.append(entry)

    modes_path = os.path.join(scratch, "modes_probe.json")
    with open(modes_path, "w") as f:
        json.dump(modes, f)

    # A fresh hive every time. Reusing one accumulates modes across runs.
    hive = os.path.join(scratch, "modeguid_%s.hive" % variant)
    for suffix in ("", ".LOG1", ".LOG2"):
        try:
            os.remove(hive + suffix)
        except OSError:
            pass

    eng = H.Engine(os.path.join(ADDON, "engine"), modes_json=modes_path, hive_path=hive)
    eng.load()
    found = eng.list_modes()
    names = sorted((m.szModeName or "").strip() for m in found)

    print("variant %-8s  offered %d modes  ->  engine enumerated %d"
          % (variant, len(modes), len(found)))
    extra = [n for n in names if n.startswith("ZZ")]
    print("  names beginning ZZ that came back: %s" % (extra if extra else "none"))
    print("  first five returned: %s" % names[:5])
    print("  last five returned:  %s" % names[-5:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
