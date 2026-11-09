// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The font families a page can match while "Restrict fonts" is on (issue #117),
// besides the CSS generic keywords the renderer never filters. One quoted name
// per line, each followed by a comma; names match without regard to case.
// components/aerie_fonts/font_allowlist.cc includes this file inside an array,
// and tests/test_fonts_allowlist.py reads it, so keep that shape. The list is
// the one the maintainer approved in the issue.

// CSS generic keywords, in case a page writes one as a quoted family name.
"serif",
"sans-serif",
"monospace",
"cursive",
"fantasy",
"system-ui",
"ui-serif",
"ui-sans-serif",
"ui-monospace",
"ui-rounded",
"emoji",
"math",
"fangsong",

// DejaVu, Liberation, Noto, Cantarell, Ubuntu, Free and Droid families.
"DejaVu Sans",
"DejaVu Serif",
"DejaVu Sans Mono",
"Liberation Sans",
"Liberation Serif",
"Liberation Mono",
"Noto Sans",
"Noto Serif",
"Noto Sans Mono",
"Noto Sans CJK SC",
"Noto Sans CJK TC",
"Noto Sans CJK JP",
"Noto Sans CJK KR",
"Noto Serif CJK SC",
"Noto Serif CJK TC",
"Noto Serif CJK JP",
"Noto Serif CJK KR",
"Noto Color Emoji",
"Cantarell",
"Ubuntu",
"Ubuntu Mono",
"FreeSans",
"FreeSerif",
"FreeMono",
"Droid Sans",
"Droid Serif",
"Droid Sans Mono",

// The Microsoft names that fontconfig maps to Liberation on most Linux systems.
// Every Chrome on Linux answers to them, so hiding them would make Aerie stand
// out.
"Arial",
"Helvetica",
"Times New Roman",
"Times",
"Courier New",
"Courier",
