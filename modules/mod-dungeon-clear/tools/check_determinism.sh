#!/bin/bash
# Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3.
#
# Determinism guard for the decision cores (Tier 1 of the headless-sim plan).
#
# The capture->replay net only works if DC decisions are pure, deterministic
# functions of their observation: a replayed fixture must decide identically
# forever. RNG in the decision path would make a captured verdict unreproducible.
# This fails the build if any randomness primitive appears under the DC source
# tree, so a future change can't silently make a decision non-replayable.
#
# Run from the module root:  bash tools/check_determinism.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/../src"

# Randomness primitives that must not appear in the decision path. urand/irand/
# frand/rand32/rand_chance/roll_chance are the project helpers; std::rand and
# <random> are the stdlib ones. (Comments/strings are not excluded — keep the
# pattern out of the code entirely; if a legitimate mention is ever needed, gate
# it behind a reviewed allowlist rather than loosening this.)
PATTERN='\b(urand|irand|frand|rand32|rand_chance|roll_chance_[fi]|std::rand)\b|#include[[:space:]]*<random>'

# Reviewed allowlist: src/TestRun is the test-run harness, not the decision
# path. Its only live RNG use is rolling a run seed (recorded in the run record
# for exact replay via `.dc test start <d> seed=N`), which preserves
# replayability; comp selection itself uses a pure seeded PRNG.
EXCLUDES=(--exclude-dir=TestRun)

if hits=$(grep -rEn "${PATTERN}" "${SRC_DIR}" --include='*.cpp' --include='*.h' "${EXCLUDES[@]}" 2>/dev/null); then
    echo "ERROR: randomness primitive found in the DungeonClear source tree."
    echo "       Decisions must stay deterministic so capture->replay fixtures hold."
    echo "${hits}"
    exit 1
fi

echo "determinism check: OK (no RNG primitives under ${SRC_DIR}, TestRun exempt)"
