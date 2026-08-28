Infovox 230 SAPI5 Voices
========================

Sixty voices in twelve languages, available to any Windows program that uses
speech: NVDA, JAWS, Narrator, Balabolka, Microsoft Word, and anything else that
speaks through the Windows speech interface (SAPI 5).

Languages: American English, British English, Danish, Dutch, Finnish, French,
German, Icelandic, Italian, Norwegian, Castilian Spanish, Swedish.
Speakers, in every language: Male, Female, Child, Giant, Zombie.

You choose which of them to install; only the ones you chose are here. See
"Choosing what is installed" below.


Checking it works
-----------------

Start menu, under "Infovox 230":

  Infovox 230 Configuration define voices of your own, and change every
                            setting the engine has
  Speak a test sentence     speaks aloud with the first Infovox voice
  List the Infovox voices   prints every voice, its language and gender
  Refresh the voice list    republishes the voices after editing voices.ini
  Read me                   this file

If the voices do not appear in your program, close and reopen that program
first: most programs read the voice list only once, when they start.


Choosing what is installed
--------------------------

The installer's "Select components" page offers the twelve languages, and
inside each language its five voices, one tick at a time. Nothing has to be
taken as a job lot: American English Male on its own is a legitimate
installation, and so is every voice of all twelve languages.

Two things follow from a tick.

A language carries its own pronunciation rules, and those are what take up
room -- from 37 KB for Castilian Spanish to 6.7 MB for Danish, about 23 MB for
all twelve. A language you do not tick is not put on the disk at all. The
size of each is shown against it on that page.

A voice costs nothing but a name in the Windows voice list, because the five
speakers of a language differ only in four numbers and all five read the same
rule file. Ticking a language takes all five of its voices; open it and untick
the ones you do not want.

There are three ready-made choices on the same page, and you can start from one
and adjust it:

  All 60 voices, in all 12 languages
  The English voices only -- American and British, 10 voices
  American English Male and Female only

To change your mind later, run the installer again. It starts from what you
chose last time, and a language you clear is removed from the disk and its
voices from the Windows voice list.

What you chose is written to installed.ini in this folder, which is how the
speech interface knows which voices to publish. The file explains itself if you
open it. Deleting it publishes every built-in voice again -- but a voice whose
language was never installed has no rule file to speak from, so it would then
be a name in the voice list that cannot speak. Running the installer again is
the better way round.

One exception, for when you want a single voice back without running the
installer: a voice you name as a section in voices.ini is published whether or
not installed.ini lists it, as long as its language is installed. Naming a
voice is taken as asking for it. See "Adding your own voices by hand" below.


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

Everything else -- which voices exist, what each one sounds like, how far the
rate and pitch controls reach, and the engine's own behaviour -- is set in the
configuration utility, described below.

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


The configuration utility
-------------------------

Start menu, Infovox 230, "Infovox 230 Configuration"; and on the desktop too, if
you asked the installer for an icon there.

This is where voices are made. The five speakers of each language differ only in
four numbers the engine reads -- base pitch, loudness contour, breathiness, and
which of five vocal tract shapes to use -- and those numbers are yours. You can
define as many as 256 voices of your own alongside the sixty built in, in any of
the twelve languages, and you can change the built-in voices as well.

The main window lists every voice. Choose one and:

  New voice        start a new voice, taking its settings from the one selected
  Change           alter the selected voice
  Duplicate        copy it under a new name, so the original is left alone
  Rename           give one of your own voices a different name
  Delete           remove one of your voices, or put a built-in one back as it
                   was made
  Preview          speak the voice, as it now stands

Preview is worth knowing about: it speaks the settings as they are at that
moment, before anything has been kept, so a voice can be listened to and
adjusted until it is right. It restarts the speech engine to do it, which takes
about a second, and a screen reader speaking through an Infovox voice will pause
while that happens.

Save writes the voices to a file. "Publish voices to Windows" is the separate
step that puts them in the Windows voice list where your programs will find
them; it needs administrator permission, and Windows asks for it at that point
rather than the utility demanding it for everything else. A program that is
already running will not see a new voice until it is restarted.

