#!/usr/bin/env python3
"""Generate installer/voices.iss, the installer's languages and voices.

The installer offers one component per language and one per voice inside it, so
someone installing only Danish is not made to take 23 MB of rule files for
eleven languages they will never hear. Four things follow from a choice on that
page and all four are generated here, from the same mode table the engine itself
is built from, so they cannot drift apart:

  [Components]     the tree the components page shows
  [Files]          each language's rule files, tagged with its component
  [InstallDelete]  the same files, removed when the language is not chosen, so
                   re-running setup and clearing a language actually frees it
  [INI]            installed.ini, the record of what was chosen; the catalogue
                   (src/ivx_catalog.cpp) reads it back and publishes only these
                   voices to Windows speech

Source of truth is bin/infovox230/modes.json, as it is for the engine's own
table. Regenerate with:

    python tools/gen_installer_voices.py
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MODES = os.path.join(ROOT, "bin", "infovox230", "modes.json")
ENGINE = os.path.join(ROOT, "bin", "infovox230", "engine")
OUT = os.path.join(ROOT, "installer", "voices.iss")

# The order the five speakers are offered in. Not alphabetical: Male and Female
# are what nearly everyone wants, so they come first and the novelties last.
SPEAKERS = ("Male", "Female", "Child", "Giant", "Zombie")

# Setup types, in the order they appear in the drop-down list. A language or a
# voice names the types it belongs to; "custom" is deliberately named by none of
# them, because choosing Custom means "leave my ticks alone".
TYPE_FULL = "full"
TYPE_ENGLISH = "english"
TYPE_MINIMAL = "minimal"

# Which languages and voices the smaller types install, by component name.
ENGLISH_LANGUAGES = ("am", "bl")
MINIMAL_LANGUAGE = "am"
MINIMAL_SPEAKERS = ("Male", "Female")

FLAGS = "ignoreversion restartreplace uninsrestartdelete"


def language_name(mode_name):
    head, _, tail = mode_name.rpartition(" ")
    if tail not in SPEAKERS:
        raise ValueError("unexpected mode name %r" % mode_name)
    return head


# The component name for a language: its rule file without the "rules" ("am"
# from "amrules.ivx"). Short, stable, and unique across the twelve, because the
# engine distinguishes languages by exactly this file.
def component_name(language_file):
    stem = os.path.splitext(language_file)[0].lower()
    if not stem.endswith("rules"):
        raise ValueError("unexpected rule file %r" % language_file)
    return stem[: -len("rules")]


# The rule files a language needs, named as they actually are on disk. The mode
# table spells them in lower case; the files themselves are a mixture of cases,
# and an installer entry naming a file that is not there should fail the build
# rather than ship a language that cannot speak.
#
# PhSymFile is left out on purpose: Swedish names "swphsym", which is not a file
# at all but a symbol set inside the engine.
def rule_files(modes_for_language):
    wanted = []
    for mode in modes_for_language:
        for key in ("LanguageFile", "LibraryFile", "DiphoneFile", "MappingFile"):
            name = mode.get(key, "").strip()
            if name and name.lower() not in [w.lower() for w in wanted]:
                wanted.append(name)

    on_disk = dict((name.lower(), name) for name in os.listdir(ENGINE))
    resolved = []
    for name in wanted:
        if name.lower() not in on_disk:
            raise SystemExit("%s: no such file in %s" % (name, ENGINE))
        resolved.append(on_disk[name.lower()])
    return resolved


def quote(s):
    return '"%s"' % s.replace('"', '""')


def main():
    with open(MODES, "r", encoding="utf-8") as f:
        modes = json.load(f)

    # Languages in alphabetical order, voices in speaker order within each.
    languages = []
    seen = {}
    for mode in sorted(modes, key=lambda m: m["_name"]):
        language = language_name(mode["_name"])
        if language not in seen:
            seen[language] = {"name": language, "modes": []}
            languages.append(seen[language])
        seen[language]["modes"].append(mode)

    for language in languages:
        language["component"] = component_name(language["modes"][0]["LanguageFile"])
        language["files"] = rule_files(language["modes"])
        language["bytes"] = sum(
            os.path.getsize(os.path.join(ENGINE, f)) for f in language["files"]
        )
        by_speaker = dict(
            (m["_name"].rpartition(" ")[2], m) for m in language["modes"]
        )
        missing = [s for s in SPEAKERS if s not in by_speaker]
        if missing:
            raise SystemExit(
                "%s has no %s voice" % (language["name"], ", ".join(missing))
            )
        language["speakers"] = [(s, by_speaker[s]) for s in SPEAKERS]

    def types_for_language(language):
        types = [TYPE_FULL]
        if language["component"] in ENGLISH_LANGUAGES:
            types.append(TYPE_ENGLISH)
        if language["component"] == MINIMAL_LANGUAGE:
            types.append(TYPE_MINIMAL)
        return types

    def types_for_voice(language, speaker):
        types = types_for_language(language)
        if TYPE_MINIMAL in types and speaker not in MINIMAL_SPEAKERS:
            types.remove(TYPE_MINIMAL)
        return types

    out = []
    out.append("; Generated by tools/gen_installer_voices.py -- do not edit by hand.")
    out.append(";")
    out.append("; The languages and voices the installer offers, the rule files each")
    out.append("; language needs, and installed.ini -- the record of what was chosen,")
    out.append("; which the catalogue reads back so that Windows is told about those")
    out.append("; voices and no others. Included by Infovox230SAPI.iss.")
    out.append(";")
    out.append("; %d voices in %d languages." % (len(modes), len(languages)))
    out.append("")

    out.append("[Components]")
    out.append("; A language carries its own rule files, so the size shown against it on")
    out.append("; the components page is the real cost of adding it. The voices inside it")
    out.append("; cost nothing but a name in the Windows voice list: the five speakers of")
    out.append("; a language differ only in four numbers and all five read the same file.")
    out.append(";")
    out.append("; Each voice is named in full rather than just \"Male\": on that page a")
    out.append("; screen reader reads the line it is on and not the branch above it, and")
    out.append("; \"Male\" on its own does not say which language it belongs to.")
    out.append("")
    for language in languages:
        c = language["component"]
        out.append(
            "; %s -- %s, %.1f MB"
            % (
                language["name"],
                ", ".join(language["files"]),
                language["bytes"] / (1024.0 * 1024.0),
            )
        )
        out.append(
            "Name: %s; Description: %s; Types: %s"
            % (
                quote(c),
                quote(language["name"]),
                " ".join(types_for_language(language)),
            )
        )
        for speaker, mode in language["speakers"]:
            out.append(
                "Name: %s; Description: %s; Types: %s"
                % (
                    quote("%s\\%s" % (c, speaker.lower())),
                    quote(mode["_name"]),
                    " ".join(types_for_voice(language, speaker)),
                )
            )
        out.append("")

    out.append("[Files]")
    out.append("; Each language's rule files, installed only if that language was chosen.")
    for language in languages:
        for name in language["files"]:
            out.append(
                'Source: "{#StageDir}\\engine\\%s"; DestDir: "{app}\\engine"; '
                "Components: %s; Flags: %s" % (name, language["component"], FLAGS)
            )
    out.append("")

    out.append("[InstallDelete]")
    out.append("; Runs before anything is copied, so re-running setup with a language")
    out.append("; cleared genuinely gives the disk space back, instead of leaving a rule")
    out.append("; file behind for a voice that is no longer published.")
    for language in languages:
        for name in language["files"]:
            out.append(
                'Type: files; Name: "{app}\\engine\\%s"; Components: not %s'
                % (name, language["component"])
            )
    out.append("")

    out.append("[INI]")
    out.append("; What was chosen, written where the catalogue will read it. Only the")
    out.append("; entries whose component is selected are written, and installed.ini is")
    out.append("; deleted and recreated on every run (see Infovox230SAPI.iss), so the")
    out.append("; file is always the current choice rather than the union of every choice")
    out.append("; ever made. Each key is the name the engine knows that voice by.")
    for language in languages:
        out.append(
            'Filename: "{app}\\installed.ini"; Section: "Languages"; Key: %s; '
            'String: "1"; Components: %s' % (quote(language["name"]), language["component"])
        )
    out.append("")
    for language in languages:
        for speaker, mode in language["speakers"]:
            out.append(
                'Filename: "{app}\\installed.ini"; Section: "Voices"; Key: %s; '
                'String: "1"; Components: %s\\%s'
                % (quote(mode["_name"]), language["component"], speaker.lower())
            )
    out.append("")

    text = "\r\n".join(out)
    with open(OUT, "w", encoding="ascii", newline="") as f:
        f.write(text)
    print("wrote %s (%d voices in %d languages)" % (OUT, len(modes), len(languages)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
