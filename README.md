# BestSpeech SAPI5 Voices

A native Windows SAPI5 speech engine for the BestSpeech synthesizers, exposing every
language and every character voice to any SAPI5 application — Windows Narrator, NVDA,
JAWS, Balabolka, Bookworm, and anything else that speaks through SAPI.

> **Note:** early-stage software under active development. Please report problems you run into.

## What you get

**143 voices across 13 engines.** The classic 1994 English engine plus twelve 2006
language engines:

| Language | Voices | Language | Voices |
|---|---|---|---|
| English (Classic 1994) | 14 | Italian | 14 |
| English | 14 | Japanese | 1 |
| Dutch | 14 | Polish | 1 |
| French | 14 | Portuguese | 14 |
| German | 14 | Russian | 14 |
| Greek | 1 | Spanish | 14 |
| Hebrew | 14 | | |

The fourteen character voices are Fred, Sara, Hary, Wendy, Dexter, Alien, Kit, Bruno,
Ghost, Peeper, Dracula, Granny, Martha and Tim. Greek, Japanese and Polish publish a
single voice each because their text frontends ignore every voice command — publishing
fourteen identical entries would only be misleading.

**Both architectures.** A 32-bit and a 64-bit COM server are installed and registered
into their own registry views, so 32-bit hosts and 64-bit hosts (Windows 11 Narrator
among them) both see the full voice list. Every engine DLL is 32-bit, so a 64-bit host
reaches them through a dedicated `b32_helper.exe` per engine -- one engine per process,
synthesized on the thread that opened it, audio returned over a pipe. A 32-bit host runs
the engine in process and falls back to the same helper if that fails. Both paths produce
byte-identical audio.

**Full SAPI parameter support.** Rate, pitch and volume, the per-fragment `RateAdj`,
`PitchAdj` and `Volume` adjustments, rate and volume changed mid-utterance, word and
sentence boundary events, bookmarks, spell-out and silence fragments, and correct
per-engine output sample rates.

## Installing

Run `BestSpeechSAPI_Setup.exe` and accept the elevation prompt — registering voices
writes to `HKEY_LOCAL_MACHINE`. The voices appear immediately in any SAPI5 application;
restart the application if it caches its voice list.

## Engine quirks this works around

These engines are old and have sharp edges. Each of the following was established by
measurement — synthesizing through the real DLLs and comparing the audio — not by
guesswork, and each is re-checked by `tools/verify_engines.py`.

- **Greek and Russian drop the Latin alphabet entirely.** Every ASCII letter is
  discarded silently, so "Open Firefox settings" reaching either engine is pure silence.
  Latin text is transliterated into Greek or Cyrillic before it is sent. A lone Latin
  letter becomes the English letter name, which is what a screen reader means when it
  echoes a character.
- **Greek, Japanese and Polish cannot read digits.** Greek and Japanese ignore ASCII
  digits outright; Polish only spells them one at a time. Numbers are converted to words
  in the target language first.
- **Braces silence every 2006 engine,** and can crash the Russian one outright once it
  has synthesized a few utterances. Russian additionally truncates on `)`, `]`, `*`, `^`
  and goes silent on a backtick. These are neutralized before they reach the engine.
- **Command order is load-bearing.** `~v` (headsize) resets the fundamental frequency,
  and `~f` (frequency) in turn resets the inflection, so a prefix that sets them in the
  wrong order silently discards the settings made before them. The order in
  `src/text_pipeline.cpp` was verified one command at a time.
- **A dot between digits is sentence punctuation** to these frontends, so "7.1" reads as
  "seven. one". Decimals, version strings and IP addresses are spelled out first.
- **The 2006 engines are far quieter than the classic one** — Russian peaks below a
  fifth of full scale — so each carries a fixed gain trim, set just short of where the
  loudest voices hit the engine's internal limiter.
- **`~v` headsize is ignored by every 2006 engine,** and Dutch additionally ignores `~e`
  excitation, which is why its Bruno and Ghost voices are indistinguishable.
- **German has no inflection command at all.** Rather than obeying `~h` it reads the
  command out as text, so a stray fragment was spoken ahead of every German utterance —
  and because it landed before the gain command had been applied, that fragment was
  quiet and the real speech then jumped in louder. German no longer receives `~h`, and
  the gain command now leads the prefix so nothing can be spoken at the wrong volume.