Voices can be kept for just you (%LOCALAPPDATA%\Infovox230SAPI\voices.ini) or
for everybody (voices.ini in the installation folder). "Just me" needs no
special permission and is what the utility starts with; for "All users", start
the utility with "Run as administrator".

Engine settings covers everything that is not a property of one voice: whether
the padding at the end of each utterance is trimmed and at what level, whether
positions in the text are reported while speaking, how long a wedged engine is
waited for, how far the rate and pitch controls in your programs reach, the
sentence Preview speaks, and how much detail is written to the log.

Everything is kept when you close the utility -- it asks first, and asks whether
to publish, so nothing is written or announced to Windows behind your back.


Using it with a screen reader
-----------------------------

The utility was built to be driven without seeing it:

  * every control can be reached with the Tab key, and Shift+Tab back;
  * every control has a name of its own, taken from the text beside it, so
    nothing is announced as just "edit" or "combo box";
  * everything is a standard Windows control -- nothing is drawn by hand;
  * each number box takes the up and down arrow keys as well as typed digits;
  * anything the utility has to tell you appears in a read-only box you can
    put the cursor in and read line by line, not in a label that a screen
    reader would skip: the details of the selected voice, the status of the
    last thing you did, the file being edited, and the notes on each page;
  * alt with the underlined letter reaches every button and box directly;
  * when a preview starts, focus moves to the "Stop speaking" button, so the
    way to stop it is under your hands already;
  * nothing is said with colour, position or shape alone.

The pitch box has a companion box, next to it, that shows the same pitch in
hertz as you change it -- the engine's own pitch number is not hertz, and this
saves doing the arithmetic.


Adding your own voices by hand
------------------------------

The utility writes an ordinary ini file, and you can write it yourself instead.
See voices.example.ini in this folder: copy it to voices.ini -- either here, or
in %LOCALAPPDATA%\Infovox230SAPI\ for just yourself -- edit it, then use
"Refresh the voice list" from the Start menu. The utility keeps your comments
and anything in the file it does not recognise, so the two ways of working can
be mixed.

Pitch is worth one note, because the number is not in hertz: the engine works
out the pitch as 3 x Pitch - 49, and clamps the result to between 30 and 250
hertz, so useful values run from about 27 to 99. The built-in male voice uses
50, which is 101 hertz.

You can call your voice anything. Behind the scenes it is given a name starting
with its language, because the engine checks that and quietly ignores any voice
whose name it does not recognise; that renaming is done for you and does not
change what you or your programs see.

The same file's [Settings] section holds the engine-wide settings the utility's
Engine settings page writes. voices.example.ini lists all of them.

A section named after a built-in voice does two things: it changes that voice,
and it asks for it. So if the installer left out, say, American English Child
but you installed American English, adding

  [Infovox American English Child]
  Pitch = 92

to voices.ini puts that voice back as well as raising its pitch. A section
naming a voice whose language was never installed does nothing, because there
is no rule file for it to speak from; install the language first.

Logs
----

Everything the voices do is logged to:

  %LOCALAPPDATA%\Infovox230SAPI\infovox230.log

One file, written by all three parts (the 32-bit interface, the 64-bit
interface, and the background engine), each line marked with which one wrote it,
so a single utterance can be followed all the way through. The installer's own
log is beside it as install.log.

The log is capped at 8 MB and rolls over to infovox230.log.1.

To turn the detail up or down, use the configuration utility: Engine settings,
"Log detail". It writes a file called loglevel.txt in that same folder holding a
single number, which you can also write yourself:

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

  Infovox230Diag list                    every voice this installation has
  Infovox230Diag registry                confirms the engine reads no registry
  Infovox230Diag speak out.wav "text"    drives the engine directly
  Infovox230Diag worker out.wav "text"   drives it the way the speech interface
                                         does, through the background program
  Infovox230Diag all outdir              one clip per installed voice
  Infovox230Diag stop                    stops the background program

If "speak" works but "worker" does not, the engine is fine and the problem is in
the connection to the background program. If neither works, the log will say
why. The 64-bit copies of these tools are in the x64 subfolder.

"list" is also the quickest way to see what was installed: it prints the voices
this installation actually has, which is what your programs will be offered. If
a voice you expected is missing, the log says which of the two reasons it was --
not chosen when you installed, or chosen but with its rule file absent.

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
