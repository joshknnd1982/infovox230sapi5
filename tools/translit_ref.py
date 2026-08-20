# -*- coding: utf-8 -*-
"""Transliteration and number-word tables for the BestSpeech engines.

The Greek and Russian frontends vocalize nothing at all for Latin letters -- every
ascii letter is dropped silently -- so Latin text reaching them simply disappears.
Greek drops ascii digits too, as do the Japanese and Polish frontends (Polish spells
them one at a time instead of reading the number). These tables rewrite such text into
sequences the engines actually pronounce; every target here was checked against the
real dll through tools/verify_engines.py.

Notable engine quirks the Greek table works around: the frontend drops a word-initial
gamma before alpha or iota, and ignores a bare upsilon, so hard /g/ uses the gamma-kappa
digraph Greek itself uses for that sound and /u/ uses omicron-upsilon.

tools/gen_translit.py turns these tables into src/translit_tables.inc, so the C++ engine
and this reference cannot drift apart.
"""

# --- Latin -> Greek ----------------------------------------------------------
# Longest match first; the generator preserves this order and the C++ scanner relies
# on it, so keep every multi-letter rule ahead of any rule that is a prefix of it.
GRE_MULTI = [
    ("tion", "σιον"), ("tch", "τσ"), ("sch", "σκ"), ("chr", "κρ"),
    ("ch", "τσ"), ("sh", "σ"), ("th", "θ"), ("ph", "φ"),
    ("gh", "γκ"), ("kh", "χ"), ("wh", "ου"), ("qu", "κου"),
    ("ck", "κ"), ("ng", "γκ"), ("ps", "ψ"), ("ee", "ι"),
    ("oo", "ου"), ("ou", "ου"), ("ow", "ου"), ("oy", "οι"),
    ("ea", "ι"), ("ai", "ει"), ("ay", "ει"),
]
GRE_SINGLE = {
    "a": "α", "b": "μπ", "c": "κ", "d": "ντ", "e": "ε", "f": "φ",
    "g": "γκ", "h": "χ", "i": "ι", "j": "τζ", "k": "κ", "l": "λ",
    "m": "μ", "n": "ν", "o": "ο", "p": "π", "q": "κ", "r": "ρ",
    "s": "σ", "t": "τ", "u": "ου", "v": "β", "w": "ου", "x": "ξ",
    "y": "ι", "z": "ζ",
}
# A lone Latin letter is a screen reader echoing a character, so it is read as the
# English letter name; a bare consonant transliteration would be an unintelligible grunt.
GRE_NAMES = {
    "a": "ει", "b": "μπι", "c": "σι", "d": "ντι", "e": "ι", "f": "εφ",
    "g": "τζι", "h": "ειτσ", "i": "αι", "j": "τζει", "k": "κει", "l": "ελ",
    "m": "εμ", "n": "εν", "o": "οου", "p": "πι", "q": "κιου", "r": "αρ",
    "s": "ες", "t": "τι", "u": "γιου", "v": "βι", "w": "νταμπλιου", "x": "εξ",
    "y": "γουαι", "z": "ζεντ",
}

# --- Latin -> Cyrillic -------------------------------------------------------
CYR_MULTI = [
    ("tion", "шн"), ("tch", "ч"), ("sch", "ск"), ("chr", "кр"),
    ("ch", "ч"), ("sh", "ш"), ("th", "т"), ("ph", "ф"),
    ("kh", "х"), ("gh", "г"), ("zh", "ж"), ("qu", "кв"),
    ("ck", "к"), ("ee", "и"), ("oo", "у"), ("ou", "ау"),
    ("ow", "ау"), ("yu", "ю"), ("ya", "я"), ("ye", "е"),
    ("yo", "ё"), ("ea", "и"), ("ai", "эй"), ("ay", "эй"),
    ("ei", "эй"),
]
CYR_SINGLE = {
    "a": "а", "b": "б", "c": "к", "d": "д", "e": "е", "f": "ф",
    "g": "г", "h": "х", "i": "и", "j": "дж", "k": "к", "l": "л",
    "m": "м", "n": "н", "o": "о", "p": "п", "q": "к", "r": "р",
    "s": "с", "t": "т", "u": "у", "v": "в", "w": "в", "x": "кс",
    "y": "и", "z": "з",
}
CYR_NAMES = {
    "a": "эй", "b": "би", "c": "си", "d": "ди", "e": "и", "f": "эф",
    "g": "джи", "h": "эйч", "i": "ай", "j": "джэй", "k": "кей", "l": "эл",
    "m": "эм", "n": "эн", "o": "оу", "p": "пи", "q": "кью", "r": "ар",
    "s": "эс", "t": "ти", "u": "ю", "v": "ви", "w": "даблью", "x": "экс",
    "y": "уай", "z": "зед",
}

