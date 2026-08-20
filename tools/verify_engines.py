# -*- coding: utf-8 -*-
"""Check that every BestSpeech engine dll and every voice actually produces speech.

Drives the real 32-bit engine dlls through b32_helper.exe and measures the pcm that
comes back, so a dll that returns silence, truncates, or kills the process is caught
here rather than in a screen reader. When the C++ pipeline probe has been built, the
text is put through the shipping pipeline first, so what is measured is exactly what
the SAPI engine will send.

    python tools/verify_engines.py [--bin DIR] [--probe pipeline_probe.exe] [--wav DIR]

Exit status is non-zero if any check fails.
"""
import argparse
import array
import os
import struct
import subprocess
import sys
import wave

HANDSHAKE_MAGIC = 0xFFFFFFFE
QUIT = 0xFFFFFFFF
CREATE_NO_WINDOW = 0x08000000

# (engine id, dll, native-script probe text). The native text is deliberately in each
# engine's own script: that is what the frontend is built to read, so anything less than
# clean audio here means the dll itself is broken.
ENGINES = [
    ("classic", "b32_tts.dll", "Hello, this is a test of the speech engine."),
    ("eng", "dll_eng.dll", "Hello, this is a test of the speech engine."),
    ("dut", "dll_dut.dll", "Hallo, dit is een test van de spraakmachine."),
    ("fre", "dll_fre.dll", "Bonjour, ceci est un test de la synthese vocale."),
    ("ger", "dll_ger.dll", "Hallo, dies ist ein Test der Sprachausgabe."),
    ("gre", "dll_gre.dll", "Γεια σας, αυτό είναι μια δοκιμή της φωνής."),
    ("heb", "dll_heb.dll", "שלום, זו בדיקה של מנוע הדיבור."),
    ("ita", "dll_ita.dll", "Ciao, questo e un test del sintetizzatore vocale."),
    ("jpn", "dll_jpn.dll", "こんにちは。これはおんせいごうせいのてすとです。"),
    ("pol", "dll_pol.dll", "Dzień dobry, to jest test syntezatora mowy."),
    ("por", "dll_por.dll", "Olá, este é um teste do sintetizador de voz."),
    ("rus", "dll_rus.dll", "Привет, это тест синтезатора речи."),
    ("spa", "dll_spa.dll", "Hola, esto es una prueba del sintetizador de voz."),
]

# Engines whose frontend ignores every inline command, so they have one voice, not
# fourteen identical ones. Kept in step with cmd_mode::none in src/engines.hpp.
SINGLE_VOICE = {"gre", "jpn", "pol"}
CLASSIC = {"classic"}

# The German frontend has no ~h command: rather than setting the inflection it reads the
# command out as text, so a stray "h nought" gets spoken ahead of every utterance -- at
# the engine's default gain, since the ~g after it has not been applied yet, which is
# what made German sound quiet and then abruptly louder. Kept in step with the
# inflection flag in src/engines.hpp.
NO_INFLECTION = {"ger"}

# Voice pairs a given engine genuinely cannot tell apart, because its frontend ignores
# the one command that separates them. The Dutch dll ignores ~e excitation, so Bruno and
# Ghost -- identical but for excitation -- collapse onto each other there. Listed so the
# check still catches any duplicate that is not a known engine limitation.
KNOWN_ALIKE = {"dut": {("Bruno", "Ghost")}}

# name, headsize, excitation, inflection, unvoiced, pitch -- mirrors voices[] in engines.hpp
VOICES = [
    ("Fred", 1, 3, 0, 0, 80), ("Sara", 2, 3, -20, 0, 175), ("Hary", 3, 3, 10, 0, 65),
    ("Wendy", 2, 1, 50, 0, 150), ("Dexter", 6, 6, 0, -25, 90), ("Alien", 4, 6, -50, -20, 115),
    ("Kit", 5, 3, 40, 0, 230), ("Bruno", 3, 3, 50, 0, 60), ("Ghost", 3, 2, 50, 0, 60),
    ("Peeper", 2, 2, 0, 5, 80), ("Dracula", 3, 3, 45, -5, 47), ("Granny", 4, 3, -60, 0, 350),
    ("Martha", 6, 4, 100, -5, 300), ("Tim", 3, 4, -10, 0, 60),
]

ASCII_SYMBOLS = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"

# Latin text is the whole point of the transliteration: these two frontends drop every
# ascii letter, so without the rewrite this phrase comes back as pure silence.
LATIN_PROBE = "Hello world, open Firefox settings."


