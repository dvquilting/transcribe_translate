Transcribe
==========

Start "Transcribe" from the Start menu, or right-click an audio or video file
in File Explorer and choose "Transcribe (speech to text)".

First use of each model
-----------------------
The speech model is downloaded the first time you use it, into
%USERPROFILE%\.cache\crispasr :

  Parakeet Ultra       0.7 GB   fastest, very good accuracy (default)
  Qwen3-ASR-1.7B       2.5 GB   accurate, multilingual
  ARK-ASR-3B           4.3 GB   most accurate, slowest
  Granite Speech 4.1   3.0 GB   very accurate; English, French, German,
                                Spanish, Portuguese
  Cohere Transcribe    2.4 GB   very accurate, 14 languages; choose the
                                spoken language (it can't detect it)
  (+ about 160 MB once, with ARK-ASR: a model that adds punctuation and
   capitals, which ARK doesn't write by default. The option "Ask ARK to
   write punctuation" asks ARK itself instead; it is experimental.)
  (+ about 1 GB once, for word-timed subtitles with any model except
   Parakeet)
  (+ about 2.5 GB once, for the AI options: smart paragraphs, spelling and
   grammar, summary)

An internet connection is needed for that first download only. The models are
kept when Transcribe is uninstalled; delete that folder to free the space.

Graphics cards
--------------
Transcribe uses, in order of preference: an NVIDIA GPU with CUDA (if the CUDA
component was installed), any GPU through Vulkan (NVIDIA, AMD, Intel), or the
CPU. The window shows which one it is using. Keep your graphics driver up to
date; Vulkan comes with the driver.

AI post-processing (optional)
-----------------------------
"Smart paragraphs" starts new paragraphs where the topic changes or the
speaker pauses, instead of every few sentences; the words are not changed.
"Fix spelling & grammar" saves a corrected copy of the transcript as
NAME.clean.txt, and "Write a summary" saves NAME.summary.txt. Both use a
small language model (Qwen3-4B) that runs on your own computer; nothing is
uploaded. AI output can contain mistakes: the original transcript (NAME.txt)
is always saved unchanged, so check anything important against it.

Several speakers
----------------
Tick "Several speakers: write who is speaking" to have the speakers' names
in the transcript. After the speech is written down, a window shows the
speakers that were found: listen to each one, type their names, mark two
that are really the same person, or detect again with the number you know.
Each speaker's turn then starts a new paragraph with their name ("Anna:",
in bold in the Word and PDF files), and the subtitles show the name where
the speaker changes. A small voice model (24 MB) is downloaded once. It
works best with clear recordings and different voices; check the result.

Audio and video formats
-----------------------
WAV, MP3, FLAC and OGG/Opus always work. Video files and M4A/AAC need FFmpeg:
either the copy included by the installer, or one on your PATH.

Command line
------------
transcribe_multi.exe, in the installation folder, does the same from a command
prompt. Run "transcribe_multi --help" for the options.

Requirements
------------
Windows 10 or 11 (64-bit), a CPU with AVX2 (2013 or newer).
