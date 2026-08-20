#!/usr/bin/env python3
"""Third-stage probe: pin down how the engine reports WHERE in the audio a word
or a bookmark falls, which is what word highlighting in a reader depends on.

Three candidate signals are recorded side by side for the same utterance:
  * ITTSBufNotifySink::WordPosition  -> (qTimeStamp, text offset)
  * ITTSBufNotifySink::BookMark      -> (qTimeStamp, our \\mrk=N\\ number)
  * IAudioDest::BookMark             -> engine-internal id, at an exact byte
                                        position in the sample stream
plus the exact byte count written to the sink at the moment of each callback,
and the sizes of the DataSet chunks the engine delivers.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ADDON = os.path.join(ROOT, "bin", "infovox230")
sys.path.insert(0, os.path.join(ADDON, "host"))

import infovox_host as H  # noqa: E402


class TracingAudio(H.CaptureAudio):
    def __init__(self):
        super().__init__()
        self.chunks = []
        self.audio_marks = []

    def IAudioDest_DataSet(self, pBuffer, dwSize):
        self.chunks.append((self.written, int(dwSize)))
        return super().IAudioDest_DataSet(pBuffer, dwSize)

    def IAudioDest_BookMark(self, dwMarkID):
        self.audio_marks.append((self.written, int(dwMarkID)))
        return super().IAudioDest_BookMark(dwMarkID)


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


def main():
    from comtypes import CoInitialize, GUID
    from ctypes import POINTER, byref, cast, c_void_p

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

    # Rebuild the selection by hand so the tracing audio sink is the one the
    # engine talks to.
    eng.release_current()
    eng.audio = TracingAudio()
    eng.buf = TracingBuf(eng.audio)
    eng.bufPtr = eng.buf.QueryInterface(H.ITTSBufNotifySink)
    eng.central = POINTER(H.ITTSCentralW)()
    eng.enum.Select(GUID(str(target.gModeID)), byref(eng.central), eng.audio)
    eng.sink = H.TTSNotifySink()
    eng.sinkPtr = eng.sink.QueryInterface(H.ITTSNotifySinkW)
    try:
        eng.central.Register(cast(eng.sinkPtr, c_void_p), H.ITTSNotifySinkW._iid_,
                             byref(eng.sinkKey))
    except Exception as e:
        print("Register failed: %s" % e)
    eng.attrs = eng.central.QueryInterface(H.ITTSAttributesW)
    eng.features = target.dwFeatures
    eng._query_ranges()
    eng.attrs.SpeedSet(150)
    eng.attrs.PitchSet(101)

    words = ["alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf",
             "hotel", "india", "juliet", "kilo", "lima", "mike", "november"]
    # A bookmark before every word, numbered the same as the word index, so the
    # expected ordering is unambiguous.
    text = "".join("\\mrk=%d\\%s " % (i, w) for i, w in enumerate(words))

    eng.audio.chunks = []
    eng.audio.audio_marks = []
    eng.buf.reset()
    pcm, wfx, _ = eng.speak(text, pump, timeout=60)
    bps = wfx.nAvgBytesPerSec
    total = len(pcm)
    print("utterance: %d words, %d bytes, %.2fs, %d bytes/sec"
          % (len(words), total, total / float(bps), bps))
    print("submitted text: %r" % text)
    print()

    print("DataSet chunks (offset, size), first 12: %s" % (eng.audio.chunks[:12],))
    print("distinct chunk sizes: %s" % sorted({c[1] for c in eng.audio.chunks}))
    print()

    print("IAudioDest::BookMark  (exact audio byte offset, engine-internal id):")
    for m in eng.audio.audio_marks:
        print("   byte %7d  id %d" % m)
    print()

    print("ITTSBufNotifySink::BookMark  (qTimeStamp, bytes written at callback, our N):")
    for qts, written, num in eng.buf.marks2:
        window = bps
        base = (written // window) * window
        print("   qts %7d  written %7d  N=%-3d   base+qts=%7d   ideal=%7d"
              % (qts, written, num, base + qts, int(total * num / float(len(words)))))
    print()

    print("ITTSBufNotifySink::WordPosition  (qTimeStamp, bytes written, text offset):")
    for qts, written, off in eng.buf.words:
        window = bps
        base = (written // window) * window
        print("   qts %7d  written %7d  textoff %-4d  base+qts=%7d  text=%r"
              % (qts, written, off, base + qts, text[off - 1:off + 8]))

    H.write_wav(os.path.join(scratch, "timing.wav"), pcm, wfx)
    print()
    print("wrote %s" % os.path.join(scratch, "timing.wav"))

    # Does volume 0 give true digital silence through the engine's own tag?
    print()
    print("--- \\Vol=0\\ ---")
    eng.buf.reset()
    pcm0, _, _ = eng.speak("\\Vol=0\\Silence test.", pump, timeout=30)
    peak = max((abs(int.from_bytes(pcm0[i:i + 2], "little", signed=True))
                for i in range(0, len(pcm0) & ~1, 2)), default=0)
    print("bytes=%d peak sample=%d" % (len(pcm0), peak))
    eng.attrs.VolumeSet(0xFFFFFFFF)
    return 0


if __name__ == "__main__":
    sys.exit(main())