- **The in-process audio capture is unreliable, which is why Portuguese can go silent.**
  The shim captures what an engine plays by hooking `waveOut`, and that capture only
  happens when two conditions hold together: the hook state is set for the calling thread
  (it lives in a `thread_local`) and the engine echoes the shim's own state pointer back
  through `waveOutOpen`'s callback argument. Miss either and the hook passes the call
  through to the real winmm -- no audio reaches the caller and nothing reports an error.
  The engine loads fine, `Speak` returns success, and the voice is simply silent. When it
  happens the shim also never sees the wave format, so it reports a default 10000 Hz
  instead of the engine's real rate, which makes the failure easy to spot in the log.

  Observed on Portuguese inside NVDA and the Speech control panel, intermittently: the
  same binaries in the same directory succeed on one run and fail on the next. Every
  voice is therefore checked with a throwaway utterance when its engine loads, and an
  engine that produces nothing is moved out of process for the rest of the session --
  before the user's first real word rather than after it.

  Out of process it is reliable, because a dedicated `b32_helper.exe` runs one engine on
  the thread that opened it. That is the same mechanism the BestSpeech NVDA add-on uses,
  which is the configuration known to work on affected machines. A 64-bit host has no
  in-process option at all -- the engines are 32-bit -- so it always takes that route. An
  engine can also be pinned there up front on 32-bit, see below.

- **Arabic is deliberately excluded.** `dll_ara.dll`'s synthesis core is a stub that
  emits the same buffer of digital silence for any input, in either script.

## Talking to b32_helper.exe

The helper's wire format is small and unforgiving, and one detail is worth stating
plainly because getting it wrong is catastrophic rather than merely wrong:

| Command | Bytes |
|---|---|
| Speak | `uint32` text length, `float32` rate multiplier, then the text |
| Cancel | `uint32` zero — **and nothing else** |
| Quit | `uint32` 0xFFFFFFFF |

A cancel is *not* a zero-length speak. Sending the rate multiplier after the zero leaves
four bytes in the pipe that the helper reads as the next command's length: `0x3F800000`,
about a gigabyte, which it then tries to read as utterance text. The helper balloons to a
gigabyte of memory and stops answering, and because the caller is blocked reading the
pipe, the application doing the talking freezes with it. That is what made 64-bit hosts
die a few seconds in, once the first utterance got cancelled.

Every read in `src/helper_client.cpp` is therefore bounded, with the clock restarting on
each byte received, so a wedged helper can slow a voice down but can never hang the host.
`cancel_probe` exercises this directly.

## Diagnostic log

A log is written to `%LOCALAPPDATA%\BestSpeech\bestspeech.log` while the engine is
still settling. It records the engine and voice chosen, the resolved rate, pitch and
volume, and — most usefully — the exact byte string handed to the engine, which is where
a "what is it saying?" problem becomes obvious. Every line is tagged with the process,
its bitness and its pid, since a 64-bit host's utterance crosses two processes.

The file is capped at 4 MB with one previous copy kept, so it cannot fill a disk.

To turn it off, set this registry value and restart the speaking application:

```
HKEY_CURRENT_USER\Software\BestSpeech   DWORD   Logging = 0
```

Set it to `1`, or delete it, to turn logging back on.

The log is opened for shared append. An earlier build opened it exclusively, which meant
the first process to log locked out every other one -- and since a screen reader and the
worker are both long-lived, that silently hid exactly the processes worth looking at.

## Forcing an engine out of process

If a language is silent in one application but fine elsewhere, it can be pinned to a
dedicated `b32_helper.exe`, which runs that engine in isolation:

```
HKEY_CURRENT_USER\Software\BestSpeech   REG_SZ   WorkerEngines = por
```

Comma-separate several ids, or use `*` for all of them. The ids are the short names in
`src/engines.hpp`: `classic eng dut fre ger gre heb ita jpn pol por rus spa`. This is
only a shortcut -- an engine that fails its load-time check is moved out of process
automatically. Both paths are covered by the verification suite.

## Checking an installation

