# Number to words conversion for the BestSpeech v2 language dlls whose text frontends
# can't read digits themselves: Japanese and Greek ignore ascii digits entirely (they
# emit the same canned fallback for any digit string), and Polish only reads them digit
# by digit. Cardinal, nominative forms only; good enough for screen reading.
# Named with a _bst prefix because NVDA merges every addon's synthDrivers folder into
# one package namespace, where a generic module name would collide across addons.

import re

_PL_UNITS = ["zero", "jeden", "dwa", "trzy", "cztery", "pięć", "sześć", "siedem", "osiem", "dziewięć"]
_PL_TEENS = ["dziesięć", "jedenaście", "dwanaście", "trzynaście", "czternaście", "piętnaście", "szesnaście", "siedemnaście", "osiemnaście", "dziewiętnaście"]
_PL_TENS = ["", "", "dwadzieścia", "trzydzieści", "czterdzieści", "pięćdziesiąt", "sześćdziesiąt", "siedemdziesiąt", "osiemdziesiąt", "dziewięćdziesiąt"]
_PL_HUNDREDS = ["", "sto", "dwieście", "trzysta", "czterysta", "pięćset", "sześćset", "siedemset", "osiemset", "dziewięćset"]
# (singular, paucal 2-4, plural) forms per scale
_PL_SCALES = [("tysiąc", "tysiące", "tysięcy"), ("milion", "miliony", "milionów"), ("miliard", "miliardy", "miliardów")]

def _pl_under_thousand(n):
	parts = []
	if n >= 100:
		parts.append(_PL_HUNDREDS[n // 100])
		n %= 100
	if 10 <= n <= 19:
		parts.append(_PL_TEENS[n - 10])
	else:
		if n >= 20:
			parts.append(_PL_TENS[n // 10])
			n %= 10
		if n:
			parts.append(_PL_UNITS[n])
	return parts

def _pl_scale_form(n, forms):
	if n == 1:
		return forms[0]
	last = n % 10
	if 2 <= last <= 4 and not (10 <= n % 100 <= 19):
		return forms[1]
	return forms[2]

def _polish(n):
	if n == 0:
		return _PL_UNITS[0]
	groups = []
	scale = -1  # -1 = the bare hundreds group
	while n:
		groups.append((scale, n % 1000))
		n //= 1000
		scale += 1
	parts = []
	for scale, g in reversed(groups):
		if not g:
			continue
		if scale < 0:
			parts.extend(_pl_under_thousand(g))
		else:
			if g != 1:  # "tysiąc", not "jeden tysiąc"
				parts.extend(_pl_under_thousand(g))
			parts.append(_pl_scale_form(g, _PL_SCALES[scale]))
	return " ".join(parts)

_EL_UNITS = ["μηδέν", "ένα", "δύο", "τρία", "τέσσερα", "πέντε", "έξι", "επτά", "οκτώ", "εννέα"]
_EL_TEENS = ["δέκα", "έντεκα", "δώδεκα", "δεκατρία", "δεκατέσσερα", "δεκαπέντε", "δεκαέξι", "δεκαεπτά", "δεκαοκτώ", "δεκαεννέα"]
_EL_TENS = ["", "", "είκοσι", "τριάντα", "σαράντα", "πενήντα", "εξήντα", "εβδομήντα", "ογδόντα", "ενενήντα"]
_EL_HUNDREDS = ["", "εκατόν", "διακόσια", "τριακόσια", "τετρακόσια", "πεντακόσια", "εξακόσια", "επτακόσια", "οκτακόσια", "εννιακόσια"]

def _el_under_thousand(n):
	parts = []
	if n >= 100:
		if n == 100:
			return ["εκατό"]
		parts.append(_EL_HUNDREDS[n // 100])
		n %= 100
	if 10 <= n <= 19:
		parts.append(_EL_TEENS[n - 10])
	else:
		if n >= 20:
			parts.append(_EL_TENS[n // 10])
			n %= 10
		if n:
			parts.append(_EL_UNITS[n])
	return parts

def _greek(n):
	if n == 0:
		return _EL_UNITS[0]
	parts = []
	millions, rest = divmod(n, 1000000)
	thousands, units = divmod(rest, 1000)
	if millions:
		if millions == 1:
			parts.append("ένα εκατομμύριο")
		else:
			parts.extend(_el_under_thousand(millions) if millions < 1000 else [_greek(millions)])
			parts.append("εκατομμύρια")
	if thousands:
		if thousands == 1:
			parts.append("χίλια")
		else:
			parts.extend(_el_under_thousand(thousands))
			parts.append("χιλιάδες")
	if units:
		parts.extend(_el_under_thousand(units))
	return " ".join(parts)

# The Japanese dll's frontend reads kana phonetically but drops kanji numerals (and
# ascii digits), so numbers are written out in hiragana with the usual sound changes.
_JA_DIGITS = ["ゼロ", "いち", "に", "さん", "よん", "ご", "ろく", "なな", "はち", "きゅう"]
_JA_HUNDREDS = ["", "ひゃく", "にひゃく", "さんびゃく", "よんひゃく", "ごひゃく", "ろっぴゃく", "ななひゃく", "はっぴゃく", "きゅうひゃく"]
_JA_THOUSANDS = ["", "せん", "にせん", "さんぜん", "よんせん", "ごせん", "ろくせん", "ななせん", "はっせん", "きゅうせん"]
_JA_GROUPS = ["", "まん", "おく", "ちょう"]  # 10^4, 10^8, 10^12

def _ja_group(n):
	# 0 < n < 10000 -> hiragana reading
	out = ""
	out += _JA_THOUSANDS[n // 1000]
	n %= 1000
	out += _JA_HUNDREDS[n // 100]
	n %= 100
	tens = n // 10
	if tens:
		if tens != 1:
			out += _JA_DIGITS[tens]
		out += "じゅう"
	if n % 10:
		out += _JA_DIGITS[n % 10]
	return out

def _japanese(n):
	if n == 0:
		return _JA_DIGITS[0]
	groups = []
	i = 0
	while n:
		g = n % 10000
		if g:
			prefix = _ja_group(g)
			# ichi-man, ichi-oku etc keep their leading one
			if g == 1 and i > 0:
				prefix = _JA_DIGITS[1]
			groups.append(prefix + _JA_GROUPS[i])
		n //= 10000
		i += 1
	return "".join(reversed(groups))

_converters = {"pol": (_polish, 12, _PL_UNITS), "gre": (_greek, 9, _EL_UNITS), "jpn": (_japanese, 16, _JA_DIGITS)}

def localizeNumbers(text, langId, full):
	"""Replace digit runs with words the given language's dll can speak.
	With full=False only per-digit reading is produced (Japanese and Greek still need
	that, since their frontends drop ascii digits entirely; Polish reads single digits
	itself and is left alone)."""
	conv = _converters.get(langId)
	if conv is None:
		return text
	toWords, maxDigits, digitNames = conv
	if not full and langId == "pol":
		return text
	sep = "" if langId == "jpn" else " "
	def repl(m):
		s = m.group(0)
		if full and len(s) <= maxDigits:
			return toWords(int(s))
		return sep.join(digitNames[int(c)] for c in s) if sep else "".join(digitNames[int(c)] for c in s)
	return re.sub(r"\d+", repl, text)
