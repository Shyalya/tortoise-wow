#!/usr/bin/env python3
# Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3.
#
# MSVC portability guard: catch Windows-only build breaks from a Linux runner.
#
# Every automated build we have (tests.yml, upstream-smoke.yml, the local
# build.sh) uses clang against glibc, so code that compiles here and nowhere else
# ships undetected until a Windows contributor reports it. windows-smoke.yml is
# the real answer, but a full MSVC compile is minutes-to-an-hour; this is the
# one-second version that runs on every push and catches the recurring cases.
#
# What it flags, and why each is genuinely MSVC-only:
#
#   1. The M_* math macros (M_PI and friends). glibc's <math.h> defines them
#      unconditionally; MSVC's only defines them when _USE_MATH_DEFINES was set
#      BEFORE math.h was first included, and math.h is #pragma once so setting it
#      afterwards does nothing. A module TU that reaches <cmath> before the
#      core's Define.h therefore fails with "C2065: 'M_PI': undeclared
#      identifier" — and takes the core's own Position.h down with it, since
#      NormalizeOrientation calls std::fmod(o, 2.0f * static_cast<float>(M_PI))
#      and the dead argument resurfaces as "C2661: 'fmod': no overloaded function
#      takes 1 arguments". Use DC_PI from Util/DungeonClearTuning.h.
#
#   2. POSIX-only headers with no MSVC equivalent.
#
#   3. GNU compiler extensions that MSVC does not parse.
#
# This is deliberately a small, high-signal list: every entry is a hard compile
# error on MSVC, never a warning or a style opinion. If a check here ever fires
# on legitimate code, the fix is to add a portable spelling, not to widen the
# allowlist.
#
# Run from anywhere:  python3 tools/check_msvc_portability.py

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCAN_DIRS = [ROOT / "src", ROOT / "t"]
SUFFIXES = {".cpp", ".h", ".hpp", ".cc"}

# Files allowed to mention a banned token — the guard's own documentation.
ALLOWLIST = {
    "src/Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h",  # explains the M_PI trap
}

# (compiled pattern, human explanation). Patterns are matched against source with
# comments and string literals stripped, so prose about M_PI does not trip it.
CHECKS = [
    (
        re.compile(r"\bM_(PI|PI_2|PI_4|E|LOG2E|LOG10E|LN2|LN10|1_PI|2_PI|SQRT2|SQRT1_2)\b"),
        "MSVC does not define the M_* math macros unless _USE_MATH_DEFINES was set\n"
        "    before the first <math.h>/<cmath> include. Use DC_PI from\n"
        "    Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h (or a local literal).",
    ),
    (
        re.compile(r"#\s*include\s*<(unistd|alloca|sys/time|pthread|dirent|strings)\.h>"),
        "POSIX-only header with no MSVC equivalent.",
    ),
    (
        re.compile(r"\b__attribute__\s*\(\("),
        "GCC/clang attribute syntax; MSVC does not parse it. Use a portable\n"
        "    [[attribute]] or guard it on the compiler.",
    ),
    (
        re.compile(r"\b(strcasecmp|strncasecmp|ffs|getpid)\s*\("),
        "POSIX function absent from the MSVC CRT (MSVC spells these _stricmp,\n"
        "    _strnicmp, _getpid, ...). Use the core's portable helper.",
    ),
]


def strip_comments_and_strings(text):
    """Blank out // and /* */ comments plus string/char literals.

    Replaced with spaces rather than deleted so reported line numbers stay true.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif two == "/*":
            while i < n and text[i:i + 2] != "*/":
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif c in "\"'":
            quote = c
            out.append(" ")
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out.append(" ")
                    i += 1
                    if i < n:
                        out.append("\n" if text[i] == "\n" else " ")
                        i += 1
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append(" ")
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main():
    errors = []
    scanned = 0

    for scan_dir in SCAN_DIRS:
        if not scan_dir.is_dir():
            continue
        for path in sorted(scan_dir.rglob("*")):
            if path.suffix not in SUFFIXES or not path.is_file():
                continue
            rel = path.relative_to(ROOT).as_posix()
            if rel in ALLOWLIST:
                continue
            scanned += 1
            code = strip_comments_and_strings(path.read_text(encoding="utf-8", errors="replace"))
            for pattern, explanation in CHECKS:
                for match in pattern.finditer(code):
                    line = code.count("\n", 0, match.start()) + 1
                    errors.append(f"{rel}:{line}: `{match.group(0)}`\n    {explanation}")

    if errors:
        print("ERROR: MSVC-only build breaks found "
              "(these compile fine under clang/glibc and fail on Windows).\n")
        print("\n\n".join(errors))
        print(f"\n{len(errors)} problem(s) across {scanned} scanned files.")
        return 1

    print(f"msvc-portability check: OK ({scanned} files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
