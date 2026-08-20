#!/usr/bin/env python3
"""Render a listenable sample of every Infovox voice.

Produces, under samples/:
  all_voices.wav        every voice in turn, each announcing itself
  by_language/*.wav     one file per language, its five speakers in turn
  voices/*.wav          one file per voice
  CONTENTS.txt          the running order, with timings

Each clip is the voice saying its own name, then a sentence in its own
language -- so a listener can tell both which voice it is and whether the right
language rules are in use.

Nothing needs to be installed: this drives Infovox230Diag straight out of the
build's output folder.

    python tools/make_samples.py [--out samples] [--direct]
"""

import argparse
import os
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DIAG = os.path.join(ROOT, "output", "Infovox230Diag.exe")

# One sentence per language, chosen so the language's own rules are audible
# rather than English phonetics applied to foreign words.
SAMPLES = {
    0x0409: "Hello. The quick brown fox jumps over the lazy dog.",
    0x0809: "Good day. The quick brown fox jumps over the lazy dog.",
    0x0406: "Goddag. Dette er den danske stemme fra Infovox.",
    0x0413: "Hallo. Dit is de Nederlandse stem van Infovox.",
    0x040B: "Hyvaa paivaa. Tama on Infovoxin suomenkielinen aani.",
    0x040C: "Bonjour. Ceci est la voix francaise de Infovox.",
    0x0407: "Guten Tag. Dies ist die deutsche Stimme von Infovox.",
    0x040F: "Godan dag. Thetta er islenska roeddin fra Infovox.",
    0x0410: "Buongiorno. Questa e la voce italiana di Infovox.",
    0x0414: "God dag. Dette er den norske stemmen fra Infovox.",
    0x040A: "Hola. Esta es la voz castellana de Infovox.",
    0x041D: "God dag. Det haer aer den svenska roesten fraan Infovox.",
}

LANGUAGE_NAMES = {
    0x0409: "American English", 0x0809: "British English", 0x0406: "Danish",
    0x0413: "Dutch", 0x040B: "Finnish", 0x040C: "French", 0x0407: "German",
    0x040F: "Icelandic", 0x0410: "Italian", 0x0414: "Norwegian",
    0x040A: "Castilian Spanish", 0x041D: "Swedish",
}


def list_voices():
    """Ask the diagnostics tool for the catalogue: name, lcid, gender, age."""
    out = subprocess.run([DIAG, "list"], capture_output=True, text=True, check=True).stdout
    voices = []
    for line in out.splitlines():
        if "0x" not in line or not line.startswith("Infovox"):
            continue
        # "Infovox American English Male  0x0409  Male  Adult  {guid}"
        marker = line.index("0x")
        name = line[:marker].strip()
        rest = line[marker:].split()
        voices.append({"name": name, "lcid": int(rest[0], 16), "gender": rest[1]})
    return voices


def render(voice, text, path, direct):
    command = [DIAG, "speak" if direct else "worker", path, text, "--voice", voice["name"]]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(path):
        print("  FAILED %s: %s" % (voice["name"], result.stdout.strip().splitlines()[-1:]))
        return False
    return True


def read_wav(path):
    with wave.open(path, "rb") as w:
        return w.getparams(), w.readframes(w.getnframes())


def write_wav(path, params, frames):
    with wave.open(path, "wb") as w:
        w.setnchannels(params.nchannels)
        w.setsampwidth(params.sampwidth)
        w.setframerate(params.framerate)
        w.writeframes(frames)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join(ROOT, "samples"))
    parser.add_argument("--direct", action="store_true",
                        help="drive the engine directly instead of through the worker")
    args = parser.parse_args()

    if not os.path.exists(DIAG):
        print("Build first: %s is missing." % DIAG)
        return 1

    voices_dir = os.path.join(args.out, "voices")
    by_language_dir = os.path.join(args.out, "by_language")
    for d in (args.out, voices_dir, by_language_dir):
        os.makedirs(d, exist_ok=True)

    voices = list_voices()
    if not voices:
        print("No voices reported.")
        return 2
    print("%d voices to render\n" % len(voices))

    params = None
    everything = bytearray()
    per_language = {}
    contents = []
    gap = None

    for voice in voices:
        sentence = SAMPLES.get(voice["lcid"], "This is a test.")
        # The name first, spoken by the voice itself: that is what makes one
        # speaker tellable from another by ear.
        text = "%s. %s" % (voice["name"].replace("Infovox ", ""), sentence)
        path = os.path.join(voices_dir, voice["name"].replace(" ", "_") + ".wav")
        if not render(voice, text, path, args.direct):
            continue

        clip_params, frames = read_wav(path)
        if params is None:
            params = clip_params
            gap = b"\x00" * (params.framerate * params.sampwidth * params.nchannels // 2)

        start = len(everything) / float(params.framerate * params.sampwidth * params.nchannels)
        everything += frames + gap
        per_language.setdefault(voice["lcid"], bytearray()).extend(frames + gap)
        contents.append((start, voice["name"], LANGUAGE_NAMES.get(voice["lcid"], "?")))
        print("  %-36s %5.1fs  at %s" % (voice["name"], len(frames) / float(
            params.framerate * params.sampwidth * params.nchannels), fmt(start)))

    if params is None:
        print("Nothing rendered.")
        return 3

    write_wav(os.path.join(args.out, "all_voices.wav"), params, bytes(everything))
    for lcid, frames in per_language.items():
        name = LANGUAGE_NAMES.get(lcid, "0x%04X" % lcid).replace(" ", "_")
        write_wav(os.path.join(by_language_dir, name + ".wav"), params, bytes(frames))

    with open(os.path.join(args.out, "CONTENTS.txt"), "w", encoding="utf-8") as f:
        f.write("Infovox 230 voice samples\n")
        f.write("=========================\n\n")
        f.write("all_voices.wav plays every voice in turn, each one saying its own\n")
        f.write("name and then a sentence in its own language.\n\n")
        f.write("Running order in all_voices.wav:\n\n")
        for start, name, language in contents:
            f.write("  %s  %-36s %s\n" % (fmt(start), name, language))
        f.write("\nby_language\\ has the same clips grouped one file per language.\n")
        f.write("voices\\ has them one file per voice.\n")

    total = len(everything) / float(params.framerate * params.sampwidth * params.nchannels)
    print("\nall_voices.wav: %d voices, %s total" % (len(contents), fmt(total)))
    print("written to %s" % args.out)
    return 0


def fmt(seconds):
    return "%d:%05.2f" % (int(seconds) // 60, seconds % 60)


if __name__ == "__main__":
    sys.exit(main())
