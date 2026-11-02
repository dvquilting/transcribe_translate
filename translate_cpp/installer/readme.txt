Translate
=========

Translate text, Markdown, subtitle (.srt) and Word (.docx) files on your own
computer with a local AI model. Nothing is uploaded.

Start "Translate" from the Start menu, or right-click a .txt, .md, .srt or
.docx file in File Explorer and choose "Translate...".

  Text tab   type or paste text, choose the languages, press Translate
             (or Ctrl+Enter). The translation fills in as it is written.
  File tab   choose a file; the translation is saved next to it as
             NAME.<language>.EXT, e.g. report.de.docx.

Models
------
The model is downloaded the first time you use it, into
%LOCALAPPDATA%\Translate\models :

  TranslateGemma 4B      4.1 GB   fast, good quality, 55 languages
  TranslateGemma 12B     9.7 GB   very good quality (default)
  TranslateGemma 27B    16.5 GB   best quality; partly runs on the CPU
  Hy-MT2 7B              8.0 GB   strong on Chinese and Asian languages
  Qwen 3.5 9B            7.5 GB   general model, any language pair
  Qwen 3.5 35B-A3B      22.2 GB   larger general model; needs 32 GB of RAM

An internet connection is needed for that first download only. The models
are kept when Translate is uninstalled; delete that folder to free the space.

Models that are bigger than your graphics card's memory are split between
the graphics card and the CPU automatically. That works, but is slower.

What is kept in documents
-------------------------
Text and Markdown keep their layout; code blocks are not translated.
Subtitles keep their timing (sentences that run over several subtitles are
translated as a whole and divided again). Word documents keep paragraph
styles, lists, tables, headers and footers, but formatting that changes
inside a paragraph (a single bold word, say) is not kept.

AI translations can contain mistakes. Check anything important.

Graphics cards
--------------
Translate uses, in order of preference: an NVIDIA GPU with CUDA (if the CUDA
component was installed), any GPU through Vulkan (NVIDIA, AMD, Intel), or the
CPU. The window shows which one it is using.

Command line
------------
translate_text.exe, in the installation folder, does the same from a command
prompt. Run "translate_text --help" for the options.

Requirements
------------
Windows 10 or 11 (64-bit), a CPU with AVX2 (2013 or newer), 16 GB of RAM
(32 GB for the largest models).
