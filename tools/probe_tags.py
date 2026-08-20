#!/usr/bin/env python3
"""Probe which Infovox 230 control tags and progress callbacks actually work.

Runs under the bundled 32-bit Python against the real engine and reports, for
each experiment, how much audio came back and which sink callbacks fired. The
C++ engine driver is built on whatever this proves; nothing is assumed.

    bin\\infovox230\\python32\\python.exe tools\\probe_tags.py
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADDON = os.path.join(ROOT, "bin", "infovox230")
sys.path.insert(0, os.path.join(ADDON, "host"))

import infovox_host as H  # noqa: E402


class TracingBuf(H.BufNotifySink):
    """The stock sink plus a record of every callback, so the probe can say
    whether the engine reports word positions and bookmarks at all."""

    def __init__(self, audio=None):
        super().__init__(audio)
        self.words = []
        self.marks = []
        self.started = 0
        self.dones = 0

    def reset(self):
        super().reset()
        self.words = []
        self.marks = []
        self.started = 0
        self.dones = 0

    def ITTSBufNotifySink_TextDataStarted(self, this, qTimeStamp):
        self.started += 1
        return super().ITTSBufNotifySink_TextDataStarted(this, qTimeStamp)

    def ITTSBufNotifySink_TextDataDone(self, this, qTimeStamp, dwFlags):
        self.dones += 1
        return super().ITTSBufNotifySink_TextDataDone(this, qTimeStamp, dwFlags)

    def ITTSBufNotifySink_BookMark(self, this, qTimeStamp, dwMarkNum):
        off = self.audio.written if self.audio else 0
        self.marks.append((off, int(dwMarkNum)))
        return 0

    def ITTSBufNotifySink_WordPosition(self, this, qTimeStamp, dwByteOffset):
        off = self.audio.written if self.audio else 0
        self.words.append((off, int(dwByteOffset)))
        return 0


class TracingNotify(H.TTSNotifySink):
    def __init__(self):
        super().__init__()
        self.visuals = []

    def ITTSNotifySinkW_Visual(self, this, qTimeStamp, cIPAPhoneme, cEnginePhoneme,
                               dwHints, pTTSMouth):
        self.visuals.append((int(qTimeStamp), cIPAPhoneme, cEnginePhoneme, int(dwHints)))
        return 0


def install_tracing(eng):
    """Swap the engine's sinks for the tracing ones. Mirrors Engine.select()."""
    from ctypes import cast, c_void_p, byref
    from comtypes import COMError
    eng.buf = TracingBuf(eng.audio)
    eng.bufPtr = eng.buf.QueryInterface(H.ITTSBufNotifySink)
    eng.sink = TracingNotify()
    eng.sinkPtr = eng.sink.QueryInterface(H.ITTSNotifySinkW)
    try:
        eng.central.Register(cast(eng.sinkPtr, c_void_p),
                             H.ITTSNotifySinkW._iid_, byref(eng.sinkKey))
    except COMError as e:
        print("  (Register failed: %s)" % e)


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
    if not modes:
        print("FAIL: no voices enumerated")
        return 2

    target = None
    for m in modes:
        if (m.szModeName or "").strip() == "American English Male":
            target = m
            break
    target = target or modes[0]
    eng.select(str(target.gModeID))
    install_tracing(eng)

    print("engine ranges: rate[%s..%s def %s] pitch[%s..%s def %s] vol[%s..%s]"
          % (eng.rate_min, eng.rate_max, eng.rate_def,
             eng.pitch_min, eng.pitch_max, eng.pitch_def, eng.vol_min, eng.vol_max))
    print("voice: %s  features=0x%x" % (target.szModeName, eng.features))
    print()

    SENT = "The quick brown fox jumps over the lazy dog."

    experiments = [
        ("plain",                 SENT),
        ("Spd=80",                "\\Spd=80\\" + SENT),
        ("Spd=150 (default)",     "\\Spd=150\\" + SENT),
        ("Spd=400",               "\\Spd=400\\" + SENT),
        ("Pit=60",                "\\Pit=60\\" + SENT),
        ("Pit=200",               "\\Pit=200\\" + SENT),
        ("Vol=25% both chans",    "\\Vol=%d\\%s" % ((0x4000 | (0x4000 << 16)), SENT)),
        ("Pau=2000 in middle",    "The quick brown fox\\Pau=2000\\jumps over the lazy dog."),
        ("mrk=7 then mrk=9",      "\\mrk=7\\The quick brown\\mrk=9\\fox jumps."),
        ("Emp before word",       "The quick \\Emp\\brown fox jumps."),
        ("Rst reset",             "\\Pit=200\\High. \\Rst\\Back to normal."),
        ("Chr=LetterMode",        "\\Chr=\"LetterMode\"\\abc\\Chr=\"Normal\"\\ done."),
        ("Prn pronounce",         "\\Prn=h eh l ow\\hello."),
        ("unknown tag Xyz",       "\\Xyz=1\\" + SENT),
    ]

    baseline = None
    for label, text in experiments:
        eng.buf.reset()
        eng.sink.visuals = []
        try:
            pcm, wfx, _marks = eng.speak(text, pump, timeout=30)
        except Exception as e:
            print("%-22s ERROR %s" % (label, e))
            continue
        rate = wfx.nAvgBytesPerSec if wfx else 32000
        secs = len(pcm) / float(rate)
        if baseline is None:
            baseline = secs
        delta = "" if baseline is None else "  (%+.0f%% vs plain)" % ((secs / baseline - 1) * 100)
        print("%-22s %7d bytes  %5.2fs%s  marks=%s words=%d visuals=%d done=%d"
              % (label, len(pcm), secs, delta, eng.buf.marks, len(eng.buf.words),
                 len(eng.sink.visuals), eng.buf.dones))
        if eng.buf.words[:6]:
            print("%-22s   word offsets: %s" % ("", eng.buf.words[:6]))

    # Attribute-based control, the path that does not depend on tags at all.
    print()
    print("--- ITTSAttributes (no tags) ---")
    for wpm in (eng.rate_min, 150, 300, eng.rate_max):
        eng.attrs.SpeedSet(int(wpm))
        eng.buf.reset()
        pcm, wfx, _ = eng.speak(SENT, pump, timeout=30)
        rate = wfx.nAvgBytesPerSec if wfx else 32000
        print("SpeedSet(%-4s)          %7d bytes  %5.2fs" % (wpm, len(pcm), len(pcm) / float(rate)))
    eng.attrs.SpeedSet(150)

    for hz in (eng.pitch_min, 100, eng.pitch_max):
        eng.attrs.PitchSet(int(hz))
        eng.buf.reset()
        pcm, wfx, _ = eng.speak(SENT, pump, timeout=30)
        H.write_wav(os.path.join(scratch, "pitch_%s.wav" % hz), pcm, wfx)
        print("PitchSet(%-4s)          %7d bytes  -> pitch_%s.wav" % (hz, len(pcm), hz))

    # Phonetic input: does the engine accept IPA / its own phoneme alphabet?
    print()
    print("--- phoneme charsets ---")
    from ctypes import cast, c_void_p
    for name, charset, text in (
        ("CHARSET_TEXT", H.VOICECHARSET.CHARSET_TEXT, "hello"),
        ("CHARSET_IPAPHONETIC", H.VOICECHARSET.CHARSET_IPAPHONETIC, "h\u0259\u02c8lo\u028a"),
        ("CHARSET_ENGINEPHONETIC", H.VOICECHARSET.CHARSET_ENGINEPHONETIC, "h eh l ow"),
    ):
        eng.audio.pcm = bytearray()
        eng.audio.written = 0
        eng.buf.reset()
        try:
            eng.central.TextData(charset, 0, H.TextSDATA(text),
                                 cast(eng.bufPtr, c_void_p), H.ITTSBufNotifySink._iid_)
            deadline = time.time() + 10
            while not eng.buf.done.is_set() and time.time() < deadline:
                pump()
                time.sleep(0.002)
            print("%-24s %7d bytes  done=%s" % (name, len(eng.audio.pcm), eng.buf.done.is_set()))
        except Exception as e:
            print("%-24s ERROR %s" % (name, e))

    print()
    print("scratch dir: %s" % scratch)
    return 0


if __name__ == "__main__":
    sys.exit(main())
