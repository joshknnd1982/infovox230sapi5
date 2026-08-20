Infovox 230 SAPI5 Voices
========================

Sixty voices in twelve languages, available to any Windows program that uses
speech: NVDA, JAWS, Narrator, Balabolka, Microsoft Word, and anything else that
speaks through the Windows speech interface (SAPI 5).

Languages: American English, British English, Danish, Dutch, Finnish, French,
German, Icelandic, Italian, Norwegian, Castilian Spanish, Swedish.
Speakers, in every language: Male, Female, Child, Giant, Zombie.


Checking it works
-----------------

Start menu, under "Infovox 230":

  Speak a test sentence     speaks aloud with the first Infovox voice
  List the Infovox voices   prints every voice, its language and gender
  Refresh the voice list    republishes the voices after editing voices.ini
  Read me                   this file

If the voices do not appear in your program, close and reopen that program
first: most programs read the voice list only once, when they start.


How it is put together
----------------------

The Infovox 230 speech engine is 32-bit and dates from the 1990s. Rather than
load it inside whatever program is speaking, it runs in a separate background
program, Infovox230Server.exe, which starts by itself the first time something
speaks. Two things follow from that:

  * 64-bit programs can use the voices even though the engine is 32-bit;
  * if the engine ever fails, it cannot take your screen reader down with it.

Two speech interfaces are installed, a 32-bit one and a 64-bit one, so programs
of either kind can use the voices. Both talk to the same background engine.

The engine needs no SAPI 4 runtime -- the old speech system it was written for
is neither installed nor used. It also reads none of its settings from the
Windows registry: the voice table and the paths it needs are supplied from
memory as it asks for them. Nothing about the voices is stored in the registry
and nothing is left behind by an uninstall, apart from your own log files and
any voices you have defined yourself.

The only registry entries this product creates are the ones Windows speech
itself requires in order to list a voice at all.


What you can control
--------------------

From any speech program, in the usual way:

  Voice     any of the sixty
  Rate      the full range the engine supports, about 15 to 500 words a minute
  Pitch     about 30 to 250 hertz, starting from each voice's own pitch
  Volume    0 to 100

The engine also reports where it is in the text as it speaks, so programs that
highlight the current word while reading will do so.

Responsiveness, which matters most when arrowing quickly through text: pressing
a key interrupts what is being said and starts the next line in about 40
milliseconds, measured through Windows speech at twenty keypresses a second.
Speech is synthesised at roughly fifty times faster than it is spoken, so the
engine is never what you are waiting for.

Two things this engine cannot do, so you know not to look for them: it produces
no mouth-shape (viseme) information for talking-head animation, and its own
pause tag does nothing. Pauses asked for in speech markup are produced by
inserting real silence instead. That silence is exact, but a pause splits an
utterance in two and the engine adds about a fifth of a second of its own lead-in
to the second half, so a requested one second comes out closer to one and a
quarter.

One thing worth knowing about the sound: the engine ends every utterance with
about eight tenths of a second of inaudible padding. Left alone that would be
most of a second of dead air after everything a screen reader says, so it is
trimmed. Pauses inside a sentence are left exactly as the engine made them --
only the run of silence at the very end is dropped, and only once there is
nothing further to speak.

Infovox230Diag writes the engine's audio untrimmed, so a clip saved with it is
about eight tenths of a second longer than the same words through Windows
speech. That is the diagnostic showing you the raw engine output, not a fault.


Adding your own voices
----------------------

The five speakers of each language differ only in four numbers the engine reads:
base pitch, loudness contour, breathiness, and which vocal tract shape to use.
Those numbers are yours to change, and you can define as many extra voices as
you like -- up to 256 of your own, alongside the 60 built in.

See voices.example.ini in this folder. Copy it to voices.ini -- either here, or
in %LOCALAPPDATA%\Infovox230SAPI\ for just yourself -- edit it, then use
"Refresh the voice list" from the Start menu.

Pitch is worth one note, because the number is not in hertz: the engine works
out the pitch as 3 x Pitch - 49, and clamps the result to between 30 and 250
hertz, so useful values run from about 27 to 99. The built-in male voice uses
50, which is 101 hertz.

You can call your voice anything. Behind the scenes it is given a name starting
with its language, because the engine checks that and quietly ignores any voice
whose name it does not recognise; that renaming is done for you and does not
change what you or your programs see.


Logs
----

Everything the voices do is logged to:

  %LOCALAPPDATA%\Infovox230SAPI\infovox230.log

One file, written by all three parts (the 32-bit interface, the 64-bit
interface, and the background engine), each line marked with which one wrote it,
so a single utterance can be followed all the way through. The installer's own
log is beside it as install.log.

The log is capped at 8 MB and rolls over to infovox230.log.1.

To turn the detail up or down, create a file called loglevel.txt in that same
folder containing a single number:

  0  off
  1  errors only
  2  errors and warnings
  3  normal (the default)
  4  detailed -- every utterance, with the rate, pitch and voice used
  5  everything, including each setting the engine reads and each word boundary

Level 5 is large but is the one to use when reporting a problem. The setting is
read once when a program starts speaking, so restart the speaking program after
changing it.

You can also set the environment variable INFOVOX230_LOG_LEVEL, which overrides
the file.


Diagnosing a problem
--------------------

Infovox230Diag.exe, in this folder, drives the engine two ways so a fault can be
placed:

  Infovox230Diag list                    every voice the product knows about
  Infovox230Diag registry                confirms the engine reads no registry
  Infovox230Diag speak out.wav "text"    drives the engine directly
  Infovox230Diag worker out.wav "text"   drives it the way the speech interface
                                         does, through the background program
  Infovox230Diag all outdir              one clip per voice, all sixty
  Infovox230Diag stop                    stops the background program

If "speak" works but "worker" does not, the engine is fine and the problem is in
the connection to the background program. If neither works, the log will say
why. The 64-bit copies of these tools are in the x64 subfolder.

Infovox230SapiTest.exe goes through Windows speech itself, which is the last
link in the chain:

  Infovox230SapiTest list                voices as Windows reports them
  Infovox230SapiTest say "text"          speaks aloud
  Infovox230SapiTest speak out.wav       speaks to a file
  Infovox230SapiTest all outdir --verbose


Where this came from
--------------------

This is a descendant of the BestSpeech SAPI5 wrapper, which had already worked
out how to make a 32-bit engine of an earlier generation appear as an ordinary
Windows voice in both 32- and 64-bit programs. The architecture is inherited from
it; everything specific to Infovox 230 is new.

  https://github.com/gozaltech/bstspeech-sapi     the original
  https://github.com/joshknnd1982/BstSpeech-sapi  the fork this grew out of

Source for this project:

  https://github.com/joshknnd1982/infovox230sapi5


Licence
-------

The wrapper, the background program and the tools are free software under the
GNU General Public License; see the LICENSE file in the source repository. The
32-bit Python host kept in the source tree for reference derives from NVDA and
carries NVDA's own GPL v2 terms, as its header says.

The Infovox 230 speech engine itself is not part of this project. It was made by
Telia Promotor / Babel-Infovox AB, both long defunct.
