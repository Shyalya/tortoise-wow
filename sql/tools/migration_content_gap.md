# The migrations table is not evidence that the content arrived

Our world database came as a complete dump and brought that dump's `migrations`
table with it. The rows describe a different database. `20260518050203_world`
sits there with a real hash and a timestamp of 2026-06-30 17:11:13 — and not
one of its 349 creature templates existed here. `20260507165648_world` appears
twice, with two different hashes.

So the table says nothing about whether a file's content is present. Compare the
content: `audit_migration_content.py`.

## What was missing on 2026-08-10

488 entries across 18 file/table pairs, and 327 spawns standing in the world
with no `creature_template` row of their own. Found because another operator
mentioned a trainer — Leyti Quicktongue, 80106, in Rustgate Ridge — that does
not exist here, although the area is in AreaTable.dbc and the SQL sits in this
tree.

## How it was closed

`20260518050203_world.sql` went in whole. It is purely additive: no spawns, no
deletes, no updates, and a check against every table it touches found a single
`pickpocketing_loot_template` entry in common and no primary key collision at
all.

The other thirteen files could not. Each inserts spawns with fixed guids, and
**9794 of those guids already belong to rows here** — upstream hands out the
same blocks more than once. These tables are MyISAM, so the first duplicate key
aborts the file and leaves everything after it unapplied, with nothing to roll
back.

Those collisions turned out to be the way through: the spawns are already here,
which is exactly why the guids clash. Only the templates behind them were
missing. `extract_missing_templates.py` lifts out the `creature_template` and
`gameobject_template` rows that are genuinely absent, keeps each file's own
column list, and leaves every guid alone. 132 creature and 7 gameobject rows;
orphaned spawns went from 327 to 0.

## Still open, and why it was left

Row counts against `sql/base/` still show shortfalls: `npc_vendor` around 6048,
`gameobject_loot_template` around 3344, `pickpocketing_loot_template` 234,
`skinning_loot_template` 142, `spell_effect_mod` and `spell_mod` 38 and 19.
Those are counts, not identities — a deficit is a strong hint, not proof.

`area_template` is the one to be careful with. The base file has 1633 areas, the
database 1481 — and `AreaTable.dbc` also has 1481. The client data is the same
age as the database, so filling the table alone would put the server ahead of
the client: areas, and eventually spells, that one side knows and the other does
not. That is the same mismatch that makes a trainer take money without the
ability appearing. Database and client data belong updated together or not at
all.

## 20260715141903_world.sql was applied by halves (found 2026-08-12)

Reported downstream as "the Crossroads innkeeper has nothing to sell". Boorand
Plainswind (3934) was fine here, but tracing it turned up that the migration
had only partly landed on this database.

The file does three things. Only the first had run:

| statement | state before | rows |
|---|---|---|
| `INSERT INTO npc_vendor` | applied | 12 per vendor |
| `UPDATE creature_template SET vendor_id = 0` | **missing** | 24 entries |
| `DELETE FROM npc_vendor_template` | **missing** | 4 templates, 37 rows |

Harmless in this direction: every affected NPC had both its own rows and the
template, so both gossip options showed wares - the same ones twice. The other
half is what hurts. A database that got the UPDATE and DELETE but not the
INSERT ends up with `vendor_id = 0` and no rows of its own, and then both
"browse your goods" (VENDOR_MENU_NORMAL, reads npc_vendor) and "browse your
seasonal fare" (VENDOR_MENU_TEMPLATE, reads npc_vendor_template) come back
empty. That is exactly the reported symptom, and it would hit all 24, not just
the one innkeeper.

Completed on 2026-08-12: 24 vendor_id cleared, 37 template rows removed.
Backup of the previous state in db_backups/vendor_migration_rest_20260812_182520.sql.gz,
which also carries the UPDATE statements needed to put the ids back.

Worth remembering: the `migrations` row for this file says applied, with hash
`manual`, dated 2026-07-24. It was right about the file having been run and
wrong about the file having finished - the same trap this document already
describes for missing content.
