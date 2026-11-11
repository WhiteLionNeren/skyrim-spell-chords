# Third-party notices

Spell Chords itself is public domain (see [LICENSE](LICENSE)). These parts come from others and keep their own licenses.

## SKSE Menu Framework 3 API header

`include/SKSEMenuFramework.h` is the unmodified API header from
[QTR-Modding/SKSE-Menu-Framework-3-Example](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-Example)
(commit aa8effa0fea681d89633541991072c7d4bbe6b93), MIT License, Copyright (c) 2024 Thiago Kaique. The full license text
is in [include/SKSEMenuFramework-LICENSE.txt](include/SKSEMenuFramework-LICENSE.txt). SKSE Menu Framework's DLL is not
included or linked; it is loaded at runtime if installed.

## Font Awesome glyphs

The spell bar draws Font Awesome Free 6 solid glyphs (by codepoint) from the font that SKSE Menu Framework ships. No Font
Awesome files are included here, but `docs/spellbar-mockup.png` shows some of the icons.
Font Awesome Free by Fonticons, Inc. (https://fontawesome.com): icons CC BY 4.0, fonts SIL OFL 1.1.

## CommonLibSSE-NG and its dependencies (affects the compiled DLL)

[CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) v8.3.0 (git submodule `extern/CommonLibSSE`) is licensed
**GPL-3.0-or-later with the Modding Exception and the GPL-3.0 Linking Exception** (its `COPYING.txt` and `EXCEPTIONS.md`;
older versions were MIT, kept in its `licenses/LICENSE-MIT.txt`). It is compiled statically into `SpellChords.dll`,
together with parts of libraries it pulls in through vcpkg: spdlog and fmt (MIT License) and rapidcsv (BSD 3-Clause
License). Their license texts are in [THIRD-PARTY-LICENSES.txt](THIRD-PARTY-LICENSES.txt), which the release zip carries too.

So the **compiled DLL is a combined work distributed under GPL-3.0-or-later** (with CommonLibSSE-NG's exceptions). The
Unlicense of this repository's own source is GPL-compatible, and this repository together with its submodule is the
DLL's complete corresponding source. The release zip carries a copy of the GPL (`SKSE/Plugins/SpellChords/COPYING.txt`)
and a notice (`SKSE/Plugins/SpellChords/NOTICE.txt`).