class Engine(object):
    """One b32_helper.exe process bound to one engine dll."""

    def __init__(self, helper, dll_path):
        self.helper = helper
        self.dll_path = dll_path
        self.proc = None
        self.sample_rate = 0
        self.start()

    def start(self):
        self.proc = subprocess.Popen(
            [self.helper, self.dll_path],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            creationflags=CREATE_NO_WINDOW)
        hdr = self._read(8)
        if hdr is None:
            raise RuntimeError("helper died during startup for %s" % self.dll_path)
        magic, self.sample_rate = struct.unpack("<II", hdr)
        if magic != HANDSHAKE_MAGIC:
            raise RuntimeError("unexpected handshake %#x from %s" % (magic, self.dll_path))

    def _read(self, n):
        buf = b""
        while len(buf) < n:
            try:
                chunk = self.proc.stdout.read(n - len(buf))
            except OSError:
                return None
            if not chunk:
                return None
            buf += chunk
        return buf

    def speak(self, text_bytes, rate_mult=1.0):
        """Returns pcm bytes, or None if the engine process died."""
        try:
            self.proc.stdin.write(struct.pack("<If", len(text_bytes), rate_mult) + text_bytes)
            self.proc.stdin.flush()
        except OSError:
            return None
        out = bytearray()
        while True:
            hdr = self._read(4)
            if hdr is None:
                return None
            n = struct.unpack("<I", hdr)[0]
            if n == 0:
                break
            chunk = self._read(n)
            if chunk is None:
                return None
            out += chunk
        if self.proc.poll() is not None:
            return None
        return bytes(out)

    def restart(self):
        self.close()
        self.start()

    def close(self):
        if self.proc is None:
            return
        try:
            self.proc.stdin.write(struct.pack("<I", QUIT))
            self.proc.stdin.flush()
            self.proc.wait(timeout=3)
        except Exception:
            pass
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc = None