# --- Number words ------------------------------------------------------------
# Cardinal, nominative forms only; good enough for screen reading.
EL_UNITS = ["μηδέν", "ένα", "δύο", "τρία", "τέσσερα", "πέντε", "έξι", "επτά", "οκτώ", "εννέα"]
EL_TEENS = ["δέκα", "έντεκα", "δώδεκα", "δεκατρία", "δεκατέσσερα", "δεκαπέντε", "δεκαέξι",
            "δεκαεπτά", "δεκαοκτώ", "δεκαεννέα"]
EL_TENS = ["", "", "είκοσι", "τριάντα", "σαράντα", "πενήντα", "εξήντα", "εβδομήντα",
           "ογδόντα", "ενενήντα"]
EL_HUNDREDS = ["", "εκατόν", "διακόσια", "τριακόσια", "τετρακόσια", "πεντακόσια",
               "εξακόσια", "επτακόσια", "οκτακόσια", "εννιακόσια"]
EL_HUNDRED_FLAT = "εκατό"
EL_THOUSAND = "χίλια"
EL_THOUSANDS = "χιλιάδες"
EL_MILLION = "ένα εκατομμύριο"
EL_MILLIONS = "εκατομμύρια"

PL_UNITS = ["zero", "jeden", "dwa", "trzy", "cztery", "pięć", "sześć", "siedem", "osiem", "dziewięć"]
PL_TEENS = ["dziesięć", "jedenaście", "dwanaście", "trzynaście", "czternaście", "piętnaście",
            "szesnaście", "siedemnaście", "osiemnaście", "dziewiętnaście"]
PL_TENS = ["", "", "dwadzieścia", "trzydzieści", "czterdzieści", "pięćdziesiąt",
           "sześćdziesiąt", "siedemdziesiąt", "osiemdziesiąt", "dziewięćdziesiąt"]
PL_HUNDREDS = ["", "sto", "dwieście", "trzysta", "czterysta", "pięćset", "sześćset",
               "siedemset", "osiemset", "dziewięćset"]
# (singular, paucal 2-4, plural) per scale
PL_SCALES = [
    ("tysiąc", "tysiące", "tysięcy"),
    ("milion", "miliony", "milionów"),
    ("miliard", "miliardy", "miliardów"),
]

# The Japanese frontend reads kana phonetically but drops kanji numerals and ascii
# digits alike, so numbers are written out in hiragana with the usual sound changes.
JA_DIGITS = ["ゼロ", "いち", "に", "さん", "よん", "ご", "ろく", "なな", "はち", "きゅう"]
JA_HUNDREDS = ["", "ひゃく", "にひゃく", "さんびゃく", "よんひゃく", "ごひゃく", "ろっぴゃく",
               "ななひゃく", "はっぴゃく", "きゅうひゃく"]
JA_THOUSANDS = ["", "せん", "にせん", "さんぜん", "よんせん", "ごせん", "ろくせん", "ななせん",
                "はっせん", "きゅうせん"]
JA_GROUPS = ["", "まん", "おく", "ちょう"]  # 10^4, 10^8, 10^12
JA_TEN = "じゅう"


# --- Reference implementation ------------------------------------------------
def _is_lat(c):
    return "a" <= c.lower() <= "z"


def _drop_silent_e(run):
    """English spells a long vowel with a final mute e ("drive", "complete"). Neither
    engine has that convention, so the e would come out as a whole extra syllable."""
    if len(run) > 3 and run[-1].lower() == "e" and run[-2].lower() not in "aeiou":
        return run[:-1]
    return run


def _run(text, multi, single):
    out, i, n = [], 0, len(text)
    while i < n:
        rest = text[i:].lower()
        for src, dst in multi:
            if rest.startswith(src):
                out.append(dst)
                i += len(src)
                break
        else:
            out.append(single.get(text[i].lower(), ""))
            i += 1
    return "".join(out)


def _translit(text, multi, single, names):
    out, i, n = [], 0, len(text)
    while i < n:
        if not _is_lat(text[i]):
            out.append(text[i])
            i += 1
            continue
        j = i
        while j < n and _is_lat(text[j]):
            j += 1
        run = text[i:j]
        out.append(names.get(run.lower(), "") if len(run) == 1
                   else _run(_drop_silent_e(run), multi, single))
        i = j
    return "".join(out)


def to_greek(text):
    return _translit(text, GRE_MULTI, GRE_SINGLE, GRE_NAMES)


def to_cyrillic(text):
    return _translit(text, CYR_MULTI, CYR_SINGLE, CYR_NAMES)
