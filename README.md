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

**Voices you make yourself.** The five speakers of each language differ only in
four numbers the engine reads, so a user can define their own — up to 256 of
them alongside the sixty. `Infovox230Config.exe` is the utility for it: every
per-voice setting the engine has, every engine-wide setting, and a Preview that
speaks a voice before it is kept. It is built for people who cannot see the
screen; [Making your own voices](#making-your-own-voices) below says what that
means in practice, and how it was checked.

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
| [`src/ivx_settings.cpp`](src/ivx_settings.cpp) | the engine-wide `[Settings]` a user can change |
| [`src/ivx_config_app.cpp`](src/ivx_config_app.cpp) | `Infovox230Config`, the configuration utility |
| [`src/ivx_config_model.cpp`](src/ivx_config_model.cpp) | reading and writing `voices.ini` without losing what is in it |
| [`src/ivx_config.rc`](src/ivx_config.rc) | its dialogs, where the tab order and the labelling live |
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

## Making your own voices

`Infovox230Config.exe`, installed alongside the voices and offered a desktop icon
by the installer. It writes an ordinary `voices.ini`
([`docs/voices.example.ini`](docs/voices.example.ini) is the reference), through
the Windows profile API rather than by rewriting the file, so a file someone has
commented by hand keeps its comments and anything the utility does not know
about. Hand editing and the utility can be mixed freely.

**Per voice** — everything the catalogue reads, which is everything the engine
is told:

| Setting | What it does |
| --- | --- |
| `Pitch` | base pitch; the engine computes `3 × Pitch − 49` hertz and clamps to 30–250, so 27–99 is the useful range. The editor shows the hertz beside it as you type |
| `Dynamic` | loudness contour, 0–100: higher is more forceful and more strongly stressed |
| `Aspiration` | breathiness, 0–100 |
| `FormantNo` | which of five vocal tract shapes; changes the character of a voice more than anything else here |
| `BasedOn` | copy an existing voice's settings first, then apply the rest |
| `LanguageFile`, `LanguageID`, `LCID` | which of the twelve languages, and what the voice reports itself as |
| `Gender`, `Age` | what programs report; no effect on the sound |
| `SpeakerName`, `SpeakerStyle` | the names the engine is given |
| `LibraryFile`, `PhSymFile`, `DiphoneFile`, `MappingFile` | data files the engine will look for; unset in every built-in voice |

**Engine-wide**, in a `[Settings]` section of the same file
([`src/ivx_settings.cpp`](src/ivx_settings.cpp)) — the section the catalogue had
always reserved and nothing had ever read:

| Setting | Default | What it does |
| --- | --- | --- |
| `TrimTrailingSilence`, `SilenceThreshold` | `1`, `16` | whether the ~0.8 s of inaudible padding at the end of an utterance is dropped, and what counts as inaudible |
| `WordEvents`, `SentenceEvents` | `1`, `1` | whether positions in the text are reported while speaking |
| `TimeoutBaseMs`, `TimeoutPerCharMs` | `30000`, `200` | how long a wedged engine is waited for |
| `RateMin/Max/Default`, `PitchMin/Max/Default` | `0` | override the range the engine reports for itself, which is what the ends of a program's rate and pitch controls reach. `0` means ask the engine |
| `PreviewText` | — | what Preview speaks |

Log level keeps its own file, `loglevel.txt`, which the utility writes from the
same page.

**Preview** speaks the settings as they stand, before anything has been kept: the
utility writes them to a temporary section, restarts the worker so it re-reads
the file, speaks through the pipe exactly as the SAPI5 engine does, and removes
the section again. That path found a real bug — the worker's accept loop sat in a
blocking `ConnectNamedPipe` and never looked at its quit event, so after a
shutdown request it lingered and the *next* client was served by a worker on its
way out, with a stale voice list. It now pokes its own pipe to let the loop look.

### Built to be used without seeing it

Most people configuring a speech engine are driving Windows from the keyboard
with a screen reader, so:

- the dialogs are resource templates of standard Win32 controls — nothing is
  owner-drawn, and nothing is conveyed by colour, position or shape alone;
- every control carries `WS_TABSTOP`, the up-down spinners included, and each of
  those is given a name of its own in code so it is never announced as an
  anonymous spin button;
- every control is immediately preceded in the template by the static text that
  names it, which is how MSAA derives an accessible name, so nothing is
  announced as just "edit";
- everything the utility has to say goes in a read-only edit box that takes
  focus and can be reviewed line by line — the details of the selected voice,
  the status of the last action, the file being edited, the notes on each page —
  rather than in a label a screen reader would skip;
- when a preview starts, focus moves to the "Stop speaking" button.

Checked rather than claimed: the running dialogs were walked through MSAA — the
same `IAccessible` interface NVDA and JAWS read — and through
`GetNextDlgTabItem`, which is the tab route itself. Across all three dialogs:
**no control without an accessible name, and no interactive control off the tab
route.** The one control not on the route is "Stop speaking", which Windows
skips because it is disabled until there is something to stop.

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

## Releases

Installers are on the [releases page](https://github.com/joshknnd1982/infovox230sapi5/releases).

**1.0.1** — the configuration utility.

- `Infovox230Config.exe`: define your own voices and change every setting the
  engine has, without editing a file by hand. Start menu entry, and a desktop
  icon offered by the installer.
- Engine-wide settings are now real settings rather than constants: trimming and
  its threshold, word and sentence position reporting, utterance timeouts, and
  the rate and pitch ranges. See [Making your own voices](#making-your-own-voices).
- Fixed: the worker could linger after a shutdown request until the next client
  connected, and that client was then served by the worker that was leaving.

**1.0.0** — first release: sixty voices in twelve languages, 32- and 64-bit.

## Licence

GPL; see [LICENSE](LICENSE). The Infovox 230 engine itself is not part of this
project — it was made by Telia Promotor / Babel-Infovox AB, both long defunct.
