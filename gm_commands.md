# Turtle-WoW — Game Master Command Manual

> **Who this is for:** the `sam` account, GM rank **4 (`SEC_ADMINISTRATOR`)** — the highest in-game rank. Every command in this manual is available to you.
> **How commands work:** type a dot followed by the command in the chat box (`.tele Karazhan`). Some clients use `!` instead of `.`. Arguments go after the command, separated by spaces. `<angle brackets>` mean "put a value here"; `[square brackets]` mean "optional".
> **Every command here was read from `src/game/Chat/Chat.cpp` and the PlayerBots source** — not guessed, not from a wiki.
> **Need more detail on any command?** Type `.help <command>` in-game (e.g. `.help tele`) and the server prints the exact syntax for your rank.

---

## Table of Contents

1. [Understand your rank](#1-understand-your-rank)
2. [Movement & navigation](#2-movement--navigation) — get anywhere instantly
3. [Your character](#3-your-character) — items, levels, spells, stats
4. [Spawning & world editing](#4-spawning--world-editing) — NPCs and objects
5. [Combat, casting & debugging](#5-combat-casting--debugging)
6. [Quests, instances & lookups](#6-quests-instances--lookups)
7. [Server & player administration](#7-server--player-administration)
8. [PlayerBots: `.bot` / `.rndbot` / `.ahbot`](#8-playerbots-bot--rndbot--ahbot)
9. [Turtle-WoW custom commands](#9-turtle-wow-custom-commands)
10. [Teleport reference](#10-teleport-reference) — every location token
11. [Source & verification](#11-source--verification)

---

## 1. Understand your rank

GM commands are gated by rank. You are **rank 4 (Administrator)**, so you can run everything listed here. If a command ever seems to "do nothing," it usually means it needs a higher rank than you have — but at rank 4 that only applies to two console-only commands (`.quit`, `.crash`), which you run from the server console, not in chat.

| Rank | Name | What it can do |
| ------ | ------ | ---------------- |
| 0 | Player | Normal play |
| 1 | Observer | Teleport, go, appear |
| 2 | Moderator | Kick, ban, summon players |
| 3 | Developer | Most item / spawn / modify tools |
| **4** | **Administrator (you)** | **Every in-game command** |
| 5–6 | Internal / Console | Server-internal only |

---

## 2. Movement & navigation

*These move you, move other players, or jump to any saved spot. You'll use them constantly while testing content. Every command below was traced in `Commands.cpp` (`HandleTeleCommand`, `HandleGoCommand`, etc.) so the syntax is exact.*

### `.tele <name>` — Warp to a saved location

**What it actually does:** Looks up `<name>` in the `game_tele` table (the list of saved waypoints). If it finds an exact match, it teleports **you** there instantly. It also understands a shift-click link like `|color|Htele:id|h[name]|h|r` pasted from a chat frame. If the name doesn't exist, it prints "teleport location not found" and does nothing.
**How to use it:**

- `.tele Karazhan` → you appear at the Karazhan entrance (map 814). Tokens are **case-sensitive** — `Karazhan` works; `karazhan` may not.
- `.tele name karazhan` → lists every saved location whose name contains "karazhan" (e.g. `Karazhan`, `karazhaninstance`). Use this when you're unsure of the exact token.
- If you get "not found," the spot has no token — use `.go xyz` instead (below).
- Full token list: see §10.

### `.go <x> <y> <z> [map]` — Teleport by raw coordinates

**What it actually does:** Reads three floats (X, Y, Z) and an optional map ID. If you omit the map, it uses your current map. It then teleports **you** to those exact coordinates. This is how you reach places with no `.tele` token (e.g. Stormwrought Ruins, map 818). It also accepts a shift-click `Hplayer`/`Hcreature` link instead of numbers.
**How to use it:**

- `.go 1234.5 -567.2 88.0 818` → teleport to those coordinates on map 818 (Stormwrought Ruins).
- `.go 1234 -567 88` → same coordinates but on whatever map you're already on.
- Where do you get the numbers? Stand somewhere and run `.gps` (§5) — it prints your current X/Y/Z/map. Copy them into `.go`.

### `.go corpse` — Return to your dead body

**What it actually does:** Only works if you are **dead** and have a corpse. Teleports you to your corpse's location so you can resurrect there.
**How to use it:**

- Die, then `.go corpse` → you're back at your body.

### `.go graveyard <id>` — Teleport to a graveyard

**What it actually does:** Looks up `<id>` in the `WorldSafeLocs` table (graveyard definitions) and teleports you there. Errors if the ID doesn't exist or has null coordinates.
**How to use it:**

- `.go graveyard 0` → teleport to graveyard entry 0 (if defined).

### `.appear <player>` / `.goname <player>` — Go to a player

**What it actually does:** Teleports **you** to the named player's current position. Both spellings do the same thing.
**How to use it:**

- `.appear Sam` → you warp to wherever Sam is standing.

### `.summon <player>` — Pull a player to you

**What it actually does:** The reverse of `.appear` — the named player teleports to **your** location. Useful for rescuing someone who is stuck or gathering a group.
**How to use it:**

- `.summon Newbie` → Newbie appears next to you.

### `.groupgo` / `.groupsummon` — Move your whole group

**What it actually does:** Teleports your entire group. `.groupgo` moves the group to you; `.groupsummon` summons them. Use it when relocating a party between test areas.
**How to use it:**

- Stand where you want the group, then `.groupgo`.

### `.cometome` — Drag your target to you

**What it actually does:** Pulls your **currently selected** creature or player to your position. Unlike `.summon`, this works on NPCs/creatures you have targeted, not by name.
**How to use it:**

- Target a creature, then `.cometome` → that creature walks/teleports to you.

---

## 3. Your character

*Everything for giving yourself items, levels, spells, and editing stats. These were traced in `Commands.cpp` — note which ones need a target selected vs. act on you directly, because that's the #1 reason a command "does nothing."*

### `.additem <id or "name"> [count]` — Give an item

**What it actually does:** Adds an item to your **selected player** (or you, if no one is selected). You can pass an item **entry ID**, a shift-click `Hitem` link, or a **literal item name in quotes** (it queries `item_template WHERE name = '...'`). Optional `[count]` defaults to 1. A **negative count** removes that many instead of adding. Items are placed in the target's bags; if there's no room, it tells you how many didn't fit (it does NOT delete the overflow).
**How to use it:**

- `.additem 23059` → adds 1 Ring of the Dreadnaught (entry 23059).
- `.additem "Corrupted Ashbringer"` → adds it by exact name (quotes required for multi-word names).
- `.additem 17031 5` → adds 5 of that item.
- `.additem 23059 -1` → removes 1 of that ring from the target.
- If you see "item not found," the name/ID is wrong — use `.lookup item` (below) to find the real ID.

### `.additemset <id>` — Give a full set

**What it actually does:** Adds every piece of the item set with the given `set_id` to your selected player. Faster than adding pieces one by one when you know the set ID.
**How to use it:**

- `.additemset 361` → adds all pieces of that set (find set IDs via the `item_template.set_id` column or `tier_commands.md`).

### `.deleteitem <id> [count]` — Remove an item

**What it actually does:** Removes `<count>` (default 1) of an item from your selected player's inventory (bags and bank).
**How to use it:**

- `.deleteitem 23059` → removes 1 of that ring.

### `.levelup <n>` — Gain levels

**What it actually does:** Raises the target's level by `n` (default 1). If you have a **creature selected**, it levels the creature instead. You can also name a player: `.levelup <name> <n>` or `.levelup <n> <name>`. Clamped between 1 and the server's max level.
**How to use it:**

- `.levelup` → +1 level (acts on you / selected player).
- `.levelup 10` → +10 levels.
- `.levelup Bob 5` → Bob gains 5 levels.

### `.learn <spellid> [all] [force]` — Learn a spell

**What it actually does:** Learns a spell (by ID or shift-click `Hspell` link) for your **selected player** (errors "player not found" if nothing selected — so select yourself or a target first). `all` learns the highest rank of that spell line. `force` is needed to learn a "learn-type" spell (like a class trainer spell) on another player.
**How to use it:**

- Select yourself (or a target), then `.learn 100` → learn spell 100.
- `.learn 100 all` → learn every rank of spell 100.
- Find spell IDs with `.lookup spell <name>`.

### `.learn all_myspells` — Learn every class spell

**What it actually does:** Learns **all** spells belonging to your own class/family (every trainable ability for your class). Acts on **you**, not a selected target.
**How to use it:**

- `.learn all_myspells` → your spellbook is now full of every class spell.

### `.learn all_recipes` — Learn all profession recipes

**What it actually does:** Learns every recipe for every profession you know. Acts on you.
**How to use it:**

- `.learn all_recipes` → you can craft everything.

### `.maxskill` — Max out professions

**What it actually does:** Sets every profession/skill on your **selected player** to its maximum value for their level. Errors "no char selected" if nothing is selected — select yourself first.
**How to use it:**

- Select yourself, then `.maxskill` → all your skills jump to max.

### `.setskill <id> <value>` — Set one skill

**What it actually does:** Sets a single skill (by `Hskill` link or ID) to `<value>` on your selected player. Errors if nothing selected.
**How to use it:**

- Select yourself, then `.setskill 164 300` → Blacksmithing set to 300.

### `.modify money <amount>` — Set gold

**What it actually does:** Adds (or subtracts, if negative) copper to your **selected player's** money. Errors "no char selected" if nothing selected — so to give yourself gold, **select yourself first** (e.g. click your portrait, or `.pinfo` won't help; just target yourself).
**How to use it:**

- Select yourself, then `.modify money 1000000` → +1,000,000 copper = 100 gold.
- `.modify money -50000` → removes 5 gold from the selected player.

### `.modify hp <n>` / `.modify mana <n>` / `.modify rage <n>` / `.modify energy <n>` — Set a resource

**What it actually does:** Sets the named resource to `<n>` on your selected player. (Traced: `HandleModifyHPCommand` etc. all act on the selected player.)
**How to use it:**

- Select yourself, then `.modify hp 100000` → your max HP becomes 100,000.

### `.modify xp <rate or value>` — Adjust XP

**What it actually does:** Modifies the target's experience. Exact behavior depends on args (rate vs. flat value) — traced in `HandleModifyXpCommand`.
**How to use it:**

- `.modify xp 2` → double XP gain (or set, depending on form).

### `.modify rep <faction> <value>` — Set reputation

**What it actually does:** Sets your selected player's standing with `<faction>` (faction ID) to `<value>`.
**How to use it:**

- `.modify rep 72 42000` → set rep with faction 72 (Stormwind) to Exalted (42000).

### `.modify morph <displayid>` — Change your model

**What it actually does:** Temporarily turns your character into the creature model with `<displayid>`. Fun for testing, or just to look like a dragon. Reverts on relog/`.demorph`.
**How to use it:**

- `.modify morph 15335` → you look like that creature.

### `.modify scale <n>` — Resize model

**What it actually does:** Sets your model scale to `<n>` (1 = normal, 2 = double, 0.5 = half).
**How to use it:**

- `.modify scale 2` → you become twice as tall.

### `.modify gender` — Switch gender

**What it actually does:** Flips your character's gender.

### `.modify stat str/agi/sta/int/spi <n>` — Set primary stats

**What it actually does:** Sets the named base stat to `<n>` on your selected player.
**How to use it:**

- `.modify stat sta 500` → stamina set to 500.

### `.modify ap <n>` / `.modify spellpower <n>` — Set power stats

**What it actually does:** Sets attack power or spell power to `<n>`.

### `.modify parry <n>` / `.modify honor <n>` — Set parry / honor

**What it actually does:** Sets parry rating or honor points to `<n>`.

### `.modify title <id>` — Grant a title

**What it actually does:** Unlocks the title with `<id>` for your selected player.
**How to use it:**

- `.modify title 12` → grants that title.

### `.modify drunk <n>` — Set drunk state

**What it actually does:** Sets your drunk level 0–100 (clamped). 0 = sober.
**How to use it:**

- `.modify drunk 100` → maximally drunk (screen wobbles).

### `.modify faction <id>` — Change faction

**What it actually does:** Changes your selected player's faction to `<id>`.

### `.start` — Reset to fresh character

**What it actually does:** Resets your character to its starting state (like a brand-new character of your race/class — strips gear/progress to defaults).

### `.revive` — Revive at corpse

**What it actually does:** If you're dead, revives you at your corpse.

### `.die` — Kill yourself

**What it actually does:** Kills your character (useful for testing death/respawn or to get back to a spirit healer).

### `.god` — God mode

**What it actually does:** Toggles god mode — you take no damage. Great for exploring dangerous content.

### `.bank` / `.mailbox` — Open bank / mailbox anywhere

**What it actually does:** Opens your bank or mailbox interface wherever you stand, so you don't run to a city.

### `.mount [id]` — Summon a mount

**What it actually does:** Mounts you (optionally a specific display ID).

### `.unstuck <player>` — Free a stuck player

**What it actually does:** Teleports the named player out of geometry / under the world to a safe spot.
**How to use it:**

- `.unstuck Sam` → Sam is unstuck.

### `.rename <player>` — Rename a character

**What it actually does:** Renames the named character (also clears the "renamed" flag so it can be renamed again).
**How to use it:**

- `.rename Sam Samantha` → Sam is now Samantha.

### `.repairitems` — Free repair

**What it actually does:** Repairs all your equipped gear for free.

### `.taxicheat` / `.explorecheat` — Unlock flight paths / map

**What it actually does:** `.taxicheat` unlocks all taxi/flight paths for you; `.explorecheat` reveals the entire world map. Useful when testing content across zones.
**How to use it:**

- `.explorecheat` → whole map revealed.

### `.waterwalk` / `.hover` — Walk on water / float

**What it actually does:** `.waterwalk` lets you walk on water; `.hover` lets you float in the air. Handy for reaching unreachable spots.

---

## 4. Spawning & world editing

*Place NPCs and objects in the world for testing, or inspect what's around you. Traced from `HandleNpcAddCommand`, `HandleNpcDeleteCommand`, `HandleGameObjectAddCommand`, etc. The big gotcha: `.npc add` and `.gobject add` **save to the database permanently** — they are not temp spawns. Use `.npc summon` for a temporary one.*

### `.npc add <entry>` — Spawn a creature (permanent)

**What it actually does:** Spawns a creature of `<entry>` at **your exact position and facing**, then **saves it to the database** as a static spawn (it persists after reload/server restart). It auto-assigns a reserved low-GUID and a 5-minute respawn. A few protected NPCs are blocked (e.g. Highlord Taelan Fordring, entry 1842).
**How to use it:**

- `.npc add 15632` → a creature of entry 15632 now stands where you are, saved to DB.
- Find creature entries with `.lookup creature <name>`.

### `.npc summon <entry>` — Spawn a creature (temporary)

**What it actually does:** Identical to `.npc add` but **temporary** — the creature is NOT saved to the database and disappears on reload. Use this for quick tests so you don't litter the DB with spawns.
**How to use it:**

- `.npc summon 15632` → temp creature at your position.

### `.npc delete` — Remove a creature

**What it actually does:** Deletes your **currently targeted creature** (or one specified by a shift-click `Hcreature` GUID link). For a normal creature it removes it from the world AND **deletes it from the database**. Protected NPCs (1842) are blocked.
**How to use it:**

- Target a creature, then `.npc delete` → it's gone (and removed from DB if it was a static spawn).

### `.npc info` — Inspect a creature

**What it actually does:** Prints detailed data about your **selected creature**: entry, level, health, flags, faction, position. Use this to inspect a mob you're testing.
**How to use it:**

- Target a creature, then `.npc info` → full stats dump.

### `.npc near` — List nearby creatures

**What it actually does:** Lists all creatures within a radius of you (with entries and distances).
**How to use it:**

- `.npc near` → see what's around you.

### `.npc say <text>` / `.npc yell <text>` / `.npc textemote <text>`

**What it actually does:** Makes your **selected creature** say / yell / text-emote your `<text>` (in Universal language). Requires a selected creature — errors "select a creature" otherwise.
**How to use it:**

- Target a creature, then `.npc say Hello there` → that creature says "Hello there" in chat.

### `.npc evade` — Reset a creature

**What it actually does:** Forces your selected creature out of combat and back to its spawn point (clears its victim/aggro).
**How to use it:**

- Target a creature in combat, then `.npc evade` → it resets.

### `.npc follow` / `.npc unfollow` — Make a creature follow you

**What it actually does:** Your selected creature starts following you (or stops).
**How to use it:**

- Target a creature, then `.npc follow` → it trails you.

### `.npc flag <flags>` / `.npc factionid <id>`

**What it actually does:** Edits the **selected creature's** behavior flags (e.g. vendor, trainer, gossip) or faction ID. Flags are bitmask values from the creature_flags table.
**How to use it:**

- `.npc factionid 35` → set the targeted creature's faction to 35.

### `.npc template set <field> <value>` — Edit the creature template (persists)

**What it actually does:** Changes the creature **template** so the edit applies to **every future spawn** of that entry (not just this one). Fields: `level`, `faction`, `scale`, `detectionrange`, `leashrange`, `callforhelprange`.
**How to use it:**

- Target a creature, then `.npc template set level 70` → every spawn of this creature is now level 70.

### `.npc spawn set <field> <value>` — Edit this specific spawn

**What it actually does:** Edits properties of **this spawn instance** only: `displayid`, `standstate`, `movetype`, `wanderdistance`, `respawntime`, `auras`, `emotestate`.
**How to use it:**

- `.npc spawn set scale 2` → this spawned creature is now twice as big.

### `.gobject add <entry> [respawntime]` — Place an object (permanent)

**What it actually does:** Places a game object (chest, door, quest object) at **your position and facing**, saves it to the DB. Optional second arg sets respawn time in seconds (otherwise defaults to 5 minutes).
**How to use it:**

- `.gobject add 190345` → that object now sits where you stand, saved to DB.
- `.gobject add 190345 60` → same, with a 60-second respawn.

### `.gobject tmpadd <entry>` — Place an object (temporary)

**What it actually does:** Like `.gobject add` but **not saved to DB** — disappears on reload.
**How to use it:**

- `.gobject tmpadd 190345` → temp object.

### `.gobject delete` / `.gobject despawn` — Remove an object

**What it actually does:** Deletes/despawns your **targeted object** (and removes it from DB if static).
**How to use it:**

- Target an object, then `.gobject delete`.

### `.gobject move` / `.rotate` / `.turn` / `.scale` — Reposition an object

**What it actually does:** Moves, rotates, or resizes your **targeted object** to your current position/facing (or sets scale). Use these to place objects precisely.
**How to use it:**

- Stand where you want it, target the object, then `.gobject move` → it jumps to you.

### `.gobject set gostate <v>` / `.set lootstate <v>` / `.set respawntime <v>`

**What it actually does:** Toggles the targeted object's open/closed state, loot state, or respawn timer.
**How to use it:**

- `.gobject set gostate 1` → opens the targeted object (e.g. a door).

### `.gobject info` / `.gobject near`

**What it actually does:** `.gobject info` dumps data on your targeted object; `.gobject near` lists objects around you.
**How to use it:**

- Target an object, then `.gobject info`.

---

## 5. Combat, casting & debugging

*Tools for fighting, forcing spells, and inspecting what's happening. Traced from `HandleCastCommand`, `HandleAuraCommand`, `HandleGPSCommand`, `HandleGUIDCommand`, `HandleGetDistanceCommand`, etc.*

### `.cast <spellid> [triggered]` — Cast a spell at your target

**What it actually does:** Casts `<spellid>` (by ID or shift-click `Hspell` link) at your **currently selected unit** (player or creature). If you're below rank 4 (Administrator), the spell can only target **you** — higher ranks can cast on others. Optional `triggered` flag casts it without mana/cast-time rules. Errors "select a character or creature" if nothing is selected.
**How to use it:**

- Select a target, then `.cast 100` → spell 100 fires at your target.
- Find spell IDs with `.lookup spell <name>`.

### `.cast self <spellid>` / `.cast target <spellid>` — Cast on self / on your target's victim

**What it actually does:** `cast self` casts on you; `cast target` makes your **selected creature** cast the spell at *its* current victim. Useful for scripting NPC behavior.
**How to use it:**

- Select a creature, then `.cast target 5` → that creature casts spell 5 on whatever it's fighting.

### `.castcustom <id> <bp0> <bp1> ...` — Cast with custom values

**What it actually does:** Casts a spell but overrides its effect magnitudes (the `bp` base-points) with values you supply. Advanced — for tuning spell numbers.

### `.aura <spellid> [duration]` — Apply an aura

**What it actually does:** Applies the spell as an aura (buff/debuff) to your **selected unit**. Requires rank ≥ DEVELOPER to apply to other players. Optional `duration` in seconds.
**How to use it:**

- Select a target, then `.aura 25898` → applies that aura.
- `.aura 25898 60` → same, lasts 60 seconds.

### `.unaura [all]` — Remove auras

**What it actually does:** Removes auras from your selected unit. `.unaura all` strips **every** aura.
**How to use it:**

- Select a target, then `.unaura all` → clears all its buffs/debuffs.

### `.cooldown` — Clear your cooldowns

**What it actually does:** Resets all your spell cooldowns to ready, instantly.

### `.damage <n>` / `.aoedamage <n>` — Deal damage

**What it actually does:** `.damage` deals `<n>` damage to your selected target; `.aoedamage` deals `<n>` to everything around you.
**How to use it:**

- Select a creature, then `.damage 5000` → it takes 5000 damage.

### `.fear <n>` — Make target flee

**What it actually does:** Forces your selected target to flee for `<n>` seconds.

### `.gps` — Print your position (for `.go`)

**What it actually does:** Prints the position of your **selected unit** (or a shift-click linked target): map ID, X, Y, Z, zone/area name, grid coordinates, and ground/floor height. This is where you get the numbers to feed into `.go xyz`.
**How to use it:**

- Select yourself or a creature, then `.gps` → copy the `Map:` / `X:` / `Y:` / `Z:` values into `.go x y z map`.

### `.guid` — Print selected object's GUID

**What it actually does:** Prints the GUID (unique object ID) of whatever you have **selected**. Errors "no selection" if nothing selected. Useful when scripting or linking.
**How to use it:**

- Select something, then `.guid` → its GUID string appears.

### `.distance` — Distance to target

**What it actually does:** Shows the distance between you and your selected unit.
**How to use it:**

- Select a creature, then `.distance` → e.g. "Distance: 12.34 yards".

### `.los` — Line-of-sight check

**What it actually does:** Checks whether there is a clear line of sight between you and your target (whether a spell could physically reach). Reports yes/no.
**How to use it:**

- Select a target, then `.los` → tells you if LoS is clear.

### `.list auras` / `.list threat` — Inspect state

**What it actually does:** `.list auras` lists all auras on your selected unit; `.list threat` shows the threat table of your selected creature (who it's mad at and how much).
**How to use it:**

- Target a creature in combat, then `.list threat` → see the threat list.

### `.mmaps path` / `.mmaps near` — Visualize pathfinding

**What it actually does:** Draws the path a creature would walk (requires mmaps generated for that map). `path` shows the route to your target; `near` shows paths of nearby creatures.
**How to use it:**

- `.mmaps path` → see the navigation path drawn in the world.

### `.unit statinfo` / `.unit speedinfo` — Dump unit stats

**What it actually does:** `statinfo` dumps your selected unit's stat block; `speedinfo` dumps its movement speeds (walk, run, swim, flight).
**How to use it:**

- Select a creature, then `.unit statinfo`.

### `.debug <sub>` — Deep inspection toolkit

**What it actually does:** A family of debug sub-commands. Traced entries: `condition` (evaluate a game condition), `instancedata` (dump instance script state), `packetstats` (packet counters), `leakreport`, `coeffs` (spell coefficients), `los` (LoS check), `fields` (show raw object fields). Type `.help debug` for the full current list.
**How to use it:**

- `.debug condition` → evaluate the condition system.
- `.debug instancedata` → dump the current instance's script variables.

### `.debugloot` / `.loothelper` — Inspect loot

**What it actually does:** Shows what your selected creature or object can drop (loot table), or helps you browse loot.
**How to use it:**

- Target a creature, then `.debugloot` → see its drops.

---

## 6. Quests, instances & lookups

*Test quest chains and instances without playing them, and convert names to IDs. Traced from `HandleQuestAddCommand`, `HandleQuestCompleteCommand`, `HandleLookupItemCommand`, `HandleLookupSpellCommand`, `HandleInstanceListBindsCommand`, etc.*

### `.quest add <id>` — Add a quest

**What it actually does:** Adds quest `<id>` (by ID or shift-click `Hquest` link) to your **selected player** (or named target). If the quest is started by an item, it refuses until you have that item. If the player can already complete it, it auto-completes.
**How to use it:**

- Select a player, then `.quest add 8123` → that quest is added to them.
- Find quest IDs with `.lookup quest <name>`.

### `.quest complete <id>` — Force-complete a quest

**What it actually does:** Force-completes quest `<id>` for your selected player — but **only if they already have it** (errors "quest not found" if they don't). Runs the full completion (rewards, script).
**How to use it:**

- Select a player who has the quest, then `.quest complete 8123` → it's done and rewarded.

### `.quest remove <id>` — Remove a quest

**What it actually does:** Removes quest `<id>` from your selected player's log.
**How to use it:**

- Select a player, then `.quest remove 8123`.

### `.quest test <id>` — Run a quest's completion script

**What it actually does:** Runs the quest's end script (reward/event) as if turned in, for testing — without requiring the player to have it.
**How to use it:**

- `.quest test 8123` → fires that quest's completion script.

### `.quest status` — Show a player's quests

**What it actually does:** Lists the quest log of your **selected player** (what they have, progress, status).
**How to use it:**

- Select a player, then `.quest status`.

### `.lookup item <name>` — Find an item ID

**What it actually does:** Searches `item_template` for items whose name **contains** `<name>` (case-insensitive substring). Prints a clickable list with entry IDs. This is how you turn a name into an ID for `.additem`.
**How to use it:**

- `.lookup item ashbringer` → lists "Corrupted Ashbringer" and any other matches with their IDs.
- `.lookup item "corrupted ashbringer"` → exact-ish match.

### `.lookup spell <name>` — Find a spell ID

**What it actually does:** Substring search against `Spell.dbc` names. Prints matching spells with IDs.
**How to use it:**

- `.lookup spell flash heal` → lists Flash Heal ranks with IDs.

### `.lookup creature <name>` / `.lookup object <name>` — Find NPC/object IDs

**What it actually does:** Substring search for creature or game-object entries. Use these to find the `<entry>` for `.npc add` / `.gobject add`.
**How to use it:**

- `.lookup creature onyxia` → find Onyxia's creature entry.

### `.lookup quest <name>` / `.lookup player <name>` — Find quest / player

**What it actually does:** `.lookup quest` substring-searches quests; `.lookup player` finds a player by name (also `name`/`ip`/`account`/`email`/`character` sub-variants).
**How to use it:**

- `.lookup player Sam` → find the account/character "Sam".

### `.instance listbinds` — Show your instance locks

**What it actually does:** Lists the instances your selected player (or you) is bound to: map ID, instance ID, whether it's a permanent bind, whether it can be reset, and time-to-reset.
**How to use it:**

- `.instance listbinds` → see all your saved instances and lock timers.

### `.instance unbind` / `.instance groupunbind` — Clear locks

**What it actually does:** Clears your (or your group's) instance bindings so you can re-enter fresh.
**How to use it:**

- `.instance unbind` → your instance locks are cleared.

### `.reset quests` / `.reset talents` / `.reset stats` / `.reset instances` / `.reset raids`

**What it actually does:** Resets progression on your selected player (or you):

- `.reset quests` → erases completed-quest flags.
- `.reset talents` → unlearns talents (refunds points).
- `.reset stats` → resets allocated stats.
- `.reset instances` / `.reset raids` → clears instance/raid lockouts.
**How to use it:**
- Select a player, then `.reset talents`.

---

## 7. Server & player administration

*Use these to manage players, accounts, and the server itself. Most are self-explanatory by name.*

### `.account create <user> <password>`

Creates a new game account.

- Example: `.account create tester test123`.

### `.account set gmlevel <user> <rank>`

Changes another account's GM rank (0–4).

- Example: `.account set gmlevel tester 3` — make them a Developer.

### `.account set password <user> <newpass>`

Resets an account's password.

### `.ban account <name> [reason]` / `.ban character <name>` / `.ban ip <ip>`

Bans a player by account, character, or IP. Add a reason so it's logged.

- Example: `.ban character Griefer "harassment"`.

### `.unban account <name>` / `.baninfo account <name>` / `.banlist account`

Removes a ban, shows ban details, or lists current bans.

### `.kick <player>` / `.mute <player>` / `.unmute <player>`

Removes a player from the server, or silences them in chat.

### `.freeze <player>` / `.unfreeze <player>`

Locks a player in place (or releases them) — for dealing with someone while you investigate.

### `.pinfo <player>`

Full dossier on a player: level, race, class, IP, account, playtime, bans.

### `.ticket list` / `.ticket assign <id>` / `.ticket close <id>`

Manage GM help tickets from players.

### `.guild create <name>` / `.guild invite <player>` / `.guild rank <player> <rank>` / `.guild delete <name>`

Basic guild management from chat.

### `.send items <player> <subject> "message"` / `.send mail <player> ...`

Sends items or mail to a player (e.g. to deliver a reward or a test item).

### `.reload <table>`

Hot-reloads a database table without restarting the server. After you edit content in the DB, run the matching reload to see it live. Common ones:

- `.reload creature_template` — creature definitions.
- `.reload item_template` — items.
- `.reload quest_template` — quests.
- `.reload game_tele` — teleport locations (run this after adding a new `.tele` spot).
- `.reload config` — live config values.
- Type `.help reload` for the full list (~110 tables).

### `.server info` / `.server shutdown <seconds>` / `.server restart <seconds>`

Server status, or scheduled shutdown/restart. (`.quit` / `.crash` are console-only.)

### `.instance <sub>`

Instance management: `.instance stats` shows instance diagnostics; lockout control as above.

### `.bg <sub>`

Battleground control (start a BG, check status). Rank 4.

---

## 8. PlayerBots: `.bot` / `.rndbot` / `.ahbot`

*PlayerBots are AI-controlled characters that can join your group, run dungeons with you, and populate the world. `.bot` controls a specific named bot; `.rndbot` controls the random bot population (~1000 auto-spawn at startup). Both are rank 0, so you can use them on a normal character too.*

### Creating and managing bots

**`.bot group`** — instantly spawns 4 complimentary-class bots at your level, ready to join your group. This is the fastest way to get a full party for testing dungeons.

**`.bot create level=<n> class=<c> race=<r>`** — creates a custom bot.

- Example: `.bot create level=60 class=mage race=human`.

**`.bot add <name>`** — adds an existing bot to your group.

- Example: `.bot add Dunpriest`.

**`.bot remove <name>`** (also `logout` / `rm`) — removes a bot from your group.
**`.bot delete <name>`** — permanently deletes a bot.
**`.bot gear <name>`** (also `equip`) — makes the bot equip its best available gear.
**`.bot train <name>`** — trains the bot's spells.
**`.bot enchants <name>`** — applies enchants to the bot's gear.
**`.bot prepare <name>`** (also `prep`) — does gear + train + enchants + consumables in one step. Run this after creating a bot so it's actually useful.
**`.bot summon <name>`** (also `come` / `recall`) — summons the bot to you.
**`.bot levelup <name>`** — levels the bot up.
**`.bot list`** — lists active bots.
**`.bot help <verb>`** — help for any subcommand.

### Bot behavior (the important part)

Bots run on a **strategy engine** driven by tokens you send them. You send a token via party/raid/guild chat, or through `.bot c <name> <token>`. Tokens set how the bot behaves. You combine them to define a role.

- **Posture / movement**

- `follow` — follow you (default).
- `wander` — wander instead of following.
- `stay` — hold position.
- `guard` — guard the current spot.
- `free` — release from guard/stay.
- `mount` — mount up.

- **Role / spec**

- `tank` — tank spec.
- `heal` — healer spec.
- `dps` — damage spec.
- `aoe` — AOE variant.
- `buff` — buffing variant.
- `cc` — crowd-control variant.
- `melee` / `ranged` / `caster` — attack style.

- **Combat control**

- `co + tank` — in combat, act as tank.
- `co - heal` — in combat, heal (the `-` means "instead of default").
- `react + cc` — on reacting to threats, crowd-control.
- `passive` — safe mode: only `co`, `follow`, `stay` allowed (bot won't pull).
- `attack rti star` — attack the star-marked target.
- `pull` / `flee` / `loot` / `kite` — engage / retreat / loot / kite.

- **Pet & group**

- `pet aggressive` / `pet defensive` / `pet passive` / `pet follow` / `pet attack` / `pet dismiss` / `pet call`.
- `accept invitation` / `give leader` / `join` / `leave` / `lfg`.
- `quests` / `quests all` — participate in quests.

- **Examples**

```bash
.bot c Dunpriest co + heal        # Dunpriest heals in combat
.bot c GroupTank co + tank        # tank holds aggro
.bot c Dunmage co - aoe           # mage uses AOE
.bot p attack rti star            # whole group attacks star target
.bot c Dunsupport passive         # support bot goes passive (won't pull)
```

### `.rndbot`

Same verbs as `.bot`, but targets the random bot pool. Extra console verbs: `init`, `upgrade`, `refresh`, `teleport`, `rpg`, `revive`, `grind`, `change_strategy`, `reset`, `stats`, `update`, `pid`, `diff`, `clean map`, `login debug`.

### `.ahbot <sub>`

The auction-house bot (only active if enabled in `ahbot.conf`).

- `.ahbot stats` — summary of auctions per house.
- `.ahbot update` — force a refresh.
- `.ahbot expire` — clear all AhBot auctions.
- `.ahbot <itemId>` — show buy/sell/market price for an item.

---

## 9. Turtle-WoW custom commands

*These are Turtle-WoW-specific additions beyond stock MaNGOS. They cover the in-game shop, creature pathing, the Discord bridge, performance profiling, and utilities.*

- **`.shop log` / `.shop refund`** — view shop transaction logs, or refund a shop purchase.
- **`.path setup` / `.path add` / `.path start`** — a creature-path editor for setting patrol routes.
- **`.discbot stop`** — stops the Discord integration bot.
- **`.anonymous whispers` / `.anonymous mail`** — send a GM whisper or mail without revealing your identity.
- **`.perf enable` / `.perf intervalreport` / `.perf resources` / `.perf cpu` / `.perf memory`** — performance profiling of the server.
- **`.perfmon`** — toggles a live performance monitor.
- **`.blacklist name <name>`** — adds a name to the reserved/blacklisted name list.
- **`.minchatlevel <n>`** — sets the minimum level required to use chat.
- **`.forcejoinchannel <player> <channel>`** — forces a player into a chat channel.
- **`.toggleinferno` / `.toggletrainee` / `.toggleillusions`** — toggle various event/cosmetic states.
- **`.translate <text>`** — translation helper.
- **`.blockegg` / `.cleaninventory` / `.showborders` / `.tmogdelete`** — misc utilities (block easter-egg, clean bags, show zone borders, delete transmog).

---

## 10. Teleport reference

*Every token below is a saved location you can `.tele` to. Tokens are case-sensitive. These were read from the live `game_tele` table (647 entries) and cross-checked against `map_template`.*

### Custom dungeons (Turtle-WoW originals)

| Token | Place | Map |
| ------- | ------- | ----- |
| `stormwindvault` / `stormwindvaultinstanced` | Stormwind Vault | 35 |
| `karazhancrypt` / `karazhancrypts` / `instancedkarazhancrypt` | Karazhan Crypt | 800 |
| `Karazhan` / `karazhaninstance` / `alphakarazhan` | Karazhan (entry) | 814 |
| `crescentgrove` | Crescent Grove | 802 |
| `frostmanehollow` / `frostmane` | Frostmane Hollow | 822 |
| `hateforgeinstance` | Hateforge Quarry | 808 |
| `gilneascity` / `gilneas` | Gilneas City | 815 |
| `dragonmawretreat` | Dragonmaw Retreat | 816 |
| `windhorncanyon` | Windhorn Canyon | 820 |

> **No tele token** — use `.go xyz`: Stormwrought Ruins (818), Frostmane Retreat (806).

### Custom raids (Turtle-WoW originals)

| Token | Place | Map |
| ------- | ------- | ----- |
| `ScarletCitadel` / `scarletfortress` / `scarletenclave` | Scarlet Citadel | 45 |
| `Karazhan` / `karazhaninstance` | Tower of Karazhan | 814 |
| `sanctumraid` / `emeraldsanctum` / `emeraldsanctumproper` / `deeptidesanctum` | Emerald Sanctum | 807 |
| `TimbermawHold` | Timbermaw Hold | 819 |

> **No tele token** — use `.go xyz`: Old Scarlet Citadel (44).

### New world zones

| Token | Place | Map |
| ------- | ------- | ----- |
| `AzsharaCrater` / `Azshara` | Azshara Crater | 37 |
| `EmeraldDream` / `EmeraldForest` / `EmeraldPlains` / `EmeraldSanctuary` | Emerald Dream | 169 |
| `eldrethalas` | Eldrethalas | 804 |
| `winterveilvale` | Winter Veil Vale | 813 |
| `cowlevel` | Secret Cow Level | 801 |
| `gnomeshrink` | Gnomeshrink | 809 |

### New battleground

Thorn Gorge (map 821) — enter via the battleground queue or `.bg start` (no tele token).

### Classic dungeons & raids (still in the game)

`deadmines`, `ShadowFangKeep`, `WailingCaverns`, `BlackfathomDeeps`, `RazorfenKraul`, `RazorfenDowns`, `Uldaman`, `Gnomeregan`, `Maraudon`, `Zul'Farrak`, `TheSunkenTemple`, `DireMaulEast/North/West`, `Scholomance`, `Stratholme`, `BlackrockDepths`, `BlackrockSpire`, `RagefireChasm`, `MoltenCore`, `BlackwingLair`, `Onyxia'sLair` (`onyxia`), `Zul'Gurub`, `Naxxramas`, `aq20`, `aq40`.

### Finding more tokens

- `.tele name` (no argument) — lists **all** 647 tokens.
- `.tele name <partial>` — lists tokens containing that text.
- SQL: `SELECT name FROM game_tele ORDER BY name;` — same list, from the DB.
- New place with no token? Stand there and run `.tele add <name>`, then `.reload game_tele`.

---

## 11. Source & verification

- **All commands:** `src/game/Chat/Chat.cpp` — the `commandTable[]` (line 831) and the sub-command tables in the same file.
- **Ranks:** `src/shared/Common.h` (`enum AccountTypes`).
- **PlayerBots:** `src/modules/PlayerBots/playerbot/PlayerbotMgr.cpp` and the `strategy/**/AiObjectContext.cpp` files.
- **Teleport tokens & maps:** live `tw_world.game_tele` and `tw_world.map_template`.
- Generated 2026-08-17 from the actual source and database in this repo.

*This manual describes only commands that exist in this server's code. If you need a command not listed, type `.commands` in-game for the live list, or `.help <command>` for syntax.*
