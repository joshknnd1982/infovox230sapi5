# License and third-party components

This project is published under the GNU General Public License v3 (see `LICENSE`), but
not every file in the repository is covered by it. What follows is who owns what, so
that anyone redistributing this knows what they are handling.

## Covered by the GPL

The SAPI5 engine and its supporting code and tooling:

- `src/` — the SAPI5 engine, text pipeline, transliteration, voice registration, the
  helper client, and the 32-bit worker
- `tools/` — the verification suites and diagnostic programs
- `installer/`, `CMakeLists.txt`, `build_all.bat`, `README.md`

This repository is a fork of [gozaltech/BstSpeech-sapi](https://github.com/gozaltech/bstspeech-sapi),
which carried no license file. Parts of `src/` originate from that project — `com.hpp`,
`registry.*`, `ISpDataKeyImpl.*`, `utils.hpp` and the original wrapper scaffolding. The
GPL applies to the work done in this fork; the upstream author's rights in the code they
wrote are unaffected, and anyone wanting a definitive license for those files should ask
them.

## Public domain

- `bin/b32_wrapper.dll`
- `bin/b32_helper.exe`

Built from [samtupy/b32tts_wrapper](https://github.com/samtupy/b32tts_wrapper), released
into the public domain under the Unlicense. They may be copied, modified and redistributed
freely. This project loads `b32_wrapper.dll` for in-process synthesis and runs
`b32_helper.exe` as a separate process where that is more reliable.

## Not ours to license

- `bin/b32_tts.dll` — the 1994 BestSpeech / Keynote Gold engine
- `bin/dll_*.dll` — the 2006 Lingvosoft-era language engines

**These are proprietary third-party binaries.** They are not covered by the GPL, are not
this project's to relicense, and are included only because the software is useless without
them. They have circulated in the blind community for years — the 2006 language engines
were preserved and shared by Rommix — but no one here holds their copyright and no license
grant is claimed or implied over them.

If you are the rights holder for these engines and want them removed, open an issue and
they will be taken out of the repository.

## The character voices

The fourteen voice presets (Fred, Sara, Hary, Wendy, Dexter, Alien, Kit, Bruno, Ghost,
Peeper, Dracula, Granny, Martha, Tim) are parameter sets created by Rommix, and are
reproduced here with the same provenance as the engines above.

## No warranty

As stated in the GPL: this software comes with no warranty. The speech engines are thirty
and twenty years old respectively and have documented defects, several of which are
worked around rather than fixed. See the "Engine quirks this works around" section of the
README.
