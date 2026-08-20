#!/usr/bin/env python3
"""Where do user-defined voices go?

Modes added under "Modes" with names the engine does not already know are
silently dropped -- renaming a stock mode makes it disappear, so the engine is
matching the key name against a table of its own. But Ivx230nt.dll also has the
string "Custom Modes", and a genuine installation has an empty key of that name
next to "Modes". This tries the same definitions in both places.

    python tools/probe_custom_modes.py
"""

import json
import os
import sys
import tempfile
import winreg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADDON = os.path.join(ROOT, "bin", "infovox230")
sys.path.insert(0, os.path.join(ADDON, "host"))

import infovox_host as H  # noqa: E402

ENGINE_KEY = r"Software\Babel-Infovox AB\Infovox 230"

# Names to try. Some are invented; some are names a genuine installation uses,
# to find out how wide the engine's own table really is.
# One invented speaker per language, each named "<language> Probe". If the rule
# really is "the key name must begin with a language the engine knows", every one
# of these is accepted and the two controls at the end are not.
LANGUAGES = [
    "American English", "British English", "Castilian Spanish", "Danish", "Dutch",
    "Finnish", "French", "German", "Icelandic", "Italian", "Norwegian", "Swedish",
]
TRIALS = [lang + " Probe" for lang in LANGUAGES] + [
    "My Deep English",   # control: no language prefix
    "US English Probe",  # control: a language name the engine does not use
]


def write_mode(root, subkey, name, values):
    key = winreg.CreateKeyEx(root, subkey + "\\" + name, 0, winreg.KEY_WRITE)
    try:
        for vn, vv in values.items():
            if not vn.startswith("_"):
                winreg.SetValueEx(key, vn, 0, winreg.REG_SZ, str(vv))
    finally:
        winreg.CloseKey(key)


def run(where, label):
    from comtypes import CoInitialize

    CoInitialize()
    scratch = os.environ.get("IVX_SCRATCH", tempfile.gettempdir())
    os.makedirs(scratch, exist_ok=True)

    hive = os.path.join(scratch, "custom_%s.hive" % label)
    for suffix in ("", ".LOG1", ".LOG2"):
        try:
            os.remove(hive + suffix)
        except OSError:
            pass

    with open(os.path.join(ADDON, "modes.json"), "r") as f:
        modes = json.load(f)
    template = next(m for m in modes if m["_name"] == "American English Male")

    eng = H.Engine(os.path.join(ADDON, "engine"),
                   modes_json=os.path.join(ADDON, "modes.json"), hive_path=hive)

    # Reproduce Engine.load()'s configuration step, then add the trial modes
    # into whichever key is being tested, before the engine is instantiated.
    os.environ["PATH"] = os.path.join(ADDON, "engine") + os.pathsep + os.environ.get("PATH", "")
    os.chdir(os.path.join(ADDON, "engine"))
    eng._purge_legacy_keys()
    root = eng.hive.load(hive)
    eng._write_config(root)

    for index, name in enumerate(TRIALS):
        # Each trial is cloned from a stock mode of ITS OWN language, so the
        # only thing that varies is the key name. Cloning everything from the
        # American English mode confounded an earlier run: the entry said one
        # language while the name said another.
        base = template
        for m in modes:
            if name.startswith(m["_name"].rsplit(" ", 1)[0] + " ") and m["_name"].endswith("Male"):
                base = m
                break
        entry = dict(base)
        entry["ModeGUID"] = "{c9c5eda0-7c89-11d0-03ff-%02x0000000000}" % index
        entry["SpeakerName"] = name
        entry["Pitch"] = "45"
        write_mode(root, ENGINE_KEY + "\\" + where, name, entry)

    eng.hive.override_hkcu()
    eng.enum = eng._load_direct()
    found = eng.list_modes()
    names = {(m.szModeName or "").strip() for m in found}

    print("under %-14s -> %d modes enumerated" % ('"' + where + '"', len(found)))
    for name in TRIALS:
        print("    %-26s %s" % (name, "ACCEPTED" if name in names else "rejected"))
    print()
    return names


if __name__ == "__main__":
    run("Modes", "modes")
    run("Custom Modes", "custom")
