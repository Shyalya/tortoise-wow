#!/usr/bin/env python3
# Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3.
#
# Config-read guard: every DungeonClear tunable must be read through DcSettings.
#
# The core's ConfigMgr logs "> Config: Missing property X ..." on EVERY read of a
# key absent from the deployed conf. Our tunables are all optional (the registry
# holds the authoritative default) and many are read once per tick per bot, so a
# single raw sConfigMgr->GetOption("DungeonClear.X") at a hot call site floods
# Server.log with thousands of identical lines. That has now happened twice:
# first from the AI tick path (fixed by routing everything through DcSettings,
# which passes showLogs=false and caches the conf layer), then again when the
# TestRun harness landed carrying its own raw reads.
#
# The rule that prevents a third time:
#
#   1. Only Settings/DcSettings.cpp may read a "DungeonClear.*" key from
#      sConfigMgr. Everywhere else: add a row to DcSettingsRegistry.h and read it
#      with DcSettings::GetBool/GetInt/GetUInt/GetFloat. Adding a setting is a
#      registry line, not a GetOption.
#   2. Any raw GetOption that survives (see ALLOWLIST — the registry is numeric,
#      so a string-valued key has nowhere else to live) must still pass
#      showLogs=false.
#
# Calls are matched across line breaks, so a wrapped argument list is judged on
# the whole call rather than its first line.
#
# Run from anywhere:  python3 tools/check_config_reads.py

import pathlib
import re
import sys

SRC_DIR = pathlib.Path(__file__).resolve().parent.parent / "src"

# The accessor itself is the one legitimate home for raw DungeonClear.* reads.
ACCESSOR = "Settings/DcSettings.cpp"

# Keys the numeric registry cannot hold, allowed to read raw — but still bound by
# rule 2 (showLogs=false). DcType is {Bool, UInt, Int, Float} and DcSettingDef
# stores a `double defVal`, so a string-valued key has no row to move to; listing
# it here is the whole remedy, and the error text's "add a registry row" advice
# does not apply. Both entries are the TestRun driver's identity — a character
# name and the account that owns it — read once per driver resolve, not per tick.
ALLOWLIST = {
    "DungeonClear.TestRun.DriverCharacter",
    "DungeonClear.TestRun.DriverAccount",
}

CALL = re.compile(r"sConfigMgr->Get\w*\s*(?:<[^>;]*>)?\s*\(")


def calls(text):
    """Yield (offset, full-call-text) for each sConfigMgr->Get*(...) call."""
    for m in CALL.finditer(text):
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        yield m.start(), " ".join(text[m.start():i + 1].split())


def main():
    errors = []

    for path in sorted(SRC_DIR.rglob("*")):
        if path.suffix not in (".cpp", ".h") or ACCESSOR in path.as_posix():
            continue

        text = path.read_text(encoding="utf-8", errors="replace")
        for offset, call in calls(text):
            line = text.count("\n", 0, offset) + 1
            where = f"{path.relative_to(SRC_DIR.parent)}:{line}"
            key = re.search(r'"(DungeonClear\.[\w.]+)"', call)

            if key and key.group(1) not in ALLOWLIST:
                errors.append(
                    f"{where}: raw read of {key.group(1)} outside DcSettings.cpp.\n"
                    f"    Add a row to DcSettingsRegistry.h and read it with "
                    f"DcSettings::GetT() — a raw GetOption logs\n"
                    f"    'Config: Missing property' on every single call.\n"
                    f"    {call}")
            elif not re.search(r",\s*false\s*\)$", call):
                errors.append(
                    f"{where}: sConfigMgr read without showLogs=false.\n"
                    f"    Pass false as the last argument so a missing conf line "
                    f"cannot spam the log.\n"
                    f"    {call}")

    if errors:
        print("ERROR: DungeonClear config reads must go through DcSettings.\n")
        print("\n\n".join(errors))
        return 1

    print(f"config-read check: OK (no raw DungeonClear config reads under {SRC_DIR})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