def stats(pcm, rate):
    if not pcm:
        return {"secs": 0.0, "rms": 0, "peak": 0}
    samples = array.array("h")
    samples.frombytes(pcm[:len(pcm) // 2 * 2])
    if not samples:
        return {"secs": 0.0, "rms": 0, "peak": 0}
    peak = max(max(samples), -min(samples))
    rms = int((sum(float(v) * v for v in samples) / len(samples)) ** 0.5)
    return {"secs": len(samples) / float(rate), "rms": rms, "peak": peak}


def save_wav(path, pcm, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)


class Report(object):
    def __init__(self):
        self.failures = []

    def check(self, ok, label, detail=""):
        if not ok:
            self.failures.append("%s: %s" % (label, detail) if detail else label)
        return ok


def prepared(probe, engine_id, text):
    """Run text through the shipping C++ pipeline, or return it unchanged if the probe
    has not been built."""
    if not probe:
        return text
    out = subprocess.run([probe, engine_id, text], capture_output=True)
    if out.returncode != 0:
        return text
    return out.stdout.decode("utf-8").rstrip("\r\n")


def command_prefix(engine_id, voice):
    """The inline commands the SAPI engine would prepend for this voice."""
    if engine_id in SINGLE_VOICE:
        return ""
    _, headsize, excitation, inflection, unvoiced, pitch = voice
    # Same order as command_prefix() in src/text_pipeline.cpp, which is load bearing:
    # ~v resets the frequency and ~f resets the inflection, so anything set before them
    # is discarded.
    pre = "~g0]~r0]"
    if engine_id in CLASSIC:
        pre += "~v%d]" % headsize
    pre += "~e%d]~u%d]~f%d]" % (excitation, unvoiced, pitch)
    if engine_id not in NO_INFLECTION:
        pre += "~h%d]" % inflection
    return pre


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(here, "..", "bin"),
                    help="directory holding the engine dlls and b32_helper.exe")
    ap.add_argument("--probe", default="", help="path to pipeline_probe.exe")
    ap.add_argument("--wav", default="", help="write a sample wav per engine into this directory")
    args = ap.parse_args()

    bindir = os.path.abspath(args.bin)
    helper = os.path.join(bindir, "b32_helper.exe")
    if not os.path.isfile(helper):
        print("b32_helper.exe not found in %s" % bindir)
        return 2
    probe = os.path.abspath(args.probe) if args.probe else ""
    if probe and not os.path.isfile(probe):
        print("pipeline probe not found: %s (continuing without it)" % probe)
        probe = ""
    if args.wav:
        os.makedirs(args.wav, exist_ok=True)

    rep = Report()
    print("%-9s %-6s %-22s %s" % ("engine", "rate", "native text", "voices"))
    print("-" * 72)

    for engine_id, dll, native in ENGINES:
        dll_path = os.path.join(bindir, dll)
        if not os.path.isfile(dll_path):
            rep.check(False, engine_id, "dll missing: %s" % dll)
            print("%-9s MISSING %s" % (engine_id, dll))
            continue
        try:
            eng = Engine(helper, dll_path)
        except RuntimeError as exc:
            rep.check(False, engine_id, str(exc))
            print("%-9s FAILED TO START: %s" % (engine_id, exc))
            continue

        # 1. the engine speaks its own language
        text = prepared(probe, engine_id, native)
        pcm = eng.speak((command_prefix(engine_id, VOICES[0]) + text).encode("utf-8", "replace"))
        st = stats(pcm, eng.sample_rate) if pcm is not None else {"secs": 0, "rms": 0, "peak": 0}
        rep.check(pcm is not None, engine_id, "engine died on its own language")
        rep.check(st["peak"] > 500, engine_id, "native text produced silence")
        rep.check(st["secs"] > 0.5, engine_id, "native text produced almost no audio")
        if args.wav and pcm:
            save_wav(os.path.join(args.wav, "%s.wav" % engine_id), pcm, eng.sample_rate)

        # 2. every voice speaks, and no two voices are byte-identical
        n_voices = 1 if engine_id in SINGLE_VOICE else len(VOICES)
        seen, dup, dead = {}, [], []
        for voice in VOICES[:n_voices]:
            vp = eng.speak((command_prefix(engine_id, voice) + text).encode("utf-8", "replace"))
            if vp is None:
                dead.append(voice[0])
                eng.restart()
                continue
            if stats(vp, eng.sample_rate)["peak"] < 500:
                dead.append(voice[0])
                continue
            if vp in seen:
                pair = tuple(sorted((voice[0], seen[vp])))
                if pair not in KNOWN_ALIKE.get(engine_id, set()):
                    dup.append("%s==%s" % (voice[0], seen[vp]))
            seen[vp] = voice[0]
        rep.check(not dead, engine_id, "silent voices: %s" % ", ".join(dead))
        rep.check(not dup, engine_id, "indistinct voices: %s" % ", ".join(dup))

        # 3. Latin text reaches the ear on the engines that drop the Latin alphabet
        lat = prepared(probe, engine_id, LATIN_PROBE)
        lp = eng.speak((command_prefix(engine_id, VOICES[0]) + lat).encode("utf-8", "replace"))
        lst = stats(lp, eng.sample_rate) if lp is not None else {"secs": 0, "peak": 0}
        rep.check(lst["peak"] > 500, engine_id,
                  "Latin text produced silence (transliteration not applied?)")

        # 4. no ascii symbol silences, truncates, or kills the engine
        ref = eng.speak((command_prefix(engine_id, VOICES[0]) + text).encode("utf-8", "replace"))
        ref_secs = stats(ref, eng.sample_rate)["secs"] if ref else 0.0
        broken = []
        for sym in ASCII_SYMBOLS:
            probe_text = prepared(probe, engine_id, native[:-1] + sym + " " + native)
            sp = eng.speak((command_prefix(engine_id, VOICES[0]) + probe_text).encode("utf-8", "replace"))
            if sp is None:
                broken.append("%s(crash)" % sym)
                eng.restart()
                continue
            sst = stats(sp, eng.sample_rate)
            if sst["peak"] < 500:
                broken.append("%s(silent)" % sym)
            elif sst["secs"] < ref_secs * 1.2:
                # the probe text is two copies of the sentence, so anything much shorter
                # than the single-sentence reference means the tail was dropped
                broken.append("%s(truncated)" % sym)
        rep.check(not broken, engine_id, "symbols break synthesis: %s" % " ".join(broken))

        # 5. no inline command may be read out as text instead of obeyed. A frontend
        # that does not recognize a command speaks it, which is both audible junk and
        # -- because it precedes the gain command -- junk at the wrong volume. Comparing
        # against bare text catches that: obeying a command cannot lengthen the audio.
        if engine_id not in SINGLE_VOICE:
            bare = eng.speak(text.encode("utf-8", "replace"))
            bare_secs = stats(bare, eng.sample_rate)["secs"] if bare else 0.0
            spoken = []
            for cmd in ("~g0]", "~r0]", "~v1]", "~e3]", "~u0]", "~f80]", "~h0]"):
                if cmd == "~v1]" and engine_id not in CLASSIC:
                    continue
                if cmd == "~h0]" and engine_id in NO_INFLECTION:
                    continue
                cp = eng.speak((cmd + text).encode("utf-8", "replace"))
                if cp is None:
                    eng.restart()
                    continue
                if stats(cp, eng.sample_rate)["secs"] > bare_secs + 0.15:
                    spoken.append(cmd)
            rep.check(not spoken, engine_id,
                      "engine reads these commands aloud instead of obeying them: %s"
                      % " ".join(spoken))

        print("%-9s %-6d %-22s %d ok%s" % (
            engine_id, eng.sample_rate, "%.2fs peak %d" % (st["secs"], st["peak"]),
            n_voices - len(dead), "" if not broken else "  SYMBOLS: %s" % " ".join(broken)))
        eng.close()

    print("-" * 72)
    if rep.failures:
        print("FAILED (%d)" % len(rep.failures))
        for f in rep.failures:
            print("  - %s" % f)
        return 1
    print("all engines, voices, transliteration and symbol handling OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