`BestSpeechDiagnostics.exe`, installed alongside the engine and on the Start menu as
"Check BestSpeech voices", walks every voice through SAPI as an application would and
writes a report to `%LOCALAPPDATA%\BestSpeech\diagnostics.txt`. It changes nothing --
no registration, no settings, no default voice. A 64-bit copy sits in the `x64` folder.

## Building from source

```batch
build_all.bat
```

Builds both architectures, stages `output\`, and compiles the installer.

**Requirements:** Windows 10 or later, Visual Studio 2022 Build Tools with the C++
workload, CMake 3.15+, and Inno Setup 6 for the installer step (the build skips it and
leaves a usable `output\` if Inno Setup is absent).

## Verifying

Two suites, both of which drive the real engines and fail loudly:

```batch
python tools\verify_engines.py --probe build_x86\bin\Release\pipeline_probe.exe
```

Drives every engine DLL through `b32_helper.exe` and checks that each speaks its own
language, that every voice is audible and distinct, that Latin text survives on the
engines that drop it, and that no ASCII symbol silences, truncates or crashes synthesis.

```batch
python tools\verify_sapi.py --probe sapi_probe32.exe --dll output\BestspeechSAPI.dll
```

Creates the real COM object through `DllGetClassObject`, hands it each of the 143 voice
tokens in turn, and captures what it writes — the same path a screen reader takes, with
nothing registered and no elevation needed. Point it at `output\x64\BestspeechSAPI.dll`
with `sapi_probe64.exe` to exercise the 64-bit bridge.

```batch
cancel_probe.exe outputd\BestspeechSAPI.dll por 60
```

Speaks sixty utterances, abandoning most of them part way through the way a screen reader
does when the user keeps moving. It fails if any utterance after a cancellation comes back
empty, or if the run stops making progress.

`tools/sapi_probe.cpp` also works standalone for listening to a single voice:

```batch
sapi_probe32.exe output\BestspeechSAPI.dll rus 11 out.wav "Hello world" --rate 5
```

## Layout

| Path | Purpose |
|---|---|
| `src/engines.hpp` | The engine and voice tables: languages, LCIDs, sample rates, capabilities |
| `src/text_pipeline.cpp` | Sanitizing, transliteration, number words, inline command prefixes |
| `src/ISpTTSEngineImpl.cpp` | The SAPI engine itself |
| `src/b32_wrapper.cpp` | Loader for `b32_wrapper.dll`, the shim that drives both engine families |
| `src/bestspeech_server.cpp` | 32-bit worker for 64-bit hosts |
| `src/sapi_main.cpp` | COM registration and voice token registration |
| `tools/translit_ref.py` | Transliteration and number tables, and the reference implementation |
| `tools/gen_translit.py` | Generates `src/translit_tables.inc` from those tables |

The transliteration tables live in one place and are generated into C++, so the engine
and the Python reference cannot drift apart.

## Contributing

Contributions are welcome. Please open an issue with a clear description, steps to
reproduce, your Windows version and architecture, and relevant logs. For pull requests,
branch from `master`, test on both x86 and x64, and keep to the existing style.

## Support the project

If you find this useful, consider supporting development:

[![Donate with PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://paypal.me/gozaltech)

## License

GNU General Public License v3 — see [`LICENSE`](LICENSE).

The GPL covers the SAPI5 engine and its tooling. It does **not** cover the speech engine
binaries in `bin/`: `b32_tts.dll` and the `dll_*.dll` language engines are proprietary
third-party software, included because the project cannot run without them, and are not
this project's to relicense. `b32_wrapper.dll` and `b32_helper.exe` come from
[samtupy/b32tts_wrapper](https://github.com/samtupy/b32tts_wrapper) and are public domain.

[`NOTICE.md`](NOTICE.md) sets out who owns what in full. Please read it before
redistributing.

## Credits

- The BestSpeech / Keynote Gold engines, preserved and shared with the blind community by
  **Rommix**, who also created the fourteen character voices
- [**samtupy**](https://github.com/samtupy/b32tts_wrapper) for the engine shim, released
  into the public domain — its source is what finally explained why some voices fell
  silent in a host application
- [**gozaltech**](https://github.com/gozaltech/bstspeech-sapi) for the original SAPI5
  wrapper this is forked from
