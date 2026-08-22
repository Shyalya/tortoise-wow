#ifndef DC_COMPAT_SPELLINFO_H
#define DC_COMPAT_SPELLINFO_H

// AzerothCore's SpellInfo is Penqle's SpellEntry.
//
// Deliberately NOT aliased: BattleGround.h forward-declares its own
// `class SpellInfo`, and a `using SpellInfo = SpellEntry` next to it is a
// conflicting declaration. The module's uses were renamed to SpellEntry
// instead; this header exists so the #include resolves.
#include "Spells/SpellMgr.h"
#include "Database/DBCStructure.h"

#endif
