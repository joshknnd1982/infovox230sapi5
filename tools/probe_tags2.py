#!/usr/bin/env python3
"""Second-stage probe: tag behaviour with a clean state before every experiment,
and the units of the timestamps the engine reports.

The first probe showed that a control tag changes engine state permanently, so
every measurement after the first was contaminated. Here rate, pitch and volume
are pushed back to known values through ITTSAttributes before each run, which is
also exactly what the C++ driver does before every utterance.
"""

import os
import sys
import time
import array

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADDON = os.path.join(ROOT, "bin", "infovox230")
sys.path.insert(0, os.path.join(ADDON, "host"))

import infovox_host as H  # noqa: E402


class TracingBuf(H.BufNotifySink):
    def __init__(self, audio=None):
        super().__init__(audio)
        self.words = []
        self.marks2 = []

    def reset(self):
        super().reset()
        self.words = []
        self.marks2 = []

    def ITTSBufNotifySink_BookMark(self, this, qTimeStamp, dwMarkNum):
        self.marks2.append((int(qTimeStamp), self.audio.written, int(dwMarkNum)))
        return 0

    def ITTSBufNotifySink_WordPosition(self, this, qTimeStamp, dwByteOffset):
        self.words.append((int(qTimeStamp), self.audio.written, int(dwByteOffset)))
        return 0


def rms(pcm):
    if not pcm:
        return 0.0
    a = array.array("h")
    a.frombytes(pcm[: len(pcm) & ~1])
    if not len(a):
        return 0.0
    total = 0
    for v in a:
        total += v * v
    return (total / len(a)) ** 0.5


def main():
    from comtypes import CoInitialize
    from ctypes import cast, c_void_p

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
    eng.buf = TracingBuf(eng.audio)
    eng.bufPtr = eng.buf.QueryInterface(H.ITTSBufNotifySink)

    BASE_RATE, BASE_PITCH = 150, 101

    def clean_speak(text, timeout=30):
        """Restore the engine to a known state, then speak `text`."""
        eng.attrs.SpeedSet(BASE_RATE)
        eng.attrs.PitchSet(BASE_PITCH)
        eng.attrs.VolumeSet(0xFFFFFFFF)
        eng.audio.gain_base = 1.0
        eng.audio.gain_level = 1.0
        eng.buf.reset()
        pcm, wfx, _ = eng.speak(text, pump, timeout=timeout)
        return pcm, wfx

    SENT = "The quick brown fox jumps over the lazy dog."
    print("ranges: rate[%s..%s def %s] pitch[%s..%s def %s]"
          % (eng.rate_min, eng.rate_max, eng.rate_def,
             eng.pitch_min, eng.pitch_max, eng.pitch_def))
    print()

    base_pcm, wfx = clean_speak(SENT)
    bps = wfx.nAvgBytesPerSec
    base_secs = len(base_pcm) / float(bps)
    base_rms = rms(base_pcm)
    print("baseline: %d bytes, %.2fs, rms %.0f, %d Hz %d-bit"
          % (len(base_pcm), base_secs, base_rms, wfx.nSamplesPerSec, wfx.wBitsPerSample))
    print("word timestamps (qTimeStamp, audio_written, textOffset):")
    for w in eng.buf.words:
        print("   %s" % (w,))
    print("total audio bytes %d = %d samples = %.0f ms"
          % (len(base_pcm), len(base_pcm) // 2, base_secs * 1000))
    print()

    cases = [
        ("plain",                SENT),
        ("Spd=80",               "\\Spd=80\\" + SENT),
        ("Spd=400",              "\\Spd=400\\" + SENT),
        ("Pit=40",               "\\Pit=40\\" + SENT),
        ("Pit=240",              "\\Pit=240\\" + SENT),
        ("Vol=50%",              "\\Vol=%d\\%s" % (0x80008000, SENT)),
        ("Vol=10%",              "\\Vol=%d\\%s" % (0x19991999, SENT)),
        ("Pau=2000 mid",         "The quick brown fox\\Pau=2000\\jumps over the lazy dog."),
        ("Emp",                  "The quick \\Emp\\brown fox jumps over the lazy dog."),
        ("Rst after Pit",        "\\Pit=240\\High.\\Rst\\" + SENT),
        ("Chr LetterMode",       '\\Chr="LetterMode"\\abc def'),
        ("Chr Normal",           '\\Chr="Normal"\\abc def'),
        ("Prn",                  "\\Prn=hh eh l ow\\ hello"),
        ("escaped backslash",    "a\\\\b"),
        ("mrk sequence",         "\\mrk=1\\The quick\\mrk=2\\ brown fox\\mrk=3\\ jumps."),
    ]

    for label, text in cases:
        try:
            pcm, wfx = clean_speak(text)
        except Exception as e:
            print("%-18s ERROR %s" % (label, e))
            continue
        secs = len(pcm) / float(bps)
        print("%-18s %7d B  %5.2fs (%+5.0f%%)  rms %6.0f (%+5.0f%%)  words=%d marks=%s"
              % (label, len(pcm), secs, (secs / base_secs - 1) * 100, rms(pcm),
                 (rms(pcm) / base_rms - 1) * 100 if base_rms else 0,
                 len(eng.buf.words), [(m[0], m[2]) for m in eng.buf.marks2]))

    # Does the per-voice registry Pitch/Dynamic/Aspiration/FormantNo actually
    # change the voice? Compare two stock speakers of the same language.
    print()
    print("--- stock speakers of one language (registry Pitch/Dynamic/Aspiration/FormantNo) ---")
    for name in ("American English Male", "American English Female", "American English Child",
                 "American English Giant", "American English Zombie"):
        m = next((x for x in modes if (x.szModeName or "").strip() == name), None)
        if not m:
            continue
        eng.select(str(m.gModeID))
        eng.buf = TracingBuf(eng.audio)
        eng.bufPtr = eng.buf.QueryInterface(H.ITTSBufNotifySink)
        pcm, wfx = clean_speak("Hello, this is a test of the voice.")
        H.write_wav(os.path.join(scratch, "speaker_%s.wav" % name.replace(" ", "_")), pcm, wfx)
        print("%-26s %7d B  %5.2fs  rms %6.0f  defaultPitch=%s"
              % (name, len(pcm), len(pcm) / float(wfx.nAvgBytesPerSec), rms(pcm),
                 eng.pitch_def))

    print()
    print("scratch: %s" % scratch)
    return 0


if __name__ == "__main__":
    sys.exit(main())
