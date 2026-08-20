# Infovox 230 SAPI5

A native Windows SAPI5 wrapper for the Infovox 230 text-to-speech engine, in
32-bit and 64-bit form, so its sixty voices work in any program that speaks
through Windows — NVDA, JAWS, Narrator, Balabolka, Word, and the rest.

Twelve languages — American and British English, Danish, Dutch, Finnish, French,
German, Icelandic, Italian, Norwegian, Castilian Spanish, Swedish — each with a
Male, Female, Child, Giant and Zombie speaker.

## What makes this different from the usual Infovox setup

**No SAPI 4.** The engine is a SAPI 4 in-process server from the mid-nineties.
Rather than install the SAPI 4 runtime and register the engine, the worker loads
`Ivx230nt.dll` by path and asks its own `DllGetClassObject` for a mode
enumerator. Nothing from SAPI 4 is installed, present or required.

**No registry, at all.** The engine reads every one of its settings — the rule
file directories and the whole voice table — from
`HKCU\Software\Babel-Infovox AB\Infovox 230`. It reaches the registry through
exactly ten imported ANSI functions, so those ten slots in its import address
table are rewritten to point at an in-memory tree
([`src/ivx_vregistry.cpp`](src/ivx_vregistry.cpp)). The engine then reads its
configuration out of RAM: on a normal start it makes about a thousand
configuration reads, none of which reach Windows.

Verify it on your own machine:

```bash
Infovox230Diag registry
```

**Out of process.** Both SAPI5 engines, 32-bit and 64-bit, are thin clients of
one 32-bit worker that owns the engine. That is what lets 64-bit hosts use a
32-bit engine, and it means a thirty-year-old engine that faults or wedges
cannot take a screen reader down with it.

## Layout

| Path | What it is |
| --- | --- |
| [`src/ivx_vregistry.cpp`](src/ivx_vregistry.cpp) | the in-memory registry and the import-table patch |
| [`src/ivx_engine.cpp`](src/ivx_engine.cpp) | loads and drives the engine; the capturing audio sink |
| [`src/ivx_catalog.cpp`](src/ivx_catalog.cpp) | the voice table, built-in and user-defined |
| [`src/ivx_server.cpp`](src/ivx_server.cpp) | the 32-bit worker |
| [`src/ivx_client.cpp`](src/ivx_client.cpp) | the client half of the pipe protocol |
| [`src/ivx_sapi_engine.cpp`](src/ivx_sapi_engine.cpp) | `ISpTTSEngine`: fragments in, audio and events out |
| [`src/ivx_sapi_tokens.cpp`](src/ivx_sapi_tokens.cpp) | publishing the voices to SAPI5 |
| [`tools/ivx_probe.cpp`](tools/ivx_probe.cpp) | `Infovox230Diag`, which drives the engine both ways |
| [`tools/ivx_sapi_test.cpp`](tools/ivx_sapi_test.cpp) | `Infovox230SapiTest`, which drives it through SAPI5 |
| [`installer/Infovox230SAPI.iss`](installer/Infovox230SAPI.iss) | the installer |
| [`docs/README.txt`](docs/README.txt) | the documentation that ships with the product |
| [`bin/infovox230/`](bin/infovox230) | the engine payload, from the NVDA add-on |

## What the engine can and cannot do

Measured against the real engine rather than assumed — see
[`tools/probe_tags.py`](tools/probe_tags.py) and its siblings, which are what
these conclusions come from.

Works: `\Spd=` rate, `\Pit=` pitch, `\Vol=` volume (0 gives digital silence),
`\mrk=` bookmarks, word-position reporting, and phoneme input in both IPA and
the engine's own alphabet.

Does not work, and is handled here instead: `\Pau=` produces no pause, so
silence is generated as exact PCM; `\Chr=` letter mode does nothing, so
spelling separates the characters; the engine reports no viseme information at
all, so none is offered.

Five traps worth knowing, each of which cost a measurement to find:

- **The engine paces itself against the sound card.** It delivers one second of
  audio and then waits about 200 ms for its own timer before delivering the
  next — roughly four times real time, while burning 16 ms of CPU for 28
  seconds of speech. A real audio driver raises
  `IAudioDestNotifySink::FreeSpace` as its buffers complete; doing the same from
  the capture sink takes synthesis to about **50× real time**. This is the
  difference between a screen reader that keeps up and one that does not.
- **`ISpTTSEngineSite::Write` blocks** while the host's buffer is full, and the
  host plays in real time. Handing it a second of audio in one call blocks for
  most of a second, and an abort raised during that call is not noticed until it
  returns — a third of a second of lag on every keypress. Audio goes over in
  ~30 ms pieces with a look at `GetActions` between each.

- A control tag changes engine state **permanently**, across utterances. Every
  utterance therefore states its rate, pitch and volume in full rather than
  relying on what the last one left behind.
- Loudness is handed to the audio *device* (`IAudio::LevelSet`), not applied to
  the samples. A capture sink that only stores the level makes every volume
  control a silent no-op, so [the sink scales the PCM itself](src/ivx_engine.cpp).
- Every utterance ends with ~0.8 s of inaudible padding (samples of ±1). Passed
  through, that is most of a second of dead air after everything a screen reader
  says, so the SAPI layer holds back trailing quiet and drops it — while keeping
  pauses that turn out to have speech after them.

Word positions are reported relative to everything the engine was handed, and
its timestamps are byte offsets *within the current second of output* rather
than absolute — reconstructed in
[`ivx_engine.cpp`](src/ivx_engine.cpp), and checked against evenly spaced
bookmarks.

## Building

```bash
build_all.bat
```

Needs Visual Studio 2022 (or the Build Tools), CMake 3.20+, and Inno Setup 6
for the installer (`winget install JRSoftware.InnoSetup`). Output lands in
`output\`, staged in the layout the installer expects, with
`Infovox230SAPI_Setup.exe` beside it.

## Where this came from

This project is a descendant of the **BestSpeech SAPI5 wrapper**, and it exists
because that wrapper had already solved the same shape of problem: getting a
32-bit, pre-SAPI5 speech engine to appear as an ordinary Windows voice, in both
32- and 64-bit form.

- Original: **[gozaltech/bstspeech-sapi](https://github.com/gozaltech/bstspeech-sapi)**
- The fork this grew out of: **[joshknnd1982/BstSpeech-sapi](https://github.com/joshknnd1982/BstSpeech-sapi)**

The early commits in this repository's history are BestSpeech's, so `git log`
shows the whole line of descent. What was carried over is the architecture
rather than the code: a self-registering SAPI5 COM server, a 32-bit worker
process so 64-bit hosts can reach a 32-bit engine, one log file shared by every
component, and an Inno Setup installer that registers both bitnesses with a
regsvr32 of each. The BestSpeech sources themselves are not here — none of them
are used by this engine, and two similar-looking wrappers side by side would be
more confusing than helpful. Go to either repository above if you want them.

Everything specific to Infovox 230 is new: the in-memory registry and the import
table patching, the direct load of the SAPI4 engine, the voice catalogue, the
wire protocol, and the SAPI5 engine itself.

## Licence

GPL; see [LICENSE](LICENSE). The Infovox 230 engine itself is not part of this
project — it was made by Telia Promotor / Babel-Infovox AB, both long defunct.
