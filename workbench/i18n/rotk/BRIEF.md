# Brief: ROTK (Xbox) fan subtitles, our own translation

Game: The Lord of the Rings: The Return of the King (2003 video game, based on the film). Two inputs in /home/claude/work/i18n/rotk:
- ui_en.tsv: KEY <TAB> English interface text (menus, prompts, options, upgrade and move names and descriptions, level names, objectives, codes, legal text). 552 lines.
- dlg_en.tsv: LEVEL:SOUNDID <TAB> English spoken line (film dialogue and in-game speech, shown as subtitles). 1107 lines.
Some languages also have ui_<code>.tsv: KEY <TAB> English <TAB> the official European release's line (REFERENCE ONLY, for names and terms). Polish reference text uses the game's own font mapping, not standard encoding: read it with ¹=ą, ê=ę, æ=ć, ³=ł, ñ=ń, ó=ó, º=ś, ¼=ź, ¾=ż, ¥=Ą, Æ=Ć, £=Ł, Ñ=Ń, ª=Ś, ¬=Ź, ¯=Ż (approximately; use judgment). Our own output must be in normal UTF-8.

Write two files (UTF-8, no BOM, one header comment line starting with '#', then exactly one line per input line in the same order, every key once):
- /home/claude/work/i18n/rotk/out/ui_<code>.txt   KEY <TAB> translation
- /home/claude/work/i18n/rotk/out/dlg_<code>.txt  LEVEL:SOUNDID <TAB> translation

Rules:
- Translate from the English yourself, natural and concise for on-screen subtitles. Do not copy the reference's sentences; use it for proper names and game terms so players recognise them. For dialogue, translate the meaning faithfully in the style of the film's dialogue in that language; use the established Tolkien names of that language (as the films' release in that language uses them).
- Keep tokens exactly: printf-style %s %d %i %%, anything in {} or [], sequences like @ or § or £ or ¥ used as special glyphs (copy them as they are, in the same position), '|' forced line breaks (keep them where the English has them), "..." pauses, numbers. Keep button names (A, B, X, Y, START, BACK, black/white buttons, triggers as the language usually names them), Xbox, Xbox Live, MEMORY UNIT brand names.
- An empty or untranslatable line (a name that stays the same): keep it as is. No tabs inside text.
- Re-read both files against the English once (accuracy, missing keys, tokens).
Report: lines written in each file and up to 8 doubtful lines (key and why), under 200 words.
