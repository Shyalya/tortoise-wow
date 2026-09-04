# Penqle issue audit — 2026-09-03

## Scope and method

This audit covers every issue that was open in `Penqle/tortoise-wow` on
2026-09-03 (57 issues). The audited ManTech branch contains Penqle's current
`main` tip (`17ee04d`) and 390 additional commits, so this was not an audit of
an older upstream snapshot.

Each concrete report was checked against the current C++ implementation,
world migrations and base data. Relevant 1.18.1 client DBC records were also
checked for the appearance-token and profession reports. Static verification
can prove missing handlers, bindings, records and referential errors; it
cannot truthfully prove every encounter or intermittent concurrency report
without a reproducible runtime test. Tracker/roadmap issues with no specific
failure, coordinates, IDs, packet data or expected values are therefore
listed as validation work rather than falsely marked fixed.

## Fixed in this pass

| Issue | Verification | Resolution |
|---|---|---|
| #428 Starbreeze water barrel LOS | Spawn 49614 remained at the reported obstructed coordinate. | Move it 0.5 yards out of the wall. |
| #393 Mark of Sorcery | Item 61111 invoked the generic skin-token script but had no `custom_character_skins` row. | Add male skin 19 and female skin 18, verified from the effective 1.18.1 `CharSections.dbc`. |
| #355 Holy Strike | All ranks stored Mending Light in trigger slot 1 attached to a direct-damage effect, so the generic trigger executor never cast it. | Explicitly cast Mending Light after a successful hit and halve only the caster's heal, as the spell text requires. |
| #302 Daghelm missing | Spawn 2583278 existed; the upstream maintainer confirmed its exact coordinate caused client model culling. | Nudge the X coordinate by 0.01 without changing encounter placement. |
| #423 Shining Copper Cuffs | Spell 41335 existed but `skill_line_ability` did not. The active client `patch-9.mpq` also removed its DBC row while patch 7/8 contain it. | Restore the server association. A client DBC hotfix is still required before the craft can appear in the client profession window. |

The idempotent database changes are in
`sql/database_updates/world/20260903233000_world.sql`.

## Already resolved in the audited branch

| Issue | Evidence in current branch |
|---|---|
| #401 Who Will Think of the Children? | `20260817211652_world.sql` installs the Cutpurse Warren gossip/fight flow for quest 41636. |
| #400 Empty Houses | The same migration installs quest-credit gossip scripts for quest 41643 and its three NPCs. |
| #379 Gift of Life | Spell effect 23725 is implemented in `SpellEffects.cpp`, calculating 15% maximum health and triggering 23782. |
| #245 MySQL 8 `rank` keyword | Authentication queries quote the `rank` column; the ManTech deployment has also authenticated successfully against MySQL 8. |
| #237 Spawn NPCs | Antonas Riftgaze, Lesser Arcane Elementals and Ralthas are supplied by the August world migrations. |
| #208 World/disenchant IDs | The later `20260629195406_world.sql` restoration is present. |
| #60 Missing SpellModOps | Enum values 26, 29, 30, 31 and 32 and their handling are present in the current core. |

## Confirmed or plausible defects not safely patchable from the reports

| Issue | Audit result / next evidence required |
|---|---|
| #374 Accelerated Arcana | Confirmed TODO: cast-speed-based cooldown recovery is not implemented. This needs a defined stacking/rounding rule and targeted spell tests before changing the shared cooldown system. |
| #373 Detour SIGSEGV | The unload/query race is credible. The reporter explicitly withdrew the proposed shared-mutex patch because it deadlocks and destroys a locked mutex. Do not apply that patch; use deferred tile reclamation or another lifetime-safe design with a 1000-bot soak test. |
| #326 Moroes spawn | The reported spawn exists at the audience position, but the issue provides no authoritative stage coordinates. Capture the live/reference coordinate before moving encounter state. |
| #304 Windows long-run latency | Not reproduced consistently and has no profile, dump or metric series. Requires a Windows ETW/CPU/database/thread profile during the spike. |
| #382 Druid quest objects | Lunaclaw was independently reported working; several unclickable objects may instead come from invalid client models or mismatched extracted data. Requires exact failing spawn/template IDs from this deployment before replacing display IDs. |
| #339 transient gorilla pet | Intermittent and not reproducible; no requested `character_pet` row or cast source was provided. Instrument spell 7909/pet creation if it recurs. |
| #226 Lapidis Isle missing NPCs | The maintainer's current pass found no missing NPCs and the report supplies no names or IDs. No concrete defect to patch. |
| #89 shop data format | This is a client/protocol data-format project, not a self-contained server defect; the expected schema/protocol is absent from the report. |
| #82 quest text issues | Broad data-quality report. Fixes need per-quest expected text, including formatting tokens, to avoid replacing localized/custom dialogue blindly. |

## Planning and validation trackers (not defect reports)

These issues describe whole content areas or future verification work and do
not identify a reproducible failing row or code path. They remain useful QA
checklists, but an open tracker is not evidence that every listed encounter,
class or zone is broken.

- Raids/sub-raids: #328 Blackwing Lair, #327 Molten Core, #288 Ezzel
  Darkbrewer, #287 Broodcommander Axelus, #231 Upper Karazhan and #146
  Timbermaw Hold.
- Generic NPC validation: #241/#151 placement and movement, #240/#150 attack
  power, #239/#149 spell behavior, #238/#148 size/melee range, and #147 spawn
  work.
- Class and class-quest trackers: #190, #180, #170, #165, #157 Rogue, #155
  Paladin, #154 Mage, #153 Hunter and #152 Druid.
- Zone and quest trackers: #118, #115 Moonwhisper Coast, #111, #108 Grim
  Reaches, #103, #100 Balor, #97 and #94 Northwind.
- #77 is the umbrella NPC issue tracker and #73 is the repository roadmap.
- #253 is a WSL setup guide, not a bug report.
- #437 requests a generic native-module lifecycle API. Its companion PR #438
  removes the legacy PlayerBots path and is incompatible with this branch's
  integrated PlayerBots direction; it is a feature architecture proposal,
  not a gameplay regression.

## Runtime acceptance tests

After deploying the binary and migration, test the fixed reports directly:

1. Select pool 21506's water barrel and loot spawn 49614 from normal player
   reach.
2. Use Mark of Sorcery on both male and female High Elves and relog once to
   confirm persistence.
3. Cast every learned rank of Holy Strike on a valid hostile target with the
   caster and at least one injured/mana-depleted nearby ally; verify the
   caster receives half healing while mana and ally healing occur.
4. Enter Scarlet Monastery Armory normally and confirm Daghelm renders and is
   selectable without `.go`.
5. Shining Copper Cuffs remains a client-hotfix acceptance item until the
   missing `SkillLineAbility.dbc` row for spell 41335 is restored in a client
   patch.

The intermittent #373 and #304 reports need dedicated soak tests and cannot
be certified by a clean compile or a short login test.
